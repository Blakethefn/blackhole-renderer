#pragma once

#include "bhr/kerr.hpp"
#include <cstdint>

namespace bhr {

/// Versioned, bounded artistic disk activity. It changes emissivity only; it does
/// not alter geodesics, temperature, opacity or frequency-shift calculations.
struct EmissionV1 {
    bool enabled = false;
    uint32_t seed = 7;
    float amplitude = .18f;
    float radial_modulation = .06f;
    float flow_strength = 1.0f;
};

/// Exact normalized phase supplied by a selected shot. The valid range is [0, 1).
struct EmissionPhase {
    uint64_t numerator = 0;
    uint64_t denominator = 1;
};

void validate_emission(const EmissionV1& emission);
void validate_emission_phase(const EmissionPhase& phase);

BHR_HD inline float emission_phase_value(const EmissionPhase& phase) {
    return phase.denominator == 0
        ? 0.0f
        : static_cast<float>(phase.numerator) / static_cast<float>(phase.denominator);
}

BHR_HD inline uint32_t emission_hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

/// Pure periodic emissivity multiplier sampled at the actual disk hit.
/// Phase 0 and phase 1 have matching values and first derivatives, so a loop
/// can wrap without resetting a non-periodic flow clock.
BHR_HD inline float animated_disk_emission(float r, float phi, float inner, float outer,
                                           const EmissionV1& emission, float phase) {
    if (!emission.enabled) return 1.0f;
    constexpr float tau = 6.2831853071795864769f;
    const float wrapped_phase = phase - floorf(phase);
    const float u = fminf(1.0f, fmaxf(0.0f, (r - inner) / (outer - inner)));
    const float v = phi / tau;
    const float seed_phase = tau * static_cast<float>(emission_hash(emission.seed) % 10000u) / 10000.0f;
    const float flow_profile = .20f + .75f * sqrtf(fmaxf(0.0f, 1.0f - u));
    const float periodic_displacement = emission.flow_strength * flow_profile
        * sinf(tau * wrapped_phase) / tau;
    const float advected_v = v - periodic_displacement;
    const float lanes = .65f * cosf(tau * (5.0f * u + 2.0f * advected_v) + seed_phase)
                      + .35f * sinf(tau * (11.0f * u - 3.0f * advected_v) - seed_phase);
    const float radial_weight = .35f + .65f * (1.0f - u);
    const float breathing = radial_weight * sinf(tau * (3.0f * u + wrapped_phase) + seed_phase);
    return fminf(1.75f, fmaxf(.25f,
        1.0f + emission.amplitude * lanes + emission.radial_modulation * breathing));
}

} // namespace bhr
