#pragma once
/// @file bhr/starfield.hpp
/// HDR equirectangular starfield (from NASA SVS Deep Star Maps 2020 EXR).

#include <string>
#include <cuda_runtime.h>

namespace bhr {

struct Starfield {
    int width = 0;
    int height = 0;
    cudaArray_t d_array = nullptr;
    cudaTextureObject_t tex = 0;

    bool is_valid() const { return tex != 0; }
};

/// Load a single-part scanline EXR with named R/G/B HALF or FLOAT channels.
/// Radiance must be finite/nonnegative; tiled, deep, multipart, integer and
/// subsampled channels are rejected. Source limit: 128 Mi pixels; max_width 1..16384.
/// Downsample with partial edge boxes and upload for bilinear sampling.
/// Requires an empty out; existing ownership is never overwritten. On a GPU
/// cleanup failure out retains partial ownership: call destroy_starfield to retry.
bool load_starfield(const std::string& path, int max_width, Starfield& out);

/// Cinematic-only limits: 8 Mi source pixels before decode, upload <=2 Mi pixels
/// and width <=2048, at most four channels. Caller declares linear sRGB/D65 input.
bool load_cinematic_starfield(const std::string& path, Starfield& out);

/// Call after GPU work completes. Returns false on CUDA failure, logs a reason,
/// and retains unreleased handles for retry. Empty destruction succeeds.
bool destroy_starfield(Starfield& sf);

} // namespace bhr
