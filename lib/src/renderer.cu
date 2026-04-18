#include "bhr/renderer.hpp"
#include "bhr/camera.hpp"
#include "bhr/geodesic.hpp"
#include "bhr/kerr.hpp"
#include "bhr/disk_physics.hpp"
#include "bhr/blackbody.hpp"
#include <cuda_runtime.h>
#include <cstdio>

namespace bhr {

namespace {

__global__ void render_kernel(
    uchar4* fb, int width, int height,
    CameraParams cam, DiskParams disk, float a)
{
    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= width || py >= height) return;

    GeodesicState s{};
    Conserved c{};
    camera_ray(px, py, width, height, cam, a, s, c);

    IntegratorConfig cfg{};
    cfg.disk_r_inner = disk.r_inner;
    cfg.disk_r_outer = disk.r_outer;
    cfg.max_steps = 50000;
    // Scale h_init so the first step does not overshoot: move at most r/20 in coordinate r.
    // With dr_dlam potentially O(r²) for a radial photon, h = r/(20*|dr_dlam|).
    // Clamp to [1e-6, 0.05] to avoid pathological cases.
    {
        const float dr_scale = fmaxf(1.0f, fabsf(s.dr_dlam) + fabsf(s.dth_dlam));
        cfg.h_init = fminf(0.05f, fmaxf(1e-6f, s.r / (20.0f * dr_scale)));
    }
    cfg.h_min = 1e-6f;

    const HitInfo hit = integrate_rk45(s, a, c, cfg);

    uchar4 color;
    color.x = 0; color.y = 0; color.z = 0; color.w = 255;

    switch (hit.type) {
        case HitType::kHorizon:
            break;  // Stay black
        case HitType::kDisk: {
            const float T = disk_temperature(hit.r, disk.r_inner, disk.peak_temp_K);
            float rr, gg, bb;
            blackbody_rgb(T, rr, gg, bb);
            // Simple intensity model: log(T) scaled — keeps dynamic range sensible.
            // Proper beaming g^4 applied in Unit 4.
            const float intensity = logf(fmaxf(T, 1.0f)) / logf(40000.0f);  // 0..~1 for T up to 40kK
            const float exposure = 1.5f * disk.brightness * intensity;
            rr = rr * exposure / (1.0f + rr * exposure);
            gg = gg * exposure / (1.0f + gg * exposure);
            bb = bb * exposure / (1.0f + bb * exposure);
            color.x = static_cast<unsigned char>(fminf(1.0f, rr) * 255.0f);
            color.y = static_cast<unsigned char>(fminf(1.0f, gg) * 255.0f);
            color.z = static_cast<unsigned char>(fminf(1.0f, bb) * 255.0f);
            break;
        }
        case HitType::kEscape:
            color.x = 2; color.y = 4; color.z = 8;
            break;
        case HitType::kUnknown:
            color.x = 255; color.y = 0; color.z = 255;
            break;
    }

    fb[py * width + px] = color;
}

} // namespace

void render(const RenderParams& params, Image& img) {
    const int W = params.camera.width;
    const int H = params.camera.height;
    img.allocate(W, H);

    uchar4* d_fb = nullptr;
    const size_t bytes = static_cast<size_t>(W) * H * sizeof(uchar4);
    cudaError_t err = cudaMalloc(&d_fb, bytes);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "cudaMalloc failed: %s\n", cudaGetErrorString(err));
        return;
    }

    dim3 block(16, 16);
    dim3 grid((W + block.x - 1) / block.x, (H + block.y - 1) / block.y);

    render_kernel<<<grid, block>>>(d_fb, W, H, params.camera, params.disk, params.spin);

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "render_kernel launch failed: %s\n", cudaGetErrorString(err));
        cudaFree(d_fb);
        return;
    }

    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "render_kernel execution failed: %s\n", cudaGetErrorString(err));
    }

    err = cudaMemcpy(img.rgba.data(), d_fb, bytes, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "cudaMemcpy DtoH failed: %s\n", cudaGetErrorString(err));
    }

    cudaFree(d_fb);
}

} // namespace bhr
