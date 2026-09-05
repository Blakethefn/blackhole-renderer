// Reproducible GPU benchmark. Allocation and warmup are excluded from samples.
#include "bhr/presets.hpp"
#include "bhr/renderer.hpp"
#include "cli_options.hpp"

#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr int kDefaultWarmupFrames = 2;
constexpr int kDefaultMeasuredFrames = 10;
constexpr int kMaximumFrames = 10000;

void check_cuda(cudaError_t status, const char* operation) {
    if (status != cudaSuccess) throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

bool cleanup_cuda(cudaError_t status, const char* operation) noexcept {
    if (status == cudaSuccess) return true;
    std::fprintf(stderr, "CUDA cleanup failed (%s): %s\n", operation, cudaGetErrorString(status));
    return false;
}

class BenchmarkTarget {
public:
    BenchmarkTarget() = default;
    BenchmarkTarget(const BenchmarkTarget&) = delete;
    BenchmarkTarget& operator=(const BenchmarkTarget&) = delete;
    ~BenchmarkTarget() { close(); }

    void initialize(size_t capacity_bytes) {
        check_cuda(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create benchmark stream");
        check_cuda(cudaEventCreate(&start), "Create start event");
        check_cuda(cudaEventCreate(&stop), "Create stop event");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&pixels), capacity_bytes), "Allocate persistent target");
    }

    bool close() noexcept {
        if (stream && !cleanup_cuda(cudaStreamSynchronize(stream), "synchronize")) return false;
        if (pixels) {
            if (!cleanup_cuda(cudaFree(pixels), "free pixels")) return false;
            pixels = nullptr;
        }
        if (start) {
            if (!cleanup_cuda(cudaEventDestroy(start), "destroy start event")) return false;
            start = nullptr;
        }
        if (stop) {
            if (!cleanup_cuda(cudaEventDestroy(stop), "destroy stop event")) return false;
            stop = nullptr;
        }
        if (stream) {
            if (!cleanup_cuda(cudaStreamDestroy(stream), "destroy stream")) return false;
            stream = nullptr;
        }
        return true;
    }

    uchar4* pixels = nullptr;
    cudaStream_t stream = nullptr;
    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
};

struct Sample {
    double gpu_ms;
    double wall_ms;
};

Sample render_sample(const bhr::RenderParams& params, const BenchmarkTarget& target, size_t capacity_bytes) {
    const auto wall_start = std::chrono::steady_clock::now();
    check_cuda(cudaEventRecord(target.start, target.stream), "Record start event");
    check_cuda(bhr::render_device(params, target.pixels, capacity_bytes, target.stream), "Dispatch render");
    check_cuda(cudaEventRecord(target.stop, target.stream), "Record stop event");
    check_cuda(cudaEventSynchronize(target.stop), "Wait for render");
    const double wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - wall_start).count();
    float gpu_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&gpu_ms, target.start, target.stop), "Read GPU timing");
    return {gpu_ms, wall_ms};
}

void usage(const char* executable) {
    std::printf("Usage: %s [--resolution WxH] [--integrator rk45|geokerr] [--frames N] [--warmup N]\n"
                "Defaults: shared workbench preset, 256x144 RK45, 2 warmup frames, 10 measured frames.\n"
                "Reports CUDA-event rendering time and synchronized wall frame time; no window or readback.\n",
                executable);
}

int run(int argc, char** argv) {
    auto params = bhr::workbench_preset();
    int frames = kDefaultMeasuredFrames;
    int warmup = kDefaultWarmupFrames;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") {
            usage(argv[0]);
            return 0;
        }
        if (option != "--resolution" && option != "--integrator" && option != "--frames" && option != "--warmup") {
            throw std::runtime_error("Unknown argument: " + option);
        }
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + option);
        const std::string value = argv[++i];
        if (option == "--resolution") {
            if (!bhr::cli::parse_resolution(value, params.camera.width, params.camera.height)) {
                throw std::runtime_error("--resolution requires positive integers in WxH format");
            }
        } else if (option == "--integrator") {
            if (!bhr::cli::parse_integrator(value, params.integrator)) {
                throw std::runtime_error("--integrator requires rk45 or geokerr");
            }
        } else {
            int count = 0;
            if (!bhr::cli::parse_positive_integer(value, count) || count > kMaximumFrames) {
                throw std::runtime_error(option + " requires an integer from 1 to " + std::to_string(kMaximumFrames));
            }
            if (option == "--frames") frames = count;
            else warmup = count;
        }
    }
    if (const char* invalid = bhr::validation_error(params)) throw std::runtime_error(invalid);
    int device = 0;
    check_cuda(cudaGetDevice(&device), "Select current CUDA device");
    cudaDeviceProp properties{};
    check_cuda(cudaGetDeviceProperties(&properties, device), "Read GPU properties");
    int runtime_version = 0;
    int driver_version = 0;
    check_cuda(cudaRuntimeGetVersion(&runtime_version), "Read CUDA runtime version");
    check_cuda(cudaDriverGetVersion(&driver_version), "Read CUDA driver version");
    const char* integrator = params.integrator == bhr::IntegratorKind::kRK45 ? "rk45" : "geokerr";
    std::printf("GPU: %s; CUDA runtime=%d driver=%d; device=%d\n", properties.name,
                runtime_version, driver_version, device);
    std::printf("Scene: workbench_preset; resolution=%dx%d integrator=%s warmup=%d frames=%d\n",
                params.camera.width, params.camera.height, integrator, warmup, frames);
    std::printf("spin=%.3f camera=(r=%.1f incl=%.1f azimuth=%.1f fov=%.1f) "
                "disk=(inner=%.3f outer=%.1f temperature=%.1f brightness=%.1f) "
                "doppler=on redshift=on beaming=on starfield=off\n",
                params.spin, params.camera.r_cam, params.camera.theta_cam_deg, params.camera.phi_cam_deg,
                params.camera.fov_deg, params.disk.r_inner, params.disk.r_outer,
                params.disk.peak_temp_K, params.disk.brightness);
    if (params.integrator == bhr::IntegratorKind::kGeokerr) {
        std::printf("Geokerr is approximate and has known image-agreement limitations; RK45 is the correctness reference.\n");
    }

    const size_t capacity_bytes = static_cast<size_t>(params.camera.width) * params.camera.height * sizeof(uchar4);
    BenchmarkTarget target;
    target.initialize(capacity_bytes);
    for (int frame = 0; frame < warmup; ++frame) render_sample(params, target, capacity_bytes);

    double total_gpu_ms = 0.0;
    double total_wall_ms = 0.0;
    double min_gpu_ms = std::numeric_limits<double>::max();
    double max_gpu_ms = 0.0;
    for (int frame = 0; frame < frames; ++frame) {
        const Sample sample = render_sample(params, target, capacity_bytes);
        total_gpu_ms += sample.gpu_ms;
        total_wall_ms += sample.wall_ms;
        min_gpu_ms = std::min(min_gpu_ms, sample.gpu_ms);
        max_gpu_ms = std::max(max_gpu_ms, sample.gpu_ms);
        std::printf("sample=%d resolution=%dx%d integrator=%s gpu_ms=%.3f wall_ms=%.3f\n",
                    frame + 1, params.camera.width, params.camera.height, integrator, sample.gpu_ms, sample.wall_ms);
    }
    const double mean_gpu_ms = total_gpu_ms / frames;
    const double mean_wall_ms = total_wall_ms / frames;
    std::printf("mean_gpu_ms=%.3f min_gpu_ms=%.3f max_gpu_ms=%.3f gpu_fps=%.3f "
                "mean_wall_ms=%.3f wall_fps=%.3f target_3fps=%s\n",
                mean_gpu_ms, min_gpu_ms, max_gpu_ms, 1000.0 / mean_gpu_ms,
                mean_wall_ms, 1000.0 / mean_wall_ms, mean_wall_ms <= 1000.0 / 3.0 ? "met" : "missed");
    return target.close() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }
}
