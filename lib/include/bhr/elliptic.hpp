#pragma once
/// @file bhr/elliptic.hpp
/// Carlson symmetric elliptic integrals R_F, R_D, R_J.
/// Reference: Carlson 1995, Numer. Algorithms 10, 13.
/// Algorithm: iterative duplication until the arguments are within tolerance,
///            then 5th-order Taylor series in the deviation from the mean.
///
/// All arguments are float. Accuracy is ~1e-5 relative, which is ample for
/// visual rendering — geokerr's final error is dominated by the Ferrari
/// root solver, not by Carlson.

#include "bhr/kerr.hpp"   // for BHR_HD macro
#include <cmath>

namespace bhr {

namespace detail {

// Duplication stop tolerance. Smaller = more iterations, higher accuracy.
constexpr float kCarlsonTol = 1e-4f;

}  // namespace detail

// Forward-declaration — R_J below needs R_C which we compose from R_F.
BHR_HD inline float carlson_RF(float x, float y, float z);

/// R_C(x, y) = R_F(x, y, y). Used internally by R_J.
BHR_HD inline float carlson_RC(float x, float y) {
    return carlson_RF(x, y, y);
}

/// R_F(x, y, z) = ½ ∫₀^∞ dt / √((t+x)(t+y)(t+z)).
/// x, y, z must be non-negative; at most one may be zero.
BHR_HD inline float carlson_RF(float x, float y, float z) {
    // Ensure arguments are non-negative.
    x = fmaxf(x, 0.0f);
    y = fmaxf(y, 0.0f);
    z = fmaxf(z, 0.0f);
    for (int i = 0; i < 50; ++i) {
        const float sx = sqrtf(x), sy = sqrtf(y), sz = sqrtf(z);
        const float lam = sx * sy + sx * sz + sy * sz;
        x = 0.25f * (x + lam);
        y = 0.25f * (y + lam);
        z = 0.25f * (z + lam);
        const float mu = (x + y + z) / 3.0f;
        const float dx = 1.0f - x / mu;
        const float dy = 1.0f - y / mu;
        const float dz = 1.0f - z / mu;
        const float m = fmaxf(fabsf(dx), fmaxf(fabsf(dy), fabsf(dz)));
        if (m < detail::kCarlsonTol) {
            // Use the standard form: E2 = dx dy + dy dz + dz dx, E3 = dx dy dz.
            const float E2 = dx * dy + dy * dz + dz * dx;
            const float E3 = dx * dy * dz;
            const float series = 1.0f
                               - (1.0f / 10.0f) * E2
                               + (1.0f / 14.0f) * E3
                               + (1.0f / 24.0f) * E2 * E2
                               - (3.0f / 44.0f) * E2 * E3;
            return series / sqrtf(mu);
        }
    }
    // Fallback (should not hit with reasonable inputs): return best estimate.
    return 1.0f / sqrtf((x + y + z) / 3.0f);
}

/// R_D(x, y, z) = (3/2) ∫₀^∞ dt / [(t+z) · √((t+x)(t+y)(t+z))].
/// Symmetric in (x, y) but not z.
BHR_HD inline float carlson_RD(float x, float y, float z) {
    x = fmaxf(x, 0.0f);
    y = fmaxf(y, 0.0f);
    z = fmaxf(z, 0.0f);
    float sum = 0.0f;
    float fac = 1.0f;
    for (int i = 0; i < 50; ++i) {
        const float sx = sqrtf(x), sy = sqrtf(y), sz = sqrtf(z);
        const float lam = sx * sy + sx * sz + sy * sz;
        sum += fac / (sz * (z + lam));
        fac *= 0.25f;
        x = 0.25f * (x + lam);
        y = 0.25f * (y + lam);
        z = 0.25f * (z + lam);
        const float mu = (x + y + 3.0f * z) / 5.0f;
        const float dx = 1.0f - x / mu;
        const float dy = 1.0f - y / mu;
        const float dz = 1.0f - z / mu;
        const float m = fmaxf(fabsf(dx), fmaxf(fabsf(dy), fabsf(dz)));
        if (m < detail::kCarlsonTol) {
            const float EA = dx * dy;
            const float EB = dz * dz;
            const float EC = EA - EB;
            const float ED = EA - 6.0f * EB;
            const float EE = ED + EC + EC;
            const float series = 1.0f
                               + ED * (-(3.0f/14.0f) + 0.25f * ED - (9.0f/22.0f) * dz * EE)
                               + dz * ((1.0f/6.0f) * EE + dz * (-(3.0f/26.0f) * EC + dz * (3.0f/44.0f) * EA));
            return 3.0f * sum + fac * series / (mu * sqrtf(mu));
        }
    }
    return 0.0f;
}

/// R_J(x, y, z, p) = (3/2) ∫₀^∞ dt / [(t+p) · √((t+x)(t+y)(t+z))].
/// p > 0 (Cauchy principal value handled elsewhere if needed).
BHR_HD inline float carlson_RJ(float x, float y, float z, float p) {
    x = fmaxf(x, 0.0f);
    y = fmaxf(y, 0.0f);
    z = fmaxf(z, 0.0f);
    p = fmaxf(p, 1e-20f);
    float sum = 0.0f;
    float fac = 1.0f;
    for (int i = 0; i < 50; ++i) {
        const float sx = sqrtf(x), sy = sqrtf(y), sz = sqrtf(z);
        const float lam = sx * sy + sx * sz + sy * sz;
        const float alpha = p * (sx + sy + sz) + sx * sy * sz;
        const float beta  = sqrtf(p) * (p + lam);
        sum += fac * carlson_RC(alpha * alpha, beta * beta);
        fac *= 0.25f;
        x = 0.25f * (x + lam);
        y = 0.25f * (y + lam);
        z = 0.25f * (z + lam);
        p = 0.25f * (p + lam);
        const float mu = (x + y + z + 2.0f * p) / 5.0f;
        const float dx = 1.0f - x / mu;
        const float dy = 1.0f - y / mu;
        const float dz = 1.0f - z / mu;
        const float dp = 1.0f - p / mu;
        const float m = fmaxf(fabsf(dx),
                         fmaxf(fabsf(dy),
                         fmaxf(fabsf(dz), fabsf(dp))));
        if (m < detail::kCarlsonTol) {
            const float EA = dx * dy + dy * dz + dz * dx;
            const float EB = dx * dy * dz;
            const float EC = dp * dp;
            const float ED = EA - 3.0f * EC;
            const float EE = EB + 2.0f * dp * (EA - EC);
            const float series = 1.0f
                               + ED * (-(3.0f/14.0f) + 0.25f * ED - (9.0f/22.0f) * EE)
                               + EB * ((1.0f/6.0f) + dp * (-(3.0f/22.0f) + dp * (3.0f/26.0f)))
                               - dp * EA * ((3.0f/22.0f) - dp * (3.0f/26.0f))
                               - (3.0f/26.0f) * EC * EA;
            return 3.0f * sum + fac * series / (mu * sqrtf(mu));
        }
    }
    return 0.0f;
}

} // namespace bhr
