#include "bhr/emission.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace bhr {
namespace {
void range(float value, float minimum, float maximum, const char* name) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error(std::string(name) + " is outside its finite supported range");
}
}

void validate_emission(const EmissionV1& emission) {
    range(emission.amplitude, 0.0f, .25f, "emission.amplitude");
    range(emission.radial_modulation, 0.0f, .12f, "emission.radial_modulation");
    range(emission.flow_strength, 0.0f, 1.0f, "emission.flow_strength");
}

void validate_emission_phase(const EmissionPhase& phase) {
    if (phase.denominator == 0 || phase.numerator >= phase.denominator)
        throw std::runtime_error("Emission phase must be an exact fraction in [0, 1)");
}
} // namespace bhr
