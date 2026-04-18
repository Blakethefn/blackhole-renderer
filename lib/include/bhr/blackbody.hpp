#pragma once
/// @file bhr/blackbody.hpp
/// Fast blackbody temperature → sRGB approximation (Tanner Helland).
/// Accurate to ~5% for 1000-40000 K, sufficient for visual rendering.
///
/// Returns normalized linear-space RGB in [0, 1]. Intensity (Stefan-Boltzmann)
/// and relativistic beaming (g^4) are applied externally by the caller.

#include "bhr/kerr.hpp"  // for BHR_HD macro
#include <cmath>

namespace bhr {

/// @return linear-space RGB (each in [0, ~1]) for a blackbody at @p T_kelvin.
/// Temperatures outside 1000..40000 K are clamped.
BHR_HD inline void blackbody_rgb(float T_kelvin, float& r, float& g, float& b) {
    const float T = fmaxf(1000.0f, fminf(40000.0f, T_kelvin));
    const float t = T / 100.0f;

    // Red
    if (t <= 66.0f) {
        r = 1.0f;
    } else {
        const float x = t - 60.0f;
        r = 329.698727446f * powf(x, -0.1332047592f);
        r = fmaxf(0.0f, fminf(1.0f, r / 255.0f));
    }

    // Green
    if (t <= 66.0f) {
        g = 99.4708025861f * logf(t) - 161.1195681661f;
    } else {
        const float x = t - 60.0f;
        g = 288.1221695283f * powf(x, -0.0755148492f);
    }
    g = fmaxf(0.0f, fminf(1.0f, g / 255.0f));

    // Blue
    if (t >= 66.0f) {
        b = 1.0f;
    } else if (t <= 19.0f) {
        b = 0.0f;
    } else {
        b = 138.5177312231f * logf(t - 10.0f) - 305.0447927307f;
        b = fmaxf(0.0f, fminf(1.0f, b / 255.0f));
    }
}

} // namespace bhr
