#pragma once
/// @file bhr/hello.hpp
/// Temporary CUDA toolchain sanity check. Removed in Plan 2.

namespace bhr {

/// Runs a trivial CUDA kernel that computes @p a + @p b on the GPU.
/// @return The sum, or -1 if no CUDA device is available.
int cuda_add(int a, int b);

/// Returns a human-readable description of device 0, or empty string on error.
const char* cuda_device_name();

} // namespace bhr
