// Reproducible presentation benchmark: real CUDA/GL ownership transitions and
// ImGui drawing in a hidden window, paced at 60 UI ticks/s by default. This
// measures completed GPU textures, not monitor scanout or visible-window vsync.
#include <glad/glad.h>
#include <SDL.h>
#include <cuda_gl_interop.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "bhr/presets.hpp"
#include "cli_options.hpp"
#include "workbench/viewport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kDefaultWarmupFrames = 2;
constexpr int kDefaultMeasuredFrames = 10;
constexpr int kDefaultUiHz = 60;
constexpr int kMaximumFrames = 10000;
constexpr int kMaximumUiHz = 1000;
constexpr Uint32 kHeartbeatIntervalMs = 16;
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr auto kRenderTimeout = std::chrono::seconds(120);

void check_sdl(bool okay, const char* operation) {
    if (!okay) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

void check_cuda(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

struct Options {
    bhr::RenderParams params = bhr::workbench_preset();
    int frames = kDefaultMeasuredFrames;
    int warmup = kDefaultWarmupFrames;
    int ui_hz = kDefaultUiHz;
};

void usage(const char* executable) {
    std::printf("Usage: %s [--resolution WxH] [--integrator rk45|geokerr] "
                "[--frames N] [--warmup N] [--ui-hz N]\n"
                "Defaults: shared workbench preset, 256x144 RK45, 2 warmup, 10 measured, 60 UI Hz.\n"
                "Runs a hidden SDL/OpenGL/ImGui window; reports GPU render, completed preview,\n"
                "UI frame intervals, and timer-thread SDL event delivery latency.\n",
                executable);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option != "--resolution" && option != "--integrator" && option != "--frames"
            && option != "--warmup" && option != "--ui-hz")
            throw std::runtime_error("Unknown argument: " + option);
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + option);
        const std::string value = argv[++i];
        if (option == "--resolution") {
            if (!bhr::cli::parse_resolution(value, options.params.camera.width, options.params.camera.height))
                throw std::runtime_error("--resolution requires positive integers in WxH format");
        } else if (option == "--integrator") {
            if (!bhr::cli::parse_integrator(value, options.params.integrator))
                throw std::runtime_error("--integrator requires rk45 or geokerr");
        } else {
            int count = 0;
            const int maximum = option == "--ui-hz" ? kMaximumUiHz : kMaximumFrames;
            if (!bhr::cli::parse_positive_integer(value, count) || count > maximum)
                throw std::runtime_error(option + " requires an integer from 1 to " + std::to_string(maximum));
            if (option == "--frames") options.frames = count;
            else if (option == "--warmup") options.warmup = count;
            else options.ui_hz = count;
        }
    }
    if (const char* invalid = bhr::validation_error(options.params)) throw std::runtime_error(invalid);
    return options;
}

class Application final {
public:
    Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application() {
        if (gl_backend_) ImGui_ImplOpenGL3_Shutdown();
        if (sdl_backend_) ImGui_ImplSDL2_Shutdown();
        if (imgui_) ImGui::DestroyContext();
        if (context_) SDL_GL_DeleteContext(context_);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void initialize() {
        check_sdl(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0, "Initialize SDL");
        check_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
                  "Set core GL profile");
        check_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) == 0, "Set GL major version");
        check_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) == 0, "Set GL minor version");
        check_sdl(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) == 0, "Enable double buffering");
        window = SDL_CreateWindow("Black Hole Preview Benchmark", 0, 0, kWindowWidth, kWindowHeight,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check_sdl(window != nullptr, "Create hidden benchmark window");
        context_ = SDL_GL_CreateContext(window);
        check_sdl(context_ != nullptr, "Create benchmark GL context");
        check_sdl(SDL_GL_MakeCurrent(window, context_) == 0, "Select benchmark GL context");
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
            throw std::runtime_error("Could not load OpenGL 3.3 functions");
        if (SDL_GL_SetSwapInterval(0) != 0)
            std::fprintf(stderr, "Swap interval could not be disabled: %s\n", SDL_GetError());
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui_ = true;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        sdl_backend_ = ImGui_ImplSDL2_InitForOpenGL(window, context_);
        if (!sdl_backend_) throw std::runtime_error("Could not initialize ImGui SDL backend");
        gl_backend_ = ImGui_ImplOpenGL3_Init("#version 330 core");
        if (!gl_backend_) throw std::runtime_error("Could not initialize ImGui OpenGL backend");
    }

    void draw(const bhr::workbench::Viewport& viewport, int completed, int target) const {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Preview benchmark", nullptr, ImGuiWindowFlags_NoDecoration
                     | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Completed: %d / %d | %s | GPU %.3f ms | preview %.3f ms",
                    completed, target, viewport.busy() ? "Rendering" : "Ready",
                    viewport.last_gpu_ms(), viewport.last_frame_ms());
        if (viewport.has_image()) {
            const auto available = ImGui::GetContentRegionAvail();
            const float scale = std::min(available.x / static_cast<float>(viewport.image_width()),
                                         available.y / static_cast<float>(viewport.image_height()));
            if (scale > 0.0f) {
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(viewport.texture())),
                             ImVec2(scale * static_cast<float>(viewport.image_width()),
                                    scale * static_cast<float>(viewport.image_height())));
            }
        }
        ImGui::End();
        ImGui::Render();
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.02f, 0.025f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (const auto error = glGetError(); error != GL_NO_ERROR)
            throw std::runtime_error("OpenGL benchmark drawing failed: " + std::to_string(error));
    }

    SDL_Window* window = nullptr;
private:
    SDL_GLContext context_ = nullptr;
    bool imgui_ = false, sdl_backend_ = false, gl_backend_ = false;
};

// Generate events independently of the UI so a blocked UI does not hide its own
// latency. SDL timestamps each pushed event. Join before SDL/context teardown.
class Heartbeat final {
public:
    Heartbeat() {
        event_type = SDL_RegisterEvents(1);
        check_sdl(event_type != static_cast<Uint32>(-1), "Register benchmark heartbeat");
        thread_ = SDL_CreateThread(run, "preview-heartbeat", this);
        check_sdl(thread_ != nullptr, "Start benchmark heartbeat");
    }
    Heartbeat(const Heartbeat&) = delete;
    Heartbeat& operator=(const Heartbeat&) = delete;
    ~Heartbeat() {
        stop_.store(true, std::memory_order_relaxed);
        if (thread_) SDL_WaitThread(thread_, nullptr);
    }
    bool failed() const { return failed_.load(std::memory_order_relaxed); }
    Uint32 event_type = 0;
private:
    static int run(void* userdata) {
        auto& heartbeat = *static_cast<Heartbeat*>(userdata);
        while (!heartbeat.stop_.load(std::memory_order_relaxed)) {
            SDL_Delay(kHeartbeatIntervalMs);
            SDL_Event event{};
            event.type = heartbeat.event_type;
            if (SDL_PushEvent(&event) != 1) {
                heartbeat.failed_.store(true, std::memory_order_relaxed);
                return 1;
            }
        }
        return 0;
    }
    SDL_Thread* thread_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
};

struct Statistics {
    std::uint64_t count = 0;
    double total = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = 0.0;
    void add(double sample) {
        ++count;
        total += sample;
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);
    }
    double mean() const { return count == 0 ? 0.0 : total / static_cast<double>(count); }
};

struct Sample { float gpu_ms; float preview_ms; };

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

int benchmark(const Options& options) {
    Application app;
    app.initialize();
    unsigned int device_count = 0;
    int device = 0;
    check_cuda(cudaGLGetDevices(&device_count, &device, 1, cudaGLDeviceListCurrentFrame),
               "Find CUDA device for OpenGL");
    if (device_count == 0) throw std::runtime_error("Current OpenGL context has no CUDA device");
    check_cuda(cudaSetDevice(device), "Select CUDA/OpenGL device");
    cudaDeviceProp properties{};
    check_cuda(cudaGetDeviceProperties(&properties, device), "Read GPU properties");
    const auto& params = options.params;
    const char* integrator = params.integrator == bhr::IntegratorKind::kRK45 ? "rk45" : "geokerr";
    std::printf("GPU: %s; GL renderer: %s; SDL video: %s\n", properties.name,
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)), SDL_GetCurrentVideoDriver());
    std::printf("Scene: workbench_preset; resolution=%dx%d integrator=%s warmup=%d frames=%d "
                "ui_hz=%d hidden_window=%dx%d swap_interval=%d\n",
                params.camera.width, params.camera.height, integrator, options.warmup, options.frames,
                options.ui_hz, kWindowWidth, kWindowHeight, SDL_GL_GetSwapInterval());
    std::printf("spin=%.3f camera=(r=%.1f incl=%.1f azimuth=%.1f fov=%.1f) "
                "disk=(inner=%.3f outer=%.1f temperature=%.1f brightness=%.1f) "
                "doppler=on redshift=on beaming=on starfield=off\n",
                params.spin, params.camera.r_cam, params.camera.theta_cam_deg, params.camera.phi_cam_deg,
                params.camera.fov_deg, params.disk.r_inner, params.disk.r_outer,
                params.disk.peak_temp_K, params.disk.brightness);
    if (params.integrator == bhr::IntegratorKind::kGeokerr)
        std::printf("Geokerr is approximate; RK45 remains the correctness reference.\n");
    std::fflush(stdout);

    const bhr::Starfield empty;
    bhr::workbench::Viewport viewport;
    Heartbeat heartbeat;
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(options.frames));
    Statistics gpu, preview, ui_interval, ui_work, event_latency;
    const auto ui_period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / options.ui_hz));
    int submissions = 0, completions = 0;
    const int total_frames = options.warmup + options.frames;
    bool measured = false;
    Clock::time_point measurement_start{}, measurement_end{}, previous_tick{}, last_submission{};
    while (completions < total_frames) {
        const auto tick_start = Clock::now();
        if (measured) ui_interval.add(milliseconds(tick_start - previous_tick));
        previous_tick = tick_start;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) throw std::runtime_error("Preview benchmark interrupted");
            if (event.type == heartbeat.event_type && measured)
                event_latency.add(static_cast<double>(SDL_GetTicks() - event.common.timestamp));
        }
        if (heartbeat.failed()) throw std::runtime_error("Could not enqueue benchmark heartbeat event");
        if (viewport.poll()) {
            ++completions;
            if (completions > options.warmup) {
                const Sample sample{viewport.last_gpu_ms(), viewport.last_frame_ms()};
                samples.push_back(sample);
                gpu.add(sample.gpu_ms);
                preview.add(sample.preview_ms);
                if (completions == total_frames) measurement_end = Clock::now();
            }
        }
        if (!viewport.error().empty()) throw std::runtime_error(viewport.error());
        if (viewport.busy() && tick_start - last_submission > kRenderTimeout)
            throw std::runtime_error("A preview did not complete within 120 seconds");
        if (!viewport.busy() && submissions < total_frames) {
            if (submissions == options.warmup) {
                SDL_FlushEvent(heartbeat.event_type);
                measurement_start = Clock::now();
                measured = true;
            }
            last_submission = Clock::now();
            if (!viewport.submit(params, empty)) throw std::runtime_error("Preview submission failed: " + viewport.error());
            ++submissions;
        }
        app.draw(viewport, completions, total_frames);
        if (measured) ui_work.add(milliseconds(Clock::now() - tick_start));
        if (completions < total_frames) std::this_thread::sleep_until(tick_start + ui_period);
    }
    if (!viewport.shutdown()) throw std::runtime_error(viewport.error());

    for (std::size_t index = 0; index < samples.size(); ++index) {
        std::printf("sample=%zu resolution=%dx%d integrator=%s gpu_ms=%.3f preview_ms=%.3f\n",
                    index + 1, params.camera.width, params.camera.height, integrator,
                    samples[index].gpu_ms, samples[index].preview_ms);
    }
    const double elapsed_ms = milliseconds(measurement_end - measurement_start);
    const double throughput_fps = static_cast<double>(options.frames) * 1000.0 / elapsed_ms;
    std::printf("mean_gpu_ms=%.3f min_gpu_ms=%.3f max_gpu_ms=%.3f "
                "mean_preview_ms=%.3f min_preview_ms=%.3f max_preview_ms=%.3f "
                "preview_throughput_fps=%.3f measured_elapsed_ms=%.3f "
                "target_mean_3fps=%s all_frames_within_333ms=%s\n",
                gpu.mean(), gpu.minimum, gpu.maximum, preview.mean(), preview.minimum, preview.maximum,
                throughput_fps, elapsed_ms, throughput_fps >= 3.0 ? "met" : "missed",
                preview.maximum <= 1000.0 / 3.0 ? "yes" : "no");
    std::printf("ui_frames=%llu mean_ui_interval_ms=%.3f max_ui_interval_ms=%.3f "
                "mean_ui_work_ms=%.3f max_ui_work_ms=%.3f "
                "heartbeat_events=%llu mean_event_latency_ms=%.3f max_event_latency_ms=%.3f\n",
                static_cast<unsigned long long>(ui_work.count), ui_interval.mean(), ui_interval.maximum,
                ui_work.mean(), ui_work.maximum, static_cast<unsigned long long>(event_latency.count),
                event_latency.mean(), event_latency.maximum);
    std::printf("Preview timing ends after the GL upload fence; warmup excludes allocation. "
                "UI work includes ImGui drawing and swap; UI intervals include pacing. "
                "Hidden-window results exclude monitor scanout.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
            usage(argv[0]);
            return 0;
        }
        return benchmark(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Preview benchmark failed: %s\n", error.what());
        return 1;
    }
}
