#include "doctest.h"
#include "../../app/src/shot_options.hpp"

TEST_CASE("shot CLI selection is complete unique and incompatible with legacy options") {
    using Options = std::vector<std::pair<std::string, std::string>>;
    const Options valid{{"--shot-file", "scene.json"}, {"--shot-id", "fixed"}, {"--frame", "0"}, {"--output", "frame.png"}};
    const auto selected = bhr::cli::shot_selection(valid);
    REQUIRE(selected.has_value());
    CHECK(selected->file == "scene.json");
    CHECK(selected->id == "fixed");
    CHECK(selected->frame == 0);
    CHECK(selected->output == "frame.png");
    CHECK_FALSE(bhr::cli::shot_selection({{"--spin", "0.9"}, {"--output", "a"}, {"--output", "b"}}));
    CHECK_FALSE(bhr::cli::shot_selection({}));
    for (size_t i = 0; i < valid.size(); ++i) {
        auto missing = valid;
        missing.erase(missing.begin() + i);
        CHECK_THROWS(bhr::cli::shot_selection(missing));
        auto repeated = valid;
        repeated.push_back(valid[i]);
        CHECK_THROWS(bhr::cli::shot_selection(repeated));
        for (const char* value : {"", "--shot-id"}) {
            auto empty = valid;
            empty[i].second = value;
            CHECK_THROWS(bhr::cli::shot_selection(empty));
        }
    }
    for (const char* option : {"--params", "--spin", "--resolution", "--inclination", "--azimuth", "--fov", "--distance",
        "--disk-inner", "--disk-outer", "--disk-temp", "--brightness", "--starfield", "--no-starfield",
        "--no-doppler", "--no-redshift", "--no-beaming", "--integrator"}) {
        auto mixed = valid;
        mixed.push_back({option, "anything"});
        CHECK_THROWS(bhr::cli::shot_selection(mixed));
    }
    for (const char* frame : {"-1", "+1", "1.0", "1x", " 1", "1 ", "18446744073709551616"}) {
        auto malformed = valid;
        malformed[2].second = frame;
        CHECK_THROWS(bhr::cli::shot_selection(malformed));
    }
    auto maximum = valid;
    maximum[2].second = "18446744073709551615";
    CHECK(bhr::cli::shot_selection(maximum)->frame == UINT64_MAX); // evaluator rejects range
}
