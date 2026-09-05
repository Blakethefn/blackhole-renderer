#include "doctest.h"
#include "workbench/state.hpp"

using namespace bhr::workbench;

TEST_CASE("workbench submits once and keeps changes made during a render") {
    const State initial{};
    REQUIRE(can_submit(initial));
    const auto running = submitted(initial);
    CHECK_FALSE(can_submit(running));
    auto edit = running.params;
    edit.spin = 0.8f;
    edit.disk.r_inner = bhr::r_isco(edit.spin);
    const auto edited = changed(running, edit);
    CHECK_FALSE(can_submit(edited));
    const auto finished = completed(edited);
    CHECK(finished.displayed.spin == initial.params.spin);
    CHECK(finished.params.spin == edit.spin);
    CHECK(can_submit(finished));
    const auto current = completed(submitted(finished));
    CHECK_FALSE(can_submit(current));
    CHECK(current.displayed.spin == edit.spin);
}

TEST_CASE("workbench rejects invalid edits and needs an explicit retry after failure") {
    const State initial{};
    auto invalid = initial.params;
    invalid.camera.width = 0;
    const auto bad = changed(initial, invalid);
    CHECK_FALSE(can_submit(bad));
    const auto failed = failed_render(submitted(initial), "CUDA test failure");
    CHECK_FALSE(failed.rendering);
    CHECK_FALSE(can_submit(failed));
    CHECK(failed.error == "CUDA test failure");
    CHECK(can_submit(requested(failed)));
}

TEST_CASE("workbench UI visibility does not invalidate a completed scene") {
    const auto ready = completed(submitted(State{}));
    const auto hidden = controls_toggled(ready);
    CHECK(hidden.show_controls != ready.show_controls);
    CHECK_FALSE(can_submit(hidden));
    CHECK(hidden.displayed.camera.width == ready.displayed.camera.width);
}

TEST_CASE("workbench failed update preserves the last completed snapshot") {
    const auto ready = completed(submitted(State{}));
    auto edit = ready.params;
    edit.camera.width = 512;
    const auto failure = failed_render(submitted(changed(ready, edit)), "resource failure");
    CHECK(failure.has_image);
    CHECK(failure.displayed.camera.width == ready.displayed.camera.width);
    CHECK_FALSE(can_submit(failure));
}

TEST_CASE("workbench requires the asset requested by a preset before dispatch") {
    const State initial{};
    auto params = initial.params;
    params.enable_starfield = true;
    const auto needs_asset = changed(initial, params);
    CHECK_FALSE(can_submit(needs_asset));
    CHECK(can_submit(needs_asset, true));
}
