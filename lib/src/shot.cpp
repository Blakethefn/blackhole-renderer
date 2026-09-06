#include "bhr/shot.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace bhr {
namespace {
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}
void validate_params(const RenderParams& params) {
    if (const char* error = validation_error(params)) throw std::runtime_error(error);
}
RenderParams with_camera(const RenderParams& p, const Shot& shot, const BLPose& pose) {
    return {p.spin, {pose.r_cam, pose.theta_cam_deg, pose.phi_cam_deg, p.camera.fov_deg,
                    shot.width, shot.height}, p.disk, p.enable_doppler, p.enable_redshift,
            p.enable_beaming, p.enable_starfield, p.integrator};
}
BLPose pose_of(const CameraParams& camera) {
    return {camera.r_cam, camera.theta_cam_deg, camera.phi_cam_deg};
}
void validate_scene(const Scene& scene) {
    validate_params(scene.preset);
    require(!scene.preset.enable_starfield || scene.starfield_exr.has_value(),
            "Enabled starfield requires scene.starfield_exr");
    if (!scene.starfield_exr) return;
    const auto& ref = *scene.starfield_exr;
    require(!ref.empty() && ref.size() <= kMaxAssetReferenceBytes,
            "starfield_exr must contain 1..1024 bytes");
    require(std::none_of(ref.begin(), ref.end(), [](unsigned char c) {
        return c < 32 || c == 127 || c == '\\' || c == ':';
    }), "starfield_exr must be a portable relative EXR path without control characters");
    const std::filesystem::path path(ref);
    require(!path.is_absolute() && path.extension() == ".exr",
            "starfield_exr must be a relative .exr path");
    for (const auto& component : path) {
        require(component != "..", "starfield_exr cannot contain parent traversal (..)");
    }
}
bool valid_id(const std::string& id) {
    return !id.empty() && id.size() <= kMaxShotIdBytes
        && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}
uint64_t checked_product(uint64_t a, uint64_t b) {
    require(b == 0 || a <= std::numeric_limits<uint64_t>::max() / b, "Shot time arithmetic overflow");
    return a * b;
}
void validate_shot(const Scene& scene, const Shot& shot) {
    require(valid_id(shot.id), "Shot ID must be 1..64 ASCII letters, digits, _ or -");
    require(shot.frame_count > 0 && shot.frame_count <= kMaxShotFrames,
            "frame_count must be between 1 and 1000000");
    require(shot.fps.numerator > 0 && shot.fps.denominator > 0, "FPS terms must be positive 32-bit integers");
    require(shot.fps.numerator <= uint64_t{kMaxShotFPS} * shot.fps.denominator, "Effective FPS must be at most 240");
    (void)checked_product(shot.frame_count, shot.fps.denominator);
    require(!shot.loop_frames || (*shot.loop_frames > 0 && *shot.loop_frames <= kMaxShotFrames),
            "loop_frames must be between 1 and 1000000");
    validate_params(with_camera(scene.preset, shot, pose_of(scene.preset.camera)));
    if (const auto* orbit = std::get_if<OrbitCamera>(&shot.camera)) {
        require(shot.frame_count >= 2, "Orbit shots require at least two frames");
        // Each allowed component interval is convex for fixed scene spin. Smoothstep
        // never overshoots, so valid endpoints establish validity of the entire path.
        validate_params(with_camera(scene.preset, shot, orbit->start));
        validate_params(with_camera(scene.preset, shot, orbit->end));
    }
}
float interpolate(float start, float end, double progress) {
    // Double intermediates avoid overflow of end-start for finite float endpoints.
    return static_cast<float>((1.0 - progress) * double(start) + progress * double(end));
}
BLPose evaluated_pose(const Shot& shot, const CameraParams& camera, uint64_t index) {
    const auto* orbit = std::get_if<OrbitCamera>(&shot.camera);
    if (!orbit) return pose_of(camera);
    if (index == 0) return orbit->start;
    if (index == shot.frame_count - 1) return orbit->end;
    const double u = static_cast<double>(index) / static_cast<double>(shot.frame_count - 1);
    const double s = u * u * (3.0 - 2.0 * u);
    return {interpolate(orbit->start.r_cam, orbit->end.r_cam, s),
            interpolate(orbit->start.theta_cam_deg, orbit->end.theta_cam_deg, s),
            interpolate(orbit->start.phi_cam_deg, orbit->end.phi_cam_deg, s)};
}
} // namespace

void validate_cinematic(const CinematicDocument& document) {
    validate_scene(document.scene);
    require(!document.shots.empty() && document.shots.size() <= kMaxShots, "Document must contain 1..64 shots");
    std::set<std::string> ids;
    for (const auto& shot : document.shots) {
        validate_shot(document.scene, shot);
        require(ids.insert(shot.id).second, "Duplicate shot ID: " + shot.id);
    }
}
FrameSample evaluate_frame(const CinematicDocument& document, const std::string& shot_id, uint64_t index) {
    validate_cinematic(document);
    const auto found = std::find_if(document.shots.begin(), document.shots.end(),
                                   [&](const Shot& shot) { return shot.id == shot_id; });
    require(found != document.shots.end(), "Unknown shot ID: " + shot_id);
    const auto& shot = *found;
    require(index < shot.frame_count, "Frame index outside [0, frame_count) for shot " + shot_id);
    const auto params = with_camera(document.scene.preset, shot, evaluated_pose(shot, document.scene.preset.camera, index));
    validate_params(params);
    const std::optional<FrameTime> phase = shot.loop_frames
        ? std::optional<FrameTime>{{index % *shot.loop_frames, *shot.loop_frames}} : std::nullopt;
    return {shot.id, index, {checked_product(index, shot.fps.denominator), shot.fps.numerator},
            {checked_product(shot.frame_count, shot.fps.denominator), shot.fps.numerator}, phase, params};
}
CinematicDocument import_preset(const RenderParams& preset, std::optional<std::string> starfield_exr) {
    const CinematicDocument document{{preset, starfield_exr},
        {{"fixed", 180, {60, 1}, preset.camera.width, preset.camera.height, 180, FixedCamera{}}}};
    validate_cinematic(document);
    return document;
}
std::string resolve_starfield(const Scene& scene, const std::filesystem::path& document_path) {
    validate_scene(scene);
    if (!scene.preset.enable_starfield) return {};
    require(!document_path.empty() && document_path.string().find('\0') == std::string::npos,
            "Document path must be nonempty without NUL characters");
    const auto resolved = (std::filesystem::absolute(document_path).parent_path() / *scene.starfield_exr).lexically_normal();
    require(std::filesystem::is_regular_file(resolved), "Required starfield is missing or not a regular file: " + resolved.string());
    return resolved.string();
}
} // namespace bhr
