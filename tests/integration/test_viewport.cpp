#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <glad/glad.h>
#include <SDL.h>
#include <cuda_gl_interop.h>

#include "workbench/viewport.hpp"
#include "bhr/image.hpp"
#include "bhr/renderer.hpp"
#include "bhr/cinematic_renderer.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using bhr::workbench::Viewport;
using Clock = std::chrono::steady_clock;

static_assert(!std::is_copy_constructible_v<Viewport>);
static_assert(!std::is_move_constructible_v<Viewport>);

bhr::RenderParams small_scene() {
    bhr::RenderParams params{};
    params.camera.width = 32;
    params.camera.height = 18;
    return params;
}

void await_image(Viewport& viewport) {
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    bool completed = false;
    while (viewport.busy() && Clock::now() < deadline) {
        completed = viewport.poll() || completed;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    INFO(viewport.error());
    REQUIRE(viewport.error().empty());
    REQUIRE_FALSE(viewport.busy());
    REQUIRE(completed);
    REQUIRE(viewport.has_image());
    CHECK_FALSE(viewport.poll());
}

std::vector<unsigned char> read_texture(const Viewport& viewport) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(viewport.image_width()) * viewport.image_height() * 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, viewport.texture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    REQUIRE(glGetError() == GL_NO_ERROR);
    return pixels;
}

TEST_CASE("viewport publishes completed GPU images and reuses presentation resources") {
    Viewport viewport;
    const bhr::Starfield empty;
    const auto params = small_scene();
    CHECK_FALSE(viewport.busy());
    CHECK_FALSE(viewport.has_image());
    CHECK(viewport.texture() == 0);

    REQUIRE(viewport.submit(params, empty));
    CHECK(viewport.busy());
    CHECK_FALSE(viewport.has_image());
    CHECK_FALSE(viewport.submit(params, empty));
    await_image(viewport);
    CHECK(viewport.completion_count() == 1);
    CHECK(viewport.image_width() == params.camera.width);
    CHECK(viewport.image_height() == params.camera.height);
    CHECK(viewport.last_gpu_ms() > 0.0f);
    CHECK(viewport.last_frame_ms() >= viewport.last_gpu_ms());

    bhr::Image host;
    bhr::render(params, empty, host);
    CHECK(read_texture(viewport) == host.rgba);
    const auto first_texture = viewport.texture();

    auto changed = params;
    changed.disk.brightness = 0.0f;
    REQUIRE(viewport.submit(changed, empty));
    CHECK(viewport.texture() == first_texture);
    CHECK(read_texture(viewport) == host.rgba);
    await_image(viewport);
    const auto second_texture = viewport.texture();
    CHECK(second_texture != first_texture);
    CHECK(read_texture(viewport) != host.rgba);
    REQUIRE(viewport.submit(params, empty));
    await_image(viewport);
    CHECK(viewport.texture() == first_texture);
    CHECK(viewport.completion_count() == 3);

    auto resized = params;
    resized.camera.width = 48;
    resized.camera.height = 27;
    REQUIRE(viewport.submit(resized, empty));
    CHECK(viewport.texture() == first_texture);
    CHECK(viewport.image_width() == params.camera.width);
    CHECK(viewport.image_height() == params.camera.height);
    await_image(viewport);
    CHECK(viewport.image_width() == resized.camera.width);
    CHECK(viewport.image_height() == resized.camera.height);
    bhr::render(resized, empty, host);
    CHECK(read_texture(viewport) == host.rgba);

    const auto resized_texture = viewport.texture();
    auto invalid = resized;
    invalid.camera.width = 0;
    CHECK_FALSE(viewport.submit(invalid, empty));
    CHECK_FALSE(viewport.error().empty());
    CHECK_FALSE(viewport.busy());
    CHECK(viewport.texture() == resized_texture);
    REQUIRE(viewport.submit(params, empty));
    await_image(viewport);
    CHECK(viewport.completion_count() == 5);
    REQUIRE(viewport.shutdown());
    CHECK_FALSE(viewport.has_image());
    CHECK_FALSE(viewport.busy());
    CHECK(viewport.texture() == 0);
    CHECK(viewport.shutdown());
}

TEST_CASE("viewport shutdown releases resources while a render is in flight") {
    const bhr::Starfield empty;
    Viewport viewport;
    REQUIRE(viewport.submit(small_scene(), empty));
    REQUIRE(viewport.busy());
    REQUIRE(viewport.shutdown());
    CHECK_FALSE(viewport.busy());
    CHECK_FALSE(viewport.has_image());
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(cudaGetLastError() == cudaSuccess);
}

TEST_CASE("cinematic viewport matches headless bytes and retains completed image on rejection") {
    const bhr::Starfield empty;
    Viewport viewport;
    auto p=small_scene();p.enable_starfield=false;
    for(bool bloom:{false,true}) {
        const bhr::CinematicRequest request{p,{.15f,.18f,.1f,.15f,1,{bloom,.06f,1}}};
        const auto host=bhr::render_cinematic(request,empty);
        REQUIRE(viewport.submit(request,empty));
        CHECK_FALSE(viewport.submit(request,empty));
        await_image(viewport);
        CHECK(read_texture(viewport)==host.image.rgba);
        const auto texture=viewport.texture();
        CHECK_FALSE(viewport.submit(bhr::CinematicRequest{p,{0}},empty));
        CHECK(viewport.texture()==texture);
        CHECK(read_texture(viewport)==host.image.rgba);
    }
    const bhr::EmissionV1 emission{true,7,.18f,.06f,1.0f};
    const auto animated0=bhr::AnimatedCinematicRequest{p,{.15f,.18f,.1f,.15f,1,{true,.06f,1}},emission,{0,180}};
    const auto animated90=bhr::AnimatedCinematicRequest{p,{.15f,.18f,.1f,.15f,1,{true,.06f,1}},emission,{90,180}};
    const auto animated_host=bhr::render_animated(animated0,empty);
    REQUIRE(viewport.submit(animated0,empty));
    await_image(viewport);
    CHECK(read_texture(viewport)==animated_host.image.rgba);
    REQUIRE(viewport.submit(animated90,empty));
    await_image(viewport);
    CHECK(read_texture(viewport)==bhr::render_animated(animated90,empty).image.rgba);
    CHECK(animated_host.image.rgba!=bhr::render_animated(animated90,empty).image.rgba);
    p.camera.width=47;p.camera.height=29;
    REQUIRE(viewport.submit(bhr::CinematicRequest{p,{}},empty));
    await_image(viewport);
    CHECK(read_texture(viewport)==bhr::render_cinematic({p,{}},empty).image.rgba);
    REQUIRE(viewport.submit(bhr::CinematicRequest{p,{}},empty));
    REQUIRE(viewport.shutdown());
}

TEST_CASE("viewport preserves the last image on a fatal graphics boundary error") {
    const bhr::Starfield empty;
    Viewport viewport;
    REQUIRE(viewport.submit(small_scene(), empty));
    await_image(viewport);
    const auto texture = viewport.texture();
    const auto pixels = read_texture(viewport);

    // Inject an existing GL error at the public submission boundary. The
    // viewport reports it and releases the render target, preserving display.
    glBindBuffer(GL_TEXTURE_2D, 0); // invalid buffer target
    CHECK_FALSE(viewport.submit(small_scene(), empty));
    CHECK_FALSE(viewport.error().empty());
    CHECK_FALSE(viewport.busy());
    CHECK(viewport.texture() == texture);
    CHECK(read_texture(viewport) == pixels);
    const auto error = viewport.error();
    CHECK_FALSE(viewport.poll());
    CHECK_FALSE(viewport.submit(small_scene(), empty));
    CHECK(viewport.error() == error);
    CHECK(viewport.shutdown());
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("viewport restores the callers OpenGL pixel upload state") {
    struct ForeignUnpackState {
        GLuint buffer = 0;
        GLuint texture = 0;
        ForeignUnpackState() {
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
        }
        ~ForeignUnpackState() {
            glDeleteBuffers(1, &buffer);
            glDeleteTextures(1, &texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        }
        void check() const {
            GLint actual = 0;
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual);
            CHECK(actual == static_cast<GLint>(buffer));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual);
            CHECK(actual == static_cast<GLint>(texture));
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &actual);
            CHECK(actual == 8);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &actual);
            CHECK(actual == 17);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &actual);
            CHECK(actual == 2);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &actual);
            CHECK(actual == 3);
            CHECK(glGetError() == GL_NO_ERROR);
        }
    } foreign_state;
    const bhr::Starfield empty;
    Viewport viewport;
    REQUIRE(viewport.submit(small_scene(), empty));
    foreign_state.check();
    await_image(viewport);
    foreign_state.check();
    CHECK(viewport.shutdown());
    foreign_state.check();
}

} // namespace

// A hidden window exercises real CUDA/OpenGL interop without manual interaction.
// CTest treats 77 as a skipped environment, never as a passing integration test.
int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SKIP: SDL video unavailable: %s\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    auto* window = SDL_CreateWindow("viewport integration", 0, 0, 64, 64,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    auto context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "SKIP: OpenGL 3.3 context unavailable: %s\n", SDL_GetError());
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    unsigned int device_count = 0;
    int devices[8]{};
    const auto cuda_status = cudaGLGetDevices(&device_count, devices, 8, cudaGLDeviceListAll);
    int result = 77;
    if (cuda_status != cudaSuccess || device_count == 0) {
        std::fprintf(stderr, "SKIP: CUDA/OpenGL interop unavailable: %s\n",
                     cudaGetErrorString(cuda_status));
    } else if (cudaSetDevice(devices[0]) != cudaSuccess) {
        std::fprintf(stderr, "FAIL: could not select the OpenGL CUDA device\n");
        result = 1;
    } else {
        doctest::Context tests(argc, argv);
        result = tests.run();
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
