#include "doctest.h"
#include "bhr/shot.hpp"
#include "bhr/presets.hpp"
#include <limits>
#include <type_traits>

namespace {
bhr::Shot fixed(uint64_t count = 180, bhr::FrameRate fps = {60, 1},
                std::optional<uint64_t> loop = 180) {
    return {"fixed", count, fps, 64, 36, loop, bhr::FixedCamera{}};
}
bhr::CinematicDocument document(const bhr::Shot& shot) {
    return {{bhr::workbench_preset(), std::nullopt}, {shot}};
}
bhr::Shot orbit(uint64_t count = 5, bhr::BLPose start = {40, 60, 170},
                bhr::BLPose end = {20, 80, 190}) {
    return {"orbit", count, {60, 1}, 64, 36, 2, bhr::OrbitCamera{start, end}};
}
}

TEST_CASE("shot fixed clock has exact rational time and separate loop metadata") {
    static_assert(!std::is_copy_assignable_v<bhr::CinematicDocument>);
    static_assert(!std::is_copy_assignable_v<bhr::Shot>);
    const auto doc = document(fixed());
    const auto sample = bhr::evaluate_frame(doc, "fixed", 179);
    CHECK(sample.time.numerator == 179);
    CHECK(sample.time.denominator == 60);
    CHECK(sample.time.seconds() == doctest::Approx(179.0 / 60));
    CHECK(sample.duration.seconds() == 3);
    REQUIRE(sample.loop_phase.has_value());
    CHECK(sample.loop_phase->numerator == 179);
    CHECK(sample.loop_phase->denominator == 180);
    CHECK(sample.params.camera.r_cam == doc.scene.preset.camera.r_cam);
    CHECK(sample.params.camera.width == 64);
    CHECK(doc.scene.preset.camera.width == 256);
    CHECK(sample.params.disk.peak_temp_K == doc.scene.preset.disk.peak_temp_K);
    const auto one = bhr::evaluate_frame(document(fixed(1, {60, 1}, std::nullopt)), "fixed", 0);
    CHECK(one.time.numerator == 0);
    CHECK_FALSE(one.loop_phase.has_value());
    CHECK(one.duration.seconds() == doctest::Approx(1.0 / 60));
    CHECK_THROWS(bhr::evaluate_frame(doc, "fixed", 180));
    CHECK_THROWS(bhr::evaluate_frame(doc, "fixed", UINT64_MAX));
    CHECK_THROWS(bhr::evaluate_frame(doc, "missing", 0));
}

TEST_CASE("shot rational FPS is evaluated from frame index without accumulation") {
    const auto doc = document(fixed(1000000, {30000, 1001}, 7));
    const auto before = bhr::serialize_cinematic(doc);
    for (const uint64_t index : {999999ULL, 14ULL, 0ULL, 17ULL, 999999ULL}) {
        const auto sample = bhr::evaluate_frame(doc, "fixed", index);
        CHECK(sample.time.numerator == index * 1001);
        CHECK(sample.time.denominator == 30000);
        CHECK(sample.time.seconds() == static_cast<double>(index * 1001) / 30000);
        CHECK(sample.loop_phase->numerator == index % 7);
        CHECK(sample.duration.numerator == 1001000000);
        CHECK(sample.duration.denominator == 30000);
    }
    CHECK(bhr::serialize_cinematic(doc) == before);
    const auto large = bhr::evaluate_frame(document(fixed(1000000, {1, UINT32_MAX})), "fixed", 999999);
    CHECK(large.time.numerator == 999999ULL * UINT32_MAX);
    CHECK(large.duration.numerator == 1000000ULL * UINT32_MAX);
}

TEST_CASE("shot orbit reaches endpoints uses smoothstep and never wraps progress") {
    const auto doc = document(orbit());
    const auto before = bhr::serialize_cinematic(doc);
    const auto end = bhr::evaluate_frame(doc, "orbit", 4);
    const auto quarter = bhr::evaluate_frame(doc, "orbit", 1);
    const auto start = bhr::evaluate_frame(doc, "orbit", 0);
    const auto mid = bhr::evaluate_frame(doc, "orbit", 2);
    CHECK(start.params.camera.r_cam == 40);
    CHECK(start.params.camera.theta_cam_deg == 60);
    CHECK(start.params.camera.phi_cam_deg == 170);
    CHECK(quarter.params.camera.r_cam == 36.875f); // smoothstep(1/4) = 5/32
    CHECK(quarter.params.camera.theta_cam_deg == 63.125f);
    CHECK(mid.params.camera.r_cam == 30);
    CHECK(mid.params.camera.theta_cam_deg == 70);
    CHECK(mid.params.camera.phi_cam_deg == 180);
    CHECK(mid.loop_phase->numerator == 0); // camera did not restart with loop
    CHECK(end.params.camera.r_cam == 20);
    CHECK(end.params.camera.theta_cam_deg == 80);
    CHECK(end.params.camera.phi_cam_deg == 190);
    CHECK(end.params.camera.fov_deg == doc.scene.preset.camera.fov_deg);
    CHECK(bhr::evaluate_frame(doc, "orbit", 1).params.camera.r_cam == quarter.params.camera.r_cam);
    CHECK(bhr::serialize_cinematic(doc) == before);
    const auto rotation = document(orbit(3, {40, 60, 0}, {20, 80, 360}));
    CHECK(bhr::evaluate_frame(rotation, "orbit", 1).params.camera.phi_cam_deg == 180);
    CHECK(bhr::evaluate_frame(rotation, "orbit", 2).params.camera.phi_cam_deg == 360);
    CHECK(bhr::evaluate_frame(document(orbit(2)), "orbit", 1).params.camera.r_cam == 20);
    const auto reverse = document(orbit(3, {40, 60, 190}, {20, 80, 170}));
    CHECK(bhr::evaluate_frame(reverse, "orbit", 1).params.camera.phi_cam_deg == 180);
    const float max = std::numeric_limits<float>::max();
    CHECK(bhr::evaluate_frame(document(orbit(3, {40, 60, -max}, {20, 80, max})), "orbit", 1).params.camera.phi_cam_deg == 0);
}

TEST_CASE("shot validation rejects invalid clocks poses dimensions IDs and assets") {
    for (const auto& shot : {fixed(0), fixed(1000001), fixed(UINT64_MAX), fixed(2, {0, 1}),
                            fixed(2, {1, 0}), fixed(2, {241, 1}), fixed(2, {UINT32_MAX, 1}),
                            fixed(2, {60, 1}, 0), fixed(2, {60, 1}, 1000001), orbit(1),
                            orbit(2, {1, 60, 0}), orbit(2, {1001, 60, 0}),
                            orbit(2, {40, 0, 0}), orbit(2, {40, 180, 0}),
                            orbit(2, {40, 60, std::numeric_limits<float>::infinity()})}) {
        CHECK_THROWS(bhr::validate_cinematic(document(shot)));
    }
    for (const auto& id : {"", "a b", "../a", "a\nb", "a/b", "é"}) {
        CHECK_THROWS(bhr::validate_cinematic(document({id, 1, {60, 1}, 64, 36, {}, bhr::FixedCamera{}})));
    }
    for (const int dimension : {0, -1, 16385, INT32_MAX}) {
        CHECK_THROWS(bhr::validate_cinematic(document({"x", 1, {60, 1}, dimension, 36, {}, bhr::FixedCamera{}})));
        CHECK_THROWS(bhr::validate_cinematic(document({"x", 1, {60, 1}, 64, dimension, {}, bhr::FixedCamera{}})));
    }
    const bhr::Scene scene{bhr::workbench_preset(), {}};
    CHECK_THROWS(bhr::validate_cinematic({scene, {}}));
    CHECK_THROWS(bhr::validate_cinematic({scene, {fixed(), fixed()}}));
    CHECK_THROWS(bhr::validate_cinematic({scene, std::vector<bhr::Shot>(65, fixed())}));
    for (const auto& ref : {"", "/tmp/a.exr", "../a.exr", "a/../b.exr", "https://a.exr", "a.png", "a\\b.exr", "a\nb.exr"}) {
        CHECK_THROWS(bhr::validate_cinematic({{scene.preset, ref}, {fixed()}}));
    }
    CHECK_NOTHROW(bhr::validate_cinematic(document(fixed(2, {240, 1}))));
}
