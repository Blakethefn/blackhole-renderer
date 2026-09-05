#pragma once
/// @file bhr/redshift.hpp
/// Photon redshift factor g = (p_μ u_obs^μ) / (p_μ u_emit^μ).

#include "bhr/kerr.hpp"
#include "bhr/geodesic.hpp"
#include "bhr/disk_physics.hpp"
#include <cmath>

namespace bhr {

/// Redshift factor g for a photon with conserved (E, L_z, Q) arriving at a disk
/// emitter at radius r_emit (theta = pi/2). Observer is at infinity.
///
/// Derivation: For observer at infinity, u_obs^mu = (1, 0, 0, 0), so
///   p_mu u_obs^mu = p_t = -E (since p_t is covariant).
/// For disk emitter with u^mu = (u^t, 0, 0, u^phi):
///   p_mu u_emit^mu = p_t u^t + p_phi u^phi = -E u^t + Lz u^phi
/// So
///   g = -E / (-E u^t + Lz u^phi) = E / (E u^t - Lz u^phi)
///
/// Returns 1.0 when the denominator collapses (flat limit sanity).
BHR_HD inline float redshift_disk_to_infinity(float r_emit, float a, const Conserved& c) {
    float u_t, u_phi;
    disk_4velocity(r_emit, a, u_t, u_phi);
    const float denom = c.E * u_t - c.Lz * u_phi;
    if (fabsf(denom) < 1e-12f) return 1.0f;
    return c.E / denom;
}

/// Workbench visualization switches for the existing total frequency factor.
/// Approximate split: time dilation 1/u^t (gravity AND transverse orbital motion),
/// directional Doppler 1/(1 - Omega*Lz/E). These are diagnostic contributions,
/// not a unique general-relativistic separation of gravity and emitter motion.
/// Both enabled calls the original formula directly to preserve legacy output.
BHR_HD inline float disk_frequency_shift(float r_emit, float a, const Conserved& c,
                                         bool doppler, bool redshift) {
    if (doppler && redshift) return redshift_disk_to_infinity(r_emit, a, c);
    if (!doppler && !redshift) return 1.0f;
    float u_t, u_phi;
    disk_4velocity(r_emit, a, u_t, u_phi);
    if (u_t <= 0.0f) return 1.0f;
    if (!doppler) return 1.0f / u_t;
    const float denom = c.E - c.Lz * (u_phi / u_t);
    return fabsf(denom) < 1e-12f ? 1.0f : c.E / denom;
}

} // namespace bhr
