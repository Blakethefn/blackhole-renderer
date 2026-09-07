#pragma once
#include "bhr/kerr.hpp"
#include <cstdint>

namespace bhr {
inline constexpr float kMaxRadiance = 65504.0f;
/// Float32 linear sRGB/D65 relative radiance. Packed rows, top left, opaque alpha.
struct alignas(16) RadiancePixel { float r, g, b, a; };
static_assert(sizeof(RadiancePixel) == 16);
enum class PixelStatus : uint8_t { valid, unknown, invalid, clipped };
struct FrameDiagnostics { uint32_t unknown = 0, invalid = 0, clipped = 0; };
BHR_HD inline bool valid_radiance(const RadiancePixel& p) {
    return std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b)
        && p.r >= 0 && p.g >= 0 && p.b >= 0 && p.r <= kMaxRadiance
        && p.g <= kMaxRadiance && p.b <= kMaxRadiance && p.a == 1;
}
// Defined on nonnegative finite values; boundary validation precedes dispatch.
BHR_HD inline float srgb_decode(float x) {
    return x <= .04045f ? x / 12.92f : powf((x + .055f) / 1.055f, 2.4f);
}
BHR_HD inline float srgb_encode(float x) {
    return x <= .0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f/2.4f) - .055f;
}
BHR_HD inline uint8_t display_exposed_channel(float x) {
    const float mapped = x / (1.0f + x);
    return static_cast<uint8_t>(floorf(fminf(1.0f, fmaxf(0.0f, srgb_encode(mapped))) * 255.0f + .5f));
}
BHR_HD inline uint8_t display_channel(float x, float ev) {
    return display_exposed_channel(x * exp2f(ev));
}
} // namespace bhr
