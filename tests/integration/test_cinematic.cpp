#include "doctest.h"
#include "bhr/cinematic_renderer.hpp"
#include "bhr/presets.hpp"
#include <vector>
#include <limits>
#include <algorithm>
#include <cstdint>

TEST_CASE("cinematic GPU display matches independent color patches and preserves diagnostics") {
    bhr::CinematicBuffer workspace(4,1);
    const auto target=workspace.target();
    const std::vector<bhr::RadiancePixel> input{{0,0,0,1},{1,1,1,1},{1,0,0,1},{0,0,0,1}};
    const std::vector<bhr::PixelStatus> status{bhr::PixelStatus::valid,bhr::PixelStatus::valid,bhr::PixelStatus::valid,bhr::PixelStatus::unknown};
    REQUIRE(cudaMemcpy(bhr::radiance_data(target),input.data(),input.size()*16,cudaMemcpyHostToDevice)==cudaSuccess);
    REQUIRE(cudaMemcpy(bhr::status_data(target),status.data(),status.size(),cudaMemcpyHostToDevice)==cudaSuccess);
    uchar4* pixels=nullptr; REQUIRE(cudaMalloc(&pixels,16)==cudaSuccess);
    const bhr::AppearanceV1 a{.15f,.18f,.1f,.15f,0};
    REQUIRE(bhr::display_device(a,target,pixels,16)==cudaSuccess);
    std::vector<uchar4> result(4);
    REQUIRE(cudaMemcpy(result.data(),pixels,16,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(result[0].x==0); CHECK(result[1].x==188); CHECK(result[1].y==188);
    CHECK(result[2].x==188); CHECK(result[2].y==0); CHECK(result[2].w==255);
    CHECK(result[3].x==255); CHECK(result[3].y==0); CHECK(result[3].z==255);
    CHECK(bhr::display_device(a,target,pixels,15)==cudaErrorInvalidValue);
    CHECK(bhr::display_device(a,target,reinterpret_cast<uchar4*>(target.data),16)==cudaErrorInvalidValue);
    CHECK(bhr::display_device(a,{target.data,1,4,1},pixels,16)==cudaErrorInvalidValue);
    CHECK(bhr::display_device({0},target,pixels,16)==cudaErrorInvalidValue);
    REQUIRE(cudaFree(pixels)==cudaSuccess);
}

TEST_CASE("cinematic linear shading is deterministic without modifying legacy output") {
    const bhr::RenderParams p{.6f,{55,88,0,40,64,36},{3.83f,20,40000,1},true,true,true,false,bhr::IntegratorKind::kRK45};
    const bhr::Starfield empty;
    bhr::Image legacy; bhr::render(p,empty,legacy);
    const auto first=bhr::render_cinematic({p,{}},empty);
    const auto again=bhr::render_cinematic({p,{}},empty);
    CHECK(first.image.rgba==again.image.rgba);
    CHECK(first.image.rgba!=legacy.rgba);
    CHECK(first.diagnostics.invalid==0);
    bhr::Image after; bhr::render(p,empty,after); CHECK(after.rgba==legacy.rgba);
    const auto glow=bhr::render_cinematic({p,{.15f,.18f,.1f,.15f,1,{true,.06f,1}}},empty);
    CHECK(glow.image.rgba!=first.image.rgba);
    CHECK(glow.diagnostics.invalid==0);
    const bhr::RenderParams missing{p.spin,p.camera,p.disk,true,true,true,true,p.integrator};
    CHECK_THROWS(bhr::render_cinematic({missing,{}},empty));
    CHECK_THROWS(bhr::CinematicBuffer(16384,16384));
}

TEST_CASE("cinematic bloom handles odd edges black input and invalid radiance visibly") {
    for (const auto dims: {std::pair{1,1},std::pair{7,5}}) {
        bhr::CinematicBuffer workspace(dims.first,dims.second); const auto t=workspace.target();
        const size_t n=static_cast<size_t>(dims.first)*dims.second;
        const std::vector<bhr::RadiancePixel> black(n,{0,0,0,1});
        REQUIRE(cudaMemcpy(bhr::radiance_data(t),black.data(),n*16,cudaMemcpyHostToDevice)==cudaSuccess);
        REQUIRE(cudaMemset(bhr::status_data(t),0,n)==cudaSuccess);
        uchar4* out=nullptr; REQUIRE(cudaMalloc(&out,n*4)==cudaSuccess);
        const bhr::AppearanceV1 a{.15f,.18f,.1f,.15f,1,{true,.06f,1}};
        REQUIRE(bhr::bloom_device(a,t)==cudaSuccess);
        REQUIRE(bhr::display_device(a,t,out,n*4)==cudaSuccess);
        std::vector<uchar4> got(n); REQUIRE(cudaMemcpy(got.data(),out,n*4,cudaMemcpyDeviceToHost)==cudaSuccess);
        for (auto pixel:got) { CHECK(pixel.x==0); CHECK(pixel.y==0); CHECK(pixel.z==0); CHECK(pixel.w==255); }
        const bhr::RadiancePixel bad{std::numeric_limits<float>::quiet_NaN(),0,0,1};
        REQUIRE(cudaMemcpy(bhr::radiance_data(t),&bad,16,cudaMemcpyHostToDevice)==cudaSuccess);
        REQUIRE(bhr::display_device(a,t,out,n*4)==cudaSuccess);
        REQUIRE(cudaMemcpy(got.data(),out,n*4,cudaMemcpyDeviceToHost)==cudaSuccess);
        CHECK(got[0].x==255); CHECK(got[0].y==0); CHECK(got[0].z==255);
        REQUIRE(cudaFree(out)==cudaSuccess);
    }
}

TEST_CASE("cinematic radiance clips finite extreme emission and reports every clipped pixel") {
    bhr::RenderParams p{.6f,{55,88,0,40,32,18},{3.83f,20,40000,1},true,true,true,false,bhr::IntegratorKind::kRK45};
    p.disk.brightness=std::numeric_limits<float>::max();
    bhr::CinematicBuffer workspace(p.camera.width,p.camera.height);
    REQUIRE(bhr::render_radiance_device({p,{}}, {}, workspace.target())==cudaSuccess);
    REQUIRE(workspace.read_diagnostics_async(0)==cudaSuccess);
    REQUIRE(cudaStreamSynchronize(0)==cudaSuccess);
    const auto d=workspace.diagnostics();
    CHECK(d.invalid==0);
    CHECK(d.clipped>0);
    const size_t n=static_cast<size_t>(p.camera.width)*p.camera.height;
    std::vector<bhr::RadiancePixel> radiance(n);
    std::vector<bhr::PixelStatus> status(n);
    REQUIRE(cudaMemcpy(radiance.data(),bhr::radiance_data(workspace.target()),radiance.size()*sizeof(radiance[0]),cudaMemcpyDeviceToHost)==cudaSuccess);
    REQUIRE(cudaMemcpy(status.data(),bhr::status_data(workspace.target()),status.size(),cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(static_cast<uint32_t>(std::count(status.begin(),status.end(),bhr::PixelStatus::clipped))==d.clipped);
    CHECK(std::all_of(status.begin(),status.end(),[](const auto value){return value!=bhr::PixelStatus::invalid;}));
    CHECK(std::all_of(radiance.begin(),radiance.end(),[](const auto& pixel) {
        return pixel.r<=bhr::kMaxRadiance && pixel.g<=bhr::kMaxRadiance && pixel.b<=bhr::kMaxRadiance;
    }));
    CHECK(std::any_of(radiance.begin(),radiance.end(),[](const auto& pixel) {
        return pixel.r==bhr::kMaxRadiance || pixel.g==bhr::kMaxRadiance || pixel.b==bhr::kMaxRadiance;
    }));
}

TEST_CASE("cinematic static disk appearance changes emission but not classifications") {
    bhr::RenderParams p{.6f,{55,88,0,40,32,18},{3.83f,20,40000,1},true,true,true,false,bhr::IntegratorKind::kRK45};
    bhr::CinematicBuffer detailed(p.camera.width,p.camera.height), plain(p.camera.width,p.camera.height);
    const bhr::AppearanceV1 detailed_a{.15f,.3f,.25f,.15f,0}, plain_a{.15f,0,0,.15f,0};
    REQUIRE(bhr::render_radiance_device({p,detailed_a},{},detailed.target())==cudaSuccess);
    REQUIRE(bhr::render_radiance_device({p,plain_a},{},plain.target())==cudaSuccess);
    REQUIRE(cudaDeviceSynchronize()==cudaSuccess);
    const size_t n=static_cast<size_t>(p.camera.width)*p.camera.height;
    std::vector<bhr::RadiancePixel> detailed_pixels(n),plain_pixels(n);
    std::vector<bhr::PixelStatus> detailed_status(n),plain_status(n);
    REQUIRE(cudaMemcpy(detailed_pixels.data(),bhr::radiance_data(detailed.target()),n*sizeof(detailed_pixels[0]),cudaMemcpyDeviceToHost)==cudaSuccess);
    REQUIRE(cudaMemcpy(plain_pixels.data(),bhr::radiance_data(plain.target()),n*sizeof(plain_pixels[0]),cudaMemcpyDeviceToHost)==cudaSuccess);
    REQUIRE(cudaMemcpy(detailed_status.data(),bhr::status_data(detailed.target()),n,cudaMemcpyDeviceToHost)==cudaSuccess);
    REQUIRE(cudaMemcpy(plain_status.data(),bhr::status_data(plain.target()),n,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(detailed_status==plain_status);
    CHECK(std::any_of(detailed_pixels.begin(),detailed_pixels.end(),[&](const auto& pixel) {
        const auto i=static_cast<size_t>(&pixel-detailed_pixels.data());
        return pixel.r!=plain_pixels[i].r || pixel.g!=plain_pixels[i].g || pixel.b!=plain_pixels[i].b;
    }));
}

TEST_CASE("cinematic nondefault stream pipeline matches blocking wrapper") {
    const bhr::RenderParams p{.6f,{55,88,0,40,24,14},{3.83f,20,40000,1},true,true,true,false,bhr::IntegratorKind::kRK45};
    const bhr::AppearanceV1 appearance{.15f,.22f,.12f,.15f,.5f,{true,.06f,1}};
    const auto expected=bhr::render_cinematic({p,appearance},{});
    bhr::CinematicBuffer workspace(p.camera.width,p.camera.height);
    const size_t bytes=static_cast<size_t>(p.camera.width)*p.camera.height*sizeof(uchar4);
    uchar4* output=nullptr; cudaStream_t stream=nullptr;
    REQUIRE(cudaMalloc(&output,bytes)==cudaSuccess);
    REQUIRE(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking)==cudaSuccess);
    REQUIRE(bhr::render_cinematic_device({p,appearance},{},workspace.target(),output,bytes,stream)==cudaSuccess);
    REQUIRE(workspace.read_diagnostics_async(stream)==cudaSuccess);
    REQUIRE(cudaStreamSynchronize(stream)==cudaSuccess);
    std::vector<uint8_t> actual(bytes);
    REQUIRE(cudaMemcpy(actual.data(),output,bytes,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(actual==expected.image.rgba);
    CHECK(workspace.diagnostics().unknown==expected.diagnostics.unknown);
    CHECK(workspace.diagnostics().invalid==expected.diagnostics.invalid);
    CHECK(workspace.diagnostics().clipped==expected.diagnostics.clipped);
    REQUIRE(cudaStreamDestroy(stream)==cudaSuccess); stream=nullptr;
    REQUIRE(cudaFree(output)==cudaSuccess); output=nullptr;
}

TEST_CASE("cinematic invalid boundaries reject before changing workspace or output") {
    constexpr int width=8, height=6;
    bhr::CinematicBuffer workspace(width,height);
    const auto target=workspace.target();
    const size_t workspace_bytes=bhr::cinematic_workspace_bytes(width,height), output_bytes=width*height*sizeof(uchar4);
    uchar4* output=nullptr;
    REQUIRE(cudaMalloc(&output,output_bytes)==cudaSuccess);
    REQUIRE(cudaMemset(target.data,0x5a,workspace_bytes)==cudaSuccess);
    REQUIRE(cudaMemset(output,0xa7,output_bytes)==cudaSuccess);
    const bhr::CinematicRequest request{{.6f,{55,88,0,40,width,height},{3.83f,20,40000,1},true,true,true,false,bhr::IntegratorKind::kRK45},{}};
    CHECK(bhr::render_cinematic_device(request,{},target,output,output_bytes-1)==cudaErrorInvalidValue);
    CHECK(bhr::render_cinematic_device(request,{},target,reinterpret_cast<uchar4*>(target.data),output_bytes)==cudaErrorInvalidValue);
    bhr::CinematicDeviceTarget undersized{target.data,workspace_bytes-1,width,height};
    CHECK(bhr::render_cinematic_device(request,{},undersized,output,output_bytes)==cudaErrorInvalidValue);
    std::vector<uint8_t> workspace_after(workspace_bytes), output_after(output_bytes);
    REQUIRE(cudaMemcpy(workspace_after.data(),target.data,workspace_bytes,cudaMemcpyDeviceToHost)==cudaSuccess);
    REQUIRE(cudaMemcpy(output_after.data(),output,output_bytes,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(std::all_of(workspace_after.begin(),workspace_after.end(),[](uint8_t value){return value==0x5a;}));
    CHECK(std::all_of(output_after.begin(),output_after.end(),[](uint8_t value){return value==0xa7;}));
    REQUIRE(cudaFree(output)==cudaSuccess);
}

TEST_CASE("cinematic bloom spreads a bright edge pixel but excludes diagnostic pixels") {
    constexpr int width=5, height=5;
    bhr::CinematicBuffer workspace(width,height);
    const auto target=workspace.target();
    const size_t n=static_cast<size_t>(width)*height, bytes=n*sizeof(uchar4);
    std::vector<bhr::RadiancePixel> radiance(n,{0,0,0,1});
    radiance.front()={bhr::kMaxRadiance,bhr::kMaxRadiance,bhr::kMaxRadiance,1};
    REQUIRE(cudaMemcpy(bhr::radiance_data(target),radiance.data(),n*sizeof(radiance[0]),cudaMemcpyHostToDevice)==cudaSuccess);
    REQUIRE(cudaMemset(bhr::status_data(target),0,n)==cudaSuccess);
    uchar4* output=nullptr; REQUIRE(cudaMalloc(&output,bytes)==cudaSuccess);
    const bhr::AppearanceV1 appearance{.15f,.18f,.1f,.15f,0,{true,.06f,1}};
    REQUIRE(bhr::bloom_device(appearance,target)==cudaSuccess);
    REQUIRE(bhr::display_device(appearance,target,output,bytes)==cudaSuccess);
    std::vector<uchar4> got(n); REQUIRE(cudaMemcpy(got.data(),output,bytes,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(got[0].x>0); CHECK(got[1].x>0); CHECK(got[width].x>0);

    const bhr::RadiancePixel bright{bhr::kMaxRadiance,bhr::kMaxRadiance,bhr::kMaxRadiance,1};
    REQUIRE(cudaMemcpy(bhr::radiance_data(target),&bright,sizeof(bright),cudaMemcpyHostToDevice)==cudaSuccess);
    const bhr::PixelStatus invalid=bhr::PixelStatus::invalid;
    REQUIRE(cudaMemcpy(bhr::status_data(target),&invalid,sizeof(invalid),cudaMemcpyHostToDevice)==cudaSuccess);
    REQUIRE(bhr::bloom_device(appearance,target)==cudaSuccess);
    REQUIRE(bhr::display_device(appearance,target,output,bytes)==cudaSuccess);
    REQUIRE(cudaMemcpy(got.data(),output,bytes,cudaMemcpyDeviceToHost)==cudaSuccess);
    CHECK(got[0].x==255); CHECK(got[0].y==0); CHECK(got[0].z==255);
    CHECK(got[1].x==0); CHECK(got[1].y==0); CHECK(got[1].z==0);
    REQUIRE(cudaFree(output)==cudaSuccess);
}
