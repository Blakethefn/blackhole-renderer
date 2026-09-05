#include "bhr/presets.hpp"

#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <set>
#include <vector>

namespace bhr {
namespace {

using Json = nlohmann::json;
constexpr int kSchemaVersion = 1;
constexpr std::streamoff kMaxPresetBytes = 1024 * 1024;

void require_fields(const Json& object, std::initializer_list<const char*> fields, const char* name) {
    if (!object.is_object() || object.size() != fields.size()) {
        throw std::runtime_error(std::string(name) + " must contain exactly the documented preset fields");
    }
    for (const char* field : fields) {
        if (!object.contains(field)) {
            throw std::runtime_error(std::string(name) + " is missing field " + field);
        }
    }
}

float number(const Json& object, const char* field) {
    const auto& value = object.at(field);
    if (!value.is_number()) throw std::runtime_error(std::string(field) + " must be a number");
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed) || std::abs(parsed) > std::numeric_limits<float>::max()
        || (parsed != 0.0 && std::abs(parsed) < std::numeric_limits<float>::min())) {
        throw std::runtime_error(std::string(field) + " must be a finite float");
    }
    return static_cast<float>(parsed);
}

int integer(const Json& object, const char* field) {
    const auto& value = object.at(field);
    if (!value.is_number_integer()) throw std::runtime_error(std::string(field) + " must be an integer");
    const double parsed = value.get<double>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string(field) + " is outside the supported integer range");
    }
    return value.get<int>();
}

bool boolean(const Json& object, const char* field) {
    const auto& value = object.at(field);
    if (!value.is_boolean()) throw std::runtime_error(std::string(field) + " must be true or false");
    return value.get<bool>();
}

Json to_json(const RenderParams& params) {
    return {
        {"schema_version", kSchemaVersion},
        {"spin", params.spin},
        {"camera", {{"r_cam", params.camera.r_cam}, {"theta_cam_deg", params.camera.theta_cam_deg},
                    {"phi_cam_deg", params.camera.phi_cam_deg}, {"fov_deg", params.camera.fov_deg},
                    {"width", params.camera.width}, {"height", params.camera.height}}},
        {"disk", {{"r_inner", params.disk.r_inner}, {"r_outer", params.disk.r_outer},
                  {"peak_temp_K", params.disk.peak_temp_K}, {"brightness", params.disk.brightness}}},
        {"enable_doppler", params.enable_doppler}, {"enable_redshift", params.enable_redshift},
        {"enable_beaming", params.enable_beaming}, {"enable_starfield", params.enable_starfield},
        {"integrator", params.integrator == IntegratorKind::kRK45 ? "rk45" : "geokerr"}
    };
}

RenderParams from_json(const Json& json) {
    require_fields(json, {"schema_version", "spin", "camera", "disk", "enable_doppler",
                         "enable_redshift", "enable_beaming", "enable_starfield", "integrator"}, "Preset");
    if (integer(json, "schema_version") != kSchemaVersion) throw std::runtime_error("Unsupported preset schema version");
    const auto& camera = json.at("camera");
    const auto& disk = json.at("disk");
    require_fields(camera, {"r_cam", "theta_cam_deg", "phi_cam_deg", "fov_deg", "width", "height"}, "Camera");
    require_fields(disk, {"r_inner", "r_outer", "peak_temp_K", "brightness"}, "Disk");
    const auto& integrator = json.at("integrator");
    if (!integrator.is_string() || (integrator != "rk45" && integrator != "geokerr")) {
        throw std::runtime_error("integrator must be rk45 or geokerr");
    }
    const RenderParams params{
        number(json, "spin"),
        {number(camera, "r_cam"), number(camera, "theta_cam_deg"), number(camera, "phi_cam_deg"),
         number(camera, "fov_deg"), integer(camera, "width"), integer(camera, "height")},
        {number(disk, "r_inner"), number(disk, "r_outer"), number(disk, "peak_temp_K"), number(disk, "brightness")},
        boolean(json, "enable_doppler"), boolean(json, "enable_redshift"),
        boolean(json, "enable_beaming"), boolean(json, "enable_starfield"),
        integrator == "rk45" ? IntegratorKind::kRK45 : IntegratorKind::kGeokerr
    };
    if (const char* invalid = validation_error(params)) throw std::runtime_error(invalid);
    return params;
}

} // namespace

RenderParams workbench_preset() {
    return RenderParams{0.9f, {30.0f, 85.0f, 0.0f, 35.0f, 256, 144},
                        {2.321f, 20.0f, 40000.0f, 1.0f}, true, true, true, false, IntegratorKind::kRK45};
}

bool save_preset(const RenderParams& params, const std::string& path, std::string& error) {
    error.clear();
    if (const char* invalid = validation_error(params)) {
        error = invalid;
        return false;
    }
    try {
        const std::string contents = to_json(params).dump(2) + '\n';
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open preset for writing: " + path);
        output << contents;
        output.close();
        if (!output) throw std::runtime_error("Failed to write preset: " + path);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool load_preset(const std::string& path, RenderParams& out, std::string& error) {
    error.clear();
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open preset: " + path);
        const auto size = input.tellg();
        if (size < 0 || size > kMaxPresetBytes) throw std::runtime_error("Preset must be at most 1 MiB");
        input.seekg(0);
        const std::string contents(std::istreambuf_iterator<char>(input), {});
        if (input.bad()) throw std::runtime_error("Failed to read preset: " + path);
        // JSON's default last-key-wins behavior would hide malformed duplicate
        // values. Reject those, and bound nesting before decoding the schema.
        std::vector<std::set<std::string>> object_keys;
        const auto check_structure = [&object_keys](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 4) throw std::runtime_error("Preset nesting exceeds the documented schema");
            if (event == Json::parse_event_t::object_start) object_keys.emplace_back();
            if (event == Json::parse_event_t::key
                && !object_keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate preset field: " + value.get<std::string>());
            if (event == Json::parse_event_t::object_end) object_keys.pop_back();
            return true;
        };
        const RenderParams loaded = from_json(Json::parse(contents, check_structure));
        out = loaded;
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

} // namespace bhr
