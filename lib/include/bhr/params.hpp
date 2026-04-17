#pragma once
/// @file bhr/params.hpp
/// Render parameter structs. POD by design — trivially copyable to device.

#include <cstdint>

namespace bhr {

struct CameraParams {
    float r_cam = 50.0f;
    float theta_cam_deg = 85.0f;
    float phi_cam_deg = 0.0f;
    float fov_deg = 35.0f;
    int width = 1920;
    int height = 1080;
};

struct DiskParams {
    float r_inner = 6.0f;
    float r_outer = 20.0f;
    float peak_temp_K = 1.0e7f;
    float brightness = 1.0f;
};

struct RenderParams {
    float spin = 0.0f;
    CameraParams camera;
    DiskParams disk;
    bool enable_doppler = true;
    bool enable_redshift = true;
    bool enable_beaming = true;
    bool enable_starfield = false;
};

static_assert(sizeof(RenderParams) < 256, "RenderParams is too large for constant memory upload");

} // namespace bhr
