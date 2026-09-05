#include "viewport.hpp"

#include <glad/glad.h>
#include <cuda_gl_interop.h>

#include "bhr/renderer.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace bhr::workbench {
namespace {

using Clock = std::chrono::steady_clock;

// ImGui or another caller may leave non-default pixel-transfer state behind.
class UnpackState final {
public:
    UnpackState() {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer_);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment_);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length_);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows_);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
    ~UnpackState() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer_));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_));
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length_);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows_);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels_);
    }
    UnpackState(const UnpackState&) = delete;
    UnpackState& operator=(const UnpackState&) = delete;
private:
    GLint buffer_ = 0, texture_ = 0, alignment_ = 4;
    GLint row_length_ = 0, skip_rows_ = 0, skip_pixels_ = 0;
};

} // namespace

struct Viewport::Impl {
    enum class Phase { idle, rendering, unmapping, presenting, failed, closed };

    Phase phase = Phase::idle;
    cudaStream_t stream = nullptr;
    cudaEvent_t render_start = nullptr;
    cudaEvent_t render_end = nullptr;
    cudaEvent_t unmap_end = nullptr;
    cudaGraphicsResource* resource = nullptr;
    bool mapped = false;
    GLuint buffer = 0;
    GLuint upload_texture = 0;
    GLuint spare_texture = 0;
    GLuint display_texture = 0;
    GLsync upload_fence = nullptr;
    int target_width = 0, target_height = 0;
    int displayed_width = 0, displayed_height = 0;
    float pending_gpu_ms = 0.0f;
    float gpu_ms = 0.0f, frame_ms = 0.0f;
    std::uint64_t completions = 0;
    Clock::time_point submitted{};
    std::string message;

    bool fail(const std::string& reason) {
        if (phase == Phase::failed && !message.empty()) message += "; " + reason;
        else message = reason;
        phase = Phase::failed;
        return false;
    }

    bool cuda_ok(cudaError_t status, const char* operation) {
        return status == cudaSuccess || fail(std::string(operation) + ": " + cudaGetErrorString(status));
    }

    bool gl_ok(const char* operation) {
        const auto status = glGetError();
        return status == GL_NO_ERROR || fail(std::string(operation) + ": OpenGL error " + std::to_string(status));
    }

    bool initialize() {
        if (!stream && !cuda_ok(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
                                "Create render stream")) return false;
        if (!render_start && !cuda_ok(cudaEventCreate(&render_start), "Create start event")) return false;
        if (!render_end && !cuda_ok(cudaEventCreate(&render_end), "Create render event")) return false;
        if (!unmap_end && !cuda_ok(cudaEventCreateWithFlags(&unmap_end, cudaEventDisableTiming),
                                  "Create unmap event")) return false;
        return true;
    }

    bool delete_texture(GLuint& texture) {
        if (!texture) return true;
        glDeleteTextures(1, &texture);
        if (!gl_ok("Delete texture")) return false;
        texture = 0;
        return true;
    }

    // Only used after GPU ownership has returned, or during settled teardown.
    bool release_target() {
        if (mapped || upload_fence) return fail("Cannot release a presentation target still in use");
        if (resource) {
            if (!cuda_ok(cudaGraphicsUnregisterResource(resource), "Unregister presentation buffer")) return false;
            resource = nullptr;
        }
        if (buffer) {
            glDeleteBuffers(1, &buffer);
            if (!gl_ok("Delete presentation buffer")) return false;
            buffer = 0;
        }
        const bool upload_deleted = delete_texture(upload_texture);
        const bool spare_deleted = delete_texture(spare_texture);
        if (!upload_deleted || !spare_deleted) return false;
        target_width = 0;
        target_height = 0;
        return true;
    }

    bool create_texture(GLuint& texture, int width, int height) {
        glGenTextures(1, &texture);
        if (!gl_ok("Create presentation texture")) return false;
        if (!texture) return fail("OpenGL returned an empty presentation texture");
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return gl_ok("Allocate presentation texture");
    }

    bool resize(int width, int height) {
        if (width == target_width && height == target_height) return true;
        GLint max_texture_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        if (!gl_ok("Query maximum texture size")) return false;
        if (width > max_texture_size || height > max_texture_size)
            return fail("Preview resolution exceeds the OpenGL texture limit");
        if (!release_target()) return false;
        {
            UnpackState restore;
            glGenBuffers(1, &buffer);
            if (!gl_ok("Create presentation buffer")) return false;
            if (!buffer) return fail("OpenGL returned an empty presentation buffer");
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
            const auto bytes = static_cast<GLsizeiptr>(width) * height * sizeof(uchar4);
            glBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
            if (!gl_ok("Allocate presentation buffer")) return false;
            // nullptr must mean no source data, not offset zero into the PBO.
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            if (!create_texture(upload_texture, width, height)) return false;
            if (!create_texture(spare_texture, width, height)) return false;
        }
        if (!gl_ok("Restore OpenGL unpack state")) return false;
        if (!cuda_ok(cudaGraphicsGLRegisterBuffer(&resource, buffer, cudaGraphicsRegisterFlagsWriteDiscard),
                      "Register presentation buffer with CUDA")) return false;
        target_width = width;
        target_height = height;
        return true;
    }

    bool submit(const RenderParams& params, const Starfield& starfield) {
        if (phase != Phase::idle) return false;
        if (const char* reason = validation_error(params)) {
            message = reason;
            return false;
        }
        message.clear();
        submitted = Clock::now();
        if (!gl_ok("OpenGL state before render")) return false;
        if (!initialize() || !resize(params.camera.width, params.camera.height)) return false;
        if (!cuda_ok(cudaGraphicsMapResources(1, &resource, stream), "Map presentation buffer")) return false;
        mapped = true;
        void* pixels = nullptr;
        std::size_t capacity_bytes = 0;
        if (!cuda_ok(cudaGraphicsResourceGetMappedPointer(&pixels, &capacity_bytes, resource),
                      "Get mapped presentation buffer")) return false;
        if (!cuda_ok(cudaEventRecord(render_start, stream), "Record render start")) return false;
        if (!cuda_ok(render_device(params, starfield, static_cast<uchar4*>(pixels), capacity_bytes, stream),
                      "Launch black-hole render")) return false;
        if (!cuda_ok(cudaEventRecord(render_end, stream), "Record render completion")) return false;
        phase = Phase::rendering;
        return true;
    }

    bool poll() {
        if (phase == Phase::rendering) {
            const auto status = cudaEventQuery(render_end);
            if (status == cudaErrorNotReady) return false;
            if (!cuda_ok(status, "Complete black-hole render")) return false;
            if (!cuda_ok(cudaEventElapsedTime(&pending_gpu_ms, render_start, render_end),
                          "Measure render time")) return false;
            // The event is complete before unmap: do not make the UI wait for
            // a long kernel inside the graphics ownership handoff.
            if (!cuda_ok(cudaGraphicsUnmapResources(1, &resource, stream), "Unmap presentation buffer")) return false;
            mapped = false;
            if (!cuda_ok(cudaEventRecord(unmap_end, stream), "Record unmap completion")) return false;
            phase = Phase::unmapping;
        }
        if (phase == Phase::unmapping) {
            const auto status = cudaEventQuery(unmap_end);
            if (status == cudaErrorNotReady) return false;
            if (!cuda_ok(status, "Complete presentation unmap")) return false;
            {
                UnpackState restore;
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
                glBindTexture(GL_TEXTURE_2D, upload_texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, target_width, target_height,
                                GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            if (!gl_ok("Upload completed preview")) return false;
            upload_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (!gl_ok("Create presentation fence")) return false;
            if (!upload_fence) return fail("OpenGL returned an empty presentation fence");
            glFlush();
            if (!gl_ok("Flush presentation upload")) return false;
            phase = Phase::presenting;
        }
        if (phase != Phase::presenting) return false;
        const auto fence_status = glClientWaitSync(upload_fence, 0, 0);
        if (!gl_ok("Query presentation fence")) return false;
        if (fence_status == GL_TIMEOUT_EXPIRED) return false;
        if (fence_status == GL_WAIT_FAILED) return fail("OpenGL presentation fence failed");
        glDeleteSync(upload_fence);
        if (!gl_ok("Delete presentation fence")) return false;
        upload_fence = nullptr;
        if (spare_texture) {
            // Resize/new target: retire the previous size only after success.
            if (!delete_texture(display_texture)) return false;
            display_texture = std::exchange(upload_texture, std::exchange(spare_texture, 0));
        } else {
            std::swap(display_texture, upload_texture);
        }
        displayed_width = target_width;
        displayed_height = target_height;
        gpu_ms = pending_gpu_ms;
        frame_ms = std::chrono::duration<float, std::milli>(Clock::now() - submitted).count();
        ++completions;
        phase = Phase::idle;
        return true;
    }

    // Blocking work belongs exclusively to shutdown/error cleanup.
    bool settle() {
        if (stream && !cuda_ok(cudaStreamSynchronize(stream), "Finish render during cleanup")) return false;
        if (mapped) {
            if (!cuda_ok(cudaGraphicsUnmapResources(1, &resource, stream), "Unmap buffer during cleanup")) return false;
            mapped = false;
            if (!cuda_ok(cudaStreamSynchronize(stream), "Finish unmap during cleanup")) return false;
        }
        // Also covers an upload submitted just before a GL fence creation error.
        if (buffer) {
            glFinish();
            if (!gl_ok("Finish presentation during cleanup")) return false;
        }
        if (upload_fence) {
            glDeleteSync(upload_fence);
            if (!gl_ok("Delete fence during cleanup")) return false;
            upload_fence = nullptr;
        }
        return true;
    }

    void cleanup_failure() {
        if (settle()) release_target();
    }

    bool shutdown() {
        if (phase == Phase::closed) return true;
        if (!settle() || !release_target()) return false;
        if (!delete_texture(display_texture)) return false;
        displayed_width = 0;
        displayed_height = 0;
        // Clear a handle only after its release succeeds; a caller can retry.
        for (auto* event : {&render_start, &render_end, &unmap_end}) {
            if (*event) {
                if (!cuda_ok(cudaEventDestroy(*event), "Destroy render event")) return false;
                *event = nullptr;
            }
        }
        if (stream) {
            if (!cuda_ok(cudaStreamDestroy(stream), "Destroy render stream")) return false;
            stream = nullptr;
        }
        phase = Phase::closed;
        return true;
    }
};

Viewport::Viewport() : impl_(std::make_unique<Impl>()) {}
Viewport::~Viewport() {
    if (!impl_->shutdown())
        std::fprintf(stderr, "Viewport cleanup failed: %s\n", impl_->message.c_str());
}
bool Viewport::submit(const RenderParams& params, const Starfield& starfield) {
    const bool already_failed = impl_->phase == Impl::Phase::failed;
    const bool accepted = impl_->submit(params, starfield);
    if (!already_failed && impl_->phase == Impl::Phase::failed) impl_->cleanup_failure();
    return accepted;
}
bool Viewport::poll() {
    const bool already_failed = impl_->phase == Impl::Phase::failed;
    const bool completed = impl_->poll();
    if (!already_failed && impl_->phase == Impl::Phase::failed) impl_->cleanup_failure();
    return completed;
}
bool Viewport::busy() const noexcept {
    return impl_->phase == Impl::Phase::rendering || impl_->phase == Impl::Phase::unmapping
        || impl_->phase == Impl::Phase::presenting;
}
bool Viewport::has_image() const noexcept { return impl_->display_texture != 0; }
unsigned int Viewport::texture() const noexcept { return impl_->display_texture; }
int Viewport::image_width() const noexcept { return impl_->displayed_width; }
int Viewport::image_height() const noexcept { return impl_->displayed_height; }
float Viewport::last_gpu_ms() const noexcept { return impl_->gpu_ms; }
float Viewport::last_frame_ms() const noexcept { return impl_->frame_ms; }
std::uint64_t Viewport::completion_count() const noexcept { return impl_->completions; }
const std::string& Viewport::error() const noexcept { return impl_->message; }
bool Viewport::shutdown() { return impl_->shutdown(); }

} // namespace bhr::workbench
