#include "bhr/renderer.hpp"
#include "bhr/starfield.hpp"
#include "bhr/camera.hpp"
#include "bhr/geodesic.hpp"
#include "bhr/kerr.hpp"
#include "bhr/disk_physics.hpp"
#include "bhr/blackbody.hpp"
#include "bhr/redshift.hpp"
#include <cuda_runtime.h>
#include <cstdio>
#include <exception>
#include <utility>

namespace bhr {

namespace {

__global__ void render_kernel(
    uchar4* fb, int width, int height,
    CameraParams cam, DiskParams disk, float a,
    cudaTextureObject_t starfield_tex, int starfield_valid,
    IntegratorKind integrator_kind, bool enable_doppler,
    bool enable_redshift, bool enable_beaming)
{
    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= width || py >= height) return;

    GeodesicState s{};
    Conserved c{};
    camera_ray(px, py, width, height, cam, a, s, c);

    IntegratorConfig cfg{};
    cfg.r_max = kMaxSceneRadius;
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

    const HitInfo hit = (integrator_kind == IntegratorKind::kGeokerr)
        ? integrate_geokerr(s, a, c, cfg)
        : integrate_rk45(s, a, c, cfg);

    uchar4 color;
    color.x = 0; color.y = 0; color.z = 0; color.w = 255;

    switch (hit.type) {
        case HitType::kHorizon:
            break;  // Stay black
        case HitType::kDisk: {
            const float T_emit = disk_temperature(hit.r, disk.r_inner, disk.peak_temp_K);
            const float g = disk_frequency_shift(hit.r, a, c, enable_doppler, enable_redshift);
            const float T_obs = g * T_emit;
            float rr, gg, bb;
            blackbody_rgb(T_obs, rr, gg, bb);
            // Beaming: bolometric intensity scales as g^4.
            // Combine with log(T)-based emissivity (intrinsic brightness proxy).
            const float g2 = g * g;
            const float beaming = enable_beaming ? g2 * g2 : 1.0f;
            const float emissivity = logf(fmaxf(T_emit, 1.0f)) / logf(40000.0f);
            const float exposure = 1.5f * disk.brightness * emissivity * beaming;
            rr = rr * exposure / (1.0f + rr * exposure);
            gg = gg * exposure / (1.0f + gg * exposure);
            bb = bb * exposure / (1.0f + bb * exposure);
            color.x = static_cast<unsigned char>(fminf(1.0f, rr) * 255.0f);
            color.y = static_cast<unsigned char>(fminf(1.0f, gg) * 255.0f);
            color.z = static_cast<unsigned char>(fminf(1.0f, bb) * 255.0f);
            break;
        }
        case HitType::kEscape: {
            if (starfield_valid) {
                // Equirectangular sampling of the lensed escape direction.
                // hit.theta and hit.phi are the final BL coordinates at r_max.
                const float PI = 3.14159265358979323846f;
                float phi_w = hit.phi;
                // Wrap phi into [0, 2π)
                phi_w = phi_w - 2.0f * PI * floorf(phi_w / (2.0f * PI));
                const float u = phi_w / (2.0f * PI);
                const float v = hit.theta / PI;
                float4 star = tex2D<float4>(starfield_tex, u, v);
                // Reinhard tone map (star map is HDR so exposure matters)
                const float exposure = 0.15f;
                star.x = star.x * exposure / (1.0f + star.x * exposure);
                star.y = star.y * exposure / (1.0f + star.y * exposure);
                star.z = star.z * exposure / (1.0f + star.z * exposure);
                color.x = static_cast<unsigned char>(fminf(1.0f, star.x) * 255.0f);
                color.y = static_cast<unsigned char>(fminf(1.0f, star.y) * 255.0f);
                color.z = static_cast<unsigned char>(fminf(1.0f, star.z) * 255.0f);
            } else {
                color.x = 2; color.y = 4; color.z = 8;
            }
            break;
        }
        case HitType::kUnknown:
            color.x = 255; color.y = 0; color.z = 255;
            break;
    }

    fb[py * width + px] = color;
}

} // namespace

cudaError_t render_device(const RenderParams& params, const Starfield& sf,
                          uchar4* device_pixels, size_t capacity_bytes,
                          cudaStream_t stream) {
    if (!params.validate() || device_pixels == nullptr
        || reinterpret_cast<std::uintptr_t>(device_pixels) % alignof(uchar4) != 0)
        return cudaErrorInvalidValue;
    const int W = params.camera.width;
    const int H = params.camera.height;
    const size_t bytes = static_cast<size_t>(W) * H * sizeof(uchar4);
    if (capacity_bytes < bytes) return cudaErrorInvalidValue;

    const dim3 block(16, 16);
    const dim3 grid((W + block.x - 1) / block.x, (H + block.y - 1) / block.y);
    render_kernel<<<grid, block, 0, stream>>>(device_pixels, W, H, params.camera,
        params.disk, params.spin, sf.tex, params.enable_starfield && sf.is_valid(),
        params.integrator, params.enable_doppler, params.enable_redshift, params.enable_beaming);
    return cudaGetLastError();
}

void render(const RenderParams& params, const Starfield& sf, Image& img) {
    img = Image{};
    if (const char* reason = validation_error(params)) {
        std::fprintf(stderr, "Render parameters invalid: %s\n", reason);
        return;
    }
    // Build the host result separately so no partial or stale image is exposed.
    Image result;
    try {
        result.allocate(params.camera.width, params.camera.height);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Host image allocation failed: %s\n", error.what());
        return;
    }

    uchar4* device_pixels = nullptr;
    const size_t bytes = result.rgba.size();
    cudaError_t err = cudaMalloc(&device_pixels, bytes);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "Render cudaMalloc failed: %s\n", cudaGetErrorString(err));
        return;
    }
    err = render_device(params, sf, device_pixels, bytes);
    if (err != cudaSuccess)
        std::fprintf(stderr, "Render launch failed: %s\n", cudaGetErrorString(err));
    if (err == cudaSuccess) {
        err = cudaStreamSynchronize(nullptr);
        if (err != cudaSuccess)
            std::fprintf(stderr, "Render execution failed: %s\n", cudaGetErrorString(err));
    }
    if (err == cudaSuccess) {
        err = cudaMemcpy(result.rgba.data(), device_pixels, bytes, cudaMemcpyDeviceToHost);
        if (err != cudaSuccess)
            std::fprintf(stderr, "Render copy to host failed: %s\n", cudaGetErrorString(err));
    }
    const cudaError_t free_error = cudaFree(device_pixels);
    if (free_error != cudaSuccess)
        std::fprintf(stderr, "Render cudaFree failed: %s\n", cudaGetErrorString(free_error));
    if (err == cudaSuccess && free_error == cudaSuccess) img = std::move(result);
}

} // namespace bhr
