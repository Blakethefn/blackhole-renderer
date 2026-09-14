// Reproducible Plan 8 measurement and real ImGui/GL framebuffer parity.
#include <glad/glad.h>
#include <SDL.h>
#include <cuda_gl_interop.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "workbench/viewport.hpp"
#include "workbench/encoded_framebuffer.hpp"
#include "bhr/cinematic_renderer.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void require(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
void cuda_check(cudaError_t e) {if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
using Clock=std::chrono::steady_clock;
struct Sky { bhr::Starfield value; ~Sky(){bhr::destroy_starfield(value);} };
struct Pixels { uchar4* value=nullptr; ~Pixels(){if(value)cudaFree(value);} };
struct Event {cudaEvent_t value=nullptr;Event(){cuda_check(cudaEventCreate(&value));}~Event(){cudaEventDestroy(value);}};
template<class F> float measure(F f) {
    Event start,end;double sum=0;
    for(int i=0;i<12;++i) {
        cuda_check(cudaEventRecord(start.value));f();cuda_check(cudaEventRecord(end.value));
        cuda_check(cudaEventSynchronize(end.value));float ms=0;
        cuda_check(cudaEventElapsedTime(&ms,start.value,end.value));if(i>=2)sum+=ms;
    }
    return float(sum/10);
}
struct Graphics {
    SDL_Window* window=nullptr;SDL_GLContext context=nullptr;bool imgui=false,backend=false;
    Graphics(int w,int h) {
        require(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        window=SDL_CreateWindow("Cinematic parity",0,0,w,h,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        require(window,SDL_GetError());context=SDL_GL_CreateContext(window);require(context,SDL_GetError());
        require(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)),"Load GL");
        unsigned n=0;int device=0;cuda_check(cudaGLGetDevices(&n,&device,1,cudaGLDeviceListCurrentFrame));
        require(n>0,"No CUDA/GL device");cuda_check(cudaSetDevice(device));
        ImGui::CreateContext();imgui=true;ImGui::GetIO().IniFilename=nullptr;
        backend=ImGui_ImplOpenGL3_Init("#version 330 core");require(backend,"Initialize ImGui GL");
    }
    ~Graphics(){if(backend)ImGui_ImplOpenGL3_Shutdown();if(imgui)ImGui::DestroyContext();if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();}
};
bhr::Image framebuffer(unsigned source,int w,int h) {
    GLuint fbo=0,texture=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Framebuffer incomplete");
    auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(float(w),float(h));io.DeltaTime=1.0f/60;
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(source)),ImVec2(0,0),io.DisplaySize);
    ImGui::Render();glViewport(0,0,w,h);glEnable(GL_FRAMEBUFFER_SRGB);
    {bhr::workbench::EncodedFramebuffer encoded;ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());}
    require(glIsEnabled(GL_FRAMEBUFFER_SRGB),"Presentation did not restore sRGB state");
    glDisable(GL_FRAMEBUFFER_SRGB);
    bhr::Image bottom;bottom.allocate(w,h);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,bottom.rgba.data());
    require(glGetError()==GL_NO_ERROR,"Framebuffer read failed");
    bhr::Image top;top.allocate(w,h);
    for(int y=0;y<h;++y)std::copy_n(bottom.rgba.data()+size_t(h-1-y)*w*4,size_t(w)*4,top.rgba.data()+size_t(y)*w*4);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    return top;
}
void capture(const bhr::CinematicRequest& request,const bhr::Starfield& sky,const std::filesystem::path& dir,const std::string& name) {
    const auto start=Clock::now();const auto host=bhr::render_cinematic(request,sky);
    const auto wall=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    require(bhr::write_srgb_png(host.image,(dir/(name+".png")).string()),"Write headless image");
    bhr::workbench::Viewport viewport;
    require(viewport.submit(request,sky),viewport.error().c_str());
    const auto deadline=Clock::now()+std::chrono::seconds(30);
    while(viewport.busy()&&Clock::now()<deadline){viewport.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(!viewport.busy()&&viewport.has_image()&&viewport.error().empty(),"Viewport publication failed");
    const auto readback=framebuffer(viewport.texture(),host.image.width,host.image.height);
    require(readback.rgba==host.image.rgba,"Headless/ImGui framebuffer bytes differ");
    require(bhr::write_srgb_png(readback,(dir/(name+"-gui.png")).string()),"Write GUI capture");
    std::printf("capture=%s %dx%d framebuffer_exact=true unknown=%u invalid=%u clipped=%u host_ms=%.3f gui_publish_ms=%.3f\n",
        name.c_str(),host.image.width,host.image.height,host.diagnostics.unknown,host.diagnostics.invalid,host.diagnostics.clipped,wall,viewport.last_frame_ms());
    require(viewport.shutdown(),"Viewport shutdown failed");
}
int run(const std::filesystem::path& file,const std::filesystem::path& directory) {
    const auto document=bhr::load_cinematic_v2(file);const auto fixed=bhr::evaluate_frame(document.scene_shots,"fixed",0);
    const int w=fixed.params.camera.width,h=fixed.params.camera.height;
    Graphics graphics(w,h);Sky sky;
    require(bhr::load_cinematic_starfield(bhr::resolve_starfield(document.scene_shots.scene,file),sky.value),"Load sky failed");
    std::filesystem::create_directories(directory);
    const auto a=document.appearance;
    const bhr::AppearanceV1 glow{a.temperature_scale,a.disk_detail,a.outer_fade_fraction,a.star_intensity,a.exposure_ev,{true,a.bloom.strength,a.bloom.threshold}};
    capture({fixed.params,a},sky.value,directory,"fixed");
    capture({fixed.params,glow},sky.value,directory,"fixed-bloom");
    // Save full reproducible inputs beside the captures; resolve the sky from
    // each new document location, never from the current working directory.
    std::filesystem::create_directories(directory/"assets");
    std::filesystem::copy_file(bhr::resolve_starfield(document.scene_shots.scene,file),directory/"assets/stars-v1.exr",
        std::filesystem::copy_options::overwrite_existing);
    const std::string relative_sky="assets/stars-v1.exr";
    const auto save=[&](const bhr::RenderParams& p,const bhr::AppearanceV1& look,const std::string& name) {
        const bhr::Shot shot{"fixed",1,{60,1},p.camera.width,p.camera.height,std::nullopt,bhr::FixedCamera{}};
        bhr::save_cinematic_v2({{{p,relative_sky},{shot}},look},directory/(name+".json"));
    };
    save(fixed.params,a,"fixed");save(fixed.params,glow,"fixed-bloom");
    for(uint64_t index:{0u,300u,599u}) {
        const auto frame=bhr::evaluate_frame(document.scene_shots,"orbit",index);
        capture({frame.params,a},sky.value,directory,"orbit-"+std::to_string(index));
        save(frame.params,a,"orbit-"+std::to_string(index));
    }
    struct Variant {float spin,inclination;const char* name;};
    for(const auto variant:{Variant{0,60,"spin0-incl60"},Variant{.9f,75,"spin09-incl75"},Variant{.99f,85,"spin099-incl85"}}) {
        auto p=fixed.params;p.spin=variant.spin;p.disk.r_inner=bhr::r_isco(p.spin);
        p.camera.theta_cam_deg=variant.inclination;p.camera.width=1024;p.camera.height=576;
        capture({p,a},sky.value,directory,variant.name);save(p,a,variant.name);
        const std::string glow_name=std::string(variant.name)+"-bloom";
        capture({p,glow},sky.value,directory,glow_name);save(p,glow,glow_name);
    }
    bhr::Image legacy;bhr::render(fixed.params,sky.value,legacy);
    require(bhr::write_png(legacy,(directory/"legacy.png").string()),"Legacy capture failed");
    bhr::CinematicBuffer buffer(w,h);const auto sizes=bhr::cinematic_sizes(w,h);Pixels pixels;
    cuda_check(cudaMalloc(&pixels.value,sizes.rgba_bytes));
    const bhr::CinematicRequest request{fixed.params,a};
    const float legacy_ms=measure([&]{cuda_check(bhr::render_device(fixed.params,sky.value,pixels.value,sizes.rgba_bytes));});
    const float radiance_ms=measure([&]{cuda_check(bhr::render_radiance_device(request,sky.value,buffer.target()));});
    const float display_ms=measure([&]{cuda_check(bhr::display_device(a,buffer.target(),pixels.value,sizes.rgba_bytes));});
    const float bloom_ms=measure([&]{cuda_check(bhr::bloom_device(glow,buffer.target()));});
    const float combined_ms=measure([&]{cuda_check(bhr::render_cinematic_device({fixed.params,glow},sky.value,buffer.target(),pixels.value,sizes.rgba_bytes));});
    std::printf("timing warmup=2 samples=10 %dx%d legacy_ms=%.4f radiance_ms=%.4f display_ms=%.4f bloom_ms=%.4f combined_glow_ms=%.4f shading_overhead_percent=%.2f\n",
        w,h,legacy_ms,radiance_ms,display_ms,bloom_ms,combined_ms,100*(radiance_ms/legacy_ms-1));
    std::printf("resources workspace=%zu rgba=%zu sky=%zu conservative_gui_resize_budget=%zu limit=%zu bytes\n",
        bhr::cinematic_workspace_bytes(w,h),sizes.rgba_bytes,size_t(sky.value.width)*sky.value.height*16,sizes.budget_bytes,bhr::kCinematicResourceBudget);
    return 0;
}
}
int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"Usage: cinematic-evidence SCENE.json OUTPUT_DIRECTORY\n");return 1;}
    try{return run(argv[1],argv[2]);}catch(const std::exception& e){std::fprintf(stderr,"Evidence failed: %s\n",e.what());return 1;}
}
