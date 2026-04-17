#pragma once
/// @file bhr/geodesic.hpp
/// Kerr geodesic equations in Mino time.

#include "bhr/kerr.hpp"

namespace bhr {

/// Photon state in Boyer-Lindquist coordinates.
/// We integrate (r, theta) as second-order ODE so we need their derivatives.
struct GeodesicState {
    float r;
    float theta;
    float phi;
    float t;
    float dr_dlam;    ///< dr/dλ (Mino time)
    float dth_dlam;   ///< dθ/dλ
};

/// Conserved quantities for a Kerr photon.
struct Conserved {
    float E;     ///< Energy at infinity
    float Lz;    ///< Axial angular momentum
    float Q;     ///< Carter's constant
};

/// Radial potential R(r) for photons (m=0).
BHR_HD inline float potential_R(float r, float a, const Conserved& c) {
    const float rr_aa = r * r + a * a;
    const float P = c.E * rr_aa - a * c.Lz;
    const float aux = (c.Lz - a * c.E);
    return P * P - delta(r, a) * (aux * aux + c.Q);
}

/// dR/dr for the second-order radial ODE.
BHR_HD inline float dR_dr(float r, float a, const Conserved& c) {
    const float rr_aa = r * r + a * a;
    const float P = c.E * rr_aa - a * c.Lz;
    const float dP_dr = 2.0f * r * c.E;
    const float aux = (c.Lz - a * c.E);
    const float dDelta_dr = 2.0f * r - 2.0f;
    return 2.0f * P * dP_dr - dDelta_dr * (aux * aux + c.Q);
}

/// Polar potential Theta(theta). Uses sin/cos of theta directly.
BHR_HD inline float potential_Theta(float sin_theta, float cos_theta, float a, const Conserved& c) {
    const float sin2 = sin_theta * sin_theta;
    return c.Q + cos_theta * cos_theta * (a * a * c.E * c.E - (c.Lz * c.Lz) / sin2);
}

/// dTheta/dtheta.
/// Theta = Q + mu^2 [a^2 E^2 - Lz^2 / (1 - mu^2)] where mu = cos(theta).
/// dTheta/dmu = 2*mu*(a^2 E^2 - Lz^2/(1-mu^2)) - 2*mu^3*Lz^2/(1-mu^2)^2
/// dTheta/dtheta = -sin(theta) * dTheta/dmu.
BHR_HD inline float dTheta_dtheta(float sin_theta, float cos_theta, float a, const Conserved& c) {
    const float sin2 = sin_theta * sin_theta;
    const float mu = cos_theta;
    const float aE2 = a * a * c.E * c.E;
    const float Lz2 = c.Lz * c.Lz;
    const float dTh_dmu = 2.0f * mu * (aE2 - Lz2 / sin2)
                        - 2.0f * mu * mu * mu * Lz2 / (sin2 * sin2);
    return -sin_theta * dTh_dmu;
}

/// dphi/dlam for a Kerr geodesic.
BHR_HD inline float dphi_dlam(float r, float sin_theta, float a, const Conserved& c) {
    const float rr_aa = r * r + a * a;
    const float P = c.E * rr_aa - a * c.Lz;
    const float D = delta(r, a);
    const float sin2 = sin_theta * sin_theta;
    return a * P / D - a * c.E + c.Lz / sin2;
}

/// dt/dlam.
BHR_HD inline float dt_dlam(float r, float sin_theta, float a, const Conserved& c) {
    const float rr_aa = r * r + a * a;
    const float P = c.E * rr_aa - a * c.Lz;
    const float sin2 = sin_theta * sin_theta;
    return rr_aa * P / delta(r, a) - a * a * c.E * sin2 + a * c.Lz;
}

/// Full RHS: d(state)/dlam.
///   dr/dlam = state.dr_dlam (first-order component)
///   dtheta/dlam = state.dth_dlam
///   dphi/dlam = dphi_dlam(...)
///   dt/dlam = dt_dlam(...)
///   d(dr_dlam)/dlam = (1/2) dR/dr
///   d(dth_dlam)/dlam = (1/2) dTheta/dtheta
BHR_HD inline GeodesicState rhs(const GeodesicState& s, float a, const Conserved& c) {
    const float sth = std::sin(s.theta);
    const float cth = std::cos(s.theta);
    GeodesicState d{};
    d.r       = s.dr_dlam;
    d.theta   = s.dth_dlam;
    d.phi     = dphi_dlam(s.r, sth, a, c);
    d.t       = dt_dlam(s.r, sth, a, c);
    d.dr_dlam = 0.5f * dR_dr(s.r, a, c);
    d.dth_dlam = 0.5f * dTheta_dtheta(sth, cth, a, c);
    return d;
}

// ============================================================================
// Integrator entry points (implementation in geodesic_rk45.cu)
// ============================================================================

enum class HitType : int { kUnknown = 0, kHorizon, kDisk, kEscape };

struct HitInfo {
    HitType type = HitType::kUnknown;
    float r = 0.0f;
    float theta = 0.0f;
    float phi = 0.0f;
    float lambda = 0.0f;
    int steps = 0;
};

struct IntegratorConfig {
    float r_max = 1000.0f;
    float horizon_eps = 1e-3f;
    float disk_r_inner = 6.0f;
    float disk_r_outer = 20.0f;
    float tol = 1e-6f;
    float h_init = 0.1f;
    float h_min = 1e-5f;
    float h_max = 10.0f;
    int max_steps = 20000;
};

BHR_HD HitInfo integrate_rk45(GeodesicState s, float a, const Conserved& c,
                              const IntegratorConfig& cfg);

} // namespace bhr
