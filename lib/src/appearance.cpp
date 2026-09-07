#include "bhr/appearance.hpp"
#include <stdexcept>

namespace bhr {
namespace {
void range(float x, float minimum, float maximum, const char* name) {
    if (!std::isfinite(x) || x < minimum || x > maximum)
        throw std::runtime_error(std::string(name) + " is outside its finite supported range");
}
}
void validate_appearance(const AppearanceV1& a) {
    range(a.temperature_scale,.01f,4,"temperature_scale");
    range(a.disk_detail,0,.3f,"disk_detail");
    range(a.outer_fade_fraction,0,.25f,"outer_fade_fraction");
    range(a.star_intensity,0,16,"star_intensity");
    range(a.exposure_ev,-16,16,"exposure_ev");
    range(a.bloom.strength,0,.15f,"bloom.strength");
    range(a.bloom.threshold,.1f,16,"bloom.threshold");
}
CinematicSizes cinematic_sizes(int w, int h) {
    if (w <= 0 || h <= 0 || w > kMaxRenderDimension || h > kMaxRenderDimension)
        throw std::runtime_error("Cinematic dimensions must be in 1..16384");
    const size_t n = static_cast<size_t>(w)*h;
    const int bw=(w+3)/4, bh=(h+3)/4;
    const size_t bloom=static_cast<size_t>(bw)*bh*16;
    // Radiance/status, PBO plus three textures during resize, two bloom buffers,
    // and the capped 2048x1024-equivalent sky allocation. Checked before allocation.
    const size_t budget=n*33 + bloom*2 + 32*1024*1024;
    if (budget > kCinematicResourceBudget)
        throw std::runtime_error("Cinematic image resources exceed the 256 MiB budget");
    return {w,h,bw,bh,n,n*16,n*4,n,bloom,budget};
}
void validate_cinematic_request(const CinematicRequest& r) {
    if (const char* error=validation_error(r.params)) throw std::runtime_error(error);
    validate_appearance(r.appearance);
    (void)cinematic_sizes(r.params.camera.width,r.params.camera.height);
}
CinematicRenderDocument upgrade_cinematic(const CinematicDocument& doc, const AppearanceV1& a) {
    validate_cinematic(doc);
    validate_appearance(a);
    for (const auto& shot:doc.shots) (void)cinematic_sizes(shot.width,shot.height);
    return {doc,a};
}
} // namespace bhr
