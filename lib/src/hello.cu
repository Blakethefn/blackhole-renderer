#include "bhr/hello.hpp"
#include <cuda_runtime.h>
#include <cstdio>

namespace {

__global__ void add_kernel(int a, int b, int* out) {
    *out = a + b;
}

} // namespace

namespace bhr {

int cuda_add(int a, int b) {
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        return -1;
    }

    int* d_out = nullptr;
    if (cudaMalloc(&d_out, sizeof(int)) != cudaSuccess) return -1;

    add_kernel<<<1, 1>>>(a, b, d_out);

    int host_out = -1;
    cudaMemcpy(&host_out, d_out, sizeof(int), cudaMemcpyDeviceToHost);
    cudaFree(d_out);

    return host_out;
}

const char* cuda_device_name() {
    static char buf[256] = {0};
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        buf[0] = '\0';
        return buf;
    }
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);
    std::snprintf(buf, sizeof(buf), "%s (sm_%d%d, %zu MB)",
                  prop.name, prop.major, prop.minor,
                  static_cast<size_t>(prop.totalGlobalMem / (1024 * 1024)));
    return buf;
}

} // namespace bhr
