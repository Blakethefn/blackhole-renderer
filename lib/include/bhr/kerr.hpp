#pragma once
/// @file bhr/kerr.hpp
/// Kerr metric helpers (Boyer-Lindquist). Geometrized units, M=1.
/// Device-inline friendly — use from both .cpp and .cu.

#if defined(__CUDACC__)
  #define BHR_HD __host__ __device__
#else
  #define BHR_HD
#endif

#include <cmath>

namespace bhr {

/// Outer horizon radius r+ = 1 + sqrt(1 - a^2).
BHR_HD inline float horizon_radius(float a) {
    return 1.0f + std::sqrt(std::fmax(0.0f, 1.0f - a * a));
}

/// Inner-most stable circular orbit (prograde, equatorial).
/// Bardeen-Press-Teukolsky 1972. For a=0 -> 6, a=1 -> 1.
BHR_HD inline float r_isco(float a) {
    const float z1 = 1.0f + std::cbrt(1.0f - a * a)
                     * (std::cbrt(1.0f + a) + std::cbrt(1.0f - a));
    const float z2 = std::sqrt(3.0f * a * a + z1 * z1);
    return 3.0f + z2 - std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2));
}

/// Sigma(r, theta) = r^2 + a^2 cos^2(theta).
BHR_HD inline float sigma(float r, float cos_theta, float a) {
    return r * r + a * a * cos_theta * cos_theta;
}

/// Delta(r) = r^2 - 2r + a^2.
BHR_HD inline float delta(float r, float a) {
    return r * r - 2.0f * r + a * a;
}

/// A(r, theta) = (r^2 + a^2)^2 - a^2 Delta sin^2(theta).
BHR_HD inline float bl_A(float r, float sin_theta, float a) {
    const float rr_aa = r * r + a * a;
    return rr_aa * rr_aa - a * a * delta(r, a) * sin_theta * sin_theta;
}

/// Frame-dragging angular velocity omega = 2ar / A.
BHR_HD inline float omega_drag(float r, float sin_theta, float a) {
    return 2.0f * a * r / bl_A(r, sin_theta, a);
}

/// Lapse alpha = sqrt(Sigma Delta / A).
BHR_HD inline float lapse(float r, float sin_theta, float cos_theta, float a) {
    const float S = sigma(r, cos_theta, a);
    const float D = delta(r, a);
    const float A = bl_A(r, sin_theta, a);
    return std::sqrt(S * D / A);
}

} // namespace bhr
