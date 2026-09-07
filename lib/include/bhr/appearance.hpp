#pragma once
#include "bhr/shot.hpp"

namespace bhr {
struct BloomV1 {
    const bool enabled = false;
    const float strength = .06f, threshold = 1.0f;
};
/// Immutable appearance v1: linear sRGB/D65 relative emission, Reinhard, sRGB output.
struct AppearanceV1 {
    const float temperature_scale = .15f;
    const float disk_detail = .18f;
    const float outer_fade_fraction = .1f;
    const float star_intensity = .15f;
    const float exposure_ev = 1.0f;
    const BloomV1 bloom{};
};
struct CinematicRenderDocument { const CinematicDocument scene_shots; const AppearanceV1 appearance; };
struct CinematicRequest { const RenderParams params; const AppearanceV1 appearance; };
using RenderDocument = std::variant<CinematicDocument, CinematicRenderDocument>;
inline constexpr size_t kCinematicResourceBudget = 256 * 1024 * 1024;
struct CinematicSizes {
    const int width, height, bloom_width, bloom_height;
    const size_t pixels, radiance_bytes, rgba_bytes, status_bytes, bloom_bytes, budget_bytes;
};
void validate_appearance(const AppearanceV1& appearance);
CinematicSizes cinematic_sizes(int width, int height);
void validate_cinematic_request(const CinematicRequest& request);
CinematicRenderDocument upgrade_cinematic(const CinematicDocument&, const AppearanceV1&);
CinematicRenderDocument parse_cinematic_v2(const std::string&);
std::string serialize_cinematic_v2(const CinematicRenderDocument&);
CinematicRenderDocument load_cinematic_v2(const std::filesystem::path&);
void save_cinematic_v2(const CinematicRenderDocument&, const std::filesystem::path&);
RenderDocument parse_render_document(const std::string&);
RenderDocument load_render_document(const std::filesystem::path&);
} // namespace bhr
