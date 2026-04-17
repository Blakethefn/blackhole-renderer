// SDL2 + OpenGL 3.3 + Dear ImGui bootstrap.
// Opens a window, runs the ImGui frame loop, displays a "Hello" panel showing
// the detected CUDA device. No rendering yet — that arrives in Task 22.

#include <glad/glad.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "bhr/hello.hpp"

#include <cstdio>

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr const char* kGlslVersion = "#version 330 core";

} // namespace

int main(int, char**) {
    // --- SDL initialization --------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    auto* window = SDL_CreateWindow(
        "blackhole-workbench",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kInitialWidth, kInitialHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    auto gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // vsync

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "glad failed to load OpenGL\n");
        return 1;
    }

    // --- ImGui initialization ------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    // --- Main loop ------------------------------------------------------------
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT
                && ev.window.event == SDL_WINDOWEVENT_CLOSE
                && ev.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hello");
        ImGui::Text("blackhole-workbench v0.0.1");
        ImGui::Separator();
        ImGui::Text("CUDA device: %s", bhr::cuda_device_name());
        ImGui::Text("Sanity check: 1 + 2 = %d (from GPU)", bhr::cuda_add(1, 2));
        ImGui::End();

        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // --- Shutdown -------------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
