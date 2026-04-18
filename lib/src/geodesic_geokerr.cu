/// @file geodesic_geokerr.cu
/// Dexter-Agol (2009) semi-analytic geodesic integrator — scaffold only.
/// The actual integrator body is added in the follow-up commit.

#include "bhr/geodesic.hpp"
#include "bhr/elliptic.hpp"
#include "bhr/ferrari.hpp"
#include "bhr/kerr.hpp"
#include <cmath>

namespace bhr {

namespace {

struct QuarticCoeffs {
    float a, b, c, d, e;   // a u^4 + b u^3 + c u^2 + d u + e
};

// Radial quartic U(u) with u = 1/r. See plan "Physics Cheat Sheet":
//   U(u) = alpha0 + alpha2 u^2 + alpha3 u^3 + alpha4 u^4
BHR_HD inline QuarticCoeffs radial_quartic(float a, const Conserved& c) {
    const float aux = c.Lz - a * c.E;
    QuarticCoeffs q;
    q.a = -a * a * c.Q;                                // u^4
    q.b = 2.0f * (aux * aux + c.Q);                    // u^3
    q.c = a * a * c.E * c.E - c.Lz * c.Lz - c.Q;       // u^2
    q.d = 0.0f;                                        // u^1
    q.e = c.E * c.E;                                   // u^0
    return q;
}

// Polar biquadratic M(mu) = Q + (a^2 E^2 - Lz^2 - Q) mu^2 - a^2 E^2 mu^4.
BHR_HD inline void polar_biquadratic_roots(
    float a, const Conserved& c, float& mu_plus_sq, float& mu_minus_sq)
{
    const float A = -a * a * c.E * c.E;
    const float B = a * a * c.E * c.E - c.Lz * c.Lz - c.Q;
    const float C = c.Q;
    if (fabsf(A) < 1e-20f) {
        mu_plus_sq  = (fabsf(B) > 1e-20f) ? -C / B : 0.0f;
        mu_minus_sq = -1.0f;
        return;
    }
    const float disc = B * B - 4.0f * A * C;
    const float s = (disc > 0.0f) ? sqrtf(disc) : 0.0f;
    const float r1 = (-B + s) / (2.0f * A);
    const float r2 = (-B - s) / (2.0f * A);
    float lo = fminf(r1, r2);
    float hi = fmaxf(r1, r2);
    if (lo >= 0.0f && lo <= 1.0f) { mu_plus_sq = lo; mu_minus_sq = hi; }
    else if (hi >= 0.0f && hi <= 1.0f) { mu_plus_sq = hi; mu_minus_sq = lo; }
    else { mu_plus_sq = fmaxf(fminf(hi, 1.0f), 0.0f); mu_minus_sq = lo; }
}

} // anonymous namespace

// Stub — returns kUnknown until the follow-up commit fills in the body.
BHR_HD HitInfo integrate_geokerr(GeodesicState s, float /*a*/,
                                 const Conserved& /*c*/,
                                 const IntegratorConfig& /*cfg*/)
{
    HitInfo hit{};
    hit.type  = HitType::kUnknown;
    hit.r     = s.r;
    hit.theta = s.theta;
    hit.phi   = s.phi;
    return hit;
}

} // namespace bhr
