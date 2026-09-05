#pragma once
/// @file bhr/params.hpp
/// Render parameter structs. POD by design — trivially copyable to device.

#include "bhr/kerr.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace bhr {

enum class IntegratorKind : int { kRK45 = 0, kGeokerr = 1 };

inline constexpr int kMaxRenderDimension = 16384;
inline constexpr float kMaxSpin = 0.999f;
// The current ray integrators stop at r=1000 M. Keep inputs inside that domain.
inline constexpr float kMaxSceneRadius = 1000.0f;

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
    IntegratorKind integrator = IntegratorKind::kRK45;

    /// Host-side validation only; UI invalidation/lifetime state belongs elsewhere.
    bool validate() const;
};

/// Return a static, user-facing reason for the first invalid value, or nullptr.
/// Supported scenes use prograde subextremal Kerr spin and a disk at/above ISCO.
/// A 0.001 M ISCO tolerance accepts existing rounded CLI/test fixtures (2.32 M).
inline const char* validation_error(const RenderParams& p) {
    if (!std::isfinite(p.spin) || p.spin < 0.0f || p.spin > kMaxSpin)
        return "Spin must be finite and between 0 and 0.999.";
    if (p.camera.width <= 0 || p.camera.height <= 0
        || p.camera.width > kMaxRenderDimension || p.camera.height > kMaxRenderDimension)
        return "Image dimensions must each be between 1 and 16384 pixels.";
    const size_t width = static_cast<size_t>(p.camera.width);
    const size_t height = static_cast<size_t>(p.camera.height);
    if (width > std::numeric_limits<size_t>::max() / height / 4)
        return "Image byte size exceeds the supported address range.";
    if (!std::isfinite(p.camera.r_cam) || p.camera.r_cam <= horizon_radius(p.spin)
        || p.camera.r_cam > kMaxSceneRadius)
        return "Camera radius must be outside the horizon and at most 1000 M.";
    if (!std::isfinite(p.camera.theta_cam_deg) || p.camera.theta_cam_deg <= 0.0f
        || p.camera.theta_cam_deg >= 180.0f)
        return "Camera inclination must be strictly between 0 and 180 degrees.";
    if (!std::isfinite(p.camera.phi_cam_deg))
        return "Camera azimuth must be finite.";
    if (!std::isfinite(p.camera.fov_deg) || p.camera.fov_deg <= 0.0f
        || p.camera.fov_deg >= 179.0f)
        return "Field of view must be strictly between 0 and 179 degrees.";
    constexpr float kIscoRoundingTolerance = 0.001f;
    if (!std::isfinite(p.disk.r_inner) || p.disk.r_inner <= horizon_radius(p.spin)
        || p.disk.r_inner + kIscoRoundingTolerance < r_isco(p.spin))
        return "Disk inner radius must be at or above the prograde ISCO.";
    if (!std::isfinite(p.disk.r_outer) || p.disk.r_outer <= p.disk.r_inner
        || p.disk.r_outer > kMaxSceneRadius)
        return "Disk outer radius must exceed the inner radius and be at most 1000 M.";
    if (!std::isfinite(p.disk.peak_temp_K) || p.disk.peak_temp_K <= 0.0f)
        return "Disk temperature must be finite and greater than zero kelvin.";
    if (!std::isfinite(p.disk.brightness) || p.disk.brightness < 0.0f)
        return "Disk brightness must be finite and nonnegative.";
    if (p.integrator != IntegratorKind::kRK45 && p.integrator != IntegratorKind::kGeokerr)
        return "Select a supported integrator: RK45 or experimental geokerr.";
    return nullptr;
}

inline bool RenderParams::validate() const { return validation_error(*this) == nullptr; }

static_assert(sizeof(RenderParams) < 256, "RenderParams is too large for constant memory upload");
static_assert(std::is_trivially_copyable<RenderParams>::value,
              "RenderParams must remain trivially copyable to device memory");

} // namespace bhr
