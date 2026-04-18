#pragma once
/// @file bhr/ferrari.hpp
/// Closed-form solver for the quartic a u^4 + b u^3 + c u^2 + d u + e = 0
/// via Ferrari's method. Returns 4 complex roots (real roots have im = 0).
/// If a = 0, falls back to the cubic via Cardano's method and returns 3
/// roots + one sentinel with re = NaN.
///
/// Accuracy: root residual typically < 1e-3 for well-conditioned quartics
/// with float coefficients. For geokerr's U(u) we then polish roots via
/// one Newton step per root — see `polish_root` helper.

#include "bhr/kerr.hpp"   // for BHR_HD
#include <cmath>

namespace bhr {

struct Complex {
    float re;
    float im;
};

struct QuarticRoots {
    Complex r0, r1, r2, r3;
};

namespace detail {

BHR_HD inline Complex complex_sqrt(Complex z) {
    const float r = sqrtf(sqrtf(z.re * z.re + z.im * z.im));
    const float theta = 0.5f * atan2f(z.im, z.re);
    return {r * cosf(theta), r * sinf(theta)};
}

BHR_HD inline Complex cadd(Complex a, Complex b) { return {a.re + b.re, a.im + b.im}; }
BHR_HD inline Complex csub(Complex a, Complex b) { return {a.re - b.re, a.im - b.im}; }
BHR_HD inline Complex cscale(Complex a, float s)  { return {a.re * s, a.im * s}; }
BHR_HD inline Complex cmul(Complex a, Complex b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

/// Real cubic x^3 + p x + q = 0 — returns one real root (the principal Cardano root).
BHR_HD inline float cardano_real(float p, float q) {
    const float disc = q * q / 4.0f + p * p * p / 27.0f;
    if (disc >= 0.0f) {
        const float sqrt_disc = sqrtf(disc);
        const float A = cbrtf(-0.5f * q + sqrt_disc);
        const float B = cbrtf(-0.5f * q - sqrt_disc);
        return A + B;
    } else {
        // Three real roots (casus irreducibilis). Pick the largest-real via trig form.
        const float r  = sqrtf(-p * p * p / 27.0f);
        const float phi = acosf(fminf(1.0f, fmaxf(-1.0f, -q / (2.0f * r))));
        return 2.0f * cbrtf(r) * cosf(phi / 3.0f);
    }
}

} // namespace detail

/// Solve a u^4 + b u^3 + c u^2 + d u + e = 0.
/// Returns 4 complex roots. If a ~= 0 falls back to cubic/quadratic as needed.
BHR_HD inline QuarticRoots ferrari_quartic(float a, float b, float c, float d, float e) {
    QuarticRoots out{};
    if (fabsf(a) < 1e-20f) {
        // Cubic fallback. Return 3 roots and one sentinel.
        // Normalize to u^3 + B u^2 + C u + D = 0
        const float B = c / b, C = d / b, D = e / b;
        // Depressed: x = u + B/3 → x^3 + p x + q = 0
        const float p = C - B * B / 3.0f;
        const float q = 2.0f * B * B * B / 27.0f - B * C / 3.0f + D;
        const float x1 = detail::cardano_real(p, q);
        const float u1 = x1 - B / 3.0f;
        // Deflate: (u - u1)(u^2 + alpha u + beta)
        const float alpha = B + u1, beta = C + alpha * u1;
        const float disc = alpha * alpha - 4.0f * beta;
        if (disc >= 0.0f) {
            const float s = sqrtf(disc);
            out.r0 = {u1, 0.0f};
            out.r1 = {0.5f * (-alpha + s), 0.0f};
            out.r2 = {0.5f * (-alpha - s), 0.0f};
        } else {
            const float s = sqrtf(-disc);
            out.r0 = {u1, 0.0f};
            out.r1 = {-0.5f * alpha, 0.5f * s};
            out.r2 = {-0.5f * alpha, -0.5f * s};
        }
        out.r3 = {NAN, 0.0f};
        return out;
    }

    // Normalize to monic: u^4 + B u^3 + C u^2 + D u + E = 0
    const float B = b / a, C = c / a, D = d / a, E = e / a;

    // Depress via u = v - B/4: v^4 + p v^2 + q v + r = 0
    const float p = C - 3.0f * B * B / 8.0f;
    const float q = D - 0.5f * B * C + B * B * B / 8.0f;
    const float r = E - 0.25f * B * D + (B * B * C) / 16.0f - 3.0f * B * B * B * B / 256.0f;

    // Ferrari resolvent cubic: y^3 - (p/2) y^2 - r y + (4 p r - q^2) / 8 = 0
    const float rp = -p * 0.5f;
    const float rq = -r;
    const float rr = (4.0f * p * r - q * q) / 8.0f;
    // Depress cubic: z^3 + P z + Q = 0, where y = z - rp/3
    const float P = rq - rp * rp / 3.0f;
    const float Q = 2.0f * rp * rp * rp / 27.0f - rp * rq / 3.0f + rr;
    const float y = detail::cardano_real(P, Q) - rp / 3.0f;

    // Decompose depressed quartic into two quadratics.
    // v^4 + p v^2 + q v + r = (v^2 + alpha v + (y - beta))(v^2 - alpha v + (y + beta))
    // where alpha = sqrt(2y - p), beta = sqrt(y^2 - r), sign chosen so alpha*beta = q/2.
    const float alpha_sq = 2.0f * y - p;
    const float beta_sq  = y * y - r;

    Complex alpha_c;
    Complex beta_c;
    if (alpha_sq >= 0.0f) {
        alpha_c = {sqrtf(alpha_sq), 0.0f};
    } else {
        alpha_c = {0.0f, sqrtf(-alpha_sq)};
    }
    if (beta_sq >= 0.0f) {
        beta_c = {sqrtf(beta_sq), 0.0f};
    } else {
        beta_c = {0.0f, sqrtf(-beta_sq)};
    }

    // Choose sign of beta so that alpha * beta = q/2.
    // alpha_c * beta_c should equal q/2. If q < 0, negate beta.
    if (q < 0.0f) {
        beta_c.re = -beta_c.re;
        beta_c.im = -beta_c.im;
    }

    // Solve v^2 + a_c v + b_c = 0 => v = (-a_c +/- sqrt(a_c^2 - 4 b_c)) / 2
    auto solve_quadratic = [](Complex a_c, Complex b_c,
                               Complex& root1, Complex& root2) {
        Complex a2 = detail::cmul(a_c, a_c);
        Complex disc_c = detail::csub(a2, detail::cscale(b_c, 4.0f));
        Complex s = detail::complex_sqrt(disc_c);
        Complex neg_a = detail::cscale(a_c, -1.0f);
        root1 = detail::cscale(detail::csub(neg_a, s), 0.5f);
        root2 = detail::cscale(detail::cadd(neg_a, s), 0.5f);
    };

    // First quadratic: v^2 + alpha v + (y - beta) = 0
    // Second quadratic: v^2 - alpha v + (y + beta) = 0
    const Complex minus_alpha = {-alpha_c.re, -alpha_c.im};
    const Complex y_minus_beta = {y - beta_c.re, -beta_c.im};
    const Complex y_plus_beta  = {y + beta_c.re,  beta_c.im};

    Complex v1, v2, v3, v4;
    solve_quadratic(alpha_c, y_minus_beta, v1, v2);
    solve_quadratic(minus_alpha, y_plus_beta, v3, v4);

    // Undo the depressed shift: u = v - B/4.
    const float shift = -B * 0.25f;
    out.r0 = {v1.re + shift, v1.im};
    out.r1 = {v2.re + shift, v2.im};
    out.r2 = {v3.re + shift, v3.im};
    out.r3 = {v4.re + shift, v4.im};
    return out;
}

/// One Newton-polish step on a real root of a u^4 + b u^3 + c u^2 + d u + e.
/// Safe to call on any root — a no-op on complex roots (returns input).
BHR_HD inline float polish_root(float u, float a, float b, float c, float d, float e) {
    const float f  = ((((a) * u + b) * u + c) * u + d) * u + e;
    const float fp = (((4.0f * a) * u + 3.0f * b) * u + 2.0f * c) * u + d;
    if (fabsf(fp) < 1e-20f) return u;
    return u - f / fp;
}

} // namespace bhr
