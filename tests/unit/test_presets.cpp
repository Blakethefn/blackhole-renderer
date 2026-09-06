#include "doctest.h"
#include "bhr/presets.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

struct PresetFile {
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("bhr-preset-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    ~PresetFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    void write(const std::string& contents) const {
        std::ofstream output(path);
        output << contents;
        REQUIRE(output.good());
    }
    std::string read() const {
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }
};

void check_same(const bhr::RenderParams& actual, const bhr::RenderParams& expected) {
    CHECK(actual.spin == expected.spin);
    CHECK(actual.camera.r_cam == expected.camera.r_cam);
    CHECK(actual.camera.theta_cam_deg == expected.camera.theta_cam_deg);
    CHECK(actual.camera.phi_cam_deg == expected.camera.phi_cam_deg);
    CHECK(actual.camera.fov_deg == expected.camera.fov_deg);
    CHECK(actual.camera.width == expected.camera.width);
    CHECK(actual.camera.height == expected.camera.height);
    CHECK(actual.disk.r_inner == expected.disk.r_inner);
    CHECK(actual.disk.r_outer == expected.disk.r_outer);
    CHECK(actual.disk.peak_temp_K == expected.disk.peak_temp_K);
    CHECK(actual.disk.brightness == expected.disk.brightness);
    CHECK(actual.enable_doppler == expected.enable_doppler);
    CHECK(actual.enable_redshift == expected.enable_redshift);
    CHECK(actual.enable_beaming == expected.enable_beaming);
    CHECK(actual.enable_starfield == expected.enable_starfield);
    CHECK(actual.integrator == expected.integrator);
}

std::string replace_once(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    REQUIRE(position != std::string::npos);
    return text.substr(0, position) + to + text.substr(position + from.size());
}

} // namespace

TEST_CASE("workbench preset is a valid deterministic RK45 preview") {
    const auto params = bhr::workbench_preset();
    CHECK(bhr::validation_error(params) == nullptr);
    CHECK(params.integrator == bhr::IntegratorKind::kRK45);
    CHECK(params.camera.width == 256);
    CHECK(params.camera.height == 144);
    CHECK(params.spin == doctest::Approx(0.9f));
    CHECK(params.camera.r_cam == doctest::Approx(30.0f));
    CHECK(params.disk.peak_temp_K == doctest::Approx(40000.0f));
    check_same(params, bhr::workbench_preset());
}

TEST_CASE("committed gallery presets are valid prograde RK45 scenes") {
    struct ExpectedGalleryScene {
        const char* path;
        float spin;
    };
    constexpr std::array scenes{
        ExpectedGalleryScene{"presets/gallery/schwarzschild.json", 0.0f},
        ExpectedGalleryScene{"presets/gallery/kerr-moderate.json", 0.5f},
        ExpectedGalleryScene{"presets/gallery/kerr-high-prograde.json", 0.99f},
    };

    for (const auto& scene : scenes) {
        CAPTURE(scene.path);
        bhr::RenderParams params;
        std::string error;
        REQUIRE(bhr::load_preset(scene.path, params, error));
        CHECK(error.empty());
        CHECK(bhr::validation_error(params) == nullptr);
        CHECK(params.spin == doctest::Approx(scene.spin));
        CHECK(params.spin >= 0.0f);
        CHECK(params.disk.r_inner >= bhr::r_isco(params.spin));
        CHECK(params.integrator == bhr::IntegratorKind::kRK45);
        CHECK_FALSE(params.enable_starfield);
        CHECK(params.camera.width == 1024);
        CHECK(params.camera.height == 576);
    }
}

TEST_CASE("presets round trip every render field without changing floats") {
    PresetFile file;
    auto params = bhr::workbench_preset();
    params.spin = 0.73125f;
    params.camera.phi_cam_deg = -21.125f;
    params.camera.r_cam = 38.0625f;
    params.camera.width = 512;
    params.camera.height = 288;
    params.disk.r_inner = 4.25f;
    params.disk.r_outer = 24.125f;
    params.disk.brightness = 0.625f;
    params.enable_doppler = false;
    params.enable_redshift = false;
    params.enable_beaming = false;
    params.enable_starfield = true;
    params.integrator = bhr::IntegratorKind::kGeokerr;
    std::string error = "stale error";
    REQUIRE(bhr::save_preset(params, file.path.string(), error));
    CHECK(error.empty());
    bhr::RenderParams loaded;
    REQUIRE(bhr::load_preset(file.path.string(), loaded, error));
    CHECK(error.empty());
    check_same(loaded, params);
}

TEST_CASE("preset loading rejects invalid schema and leaves active parameters untouched") {
    PresetFile file;
    const auto expected = bhr::workbench_preset();
    std::string error;
    REQUIRE(bhr::save_preset(expected, file.path.string(), error));
    const std::string valid = file.read();
    std::string invalid;
    SUBCASE("malformed JSON") { invalid = "{"; }
    SUBCASE("trailing input") { invalid = valid + " true"; }
    SUBCASE("missing fields") { invalid = "{\"schema_version\":1}"; }
    SUBCASE("unknown field") { invalid = replace_once(valid, "\"spin\":", "\"spni\":"); }
    SUBCASE("duplicate root field") { invalid = replace_once(valid, "\"spin\":", "\"spin\": \"bad\", \"spin\":"); }
    SUBCASE("duplicate nested field") { invalid = replace_once(valid, "\"r_cam\":", "\"r_cam\": \"bad\", \"r_cam\":"); }
    SUBCASE("unknown integrator") { invalid = replace_once(valid, "\"rk45\"", "\"unknown\""); }
    SUBCASE("fractional resolution") { invalid = replace_once(valid, "\"width\": 256", "\"width\": 256.5"); }
    SUBCASE("overflow resolution") { invalid = replace_once(valid, "\"width\": 256", "\"width\": 4294967552"); }
    SUBCASE("negative resolution") { invalid = replace_once(valid, "\"height\": 144", "\"height\": -1"); }
    SUBCASE("string resolution") { invalid = replace_once(valid, "\"width\": 256", "\"width\": \"256\""); }
    SUBCASE("non-boolean toggle") { invalid = replace_once(valid, "\"enable_doppler\": true", "\"enable_doppler\": 1"); }
    SUBCASE("unsupported schema version") { invalid = replace_once(valid, "\"schema_version\": 1", "\"schema_version\": 2"); }
    SUBCASE("fractional schema version") { invalid = replace_once(valid, "\"schema_version\": 1", "\"schema_version\": 1.5"); }
    SUBCASE("non-finite input") { invalid = replace_once(valid, "\"r_cam\": 30.0", "\"r_cam\": 1e400"); }
    SUBCASE("float overflow") { invalid = replace_once(valid, "\"r_cam\": 30.0", "\"r_cam\": 1e100"); }
    SUBCASE("float underflow") { invalid = replace_once(valid, "\"brightness\": 1.0", "\"brightness\": 1e-100"); }
    SUBCASE("invalid parameter combination") { invalid = replace_once(valid, "\"r_outer\": 20.0", "\"r_outer\": 1.0"); }
    file.write(invalid);
    auto active = expected;
    CHECK_FALSE(bhr::load_preset(file.path.string(), active, error));
    CHECK_FALSE(error.empty());
    check_same(active, expected);
}

TEST_CASE("invalid preset save preserves existing file and reports an error") {
    PresetFile file;
    file.write("previous contents");
    auto params = bhr::workbench_preset();
    params.spin = std::numeric_limits<float>::quiet_NaN();
    std::string error;
    CHECK_FALSE(bhr::save_preset(params, file.path.string(), error));
    CHECK_FALSE(error.empty());
    CHECK(file.read() == "previous contents");
}

TEST_CASE("preset file failures are reported without altering parameters") {
    PresetFile file;
    const auto expected = bhr::workbench_preset();
    auto params = expected;
    std::string error;
    CHECK_FALSE(bhr::load_preset(file.path.string(), params, error));
    CHECK_FALSE(error.empty());
    check_same(params, expected);
    CHECK_FALSE(bhr::save_preset(params, (file.path / "missing" / "preset.json").string(), error));
    CHECK_FALSE(error.empty());
}
