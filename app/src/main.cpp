// The UI presents the last completed texture while CUDA renders into a separate
// persistent PBO. See workbench/viewport.hpp for ownership and completion rules.
#include <glad/glad.h>
#include <SDL.h>
#include <cuda_gl_interop.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "workbench/ui_panels.hpp"
#include "workbench/encoded_framebuffer.hpp"
#include "bhr/cinematic_renderer.hpp"
#include "shot_options.hpp"
#include <cstdio>
#include <exception>

namespace {
struct Application {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool imgui = false;
    bool sdl_backend = false;
    bool gl_backend = false;

    ~Application() {
        if (gl_backend) ImGui_ImplOpenGL3_Shutdown();
        if (sdl_backend) ImGui_ImplSDL2_Shutdown();
        if (imgui) ImGui::DestroyContext();
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool initialize() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return false;
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0
            || SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0
            || SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 0
            || SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0) return false;
        window = SDL_CreateWindow("Black Hole Workbench", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 1280, 720,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) return false;
        context = SDL_GL_CreateContext(window);
        if (!context || SDL_GL_MakeCurrent(window, context) != 0) return false;
        if (SDL_GL_SetSwapInterval(1) != 0)
            std::fprintf(stderr, "Vsync unavailable: %s\n", SDL_GetError());
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            std::fprintf(stderr, "Failed to load OpenGL 3.3 functions\n");
            return false;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui = true;
        // Do not read or overwrite the user's existing imgui.ini.
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        sdl_backend = ImGui_ImplSDL2_InitForOpenGL(window, context);
        gl_backend = ImGui_ImplOpenGL3_Init("#version 330 core");
        return sdl_backend && gl_backend;
    }
};

struct StarfieldOwner {
    bhr::Starfield value{};
    ~StarfieldOwner() { bhr::destroy_starfield(value); }
};

bhr::workbench::State shortcut(const bhr::workbench::State& state, SDL_Keycode key) {
    using namespace bhr::workbench;
    if (key == SDLK_F1) return controls_toggled(state);
    if (ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantTextInput) return state;
    if (key == SDLK_r) return changed(state, bhr::workbench_preset());
    auto params = state.params;
    if (key == SDLK_SPACE) {
        params.integrator = params.integrator == bhr::IntegratorKind::kRK45
            ? bhr::IntegratorKind::kGeokerr : bhr::IntegratorKind::kRK45;
        return changed(state, params);
    }
    constexpr int widths[] = {256, 512, 1024, 1920};
    constexpr int heights[] = {144, 288, 576, 1080};
    if (key >= SDLK_1 && key <= SDLK_4) {
        const int index = key - SDLK_1;
        params.camera.width = widths[index];
        params.camera.height = heights[index];
        return changed(state, params);
    }
    return state;
}

int run(const std::optional<bhr::cli::ShotSelection>& selected) {
    const auto loaded=selected?std::make_unique<bhr::RenderDocument>(bhr::load_render_document(selected->file)):nullptr;
    const auto* cinematic=loaded?std::get_if<bhr::CinematicRenderDocument>(loaded.get()):nullptr;
    const auto* document=loaded?(cinematic?&cinematic->scene_shots:&std::get<bhr::CinematicDocument>(*loaded)):nullptr;
    const auto frame=document?std::make_unique<bhr::FrameSample>(bhr::evaluate_frame(*document,selected->id,selected->frame)):nullptr;
    Application application;
    if (!application.initialize()) {
        std::fprintf(stderr, "Workbench initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    unsigned int device_count = 0;
    int device = 0;
    auto err = cudaGLGetDevices(&device_count, &device, 1, cudaGLDeviceListCurrentFrame);
    if (err != cudaSuccess || device_count == 0) {
        std::fprintf(stderr, "No CUDA device for this OpenGL context: %s\n", cudaGetErrorString(err));
        return 1;
    }
    if ((err = cudaSetDevice(device)) != cudaSuccess) {
        std::fprintf(stderr, "CUDA device selection failed: %s\n", cudaGetErrorString(err));
        return 1;
    }
    cudaDeviceProp properties{};
    if ((err = cudaGetDeviceProperties(&properties, device)) != cudaSuccess) {
        std::fprintf(stderr, "CUDA device query failed: %s\n", cudaGetErrorString(err));
        return 1;
    }

    using namespace bhr::workbench;
    StarfieldOwner starfield; // Outlives all in-flight viewport work.
    Viewport viewport;
    State state{};
    if(frame) {
        state=changed(state,frame->params);
        if(frame->params.enable_starfield) {
            const auto path=bhr::resolve_starfield(document->scene,selected->file);
            const bool ok=cinematic?bhr::load_cinematic_starfield(path,starfield.value)
                :bhr::load_starfield(path,16384,starfield.value);
            if(!ok) throw std::runtime_error("Cannot load selected frame starfield");
        }
        const std::string title="Black Hole Workbench — "+selected->id+" / frame "+std::to_string(selected->frame)
            +(cinematic?" / cinematic":" / legacy");
        SDL_SetWindowTitle(application.window,title.c_str());
    }
    Controls controls{};
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE
                && event.window.windowID == SDL_GetWindowID(application.window)) running = false;
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if(!selected) state = shortcut(state, event.key.keysym.sym);
                if (event.key.keysym.sym == SDLK_ESCAPE && !ImGui::GetIO().WantCaptureKeyboard) running = false;
            }
        }
        if (!running) break;
        if (viewport.poll()) {
            state = completed(state);
            if (controls.continuous) state = requested(state);
        }
        if (state.rendering && !viewport.busy() && !viewport.error().empty())
            state = failed_render(state, viewport.error());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if(selected) {
            if(viewport.has_image()) {
                const auto size=ImGui::GetIO().DisplaySize;
                const float scale=std::min(size.x/viewport.image_width(),size.y/viewport.image_height());
                const ImVec2 extent(viewport.image_width()*scale,viewport.image_height()*scale);
                const ImVec2 origin((size.x-extent.x)/2,(size.y-extent.y)/2);
                ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewport.texture())),
                    origin,ImVec2(origin.x+extent.x,origin.y+extent.y));
            }
            if(!state.error.empty()) {
                ImGui::Begin("Selected frame error");ImGui::TextWrapped("%s",state.error.c_str());ImGui::End();
            }
        } else draw_viewport(state, viewport);
        if(!selected) state = draw_controls(state, controls, viewport, starfield.value, properties.name);
        if (!state.rendering && state.params.enable_starfield && !starfield.value.is_valid())
            state = failed_render(state, "Load the preset's EXR starfield or disable starfield sampling.");
        if (can_submit(state, starfield.value.is_valid()) && !viewport.busy()) {
            const bool accepted=cinematic?viewport.submit(bhr::CinematicRequest{state.params,cinematic->appearance},starfield.value)
                :viewport.submit(state.params,starfield.value);
            if (accepted) state = submitted(state);
            else if (!viewport.error().empty()) state = failed_render(submitted(state), viewport.error());
        }

        ImGui::Render();
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(application.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.02f, 0.025f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            EncodedFramebuffer encoded;
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        SDL_GL_SwapWindow(application.window);
        if (SDL_GetWindowFlags(application.window) & SDL_WINDOW_MINIMIZED) SDL_Delay(16);
    }
    if (!viewport.shutdown()) return 1;
    return bhr::destroy_starfield(starfield.value) ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::pair<std::string,std::string>> options;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--help"||option=="-h") {
                std::puts("Usage: blackhole-workbench [--shot-file FILE --shot-id ID --frame INDEX]\nSelected frames are read-only; Escape closes the viewer.");return 0;
            }
            if(!bhr::cli::is_shot_option(option)) throw std::runtime_error("Unknown workbench option: "+option);
            if(i+1>=argc) throw std::runtime_error("Missing value for "+option);
            options.emplace_back(option,argv[++i]);
        }
        // Share the selected-frame validation with the CLI; the workbench has
        // no file output, so satisfy that parser field with a private sentinel.
        if(!options.empty()) options.emplace_back("--output","workbench");
        return run(bhr::cli::shot_selection(options));
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Workbench failed: %s\n", error.what());
        return 1;
    }
}
