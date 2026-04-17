// SDL2 + OpenGL 3.3 + Dear ImGui + CUDA-GL interop.
// Allocates an RGBA8 GL texture, registers it with CUDA, and on each frame
// runs a CUDA kernel that writes a pulsing gradient. ImGui displays the
// texture inside a panel.

#include <glad/glad.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "bhr/hello.hpp"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include <cstdio>
#include <cstdint>

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr int kTextureWidth = 512;
constexpr int kTextureHeight = 288;
constexpr const char* kGlslVersion = "#version 330 core";

struct InteropTexture {
    GLuint gl_tex = 0;
    cudaGraphicsResource* cuda_res = nullptr;
    int width = 0;
    int height = 0;
};

InteropTexture make_interop_texture(int w, int h) {
    InteropTexture t{};
    t.width = w;
    t.height = h;

    glGenTextures(1, &t.gl_tex);
    glBindTexture(GL_TEXTURE_2D, t.gl_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    cudaGraphicsGLRegisterImage(&t.cuda_res, t.gl_tex, GL_TEXTURE_2D,
                                cudaGraphicsRegisterFlagsWriteDiscard);
    return t;
}

void destroy_interop_texture(InteropTexture& t) {
    if (t.cuda_res) cudaGraphicsUnregisterResource(t.cuda_res);
    if (t.gl_tex) glDeleteTextures(1, &t.gl_tex);
    t = {};
}

void run_gradient_once(InteropTexture& t, float seconds) {
    cudaGraphicsMapResources(1, &t.cuda_res, 0);

    cudaArray_t array = nullptr;
    cudaGraphicsSubResourceGetMappedArray(&array, t.cuda_res, 0, 0);

    cudaResourceDesc res_desc{};
    res_desc.resType = cudaResourceTypeArray;
    res_desc.res.array.array = array;

    cudaSurfaceObject_t surface = 0;
    cudaCreateSurfaceObject(&surface, &res_desc);

    bhr::cuda_gradient(static_cast<unsigned long long>(surface),
                       t.width, t.height, seconds);

    cudaDestroySurfaceObject(surface);
    cudaGraphicsUnmapResources(1, &t.cuda_res, 0);
}

} // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
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
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "glad failed to load OpenGL\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    auto tex = make_interop_texture(kTextureWidth, kTextureHeight);

    const Uint64 start_ticks = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

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

        const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - start_ticks) / freq;
        run_gradient_once(tex, static_cast<float>(elapsed));

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hello");
        ImGui::Text("blackhole-workbench v0.0.1");
        ImGui::Separator();
        ImGui::Text("CUDA device: %s", bhr::cuda_device_name());
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::End();

        ImGui::Begin("Viewport");
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(tex.gl_tex)),
                     ImVec2(static_cast<float>(kTextureWidth),
                            static_cast<float>(kTextureHeight)));
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

    destroy_interop_texture(tex);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
