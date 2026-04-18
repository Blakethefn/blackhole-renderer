#pragma once
/// @file bhr/blackbody.hpp
/// Blackbody (Planck) spectrum to approximate sRGB color mapping.
///
/// Converts a blackbody temperature in Kelvin to linear-light RGB
/// using a fast piecewise polynomial fit to the CIE 1931 color matching
/// functions integrated against Planck's law.  Output channels are
/// un-normalized radiance proportional values suitable for tone mapping.
///
/// Reference: "Blackbody Radiation" approximation by Charity (2001) and
/// Tanner Helland's blog post on planckian locus approximation.

#include "bhr/kerr.hpp"   // for BHR_HD
#include <cmath>

namespace bhr {

/// Convert blackbody temperature T (Kelvin) to linear-light RGB.
/// Output values are in [0, +inf) representing relative spectral radiance.
/// Caller is responsible for tone mapping.
BHR_HD inline void blackbody_rgb(float T, float& r, float& g, float& b) {
    // Clamp temperature to a sensible range
    const float Tc = fmaxf(800.0f, fminf(T, 4.0e7f));

    // --- Red channel ---
    if (Tc <= 6600.0f) {
        r = 1.0f;
    } else {
        // T in hundreds of Kelvin, offset by 60
        float t = Tc / 100.0f - 60.0f;
        r = 329.698727446f * powf(t, -0.1332047592f) / 255.0f;
        r = fmaxf(0.0f, fminf(1.0f, r));
    }

    // --- Green channel ---
    if (Tc <= 6600.0f) {
        float t = Tc / 100.0f;
        g = (99.4708025861f * logf(t) - 161.1195681661f) / 255.0f;
        g = fmaxf(0.0f, fminf(1.0f, g));
    } else {
        float t = Tc / 100.0f - 60.0f;
        g = 288.1221695283f * powf(t, -0.0755148492f) / 255.0f;
        g = fmaxf(0.0f, fminf(1.0f, g));
    }

    // --- Blue channel ---
    if (Tc >= 6600.0f) {
        b = 1.0f;
    } else if (Tc <= 1900.0f) {
        b = 0.0f;
    } else {
        float t = Tc / 100.0f - 10.0f;
        b = (138.5177312231f * logf(t) - 305.0447927307f) / 255.0f;
        b = fmaxf(0.0f, fminf(1.0f, b));
    }

    // Scale by Stefan-Boltzmann T^4 for radiance magnitude (relative).
    // Normalize against a reference temperature of 6500 K so that
    // a 6500 K source has unit intensity.  This keeps tone-map exposure
    // tuning intuitive.
    const float T_ref = 6500.0f;
    const float t_norm = Tc / T_ref;
    const float intensity = t_norm * t_norm * t_norm * t_norm;

    r *= intensity;
    g *= intensity;
    b *= intensity;
}

} // namespace bhr
