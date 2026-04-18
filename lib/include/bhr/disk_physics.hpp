#pragma once
/// @file bhr/disk_physics.hpp
/// Thin accretion disk model: Shakura-Sunyaev temperature + Keplerian 4-velocity.

#include "bhr/kerr.hpp"
#include "bhr/geodesic.hpp"
#include <cmath>

namespace bhr {

/// Disk temperature as a function of radius (Shakura-Sunyaev with inner-edge factor).
/// T(r) = T_peak * (r_in / r)^(3/4) * [1 - sqrt(r_in/r)]^(1/4) * normalization.
/// Normalization chosen so that the maximum of the profile reaches T_peak.
/// Peak occurs at r = (49/36) * r_in ≈ 1.361 * r_in.
BHR_HD inline float disk_temperature(float r, float r_in, float T_peak) {
    if (r <= r_in) return 0.0f;
    const float rin_over_r = r_in / r;
    const float factor = powf(rin_over_r, 0.75f) * powf(fmaxf(0.0f, 1.0f - sqrtf(rin_over_r)), 0.25f);
    constexpr float NORM = 0.4886f;
    return T_peak * factor / NORM;
}

/// Keplerian angular velocity in geometrized units.
BHR_HD inline float kepler_omega(float r, float a) {
    return 1.0f / (powf(r, 1.5f) + a);
}

/// Disk 4-velocity at (r, theta=pi/2). Returns (u^t, u^phi) in coordinate basis.
/// u^r = u^theta = 0.
BHR_HD inline void disk_4velocity(float r, float a, float& u_t, float& u_phi) {
    const float Omega = kepler_omega(r, a);
    const float g_tt = -(1.0f - 2.0f / r);
    const float g_tphi = -2.0f * a / r;
    const float g_phiphi = r * r + a * a + 2.0f * a * a / r;
    const float denom = -g_tt - 2.0f * Omega * g_tphi - Omega * Omega * g_phiphi;
    if (denom <= 0.0f) {
        u_t = 0.0f; u_phi = 0.0f;
        return;
    }
    u_t = 1.0f / sqrtf(denom);
    u_phi = Omega * u_t;
}

} // namespace bhr
