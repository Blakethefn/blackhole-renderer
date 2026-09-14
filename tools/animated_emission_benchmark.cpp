#include "bhr/cinematic_renderer.hpp"
#include "bhr/presets.hpp"
#include <cuda_runtime.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void check(cudaError_t status, const char* operation) {
    if (status != cudaSuccess) throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}
struct Events {
    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
    Events() { check(cudaEventCreate(&start), "Create start event"); check(cudaEventCreate(&stop), "Create stop event"); }
    ~Events() { cudaEventDestroy(start); cudaEventDestroy(stop); }
};
double measure(bool animated, const bhr::RenderParams& params, const bhr::AppearanceV1& appearance,
               bhr::CinematicBuffer& buffer, uchar4* output, size_t bytes, cudaStream_t stream) {
    Events events;
    constexpr int warmup = 2;
    constexpr int samples = 10;
    double total = 0.0;
    for (int frame = 0; frame < warmup + samples; ++frame) {
        const bhr::AnimatedCinematicRequest animated_request{params, appearance, {true, 7, .18f, .06f, 1.0f},
            {static_cast<uint64_t>(frame % 180), 180}};
        check(cudaEventRecord(events.start, stream), "Record benchmark start");
        const auto status = animated
            ? bhr::render_animated_device(animated_request, {}, buffer.target(), output, bytes, stream)
            : bhr::render_cinematic_device({params, appearance}, {}, buffer.target(), output, bytes, stream);
        check(status, "Enqueue benchmark frame");
        check(cudaEventRecord(events.stop, stream), "Record benchmark stop");
        check(cudaEventSynchronize(events.stop), "Synchronize benchmark frame");
        float milliseconds = 0.0f;
        check(cudaEventElapsedTime(&milliseconds, events.start, events.stop), "Read benchmark time");
        if (frame >= warmup) total += milliseconds;
    }
    return total / samples;
}
}

int main() {
    try {
        auto params = bhr::workbench_preset();
        params.camera.width = 1920;
        params.camera.height = 1080;
        params.enable_starfield = false;
        const bhr::AppearanceV1 appearance{.15f,.18f,.1f,.15f,1,{true,.06f,1}};
        const auto bytes = static_cast<size_t>(params.camera.width) * params.camera.height * sizeof(uchar4);
        bhr::CinematicBuffer buffer(params.camera.width, params.camera.height);
        uchar4* output = nullptr;
        cudaStream_t stream = nullptr;
        check(cudaMalloc(&output, bytes), "Allocate benchmark output");
        check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create benchmark stream");
        const auto static_ms = measure(false, params, appearance, buffer, output, bytes, stream);
        const auto animated_ms = measure(true, params, appearance, buffer, output, bytes, stream);
        std::printf("resolution=%dx%d static_gpu_ms=%.4f animated_gpu_ms=%.4f overhead_ms=%.4f overhead_percent=%.3f\n",
            params.camera.width, params.camera.height, static_ms, animated_ms,
            animated_ms-static_ms, 100.0*(animated_ms-static_ms)/static_ms);
        check(cudaStreamDestroy(stream), "Destroy benchmark stream");
        stream = nullptr;
        check(cudaFree(output), "Free benchmark output");
        output = nullptr;
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Animated benchmark failed: %s\n", error.what());
        return 1;
    }
}
