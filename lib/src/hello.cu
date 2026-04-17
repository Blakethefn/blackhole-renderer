#include "bhr/hello.hpp"
#include <cuda_runtime.h>
#include <surface_functions.h>
#include <cstdio>

namespace {

__global__ void add_kernel(int a, int b, int* out) {
    *out = a + b;
}

__global__ void gradient_kernel(cudaSurfaceObject_t surf, int w, int h, float t) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;

    const float u = static_cast<float>(x) / w;
    const float v = static_cast<float>(y) / h;
    const float pulse = 0.5f + 0.5f * __sinf(t);

    uchar4 color;
    color.x = static_cast<unsigned char>(255.0f * u);
    color.y = static_cast<unsigned char>(255.0f * v);
    color.z = static_cast<unsigned char>(255.0f * pulse);
    color.w = 255;

    surf2Dwrite(color, surf, x * static_cast<int>(sizeof(uchar4)), y);
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

void cuda_gradient(unsigned long long surface, int width, int height, float t_seconds) {
    cudaSurfaceObject_t surf = static_cast<cudaSurfaceObject_t>(surface);
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    gradient_kernel<<<grid, block>>>(surf, width, height, t_seconds);
}

} // namespace bhr
