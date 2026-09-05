#pragma once
/// @file bhr/renderer.hpp
/// Public entrypoint for rendering a black hole to an RGBA8 image.

#include "bhr/params.hpp"
#include "bhr/image.hpp"
#include "bhr/starfield.hpp"
#include <cstddef>
#include <cuda_runtime.h>

namespace bhr {

/// Enqueue an RGBA8 render on @p stream into caller-owned CUDA device memory.
/// @p capacity_bytes must cover width*height*sizeof(uchar4), with no row padding.
/// The target must be aligned for uchar4 and accessible on the current CUDA device.
/// This function neither allocates, copies, maps/unmaps, nor synchronizes.
/// Returns cudaErrorInvalidValue for invalid params/null/misaligned/undersized
/// targets, or a CUDA launch error. Execution errors are reported when the caller
/// subsequently queries/synchronizes the stream or its completion event.
/// Keep the target mapped/alive and sf's texture alive until that work completes.
/// Parameter values are captured at launch; later host edits do not affect it.
cudaError_t render_device(const RenderParams& params, const Starfield& sf,
                          uchar4* device_pixels, size_t capacity_bytes,
                          cudaStream_t stream = 0);

inline cudaError_t render_device(const RenderParams& params, uchar4* device_pixels,
                                 size_t capacity_bytes, cudaStream_t stream = 0) {
    const Starfield empty{};
    return render_device(params, empty, device_pixels, capacity_bytes, stream);
}

/// Blocking host-image wrapper through the same device rendering path.
/// Sampling requires both enable_starfield and sf.is_valid(); otherwise escape
/// pixels retain the legacy dim-blue fallback. On validation, allocation, launch,
/// execution, copy, or cleanup failure, logs a reason and leaves @p img empty.
void render(const RenderParams& params, const Starfield& sf, Image& img);

/// Backwards-compatible overload without a starfield.
inline void render(const RenderParams& params, Image& img) {
    const Starfield empty{};
    render(params, empty, img);
}

} // namespace bhr
