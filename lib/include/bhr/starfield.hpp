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

/// Load an equirectangular HDR EXR from @p path, downsample if needed to stay under
/// @p max_width pixels, and upload to GPU as a cudaTextureObject for bilinear sampling.
/// If @p path is empty or load fails, returns an empty (invalid) Starfield.
bool load_starfield(const std::string& path, int max_width, Starfield& out);

void destroy_starfield(Starfield& sf);

} // namespace bhr
