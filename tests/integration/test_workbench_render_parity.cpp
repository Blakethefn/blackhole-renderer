#include "doctest.h"
#include "bhr/renderer.hpp"
#include <cuda_runtime.h>
#include <cstdint>
#include <vector>

namespace {

// Test fixtures keep CUDA handles owned even when REQUIRE unwinds a test.
struct DeviceTarget {
    uchar4* pixels = nullptr;
    cudaStream_t stream = nullptr;
    ~DeviceTarget() {
        if (stream) {
            CHECK(cudaStreamSynchronize(stream) == cudaSuccess);
            CHECK(cudaStreamDestroy(stream) == cudaSuccess);
        }
        if (pixels) CHECK(cudaFree(pixels) == cudaSuccess);
    }
};

bhr::RenderParams small_scene() {
    bhr::RenderParams p;
    p.camera.width = 32;
    p.camera.height = 18;
    p.disk.peak_temp_K = 6000.0f; // Keep colors below the existing 40000 K clamp.
    return p;
}

struct SyntheticStarfield {
    bhr::Starfield value;
    ~SyntheticStarfield() {
        if (value.tex) CHECK(cudaDestroyTextureObject(value.tex) == cudaSuccess);
        if (value.d_array) CHECK(cudaFreeArray(value.d_array) == cudaSuccess);
    }
};

void make_starfield(SyntheticStarfield& fixture) {
    const cudaChannelFormatDesc channel = cudaCreateChannelDesc<float4>();
    REQUIRE(cudaMallocArray(&fixture.value.d_array, &channel, 2, 2) == cudaSuccess);
    const float4 pixels[] = {make_float4(10, 0, 0, 1), make_float4(10, 0, 0, 1),
                             make_float4(10, 0, 0, 1), make_float4(10, 0, 0, 1)};
    REQUIRE(cudaMemcpy2DToArray(fixture.value.d_array, 0, 0, pixels,
        2 * sizeof(float4), 2 * sizeof(float4), 2, cudaMemcpyHostToDevice) == cudaSuccess);
    cudaResourceDesc resource{};
    resource.resType = cudaResourceTypeArray;
    resource.res.array.array = fixture.value.d_array;
    cudaTextureDesc texture{};
    texture.addressMode[0] = cudaAddressModeWrap;
    texture.addressMode[1] = cudaAddressModeClamp;
    texture.filterMode = cudaFilterModeLinear;
    texture.readMode = cudaReadModeElementType;
    texture.normalizedCoords = 1;
    REQUIRE(cudaCreateTextureObject(&fixture.value.tex, &resource, &texture, nullptr) == cudaSuccess);
    fixture.value.width = 2;
    fixture.value.height = 2;
}

} // namespace

TEST_CASE("Workbench device target exactly matches host rendering on both integrators") {
    for (const auto integrator : {bhr::IntegratorKind::kRK45, bhr::IntegratorKind::kGeokerr}) {
        auto p = small_scene();
        p.integrator = integrator;
        bhr::Image host;
        bhr::render(p, host);
        const size_t bytes = size_t(p.camera.width) * p.camera.height * sizeof(uchar4);
        REQUIRE(host.rgba.size() == bytes);
        DeviceTarget target;
        REQUIRE(cudaMalloc(&target.pixels, bytes + sizeof(uchar4)) == cudaSuccess);
        REQUIRE(cudaStreamCreateWithFlags(&target.stream, cudaStreamNonBlocking) == cudaSuccess);
        REQUIRE(cudaMemsetAsync(target.pixels, 0xA5, bytes + sizeof(uchar4), target.stream) == cudaSuccess);
        REQUIRE(bhr::render_device(p, target.pixels, bytes, target.stream) == cudaSuccess);
        REQUIRE(cudaStreamSynchronize(target.stream) == cudaSuccess);
        std::vector<std::uint8_t> copied(bytes + sizeof(uchar4));
        REQUIRE(cudaMemcpy(copied.data(), target.pixels, copied.size(), cudaMemcpyDeviceToHost) == cudaSuccess);
        CHECK(std::vector<std::uint8_t>(copied.begin(), copied.begin() + bytes) == host.rgba);
        for (size_t i = bytes; i < copied.size(); ++i) CHECK(copied[i] == 0xA5);
    }
}

TEST_CASE("Workbench device target rejects bad parameters and insufficient storage") {
    auto p = small_scene();
    const size_t bytes = size_t(p.camera.width) * p.camera.height * sizeof(uchar4);
    DeviceTarget target;
    REQUIRE(cudaMalloc(&target.pixels, bytes) == cudaSuccess);
    REQUIRE(cudaMemset(target.pixels, 0xA5, bytes) == cudaSuccess);
    CHECK(bhr::render_device(p, nullptr, bytes) == cudaErrorInvalidValue);
    CHECK(bhr::render_device(p, target.pixels, bytes - 1) == cudaErrorInvalidValue);
    auto* misaligned = reinterpret_cast<uchar4*>(reinterpret_cast<char*>(target.pixels) + 1);
    CHECK(bhr::render_device(p, misaligned, bytes) == cudaErrorInvalidValue);
    p.camera.width = -1;
    CHECK(bhr::render_device(p, target.pixels, bytes) == cudaErrorInvalidValue);
    std::vector<std::uint8_t> copied(bytes);
    REQUIRE(cudaMemcpy(copied.data(), target.pixels, bytes, cudaMemcpyDeviceToHost) == cudaSuccess);
    for (const auto byte : copied) CHECK(byte == 0xA5);
    bhr::Image host;
    host.allocate(1, 1);
    bhr::render(p, host);
    CHECK(host.width == 0);
    CHECK(host.height == 0);
    CHECK(host.rgba.empty());
}

TEST_CASE("Renderer honors each disk effect toggle") {
    const auto scene = small_scene();
    bhr::Image all_enabled;
    bhr::render(scene, all_enabled);
    REQUIRE_FALSE(all_enabled.rgba.empty());
    for (int effect = 0; effect < 3; ++effect) {
        auto p = scene;
        if (effect == 0) p.enable_doppler = false;
        if (effect == 1) p.enable_redshift = false;
        if (effect == 2) p.enable_beaming = false;
        bhr::Image changed;
        bhr::render(p, changed);
        REQUIRE(changed.rgba.size() == all_enabled.rgba.size());
        CAPTURE(effect);
        CHECK(changed.rgba != all_enabled.rgba);
    }
}

TEST_CASE("Renderer starfield toggle gates sampling and preserves fallback") {
    auto p = small_scene();
    SyntheticStarfield fixture;
    make_starfield(fixture);
    bhr::Image absent, disabled, enabled;
    bhr::render(p, absent);
    bhr::render(p, fixture.value, disabled);
    REQUIRE_FALSE(absent.rgba.empty());
    CHECK(disabled.rgba == absent.rgba);
    p.enable_starfield = true;
    bhr::render(p, fixture.value, enabled);
    REQUIRE(enabled.rgba.size() == absent.rgba.size());
    CHECK(enabled.rgba != absent.rgba);
    bhr::Image missing;
    bhr::render(p, missing);
    CHECK(missing.rgba == absent.rgba);

    DeviceTarget target;
    REQUIRE(cudaMalloc(&target.pixels, enabled.rgba.size()) == cudaSuccess);
    REQUIRE(bhr::render_device(p, fixture.value, target.pixels, enabled.rgba.size()) == cudaSuccess);
    std::vector<std::uint8_t> copied(enabled.rgba.size());
    REQUIRE(cudaMemcpy(copied.data(), target.pixels, copied.size(), cudaMemcpyDeviceToHost) == cudaSuccess);
    CHECK(copied == enabled.rgba);
}
