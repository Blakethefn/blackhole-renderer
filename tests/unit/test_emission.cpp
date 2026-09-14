#include "doctest.h"
#include "bhr/emission.hpp"
#include "bhr/appearance.hpp"
#include "bhr/presets.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <limits>

TEST_CASE("animated disk emission is bounded, seeded and periodic") {
    const bhr::EmissionV1 emission{true, 7, .18f, .06f, 1.0f};
    constexpr float tau = 6.2831853071795864769f;
    for (int i = 0; i < 100; ++i) {
        const float r = 3.83f + 0.16f * i;
        const float phi = -4.0f + 0.37f * i;
        for (const float phase : {0.0f, .1f, .5f, .99f, 1.0f}) {
            const float value = bhr::animated_disk_emission(r, phi, 3.83f, 20.0f, emission, phase);
            CHECK(std::isfinite(value));
            CHECK(value >= .25f);
            CHECK(value <= 1.75f);
        }
        CHECK(bhr::animated_disk_emission(r, phi, 3.83f, 20.0f, emission, 0.0f)
              == doctest::Approx(bhr::animated_disk_emission(r, phi, 3.83f, 20.0f, emission, 1.0f)).epsilon(1e-5));
        CHECK(bhr::animated_disk_emission(r, phi, 3.83f, 20.0f, emission, 0.0f)
              == doctest::Approx(bhr::animated_disk_emission(r, phi + tau, 3.83f, 20.0f, emission, 0.0f)).epsilon(1e-5));
    }
    const float eps = 1e-4f;
    const float before = bhr::animated_disk_emission(10, .4f, 3.83f, 20, emission, 1.0f - eps);
    const float at_end = bhr::animated_disk_emission(10, .4f, 3.83f, 20, emission, 1.0f);
    const float at_start = bhr::animated_disk_emission(10, .4f, 3.83f, 20, emission, 0.0f);
    const float after = bhr::animated_disk_emission(10, .4f, 3.83f, 20, emission, eps);
    CHECK(at_end == doctest::Approx(at_start).epsilon(1e-5));
    CHECK((at_end - before) / eps == doctest::Approx((after - at_start) / eps).epsilon(2e-3));
    CHECK(bhr::animated_disk_emission(10, 0, 3.83f, 20, {}, .25f) == 1.0f);
    CHECK(bhr::animated_disk_emission(10, 0, 3.83f, 20, emission,
                                      .25f) != bhr::animated_disk_emission(10, 0, 3.83f, 20,
                                      {true, 8, .18f, .06f, 1.0f}, .25f));
}

TEST_CASE("animated emission validation rejects unsafe values and phases") {
    CHECK_NOTHROW(bhr::validate_emission({}));
    CHECK_NOTHROW(bhr::validate_emission({true, UINT32_MAX, .25f, .12f, 1.0f}));
    CHECK_THROWS(bhr::validate_emission({true, 7, -.01f, .06f, 1.0f}));
    CHECK_THROWS(bhr::validate_emission({true, 7, .18f, .13f, 1.0f}));
    CHECK_THROWS(bhr::validate_emission({true, 7, .18f, .06f, 1.01f}));
    CHECK_THROWS(bhr::validate_emission({true, 7, std::numeric_limits<float>::quiet_NaN(), .06f, 1.0f}));
    CHECK_NOTHROW(bhr::validate_emission_phase({0, 1}));
    CHECK_NOTHROW(bhr::validate_emission_phase({179, 180}));
    CHECK_THROWS(bhr::validate_emission_phase({180, 180}));
    CHECK_THROWS(bhr::validate_emission_phase({1, 0}));
}

TEST_CASE("cinematic v3 requires explicit versioned emission and loop phases") {
    const auto v2 = bhr::load_cinematic_v2("presets/cinematic/scene.json");
    const bhr::AnimatedRenderDocument document{v2.scene_shots, v2.appearance, {true, 7, .18f, .06f, 1.0f}};
    const auto text = bhr::serialize_cinematic_v3(document);
    const auto round_trip = bhr::parse_cinematic_v3(text);
    CHECK(round_trip.emission.enabled);
    CHECK(round_trip.emission.seed == 7);
    CHECK(round_trip.emission.amplitude == .18f);
    CHECK(bhr::serialize_cinematic_v3(round_trip) == text);
    CHECK(std::holds_alternative<bhr::AnimatedRenderDocument>(bhr::parse_render_document(text)));
    const auto frame = bhr::evaluate_frame(round_trip.scene_shots, "fixed", 179);
    REQUIRE(frame.loop_phase.has_value());
    CHECK(frame.loop_phase->numerator == 179);
    CHECK(frame.loop_phase->denominator == 180);

    auto invalid = nlohmann::json::parse(text);
    invalid["shots"][0].erase("loop_frames");
    CHECK_THROWS(bhr::parse_cinematic_v3(invalid.dump()));
    invalid = nlohmann::json::parse(text);
    invalid["emission"]["amplitude"] = .26f;
    CHECK_THROWS(bhr::parse_cinematic_v3(invalid.dump()));
    invalid = nlohmann::json::parse(text);
    invalid["schema_version"] = 2;
    CHECK_THROWS(bhr::parse_cinematic_v3(invalid.dump()));
}
