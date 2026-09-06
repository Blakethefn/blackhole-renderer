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

// Jacobi sn^2(psi, m) via AGM descending algorithm.
BHR_HD inline float jacobi_sn_sq_agm(float psi, float m) {
    if (m < 1e-8f) { const float s = sinf(psi); return s * s; }
    if (m > 1.0f - 1e-8f) { const float t = tanhf(psi); return t * t; }
    float a[16], b[16], c_arr[16];
    int n_iter = 0;
    a[0] = 1.0f; b[0] = sqrtf(1.0f - m);
    for (int i = 0; i < 15; ++i) {
        c_arr[i] = 0.5f * (a[i] - b[i]);
        a[i+1]   = 0.5f * (a[i] + b[i]);
        b[i+1]   = sqrtf(a[i] * b[i]);
        n_iter = i + 1;
        if (fabsf(c_arr[i]) < 1e-6f * a[i]) break;
    }
    float phi = ldexpf(a[n_iter], n_iter) * psi;
    for (int i = n_iter; i > 0; --i) {
        phi = 0.5f * (phi + asinf(fminf(fmaxf(c_arr[i-1] * sinf(phi) / a[i], -1.0f), 1.0f)));
    }
    const float sp = sinf(phi);
    return sp * sp;
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
    const float r_horizon = horizon_radius(a);
    const float u_cam     = 1.0f / fmaxf(s.r, 1e-6f);
    const float u_horizon = 1.0f / fmaxf(r_horizon, 1e-6f);

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

    auto eval_U = [&](float u) -> float {
        return ((((qc.a) * u + qc.b) * u + qc.c) * u + qc.d) * u + qc.e;
    };

    // Ferrari is useful for the analytic path, but float closed-form roots
    // can be assigned a nonzero imaginary part near a physical radial turning
    // point.  Event classification only needs the roots reachable from the
    // camera, so recover those with bracketed bisection on [0, u_horizon].
    // This is independent of the closed-form branch choice and cannot invent
    // a root: every retained root has a sign change in U(u).
    constexpr int kRadialRootBrackets = 128;
    float physical_roots[4];
    int n_physical_roots = 0;
    float u_left = 0.0f;
    float U_left = eval_U(u_left);
    for (int i = 1; i <= kRadialRootBrackets && n_physical_roots < 4; ++i) {
        const float u_right = u_horizon * (float)i / (float)kRadialRootBrackets;
        const float U_right = eval_U(u_right);
        if ((U_left < 0.0f && U_right > 0.0f) || (U_left > 0.0f && U_right < 0.0f)) {
            float lo = u_left;
            float hi = u_right;
            float flo = U_left;
            for (int it = 0; it < 28; ++it) {
                const float mid = 0.5f * (lo + hi);
                const float fmid = eval_U(mid);
                if ((flo < 0.0f && fmid < 0.0f) || (flo > 0.0f && fmid > 0.0f)) {
                    lo = mid;
                    flo = fmid;
                } else {
                    hi = mid;
                }
            }
            physical_roots[n_physical_roots++] = 0.5f * (lo + hi);
        }
        u_left = u_right;
        U_left = U_right;
    }
    // Preserve the established Schwarzschild semi-analytic image path; this
    // recovery targets the Kerr Ferrari branch that exhibited the failure.
    if (a != 0.0f && n_physical_roots > 0) {
        n_real = n_physical_roots;
        for (int i = 0; i < n_real; ++i) real_roots[i] = physical_roots[i];
    }
    // du/dlam = d(1/r)/dlam = -(dr/dlam)/r^2
    const float du_dlam_cam = -s.dr_dlam / (s.r * s.r);

    // --- Polar biquadratic ---
    float mu_plus_sq, mu_minus_sq;
    polar_biquadratic_roots(a, c, mu_plus_sq, mu_minus_sq);
    mu_plus_sq = fminf(fmaxf(mu_plus_sq, 0.0f), 1.0f);
    const float mu_plus = sqrtf(fmaxf(mu_plus_sq, 1e-12f));

    // --- Polar quarter-period in Mino time ---
    // The polar potential M(mu) = (dmu/dlam)^2 factorizes as
    //   M(mu) = |aE2| * (mu_plus^2 - mu^2) * (mu^2 + |mu_minus^2|)
    // (note the SUM in the second factor: mu^2 + |mu_minus^2|, not a difference).
    // Standard Carlson reduction using mu = mu_plus * sin(psi) gives:
    //   ∫_0^{mu_0} dmu / sqrt(M) = mu_0 * R_F((p-mu_0^2)*m, (m+mu_0^2)*p, p*m) / sqrt(|aE2|)
    // where p = mu_plus^2 and m = |mu_minus^2|. Taking mu_0 = mu_plus gives the
    // quarter-period: Lth_quarter = mu_plus * R_F(0, (m+p)*p, p*m) / sqrt(|aE2|).
    const float aE2 = a * a * c.E * c.E;
    auto polar_phase = [&](float mu_0) -> float {
        if (aE2 < 1e-20f) return 0.0f;
        const float p = fmaxf(mu_plus_sq, 1e-12f);
        const float m = fmaxf(fabsf(mu_minus_sq), 1e-12f);
        const float abs_mu = fminf(fabsf(mu_0), sqrtf(p) - 1e-7f);
        if (abs_mu < 1e-8f) return 0.0f;
        const float mu2 = abs_mu * abs_mu;
        const float x = fmaxf((p - mu2) * m, 0.0f);
        const float y = fmaxf((m + mu2) * p, 1e-20f);
        const float z = fmaxf(p * m, 1e-20f);
        return abs_mu * carlson_RF(x, y, z) / sqrtf(aE2);
    };
    const float Lth_quarter = (aE2 < 1e-20f) ? 1e20f : [&]() {
        const float p = fmaxf(mu_plus_sq, 1e-12f);
        const float m = fmaxf(fabsf(mu_minus_sq), 1e-12f);
        const float y = fmaxf((m + p) * p, 1e-20f);
        const float z = fmaxf(p * m, 1e-20f);
        return sqrtf(p) * carlson_RF(0.0f, y, z) / sqrtf(aE2);
    }();

    // --- Camera polar phase ---
    const float mu_cam        = cosf(s.theta);
    const float dmu_dlam_cam  = -sinf(s.theta) * s.dth_dlam;
    auto mu_to_phase = [&](float mu_, float dmu_) -> float {
        if (aE2 < 1e-20f || Lth_quarter > 1e19f) return 0.0f;
        const float phase = polar_phase(mu_);
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
        // Schwarzschild: use explicit arcsin polar formula.
        if (c.Q < 1e-12f) {
            lambda_eq = 1e20f;   // radial photon, no equatorial crossing
        } else {
            const float mu_p  = sqrtf(fmaxf(mu_plus_sq, 1e-12f));
            const float sqrtQ = sqrtf(c.Q);
            const float Lq    = (float)M_PI_2 * mu_p / sqrtQ;
            const float phi_c = mu_p / sqrtQ * asinf(fminf(fabsf(mu_cam) / mu_p, 1.0f));
            // mu_cam * dmu_dlam_cam < 0 means ray is moving toward equator.
            // Time to equator = phi_c (current Mino phase from equator).
            // Moving away = must reach turning point first: 2*Lq - phi_c.
            const bool toward_eq = (mu_cam * dmu_dlam_cam < 0.0f)
                                || (fabsf(mu_cam) < 1e-8f && dmu_dlam_cam != 0.0f);
            lambda_eq = toward_eq ? phi_c : (2.0f * Lq - phi_c);
            if (lambda_eq < 0.0f) lambda_eq = 1e20f;
        }
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

        // 16-point Gauss-Legendre nodes/weights on [-1, 1].
        constexpr int GL_N = 16;
        constexpr float GL_NODES[GL_N] = {
            -0.9894009349916499f, -0.9445750230732326f,
            -0.8656312023878318f, -0.7554044083550030f,
            -0.6178762444026438f, -0.4580167776572274f,
            -0.2816035507792589f, -0.0950125098376374f,
             0.0950125098376374f,  0.2816035507792589f,
             0.4580167776572274f,  0.6178762444026438f,
             0.7554044083550030f,  0.8656312023878318f,
             0.9445750230732326f,  0.9894009349916499f
        };
        constexpr float GL_WEIGHTS[GL_N] = {
            0.0271524594117541f, 0.0622535239386479f,
            0.0951585116824928f, 0.1246289712555339f,
            0.1495959888165767f, 0.1691565193950025f,
            0.1826034150449236f, 0.1894506104550685f,
            0.1894506104550685f, 0.1826034150449236f,
            0.1691565193950025f, 0.1495959888165767f,
            0.1246289712555339f, 0.0951585116824928f,
            0.0622535239386479f, 0.0271524594117541f
        };
        // Detect endpoint singularity: U(endpoint) << U(interior) means GL diverges.
        // This happens when the endpoint is a radial turning point (root of U).
        const float U_h_val = eval_U(h);
        const float U_l_val = eval_U(l);
        const float U_mid   = eval_U(0.5f * (l + h));
        const float U_scale = fmaxf(fabsf(U_mid),
                              fmaxf(fabsf(U_l_val), fabsf(U_h_val))) + 1e-30f;
        const bool h_singular = (U_h_val < 1e-3f * U_scale);
        const bool l_singular = (!h_singular && U_l_val < 1e-3f * U_scale);

        float integral = 0.0f;
        if (h_singular || l_singular) {
            // Singularity-removing trig substitution to handle U(endpoint) = 0.
            // For h_singular: u = h - (h-l)*sin²(s), s ∈ [0, π/2].
            //   s=0 → u=h (singular end, but integrand → cos(0)/√|U'(h)*(h-l)| bounded)
            //   s=π/2 → u=l
            // For l_singular: u = l + (h-l)*sin²(s), s ∈ [0, π/2].
            // In both cases: ∫_l^h du/√U = 2(h-l) ∫₀^{π/2} sin(s)cos(s)/√U(u(s)) ds.
            // Map s = (π/4)(1+t) onto GL nodes t ∈ [-1, 1]: factor becomes (π/2)(h-l).
            constexpr float PI_OVER_4 = 0.7853981633974483f;
            const float factor = 1.5707963267948966f * (h - l);  // (π/2)*(h-l)
            for (int k = 0; k < GL_N; ++k) {
                const float s  = PI_OVER_4 * (1.0f + GL_NODES[k]);
                const float ss = sinf(s), cs = cosf(s);
                const float u_k = h_singular
                    ? (h - (h - l) * ss * ss)
                    : (l + (h - l) * ss * ss);
                const float U_k = eval_U(u_k);
                if (U_k > 0.0f) integral += GL_WEIGHTS[k] * ss * cs / sqrtf(U_k);
            }
            return fabsf(factor * integral);
        }

        // Standard GL (no endpoint singularity).
        const float half = 0.5f * (h - l);
        const float mid2 = 0.5f * (h + l);
        for (int k = 0; k < GL_N; ++k) {
            const float u_k = mid2 + half * GL_NODES[k];
            const float U_k = eval_U(u_k);
            if (U_k > 0.0f) integral += GL_WEIGHTS[k] / sqrtf(U_k);
        }
        return fabsf(half * integral);
    };

    // Find the first real root strictly greater than u_cam (outward turning
    // point if photon is inbound, i.e., du_dlam > 0). If such a root exists and
    // lies between u_cam and u_horizon, the photon turns back before reaching
    // the horizon — it escapes. Similarly find first root below u_cam for the
    // inner turning point (for outbound photons that may turn back inward).
    float u_turn_out = 1e30f;   // first root > u_cam
    float u_turn_in  = -1e30f;  // largest root < u_cam
    for (int i = 0; i < n_real; ++i) {
        const float r = real_roots[i];
        if (r > u_cam + 1e-6f && r < u_turn_out) u_turn_out = r;
        if (r < u_cam - 1e-6f && r > u_turn_in)  u_turn_in  = r;
    }

    float lambda_to_horizon = 1e20f;
    float lambda_to_escape  = 1e20f;
    if (du_dlam_cam > 0.0f) {
        // Inbound: can reach horizon only if no turning point between u_cam and u_horizon.
        if (u_turn_out >= u_horizon - 1e-6f) {
            lambda_to_horizon = lambda_between(u_cam, u_horizon);
        } else {
            // Turns at u_turn_out, then escapes: lambda to u_turn_out + lambda to 0.
            lambda_to_escape = lambda_between(u_cam, u_turn_out)
                             + lambda_between(u_turn_out, 0.0f);
        }
    } else if (du_dlam_cam < 0.0f) {
        // Outbound: can escape only if no turning point between u_cam and 0.
        if (u_turn_in <= 1e-6f) {
            lambda_to_escape = lambda_between(u_cam, 0.0f);
        } else {
            // Turns at u_turn_in, then falls inward: lambda to u_turn_in + lambda to u_horizon.
            lambda_to_horizon = lambda_between(u_cam, u_turn_in)
                              + lambda_between(u_turn_in, u_horizon);
        }
    }

    // --- Multi-crossing equatorial sweep ---
    // Equatorial crossings happen every two_Lq_theta in Mino time.
    // The first may be at large r (outside the disk); check subsequent ones
    // until a disk hit is found or the photon escapes/captures.
    const float M_PI_F = 3.14159265f;
    float two_Lq_theta;
    if (aE2 < 1e-20f) {
        two_Lq_theta = (c.Q > 1e-12f)
            ? (M_PI_F * mu_plus / sqrtf(c.Q))
            : 1e20f;
    } else {
        two_Lq_theta = 2.0f * Lth_quarter;
    }

    // Precompute inward distance to pericenter/horizon for outer-orbit photons.
    const float u_turn = (du_dlam_cam > 0.0f) ? u_turn_out : u_turn_in;
    float T_inward;
    if (du_dlam_cam > 0.0f && u_turn_out >= 1e29f) {
        // Direct inbound with no outer turning point: full inward leg goes to horizon.
        // Setting T_inward = lambda_to_horizon ensures u_at_mino uses the inward-leg
        // bisection (lambda_between(u_cam, u) = lam) rather than the return-leg formula.
        T_inward = lambda_to_horizon;
    } else if (u_turn < 1e29f && u_turn > -1e29f) {
        T_inward = lambda_between(u_cam, u_turn);
    } else {
        T_inward = 0.0f;
    }

    const float t_horizon = lambda_to_horizon;
    const float t_escape  = lambda_to_escape;
    const float t_limit   = fminf(t_horizon, t_escape);

    // Helper: compute u at total Mino time lam from camera start.
    // Returns u_cam if lam <= 0, or u at the appropriate leg of the orbit.
    auto u_at_mino = [&](float lam) -> float {
        if (lam <= 0.0f) return u_cam;
        const bool inbound = (du_dlam_cam > 0.0f);
        const float u_ref  = (u_turn < 1e29f) ? u_turn : u_horizon;

        if (inbound) {
            if (lam <= T_inward) {
                // Inward leg: find u in [u_cam, u_ref] with lambda_between(u_cam, u) = lam.
                float lo = u_cam, hi = u_ref;
                for (int it = 0; it < 24; ++it) {
                    const float um = 0.5f * (lo + hi);
                    if (lambda_between(u_cam, um) < lam) lo = um; else hi = um;
                }
                return 0.5f * (lo + hi);
            } else {
                // Outward leg: find u in [0, u_ref] with lambda_between(u, u_ref) = lam - T_inward.
                const float lam_ret = lam - T_inward;
                float lo = 0.0f, hi = u_ref;
                for (int it = 0; it < 24; ++it) {
                    const float um = 0.5f * (lo + hi);
                    if (lambda_between(um, u_ref) < lam_ret) hi = um; else lo = um;
                }
                return 0.5f * (lo + hi);
            }
        } else {
            // Outbound photon: initially moving toward smaller u.
            const float u_ref2 = (u_turn_in > -1e29f) ? fmaxf(u_turn_in, 0.0f) : 0.0f;
            if (lam <= lambda_between(u_ref2, u_cam)) {
                // Still on outward leg from cam to 0 (or to u_turn_in).
                float lo = u_ref2, hi = u_cam;
                for (int it = 0; it < 24; ++it) {
                    const float um = 0.5f * (lo + hi);
                    if (lambda_between(um, u_cam) < lam) hi = um; else lo = um;
                }
                return 0.5f * (lo + hi);
            } else {
                // Bounced and coming back in.
                const float lam_in = lam - lambda_between(u_ref2, u_cam);
                float lo = u_ref2, hi = u_horizon;
                for (int it = 0; it < 24; ++it) {
                    const float um = 0.5f * (lo + hi);
                    if (lambda_between(u_ref2, um) < lam_in) lo = um; else hi = um;
                }
                return 0.5f * (lo + hi);
            }
        }
    };

    // Scan crossings.
    hit.type = HitType::kUnknown;
    for (int k = 0; k < 32 && hit.type == HitType::kUnknown; ++k) {
        const float lam_k = lambda_eq + (float)k * two_Lq_theta;
        if (lam_k > t_limit + 1e-6f || two_Lq_theta > 1e19f) break;

        const float u_k   = u_at_mino(lam_k);
        const float r_k   = 1.0f / fmaxf(u_k, 1e-6f);

        if (r_k >= cfg.disk_r_inner && r_k <= cfg.disk_r_outer) {
            hit.type   = HitType::kDisk;
            hit.r      = r_k;
            hit.theta  = 1.5707963267948966f;
            hit.phi    = s.phi;
            hit.lambda = lam_k;
        }
    }

    // If no disk hit found, classify by dominant radial event.
    if (hit.type == HitType::kUnknown) {
        if (t_horizon <= t_escape) {
            hit.type   = HitType::kHorizon;
            hit.r      = r_horizon;
            hit.theta  = s.theta;
            hit.phi    = s.phi;
            hit.lambda = t_horizon;
        } else {
            hit.type   = HitType::kEscape;
            hit.r      = cfg.r_max;
            hit.theta  = s.theta;
            hit.phi    = s.phi;
            hit.lambda = t_escape;
        }
    }

    hit.steps = n_real;
    return hit;
}

} // namespace bhr
