#pragma once
/// @file bhr/hello.hpp
/// Temporary CUDA toolchain sanity check. Removed in Plan 2.

namespace bhr {

/// Runs a trivial CUDA kernel that computes @p a + @p b on the GPU.
/// @return The sum, or -1 if no CUDA device is available.
int cuda_add(int a, int b);

/// Returns a human-readable description of device 0, or empty string on error.
const char* cuda_device_name();

/// Fills a registered CUDA/OpenGL-interop surface with a diagonal RGB gradient.
/// @param surface  A CUDA surface object created from a GL texture mapped for write.
/// @param width    Texture width in pixels.
/// @param height   Texture height in pixels.
/// @param t_seconds  Elapsed seconds — drives color animation.
void cuda_gradient(unsigned long long surface, int width, int height, float t_seconds);

} // namespace bhr
