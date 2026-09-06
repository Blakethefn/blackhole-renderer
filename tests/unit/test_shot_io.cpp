#include "doctest.h"
#include "bhr/shot.hpp"
#include "bhr/presets.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>

namespace {
using Json = nlohmann::json;
struct Files {
    const std::filesystem::path dir = [] {
        std::string pattern = (std::filesystem::temp_directory_path() / "bhr-shot-XXXXXX").string();
        const auto created = mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("Cannot create test directory");
        return std::filesystem::path(created);
    }();
    ~Files() { std::filesystem::remove_all(dir); }
};
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
const bhr::CinematicDocument reference() {
    return bhr::import_preset(bhr::workbench_preset());
}
}

TEST_CASE("shot persistence round trips both cameras and imports every v1 field") {
    const auto fixture = bhr::load_cinematic("presets/shots/reference.json");
    CHECK(fixture.shots.size() == 2);
    CHECK(fixture.shots[0].frame_count == 180);
    CHECK(fixture.shots[1].frame_count == 600);
    const auto text = bhr::serialize_cinematic(fixture);
    CHECK(bhr::serialize_cinematic(bhr::parse_cinematic(text)) == text);
    Files files;
    const auto path = files.dir / "scene.json";
    bhr::save_cinematic(fixture, path);
    CHECK(bhr::serialize_cinematic(bhr::load_cinematic(path)) == text);
    bhr::save_cinematic(reference(), path);
    CHECK(bhr::serialize_cinematic(bhr::load_cinematic(path)) == bhr::serialize_cinematic(reference()));
    // Compare JSON representations of all physical fields, including nondefault toggles/backend.
    const bhr::RenderParams preset{0.5f, {38.0625f, 72.5f, -21.125f, 45, 128, 72},
        {4.25f, 24.125f, 32123.5f, 0.625f}, false, false, false, true, bhr::IntegratorKind::kGeokerr};
    const auto doc = bhr::import_preset(preset, "stars/sky.exr");
    std::string error;
    REQUIRE(bhr::save_preset(preset, (files.dir / "v1.json").string(), error));
    CHECK(Json::parse(read(files.dir / "v1.json")) == Json::parse(bhr::serialize_cinematic(doc))["scene"]["preset"]);
    CHECK(doc.shots[0].id == "fixed");
    CHECK(doc.shots[0].width == preset.camera.width);
    CHECK(doc.shots[0].height == preset.camera.height);
    CHECK_THROWS(bhr::import_preset(preset)); // never silently disables requested environment
    CHECK(bhr::serialize_cinematic(doc) == bhr::serialize_cinematic(bhr::parse_cinematic(bhr::serialize_cinematic(doc))));
}

TEST_CASE("shot JSON rejects coercions unknown fields duplicate keys and limits") {
    const auto text = bhr::serialize_cinematic(reference());
    const auto valid = Json::parse(text);
    const auto invalid = [&](const char* pointer, Json value) {
        auto changed = valid;
        changed[Json::json_pointer(pointer)] = value;
        CHECK_THROWS(bhr::parse_cinematic(changed.dump()));
    };
    invalid("/format", "other");
    invalid("/format", 1);
    invalid("/schema_version", 2);
    invalid("/schema_version", 1.0);
    invalid("/unknown", true);
    invalid("/scene/unknown", true);
    invalid("/scene/preset/spin", -1);
    invalid("/scene/preset/schema_version", 2);
    invalid("/scene/preset/enable_doppler", 1);
    invalid("/scene/preset/camera/width", 1.5);
    invalid("/scene/starfield_exr", nullptr);
    invalid("/scene/starfield_exr", std::string("sky\0.exr", 8));
    invalid("/scene/starfield_exr", std::string(1025, 'a') + ".exr");
    invalid("/shots", Json::object());
    invalid("/shots", Json::array());
    invalid("/shots/0/id", 1);
    invalid("/shots/0/id", std::string(65, 'a'));
    for (const auto& value : {Json(0), Json(-1), Json(1.5), Json("60"), Json(true), Json(UINT64_MAX)}) {
        invalid("/shots/0/frame_count", value);
        invalid("/shots/0/fps/numerator", value);
        invalid("/shots/0/fps/denominator", value);
        invalid("/shots/0/loop_frames", value);
    }
    invalid("/shots/0/fps/numerator", 4294967296ULL);
    invalid("/shots/0/fps/denominator", 4294967296ULL);
    invalid("/shots/0/fps/extra", 1);
    invalid("/shots/0/width", 1.0);
    invalid("/shots/0/height", "36");
    invalid("/shots/0/camera/kind", "spline");
    invalid("/shots/0/camera/start", Json::object());
    invalid("/shots/0/camera", {{"kind", "orbit"}});
    invalid("/shots/0/loop_frames", nullptr);
    auto missing = valid;
    missing["scene"]["preset"].erase("spin");
    CHECK_THROWS(bhr::parse_cinematic(missing.dump()));
    auto duplicate = valid;
    duplicate["shots"].push_back(duplicate["shots"][0]);
    CHECK_THROWS(bhr::parse_cinematic(duplicate.dump()));
    for (const std::string& bad : std::vector<std::string>{"{", "[]", text + " true", text + std::string("\0junk", 5), "{\"format\":1,\"format\":2}",
            "{\"scene\":{\"preset\":{\"spin\":0,\"spin\":1}}}",
            "{\"a\":[[[[[[[[[]]]]]]]]]}", std::string(1024 * 1024 + 1, ' ')}) {
        CHECK_THROWS(bhr::parse_cinematic(bad));
    }
    auto orbit = Json::parse(bhr::serialize_cinematic(bhr::load_cinematic("presets/shots/reference.json")));
    orbit["shots"][1]["camera"]["start"]["r_cam"] = "30";
    CHECK_THROWS(bhr::parse_cinematic(orbit.dump()));
    orbit["shots"][1]["camera"]["start"]["r_cam"] = 1e100;
    CHECK_THROWS(bhr::parse_cinematic(orbit.dump()));
}

TEST_CASE("shot file errors preserve source and prior destination atomically") {
    Files files;
    const auto doc = reference();
    const auto before = bhr::serialize_cinematic(doc);
    const auto path = files.dir / "scene.json";
    bhr::save_cinematic(doc, path);
    const bhr::CinematicDocument invalid{doc.scene, {}};
    CHECK_THROWS(bhr::save_cinematic(invalid, path));
    CHECK(read(path) == before);
    CHECK_THROWS(bhr::save_cinematic(doc, files.dir / "missing" / "scene.json"));
    std::filesystem::create_directory(files.dir / "destination");
    std::ofstream(files.dir / "destination" / "keep") << "previous data";
    CHECK_THROWS(bhr::save_cinematic(doc, files.dir / "destination")); // rename failure, cleanup temp
    CHECK(read(files.dir / "destination" / "keep") == "previous data");
    for (const auto& entry : std::filesystem::directory_iterator(files.dir)) {
        CHECK(entry.path().filename().string().find(".tmp-") == std::string::npos);
    }
    CHECK_THROWS(bhr::load_cinematic(files.dir / "missing.json"));
    CHECK_THROWS(bhr::load_cinematic(std::filesystem::path{}));
    CHECK_THROWS(bhr::save_cinematic(doc, std::filesystem::path{}));
    CHECK_THROWS(bhr::save_cinematic(doc, std::string("bad\0path", 8)));
    CHECK_THROWS(bhr::load_cinematic(files.dir));
    std::ofstream(path) << "{";
    CHECK_THROWS(bhr::load_cinematic(path));
    CHECK(bhr::serialize_cinematic(doc) == before);
    std::ofstream(path) << std::string(1024 * 1024 + 1, ' ');
    CHECK_THROWS(bhr::load_cinematic(path));
}

TEST_CASE("shot EXR references resolve from document directory and require real assets") {
    Files files;
    const auto base = bhr::workbench_preset();
    const bhr::RenderParams enabled{base.spin, base.camera, base.disk, true, true, true, true, base.integrator};
    const auto doc = bhr::import_preset(enabled, "assets/sky.exr");
    const auto path = files.dir / "scene.json";
    bhr::save_cinematic(doc, path); // authoring does not require an installed asset
    CHECK_THROWS(bhr::resolve_starfield(doc.scene, path));
    CHECK_THROWS(bhr::resolve_starfield(doc.scene, std::filesystem::path{}));
    CHECK_THROWS(bhr::resolve_starfield(doc.scene, std::string("bad\0path", 8)));
    std::filesystem::create_directory(files.dir / "assets");
    std::ofstream(files.dir / "assets" / "sky.exr") << "EXR format validation belongs to existing loader";
    CHECK(bhr::resolve_starfield(doc.scene, path) == (files.dir / "assets" / "sky.exr").string());
    const auto relative = std::filesystem::relative(path, std::filesystem::current_path());
    CHECK(bhr::resolve_starfield(doc.scene, relative) == (files.dir / "assets" / "sky.exr").string());
    CHECK(bhr::resolve_starfield(reference().scene, path).empty());
    CHECK(bhr::resolve_starfield({base, "assets/missing.exr"}, path).empty());
}

TEST_CASE("shot interrupted write preserves existing document and removes temporary file") {
    Files files;
    const auto path = files.dir / "scene.json";
    const auto doc = reference();
    bhr::save_cinematic(doc, path);
    const auto before = read(path);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // Only the disposable child is limited. This reproduces a real partial
        // fwrite/fflush failure without filling a disk or changing system limits.
        const rlimit limit{32, 32};
        if (std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
        try { bhr::save_cinematic(doc, path); }
        catch (const std::exception&) { _exit(0); }
        _exit(1);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    CHECK(read(path) == before);
    CHECK(std::distance(std::filesystem::directory_iterator(files.dir), std::filesystem::directory_iterator{}) == 1);
}
