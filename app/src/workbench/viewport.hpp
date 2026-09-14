#pragma once

#include "bhr/params.hpp"
#include "bhr/starfield.hpp"
#include "bhr/appearance.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace bhr::workbench {

// Replaces the gradient demo's register-image/map-array/surface/unmap loop.
// A persistent CUDA-registered GL pixel-unpack buffer feeds two GL textures:
// one visible completed image and one upload destination. No host image copy or
// steady-state allocation is involved. Resizing preserves the completed image.
//
// idle -> rendering (mapped CUDA PBO) -> unmapping (CUDA completion event)
//      -> presenting (GL upload fence) -> idle (publish completed texture).
// poll() queries events/fences without waiting. Unmap is issued only after the
// render event completes, and GL never accesses the buffer while it is mapped.
// CUDA's mapped pointer may change on every map and is never cached.
//
// All calls and destruction require the same current GL context and CUDA device
// on the UI thread. The supplied Starfield must outlive submitted GPU work.
class Viewport final {
public:
    Viewport();
    ~Viewport();
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport(Viewport&&) = delete;
    Viewport& operator=(Viewport&&) = delete;

    // False means busy, invalid input, or a reported fatal resource error.
    // Validation errors are recoverable on the next valid submission.
    bool submit(const RenderParams& params, const Starfield& starfield);
    bool submit(const CinematicRequest& request, const Starfield& starfield);
    bool poll(); // true exactly when a newly completed image is published

    bool busy() const noexcept;
    bool has_image() const noexcept;
    unsigned int texture() const noexcept;
    int image_width() const noexcept;
    int image_height() const noexcept;
    float last_gpu_ms() const noexcept;
    float last_frame_ms() const noexcept;
    std::uint64_t completion_count() const noexcept;
    const std::string& error() const noexcept;

    // Waits only for teardown; call before destroying the GL context/starfield.
    // Idempotent. Failed releases retain their handles so cleanup can be retried.
    bool shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bhr::workbench
