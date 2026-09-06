#pragma once
/// CPU-only cinematic schema v1. Immutable snapshots; operations return new values
/// or throw std::exception derivatives with an actionable reason. No GPU ownership.
#include "bhr/params.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bhr {
inline constexpr uint64_t kMaxShotFrames = 1000000;
inline constexpr size_t kMaxShots = 64;
inline constexpr size_t kMaxCinematicBytes = 1024 * 1024;
inline constexpr size_t kMaxShotIdBytes = 64;
inline constexpr size_t kMaxAssetReferenceBytes = 1024;
inline constexpr uint32_t kMaxShotFPS = 240;

struct FrameRate { const uint32_t numerator = 60, denominator = 1; };
/// Exact, unreduced rational. Only the explicit seconds() view rounds to double.
struct FrameTime {
    const uint64_t numerator, denominator;
    double seconds() const { return static_cast<double>(numerator) / static_cast<double>(denominator); }
};
struct BLPose { const float r_cam, theta_cam_deg, phi_cam_deg; };
struct FixedCamera {};
struct OrbitCamera { const BLPose start, end; };
struct Scene {
    const RenderParams preset;
    const std::optional<std::string> starfield_exr = std::nullopt;
};
struct Shot {
    const std::string id;
    const uint64_t frame_count;
    const FrameRate fps;
    const int width, height;
    const std::optional<uint64_t> loop_frames;
    const std::variant<FixedCamera, OrbitCamera> camera;
};
struct CinematicDocument { const Scene scene; const std::vector<Shot> shots; };
struct FrameSample {
    const std::string shot_id;
    const uint64_t frame_index;
    const FrameTime time, duration;
    const std::optional<FrameTime> loop_phase;
    const RenderParams params;
};

void validate_cinematic(const CinematicDocument& document);
FrameSample evaluate_frame(const CinematicDocument& document, const std::string& shot_id, uint64_t index);
/// Creates a 180-frame, 60/1 fixed shot using all original preset fields/dimensions.
/// An enabled starfield requires an explicit portable relative EXR reference.
CinematicDocument import_preset(const RenderParams& preset,
                               std::optional<std::string> starfield_exr = std::nullopt);
CinematicDocument parse_cinematic(const std::string& json);
std::string serialize_cinematic(const CinematicDocument& document);
CinematicDocument load_cinematic(const std::filesystem::path& path);
/// Atomic replacement on supported Linux/POSIX filesystems. Failure retains the
/// old destination; no crash/power-loss durability guarantee (no directory fsync).
void save_cinematic(const CinematicDocument& document, const std::filesystem::path& path);
/// Returns absolute path (or empty if sampling disabled); verifies a regular file.
/// EXR decoding/format validation remains the renderer's existing loader's job.
std::string resolve_starfield(const Scene& scene, const std::filesystem::path& document_path);
} // namespace bhr
