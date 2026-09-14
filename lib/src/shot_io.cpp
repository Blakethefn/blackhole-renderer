#include "bhr/shot.hpp"
#include "bhr/appearance.hpp"
#include "preset_json.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace bhr {
namespace {
using Json = nlohmann::json;
constexpr int kCinematicVersion = 1;
constexpr int kMaxNestingDepth = 6;
void fields(const Json& value, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional = {}) {
    if (!value.is_object()) throw std::runtime_error("Expected a JSON object");
    const std::set<std::string> allowed = [&] {
        std::set<std::string> names(required.begin(), required.end());
        names.insert(optional.begin(), optional.end());
        return names;
    }();
    for (const char* name : required) {
        if (!value.contains(name)) throw std::runtime_error(std::string("Missing field: ") + name);
    }
    for (const auto& entry : value.items()) {
        if (!allowed.count(entry.key())) throw std::runtime_error("Unknown field: " + entry.key());
    }
}
uint64_t integer(const Json& value, uint64_t maximum, const char* name) {
    if (!value.is_number_integer()
        || (!value.is_number_unsigned() && value.get<int64_t>() < 0)) {
        throw std::runtime_error(std::string(name) + " must be a nonnegative integer");
    }
    const auto result = value.get<uint64_t>();
    if (result > maximum) throw std::runtime_error(std::string(name) + " exceeds its supported range");
    return result;
}
std::string string(const Json& value, const char* name) {
    if (!value.is_string()) throw std::runtime_error(std::string(name) + " must be a string");
    return value.get<std::string>();
}
float number(const Json& value) {
    if (!value.is_number()) throw std::runtime_error("Camera pose components must be numbers");
    const double result = value.get<double>();
    if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max()
        || (result != 0 && std::abs(result) < std::numeric_limits<float>::min())) {
        throw std::runtime_error("Camera pose component must be a finite float");
    }
    return static_cast<float>(result);
}
BLPose pose(const Json& value) {
    fields(value, {"r_cam", "theta_cam_deg", "phi_cam_deg"});
    return {number(value.at("r_cam")), number(value.at("theta_cam_deg")), number(value.at("phi_cam_deg"))};
}
std::variant<FixedCamera, OrbitCamera> camera(const Json& value) {
    if (!value.is_object() || !value.contains("kind")) throw std::runtime_error("Camera requires kind");
    const auto kind = string(value.at("kind"), "camera.kind");
    if (kind == "fixed") {
        fields(value, {"kind"});
        return FixedCamera{};
    }
    if (kind == "orbit") {
        fields(value, {"kind", "start", "end"});
        return OrbitCamera{pose(value.at("start")), pose(value.at("end"))};
    }
    throw std::runtime_error("Camera kind must be fixed or orbit");
}
Shot shot(const Json& value) {
    fields(value, {"id", "frame_count", "fps", "width", "height", "camera"}, {"loop_frames"});
    const auto& fps = value.at("fps");
    fields(fps, {"numerator", "denominator"});
    const std::optional<uint64_t> loop = value.contains("loop_frames")
        ? std::optional<uint64_t>{integer(value.at("loop_frames"), kMaxShotFrames, "loop_frames")} : std::nullopt;
    return {string(value.at("id"), "shot.id"), integer(value.at("frame_count"), kMaxShotFrames, "frame_count"),
        {static_cast<uint32_t>(integer(fps.at("numerator"), UINT32_MAX, "fps.numerator")),
         static_cast<uint32_t>(integer(fps.at("denominator"), UINT32_MAX, "fps.denominator"))},
        static_cast<int>(integer(value.at("width"), kMaxRenderDimension, "width")),
        static_cast<int>(integer(value.at("height"), kMaxRenderDimension, "height")), loop, camera(value.at("camera"))};
}
Json pose_json(const BLPose& pose) {
    return {{"r_cam", pose.r_cam}, {"theta_cam_deg", pose.theta_cam_deg}, {"phi_cam_deg", pose.phi_cam_deg}};
}
Json camera_json(const Shot& shot) {
    if (const auto* orbit = std::get_if<OrbitCamera>(&shot.camera)) {
        return {{"kind", "orbit"}, {"start", pose_json(orbit->start)}, {"end", pose_json(orbit->end)}};
    }
    return {{"kind", "fixed"}};
}
void check_path(const std::filesystem::path& path) {
    if (path.empty() || path.string().find('\0') != std::string::npos)
        throw std::runtime_error("Document path must be nonempty without NUL characters");
}
std::runtime_error io_error(const std::string& operation, const std::filesystem::path& path) {
    return std::runtime_error(operation + ": " + path.string() + ": " + std::strerror(errno));
}
// Temporary-file resource ownership is local. Shared scene/shot snapshots never mutate.
class TemporaryFile {
public:
    explicit TemporaryFile(const std::filesystem::path& destination) {
        std::string pattern = destination.string() + ".tmp-XXXXXX";
        const int descriptor = mkstemp(pattern.data()); // exclusive, same directory/filesystem
        if (descriptor < 0) throw io_error("Cannot create temporary document", destination);
        path_ = pattern;
        stream_ = fdopen(descriptor, "wb");
        if (!stream_) {
            const auto error = io_error("Cannot open temporary document stream", path_);
            if (::close(descriptor) != 0) std::perror("Temporary document close failed");
            cleanup();
            throw error;
        }
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile() {
        if (stream_ && std::fclose(stream_) != 0) std::perror("Temporary document close failed");
        cleanup();
    }
    void replace(const std::string& contents, const std::filesystem::path& destination) {
        if (std::fwrite(contents.data(), 1, contents.size(), stream_) != contents.size()
            || std::fflush(stream_) != 0) throw io_error("Cannot write document", path_);
        FILE* const closing = stream_;
        stream_ = nullptr;
        if (std::fclose(closing) != 0) throw io_error("Cannot close document", path_);
        if (std::rename(path_.c_str(), destination.c_str()) != 0)
            throw io_error("Cannot replace document", destination);
        path_.clear();
    }
private:
    void cleanup() const {
        if (!path_.empty() && std::remove(path_.c_str()) != 0)
            std::perror("Temporary document cleanup failed");
    }
    std::string path_;
    FILE* stream_ = nullptr;
};
Json parse_root(const std::string& contents) {
    if (contents.size() > kMaxCinematicBytes) throw std::runtime_error("Cinematic JSON must be at most 1 MiB");
    // The JSON lexer treats a raw NUL as EOF; strict documents must not accept
    // an otherwise valid prefix followed by NUL and unparsed trailing bytes.
    if (contents.find('\0') != std::string::npos) throw std::runtime_error("Cinematic JSON cannot contain raw NUL bytes");
    std::vector<std::set<std::string>> keys;
    const auto structure = [&keys](int depth, Json::parse_event_t event, Json& value) {
        if (depth > kMaxNestingDepth) throw std::runtime_error("Cinematic JSON nesting exceeds limit 6");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate cinematic field: " + value.get<std::string>());
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    return Json::parse(contents, structure);
}
CinematicDocument document_from_json(const Json& json, int version) {
    if (version == 1) fields(json, {"format", "schema_version", "scene", "shots"});
    else if (version == 2) fields(json, {"format", "schema_version", "scene", "shots", "appearance"});
    else if (version == 3) fields(json, {"format", "schema_version", "scene", "shots", "appearance", "emission"});
    else throw std::runtime_error("Unsupported cinematic schema version");
    if (string(json.at("format"), "format") != "bhr.cinematic") throw std::runtime_error("Expected format bhr.cinematic");
    if (integer(json.at("schema_version"), UINT32_MAX, "schema_version") != static_cast<uint64_t>(version))
        throw std::runtime_error("Unsupported cinematic schema version");
    const auto& scene = json.at("scene");
    fields(scene, {"preset"}, {"starfield_exr"});
    const std::optional<std::string> asset = scene.contains("starfield_exr")
        ? std::optional<std::string>{string(scene.at("starfield_exr"), "starfield_exr")} : std::nullopt;
    const auto& entries = json.at("shots");
    if (!entries.is_array() || entries.empty() || entries.size() > kMaxShots)
        throw std::runtime_error("shots must be an array of 1..64 shots");
    const std::vector<Shot> shots = [&] {
        std::vector<Shot> result;
        result.reserve(entries.size());
        for (const auto& entry : entries) result.push_back(shot(entry));
        return result;
    }();
    const CinematicDocument document{{detail::preset_from_json(scene.at("preset")), asset}, shots};
    validate_cinematic(document);
    return document;
}
Json document_json(const CinematicDocument& document) {
    validate_cinematic(document);
    Json scene = {{"preset", detail::preset_to_json(document.scene.preset)}};
    if (document.scene.starfield_exr) scene["starfield_exr"] = *document.scene.starfield_exr;
    Json shots = Json::array();
    for (const auto& shot : document.shots) {
        Json value = {{"id", shot.id}, {"frame_count", shot.frame_count},
            {"fps", {{"numerator", shot.fps.numerator}, {"denominator", shot.fps.denominator}}},
            {"width", shot.width}, {"height", shot.height}, {"camera", camera_json(shot)}};
        if (shot.loop_frames) value["loop_frames"] = *shot.loop_frames;
        shots.push_back(value);
    }
    const Json json = {{"format", "bhr.cinematic"}, {"schema_version", kCinematicVersion}, {"scene", scene}, {"shots", shots}};
    return json;
}
std::string formatted(const Json& json) {
    const auto contents = json.dump(2) + '\n';
    if (contents.size() > kMaxCinematicBytes) throw std::runtime_error("Cinematic JSON must be at most 1 MiB");
    return contents;
}
std::string read_document(const std::filesystem::path& path) {
    check_path(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open cinematic document: " + path.string());
    // A bounded read also handles a file growing while it is read; never trust tellg alone.
    std::string contents(kMaxCinematicBytes + 1, '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.bad()) throw std::runtime_error("Cannot read cinematic document: " + path.string());
    contents.resize(static_cast<size_t>(input.gcount()));
    return contents;
}
} // namespace

CinematicDocument parse_cinematic(const std::string& contents) { return document_from_json(parse_root(contents),1); }
std::string serialize_cinematic(const CinematicDocument& document) { return formatted(document_json(document)); }
CinematicDocument load_cinematic(const std::filesystem::path& path) { return parse_cinematic(read_document(path)); }
void save_cinematic(const CinematicDocument& document, const std::filesystem::path& path) {
    check_path(path);
    const auto contents = serialize_cinematic(document);
    TemporaryFile temporary(path);
    temporary.replace(contents, path);
}

namespace {
AppearanceV1 appearance_from_json(const Json& j) {
    fields(j, {"schema_version","working_space","radiance_model","temperature_scale",
        "disk_detail","outer_fade_fraction","star_intensity","exposure_ev","tone_map","output_transfer","bloom"});
    if (integer(j.at("schema_version"),UINT32_MAX,"appearance version") != 1)
        throw std::runtime_error("Unsupported appearance version");
    for (const auto& pair : {std::pair{"working_space","linear-srgb-d65"},
            {"radiance_model","relative-disk-v1"}, {"tone_map","reinhard-rgb-v1"}, {"output_transfer","srgb"}}) {
        if (string(j.at(pair.first),pair.first) != pair.second)
            throw std::runtime_error(std::string("Unsupported appearance ") + pair.first);
    }
    const auto& b=j.at("bloom"); fields(b,{"enabled","strength","threshold"});
    if (!b.at("enabled").is_boolean()) throw std::runtime_error("bloom.enabled must be boolean");
    const AppearanceV1 a{number(j.at("temperature_scale")),number(j.at("disk_detail")),
        number(j.at("outer_fade_fraction")),number(j.at("star_intensity")),number(j.at("exposure_ev")),
        {b.at("enabled").get<bool>(),number(b.at("strength")),number(b.at("threshold"))}};
    validate_appearance(a);
    return a;
}
Json appearance_json(const AppearanceV1& a) {
    validate_appearance(a);
    return {{"schema_version",1},{"working_space","linear-srgb-d65"},{"radiance_model","relative-disk-v1"},
        {"temperature_scale",a.temperature_scale},{"disk_detail",a.disk_detail},{"outer_fade_fraction",a.outer_fade_fraction},
        {"star_intensity",a.star_intensity},{"exposure_ev",a.exposure_ev},{"tone_map","reinhard-rgb-v1"},
        {"output_transfer","srgb"},{"bloom",{{"enabled",a.bloom.enabled},{"strength",a.bloom.strength},{"threshold",a.bloom.threshold}}}};
}
EmissionV1 emission_from_json(const Json& j) {
    fields(j, {"schema_version", "enabled", "seed", "amplitude", "radial_modulation", "flow_strength"});
    if (integer(j.at("schema_version"), UINT32_MAX, "emission version") != 1)
        throw std::runtime_error("Unsupported emission version");
    if (!j.at("enabled").is_boolean()) throw std::runtime_error("emission.enabled must be boolean");
    const EmissionV1 emission{j.at("enabled").get<bool>(),
        static_cast<uint32_t>(integer(j.at("seed"), UINT32_MAX, "emission.seed")),
        number(j.at("amplitude")), number(j.at("radial_modulation")), number(j.at("flow_strength"))};
    validate_emission(emission);
    return emission;
}
Json emission_json(const EmissionV1& emission) {
    validate_emission(emission);
    return {{"schema_version",1},{"enabled",emission.enabled},{"seed",emission.seed},
        {"amplitude",emission.amplitude},{"radial_modulation",emission.radial_modulation},
        {"flow_strength",emission.flow_strength}};
}
CinematicRenderDocument v2_from_json(const Json& j) {
    const auto doc=document_from_json(j,2);
    return upgrade_cinematic(doc,appearance_from_json(j.at("appearance")));
}
AnimatedRenderDocument v3_from_json(const Json& j) {
    const auto doc = document_from_json(j, 3);
    const AnimatedRenderDocument result{doc, appearance_from_json(j.at("appearance")), emission_from_json(j.at("emission"))};
    validate_animated_document(result);
    return result;
}
}
CinematicRenderDocument parse_cinematic_v2(const std::string& contents) { return v2_from_json(parse_root(contents)); }
AnimatedRenderDocument parse_cinematic_v3(const std::string& contents) { return v3_from_json(parse_root(contents)); }
RenderDocument parse_render_document(const std::string& contents) {
    const auto j=parse_root(contents);
    const auto version=integer(j.at("schema_version"),UINT32_MAX,"schema_version");
    if (version == 1) return document_from_json(j,1);
    if (version == 2) return v2_from_json(j);
    if (version == 3) return v3_from_json(j);
    throw std::runtime_error("Unsupported cinematic schema version");
}
std::string serialize_cinematic_v2(const CinematicRenderDocument& doc) {
    (void)upgrade_cinematic(doc.scene_shots,doc.appearance);
    auto j=document_json(doc.scene_shots);
    j["schema_version"]=2;
    j["appearance"]=appearance_json(doc.appearance);
    return formatted(j);
}
std::string serialize_cinematic_v3(const AnimatedRenderDocument& doc) {
    validate_animated_document(doc);
    auto j=document_json(doc.scene_shots);
    j["schema_version"]=3;
    j["appearance"]=appearance_json(doc.appearance);
    j["emission"]=emission_json(doc.emission);
    return formatted(j);
}
CinematicRenderDocument load_cinematic_v2(const std::filesystem::path& path) { return parse_cinematic_v2(read_document(path)); }
AnimatedRenderDocument load_cinematic_v3(const std::filesystem::path& path) { return parse_cinematic_v3(read_document(path)); }
RenderDocument load_render_document(const std::filesystem::path& path) { return parse_render_document(read_document(path)); }
void save_cinematic_v2(const CinematicRenderDocument& doc, const std::filesystem::path& path) {
    check_path(path);
    const auto contents=serialize_cinematic_v2(doc);
    TemporaryFile temporary(path);
    temporary.replace(contents,path);
}
namespace detail {
void atomic_write(const std::filesystem::path& path, const std::string& contents) {
    check_path(path); TemporaryFile temporary(path); temporary.replace(contents,path);
}
}
} // namespace bhr
