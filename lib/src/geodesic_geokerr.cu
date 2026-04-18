/// @file geodesic_geokerr.cu
/// Dexter-Agol (2009) semi-analytic geodesic integrator — v1.
///
/// This is the first cut: Ferrari quartic + exact R_F for the polar
/// quarter-period, plus APPROXIMATED radial integrals and radial-inversion
/// (clearly marked with `APPROX v1:`). Task 3.4 will replace these with
/// exact Carlson reductions and Jacobi-cn inversion.

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
//   alpha0 = E^2, alpha2 = a^2 E^2 - Lz^2 - Q,
//   alpha3 = 2 (aux^2 + Q) with aux = Lz - a E,
//   alpha4 = -a^2 Q.
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

// Polar biquadratic M(mu) = Q + (a^2 E^2 - Lz^2 - Q) mu^2 - a^2 E^2 mu^4
// Substitute x = mu^2: A x^2 + B x + C = 0.
BHR_HD inline void polar_biquadratic_roots(
    float a, const Conserved& c, float& mu_plus_sq, float& mu_minus_sq)
{
    const float A = -a * a * c.E * c.E;
    const float B = a * a * c.E * c.E - c.Lz * c.Lz - c.Q;
    const float C = c.Q;
    if (fabsf(A) < 1e-20f) {
        // Schwarzschild degeneracy: M(mu) = Q + B mu^2 is linear in x.
        mu_plus_sq  = (fabsf(B) > 1e-20f) ? -C / B : 0.0f;
        mu_minus_sq = -1.0f;   // sentinel
        return;
    }
    const float disc = B * B - 4.0f * A * C;
    const float s = (disc > 0.0f) ? sqrtf(disc) : 0.0f;
    const float r1 = (-B + s) / (2.0f * A);
    const float r2 = (-B - s) / (2.0f * A);
    // Smaller-positive root = physical turning point mu_+^2.
    float lo = fminf(r1, r2);
    float hi = fmaxf(r1, r2);
    // Pick the root in [0, 1] if any.
    if (lo >= 0.0f && lo <= 1.0f) {
        mu_plus_sq  = lo;
        mu_minus_sq = hi;
    } else if (hi >= 0.0f && hi <= 1.0f) {
        mu_plus_sq  = hi;
        mu_minus_sq = lo;
    } else {
        mu_plus_sq  = fmaxf(fminf(hi, 1.0f), 0.0f);
        mu_minus_sq = lo;
    }
}

} // anonymous namespace

BHR_HD HitInfo integrate_geokerr(GeodesicState s, float a, const Conserved& c,
                                 const IntegratorConfig& cfg)
{
    HitInfo hit{};
    hit.type  = HitType::kUnknown;
    hit.r     = s.r;
    hit.theta = s.theta;
    hit.phi   = s.phi;

    // --- Radial quartic roots via Ferrari ---
    const QuarticCoeffs qc = radial_quartic(a, c);
    const QuarticRoots roots = ferrari_quartic(qc.a, qc.b, qc.c, qc.d, qc.e);

    // Collect real roots in ascending order.
    float real_roots[4];
    int n_real = 0;
    const Complex raw[4] = {roots.r0, roots.r1, roots.r2, roots.r3};
    for (int i = 0; i < 4; ++i) {
        const Complex r = raw[i];
        if (isfinite(r.re) && fabsf(r.im) < 1e-3f) {
            real_roots[n_real++] = r.re;
        }
    }
    // Insertion sort.
    for (int i = 1; i < n_real; ++i) {
        float key = real_roots[i];
        int j = i - 1;
        while (j >= 0 && real_roots[j] > key) {
            real_roots[j + 1] = real_roots[j];
            --j;
        }
        real_roots[j + 1] = key;
    }

    const float r_horizon = horizon_radius(a);
    const float u_cam     = 1.0f / fmaxf(s.r, 1e-6f);
    const float u_horizon = 1.0f / fmaxf(r_horizon, 1e-6f);
    // du/dlam = d(1/r)/dlam = -(dr/dlam)/r^2
    const float du_dlam_cam = -s.dr_dlam / (s.r * s.r);

    // --- Polar biquadratic ---
    float mu_plus_sq, mu_minus_sq;
    polar_biquadratic_roots(a, c, mu_plus_sq, mu_minus_sq);
    mu_plus_sq = fminf(fmaxf(mu_plus_sq, 0.0f), 1.0f);
    const float mu_plus = sqrtf(fmaxf(mu_plus_sq, 1e-12f));

    // --- Polar quarter-period in Mino time ---
    // Exact via R_F. For Schwarzschild (a=0) polar motion is trivial and
    // Lth_full can effectively be +infinity; we special-case that below by
    // letting lambda_eq be whatever the radial inversion says.
    const float aE2 = a * a * c.E * c.E;
    auto quarter_period = [&](float mp2, float mm2_abs) -> float {
        if (aE2 < 1e-20f) return 1e20f;   // Schwarzschild: no polar oscillation in Mino
        const float p = fmaxf(mp2, 1e-12f);
        const float m = fmaxf(mm2_abs, p + 1e-12f);
        return carlson_RF(0.0f, fmaxf(m - p, 1e-12f), m) / sqrtf(aE2);
    };
    const float Lth_quarter = quarter_period(mu_plus_sq, fabsf(mu_minus_sq));

    // --- Camera polar phase ---
    const float mu_cam        = cosf(s.theta);
    const float dmu_dlam_cam  = -sinf(s.theta) * s.dth_dlam;
    auto mu_to_phase = [&](float mu_, float dmu_) -> float {
        if (aE2 < 1e-20f || Lth_quarter > 1e19f) return 0.0f;
        const float abs_mu = fminf(fabsf(mu_), mu_plus - 1e-6f);
        const float p = mu_plus_sq;
        const float m = fabsf(mu_minus_sq);
        const float phase = (abs_mu < 1e-8f)
            ? 0.0f
            : carlson_RF(fmaxf(p - abs_mu * abs_mu, 0.0f),
                         fmaxf(m - abs_mu * abs_mu, 0.0f),
                         fmaxf(m, 1e-12f)) / sqrtf(aE2);
        if (mu_ >= 0.0f && dmu_ >= 0.0f) return phase;
        if (mu_ >= 0.0f && dmu_ <  0.0f) return 2.0f * Lth_quarter - phase;
        if (mu_ <  0.0f && dmu_ <  0.0f) return 2.0f * Lth_quarter + phase;
        return 4.0f * Lth_quarter - phase;
    };
    const float phase0 = mu_to_phase(mu_cam, dmu_dlam_cam);

    // First equatorial (mu = 0) crossing in Mino time.
    // Equator is at phase ∈ {0, 2 Lth_q, 4 Lth_q, ...}; find smallest positive
    // increment. For a = 0 polar motion does not actually decouple — we fall
    // back to a large lambda_eq so radial integration decides the fate.
    float lambda_eq;
    if (aE2 < 1e-20f) {
        // APPROX v1: Schwarzschild has Q encoded in theta motion only; we
        // conservatively set lambda_eq very large so the radial events win.
        lambda_eq = 1e20f;
    } else {
        const float two_Lq = 2.0f * Lth_quarter;
        const float rem = fmodf(two_Lq - fmodf(phase0, two_Lq), two_Lq);
        lambda_eq = rem;
    }

    // --- Radial Mino-time integrals ---
    // Reduce ∫_{ua}^{ub} du / sqrt(U(u)) to Carlson R_F when we have 4 real roots
    // and motion is in [u_3, u_4]. In all other cases (complex pairs, motion in
    // a different interval) fall back to a midpoint approximation — APPROX v1.
    auto lambda_between = [&](float ua, float ub) -> float {
        const float l = fminf(ua, ub);
        const float h = fmaxf(ua, ub);
        if (h - l < 1e-12f) return 0.0f;

        if (n_real == 4) {
            const float u1 = real_roots[0], u2 = real_roots[1];
            const float u3 = real_roots[2], u4 = real_roots[3];
            // Physical motion in [u3, u4]?
            if (l >= u3 - 1e-3f && h <= u4 + 1e-3f) {
                // Dexter-Agol 2009 eq. (32) style reduction. For the interval
                // [u3, u4] with U(u) = alpha4 (u - u1)(u - u2)(u - u3)(u - u4),
                // ∫ du / sqrt(U) = 2 / sqrt(alpha4) * [F(h) - F(l)] where
                //   F(u) = R_F((u - u2)(u3 - u1), (u - u1)(u3 - u2), (u3 - u)(u4 - u))
                // (sign conventions from D-A Appendix B; we clamp arguments to
                //  be non-negative as R_F requires).
                auto F = [&](float u) {
                    const float x = (u - u2) * (u3 - u1);
                    const float y = (u - u1) * (u3 - u2);
                    const float z = (u3 - u) * (u4 - u);
                    return carlson_RF(fmaxf(x, 0.0f),
                                      fmaxf(y, 0.0f),
                                      fmaxf(z, 0.0f));
                };
                const float alpha4 = fabsf(qc.a);
                if (alpha4 > 1e-20f) {
                    return 2.0f * fabsf(F(h) - F(l)) / sqrtf(alpha4);
                }
            }
        }

        // APPROX v1: midpoint quadrature fallback. Correct-form replacement
        // requires per-orbit-type Carlson reductions (Dexter-Agol 2009
        // Appendix B) — see Task 3.4.
        const float mid = 0.5f * (l + h);
        // Evaluate U(u) directly from coeffs (stable in mid-of-interval).
        const float U_mid = ((((qc.a) * mid + qc.b) * mid + qc.c) * mid + qc.d) * mid + qc.e;
        const float denom = sqrtf(fmaxf(fabsf(U_mid), 1e-20f));
        return (h - l) / denom;
    };

    const float lambda_to_horizon = (du_dlam_cam > 0.0f)
        ? lambda_between(u_cam, u_horizon) : 1e20f;
    const float lambda_to_escape  = (du_dlam_cam < 0.0f)
        ? lambda_between(u_cam, 0.0f) : 1e20f;

    // --- Event selection ---
    const float t_horizon = lambda_to_horizon;
    const float t_escape  = lambda_to_escape;
    const float t_disk    = lambda_eq;

    if (t_horizon <= t_escape && t_horizon <= t_disk) {
        hit.type   = HitType::kHorizon;
        hit.r      = r_horizon;
        hit.theta  = s.theta;
        hit.phi    = s.phi;
        hit.lambda = t_horizon;
    } else if (t_escape <= t_disk) {
        hit.type   = HitType::kEscape;
        hit.r      = cfg.r_max;
        hit.theta  = s.theta;
        hit.phi    = s.phi;
        hit.lambda = t_escape;
    } else {
        // APPROX v1: linear drift to estimate u at lambda_eq. Task 3.4 replaces
        // this with the exact Jacobi-cn inversion of I_u(u) = lambda.
        const float u_at_eq = u_cam + du_dlam_cam * t_disk;
        const float r_at_eq = 1.0f / fmaxf(u_at_eq, 1e-6f);
        if (r_at_eq >= cfg.disk_r_inner && r_at_eq <= cfg.disk_r_outer) {
            hit.type   = HitType::kDisk;
            hit.r      = r_at_eq;
            hit.theta  = 1.5707963267948966f;
            hit.phi    = s.phi;
            hit.lambda = t_disk;
        } else {
            hit.type   = (du_dlam_cam > 0.0f) ? HitType::kHorizon : HitType::kEscape;
            hit.r      = (du_dlam_cam > 0.0f) ? r_horizon : cfg.r_max;
            hit.theta  = s.theta;
            hit.phi    = s.phi;
            hit.lambda = t_disk;
        }
    }

    hit.steps = n_real;   // profiling: number of real radial roots
    return hit;
}

} // namespace bhr
