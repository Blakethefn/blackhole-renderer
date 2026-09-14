#include "bhr/cinematic_renderer.hpp"
#include "bhr/camera.hpp"
#include "bhr/geodesic.hpp"
#include "bhr/disk_physics.hpp"
#include "bhr/blackbody.hpp"
#include "bhr/redshift.hpp"
#include "bhr/disk_appearance.hpp"
#include <cstdio>
#include <stdexcept>

namespace bhr {
namespace {
size_t padded(size_t n) { return (n+15)/16*16; }
size_t workspace_bytes(const CinematicSizes& s) {
    return s.radiance_bytes+padded(s.status_bytes)+2*s.bloom_bytes+16;
}
struct View {
    RadiancePixel* radiance;
    PixelStatus* status;
    RadiancePixel* bloom_a;
    RadiancePixel* bloom_b;
    FrameDiagnostics* diagnostics;
};
View view(CinematicDeviceTarget t) {
    const auto s=cinematic_sizes(t.width,t.height);
    if (!t.data || reinterpret_cast<uintptr_t>(t.data)%16 || t.capacity_bytes<workspace_bytes(s))
        throw std::runtime_error("Invalid cinematic workspace alignment or capacity");
    auto* base=static_cast<unsigned char*>(t.data);
    auto* a=base+s.radiance_bytes+padded(s.status_bytes);
    return {reinterpret_cast<RadiancePixel*>(base),reinterpret_cast<PixelStatus*>(base+s.radiance_bytes),
        reinterpret_cast<RadiancePixel*>(a),reinterpret_cast<RadiancePixel*>(a+s.bloom_bytes),
        reinterpret_cast<FrameDiagnostics*>(a+2*s.bloom_bytes)};
}
void check(cudaError_t status, const char* operation) {
    if (status!=cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(status));
}
bool overlaps(const void* a, size_t an, const void* b, size_t bn) {
    const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
    return x<=y ? y-x<an : x-y<bn;
}
__device__ void store_pixel(View v, int i, double r, double g, double b) {
    if (!isfinite(r)||!isfinite(g)||!isfinite(b)||r<0||g<0||b<0) {
        v.radiance[i]={0,0,0,1}; v.status[i]=PixelStatus::invalid;
        atomicAdd(&v.diagnostics->invalid,1u); return;
    }
    const bool clipped=r>kMaxRadiance||g>kMaxRadiance||b>kMaxRadiance;
    v.radiance[i]={float(fmin(r,double(kMaxRadiance))),float(fmin(g,double(kMaxRadiance))),float(fmin(b,double(kMaxRadiance))),1};
    v.status[i]=clipped?PixelStatus::clipped:PixelStatus::valid;
    if(clipped) atomicAdd(&v.diagnostics->clipped,1u);
}
__global__ void radiance_kernel(CinematicRequest request, View v, cudaTextureObject_t sky) {
    const auto p=request.params; const auto a=request.appearance;
    const int x=blockIdx.x*blockDim.x+threadIdx.x, y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=p.camera.width||y>=p.camera.height)return;
    const int i=y*p.camera.width+x;
    GeodesicState state{}; Conserved c{};
    camera_ray(x,y,p.camera.width,p.camera.height,p.camera,p.spin,state,c);
    IntegratorConfig cfg{};
    cfg.r_max=kMaxSceneRadius;cfg.disk_r_inner=p.disk.r_inner;cfg.disk_r_outer=p.disk.r_outer;cfg.max_steps=50000;
    const float scale=fmaxf(1.0f,fabsf(state.dr_dlam)+fabsf(state.dth_dlam));
    cfg.h_init=fminf(.05f,fmaxf(1e-6f,state.r/(20.0f*scale)));cfg.h_min=1e-6f;
    const HitInfo hit=p.integrator==IntegratorKind::kGeokerr
        ? integrate_geokerr(state,p.spin,c,cfg) : integrate_rk45(state,p.spin,c,cfg);
    if(hit.type==HitType::kUnknown) {
        v.radiance[i]={0,0,0,1};v.status[i]=PixelStatus::unknown;
        atomicAdd(&v.diagnostics->unknown,1u);return;
    }
    if(hit.type==HitType::kHorizon) { store_pixel(v,i,0,0,0);return; }
    if(hit.type==HitType::kEscape) {
        if(!sky) { store_pixel(v,i,0,0,0);return; }
        constexpr float pi=3.14159265358979323846f;
        const float u=hit.phi/(2*pi)-floorf(hit.phi/(2*pi));
        const auto star=tex2D<float4>(sky,u,hit.theta/pi);
        store_pixel(v,i,double(star.x)*a.star_intensity,double(star.y)*a.star_intensity,double(star.z)*a.star_intensity);return;
    }
    const float temperature=disk_temperature(hit.r,p.disk.r_inner,p.disk.peak_temp_K);
    const float g=disk_frequency_shift(hit.r,p.spin,c,p.enable_doppler,p.enable_redshift);
    if(!isfinite(temperature)||!isfinite(g)||g<0) {store_pixel(v,i,-1,0,0);return;}
    const double shifted=double(temperature)*g*a.temperature_scale;
    float r,green,b;blackbody_rgb(float(fmin(40000.0,fmax(1000.0,shifted))),r,green,b);
    const double g2=double(g)*g;
    const double beam=p.enable_beaming?g2*g2:1.0;
    const double emissivity=log(fmax(double(temperature),1.0))/log(40000.0);
    const double intensity=1.5*double(p.disk.brightness)*emissivity*beam
        *disk_appearance(hit.r,hit.phi,p.disk.r_inner,p.disk.r_outer,a.disk_detail,a.outer_fade_fraction);
    store_pixel(v,i,srgb_decode(r)*intensity,srgb_decode(green)*intensity,srgb_decode(b)*intensity);
}
__device__ RadiancePixel bright(RadiancePixel p, PixelStatus status, float exposure, float threshold) {
    if(status==PixelStatus::unknown||status==PixelStatus::invalid||!valid_radiance(p)) return {0,0,0,1};
    const float r=p.r*exposure,g=p.g*exposure,b=p.b*exposure;
    const float luminance=.2126f*r+.7152f*g+.0722f*b;
    const float weight=fmaxf(luminance-threshold,0)/fmaxf(luminance,1e-12f);
    return {r*weight,g*weight,b*weight,1};
}
__global__ void downsample(View v,int w,int h,int bw,int bh,float exposure,float threshold) {
    const int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=bw||y>=bh)return;
    float r=0,g=0,b=0;int count=0;
    for(int j=y*4;j<min(y*4+4,h);++j) for(int i=x*4;i<min(x*4+4,w);++i) {
        const auto status=v.status[j*w+i];
        if(status==PixelStatus::unknown||status==PixelStatus::invalid||!valid_radiance(v.radiance[j*w+i]))continue;
        const auto p=bright(v.radiance[j*w+i],v.status[j*w+i],exposure,threshold);
        r+=p.r;g+=p.g;b+=p.b;++count;
    }
    const float divisor=float(max(count,1));
    v.bloom_a[y*bw+x]={r/divisor,g/divisor,b/divisor,1};
}
__global__ void blur(const RadiancePixel* source,RadiancePixel* dest,int w,int h,bool horizontal) {
    const int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=w||y>=h)return;
    float r=0,g=0,b=0,total=0;
    for(int k=-6;k<=6;++k) {
        const float weight=expf(-float(k*k)/8.0f);
        const int sx=horizontal?max(0,min(w-1,x+k)):x;
        const int sy=horizontal?y:max(0,min(h-1,y+k));
        const auto p=source[sy*w+sx];r+=p.r*weight;g+=p.g*weight;b+=p.b*weight;total+=weight;
    }
    dest[y*w+x]={r/total,g/total,b/total,1};
}
__device__ RadiancePixel bloom_sample(const RadiancePixel* data,int w,int h,int px,int py) {
    const float x=fmaxf(0,fminf(w-1,(px+.5f)/4-.5f)),y=fmaxf(0,fminf(h-1,(py+.5f)/4-.5f));
    const int x0=int(x),y0=int(y),x1=min(w-1,x0+1),y1=min(h-1,y0+1);
    const float fx=x-x0,fy=y-y0;
    const auto a=data[y0*w+x0],b=data[y0*w+x1],c=data[y1*w+x0],d=data[y1*w+x1];
    return {(1-fy)*((1-fx)*a.r+fx*b.r)+fy*((1-fx)*c.r+fx*d.r),
        (1-fy)*((1-fx)*a.g+fx*b.g)+fy*((1-fx)*c.g+fx*d.g),
        (1-fy)*((1-fx)*a.b+fx*b.b)+fy*((1-fx)*c.b+fx*d.b),1};
}
__global__ void display_kernel(AppearanceV1 a,View v,uchar4* out,int w,int h,int bw,int bh) {
    const int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=w||y>=h)return;
    const int i=y*w+x;const auto p=v.radiance[i];const auto status=v.status[i];
    if(status==PixelStatus::unknown||status==PixelStatus::invalid||!valid_radiance(p)) {out[i]=make_uchar4(255,0,255,255);return;}
    const auto glow=a.bloom.enabled?bloom_sample(v.bloom_a,bw,bh,x,y):RadiancePixel{0,0,0,1};
    const float exposure=exp2f(a.exposure_ev),strength=a.bloom.strength;
    out[i]=make_uchar4(display_exposed_channel(p.r*exposure+strength*glow.r),
        display_exposed_channel(p.g*exposure+strength*glow.g),display_exposed_channel(p.b*exposure+strength*glow.b),255);
}
} // namespace
size_t cinematic_workspace_bytes(int w,int h) {return workspace_bytes(cinematic_sizes(w,h));}
RadiancePixel* radiance_data(CinematicDeviceTarget t) {return view(t).radiance;}
PixelStatus* status_data(CinematicDeviceTarget t) {return view(t).status;}
FrameDiagnostics* diagnostics_data(CinematicDeviceTarget t) {return view(t).diagnostics;}
cudaError_t render_radiance_device(const CinematicRequest& r,const Starfield& sf,CinematicDeviceTarget t,cudaStream_t stream) {
    try {
        validate_cinematic_request(r);const auto v=view(t);
        if(r.params.camera.width!=t.width||r.params.camera.height!=t.height||
            (r.params.enable_starfield&&(!sf.is_valid()||sf.width<=0||sf.height<=0||sf.width>2048||static_cast<size_t>(sf.width)*sf.height>2*1024*1024))) return cudaErrorInvalidValue;
        const auto clear=cudaMemsetAsync(v.diagnostics,0,sizeof(FrameDiagnostics),stream);if(clear!=cudaSuccess)return clear;
        const dim3 block(16,16),grid((t.width+15)/16,(t.height+15)/16);
        radiance_kernel<<<grid,block,0,stream>>>(r,v,r.params.enable_starfield?sf.tex:0);
        return cudaGetLastError();
    } catch(const std::exception&) {return cudaErrorInvalidValue;}
}
cudaError_t bloom_device(const AppearanceV1& a,CinematicDeviceTarget t,cudaStream_t stream) {
    try {
        validate_appearance(a);const auto v=view(t);const auto s=cinematic_sizes(t.width,t.height);
        if(!a.bloom.enabled)return cudaSuccess;
        const dim3 block(16,16),grid((s.bloom_width+15)/16,(s.bloom_height+15)/16);
        downsample<<<grid,block,0,stream>>>(v,t.width,t.height,s.bloom_width,s.bloom_height,exp2f(a.exposure_ev),a.bloom.threshold);
        auto error=cudaGetLastError();if(error!=cudaSuccess)return error;
        blur<<<grid,block,0,stream>>>(v.bloom_a,v.bloom_b,s.bloom_width,s.bloom_height,true);
        error=cudaGetLastError();if(error!=cudaSuccess)return error;
        blur<<<grid,block,0,stream>>>(v.bloom_b,v.bloom_a,s.bloom_width,s.bloom_height,false);
        return cudaGetLastError();
    } catch(const std::exception&) {return cudaErrorInvalidValue;}
}
cudaError_t display_device(const AppearanceV1& a,CinematicDeviceTarget t,uchar4* out,size_t capacity,cudaStream_t stream) {
    try {
        validate_appearance(a);const auto v=view(t);const auto s=cinematic_sizes(t.width,t.height);
        if(!out||reinterpret_cast<uintptr_t>(out)%alignof(uchar4)||capacity<s.rgba_bytes||
            overlaps(t.data,workspace_bytes(s),out,s.rgba_bytes)) return cudaErrorInvalidValue;
        const dim3 block(16,16),grid((t.width+15)/16,(t.height+15)/16);
        display_kernel<<<grid,block,0,stream>>>(a,v,out,t.width,t.height,s.bloom_width,s.bloom_height);
        return cudaGetLastError();
    } catch(const std::exception&) {return cudaErrorInvalidValue;}
}
cudaError_t render_cinematic_device(const CinematicRequest& r,const Starfield& sf,CinematicDeviceTarget t,uchar4* out,size_t capacity,cudaStream_t stream) {
    // Validate the output boundary before writing any workspace data.
    try {
        const auto s=cinematic_sizes(t.width,t.height);(void)view(t);
        if(!out||reinterpret_cast<uintptr_t>(out)%alignof(uchar4)||capacity<s.rgba_bytes||overlaps(t.data,workspace_bytes(s),out,s.rgba_bytes))return cudaErrorInvalidValue;
    } catch(const std::exception&) {return cudaErrorInvalidValue;}
    auto status=render_radiance_device(r,sf,t,stream);if(status!=cudaSuccess)return status;
    status=bloom_device(r.appearance,t,stream);if(status!=cudaSuccess)return status;
    return display_device(r.appearance,t,out,capacity,stream);
}
CinematicBuffer::CinematicBuffer(int w,int h) {
    const auto bytes=cinematic_workspace_bytes(w,h);
    void* data=nullptr;check(cudaMalloc(&data,bytes),"Allocate cinematic workspace");
    target_={data,bytes,w,h};
    const auto error=cudaMallocHost(&host_,sizeof(FrameDiagnostics));
    if(error!=cudaSuccess) {close();check(error,"Allocate pinned diagnostics");}
    *host_={};
}
CinematicBuffer::~CinematicBuffer() {if(!close())std::fprintf(stderr,"Cinematic workspace cleanup failed\n");}
bool CinematicBuffer::close() {
    if(target_.data) {
        const auto error=cudaFree(target_.data);
        if(error!=cudaSuccess) {std::fprintf(stderr,"Cinematic cudaFree: %s\n",cudaGetErrorString(error));return false;}
        target_.data=nullptr;
    }
    if(host_) {
        const auto error=cudaFreeHost(host_);
        if(error!=cudaSuccess) {std::fprintf(stderr,"Cinematic cudaFreeHost: %s\n",cudaGetErrorString(error));return false;}
        host_=nullptr;
    }
    return true;
}
cudaError_t CinematicBuffer::read_diagnostics_async(cudaStream_t stream) {
    return cudaMemcpyAsync(host_,diagnostics_data(target_),sizeof(FrameDiagnostics),cudaMemcpyDeviceToHost,stream);
}
CinematicResult render_cinematic(const CinematicRequest& request,const Starfield& sf) {
    validate_cinematic_request(request);
    if(request.params.enable_starfield&&!sf.is_valid())throw std::runtime_error("Cinematic scene requires its starfield");
    const auto s=cinematic_sizes(request.params.camera.width,request.params.camera.height);
    CinematicBuffer buffer(s.width,s.height);
    struct Output {
        uchar4* pixels=nullptr;
        ~Output() {if(pixels) {const auto e=cudaFree(pixels);if(e!=cudaSuccess)std::fprintf(stderr,"Cinematic output cleanup: %s\n",cudaGetErrorString(e));}}
    } out;
    check(cudaMalloc(&out.pixels,s.rgba_bytes),"Allocate cinematic output");
    check(render_cinematic_device(request,sf,buffer.target(),out.pixels,s.rgba_bytes),"Render cinematic image");
    check(buffer.read_diagnostics_async(0),"Read cinematic diagnostics");
    check(cudaStreamSynchronize(0),"Complete cinematic image");
    const auto diagnostics=buffer.diagnostics();
    if(diagnostics.invalid)throw std::runtime_error("Cinematic shading produced invalid radiance; frame rejected");
    Image image;image.allocate(s.width,s.height);
    check(cudaMemcpy(image.rgba.data(),out.pixels,s.rgba_bytes,cudaMemcpyDeviceToHost),"Read cinematic pixels");
    check(cudaFree(out.pixels),"Free cinematic output");out.pixels=nullptr;
    if(!buffer.close())throw std::runtime_error("Free cinematic workspace failed");
    return {std::move(image),diagnostics};
}
} // namespace bhr
