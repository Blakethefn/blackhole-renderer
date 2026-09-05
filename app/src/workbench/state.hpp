#pragma once

#include "bhr/params.hpp"
#include "bhr/presets.hpp"
#include <cstdint>
#include <string>

namespace bhr::workbench {

// CPU-only immutable transitions. A submitted snapshot stays fixed while edits
// coalesce into the next revision; completion cannot discard newer UI edits.
struct State {
    RenderParams params = workbench_preset();
    RenderParams in_flight{};
    RenderParams displayed{};
    std::uint64_t revision = 1;
    std::uint64_t submitted_revision = 0;
    bool rendering = false;
    bool has_image = false;
    bool show_controls = false;
    std::string error;
};

inline bool can_submit(const State& state, bool starfield_available = false) {
    return !state.rendering && state.revision != state.submitted_revision
        && validation_error(state.params) == nullptr
        && (!state.params.enable_starfield || starfield_available);
}

inline State requested(const State& state) {
    auto next = state;
    ++next.revision;
    next.error.clear();
    return next;
}

inline State changed(const State& state, const RenderParams& params) {
    auto next = requested(state);
    next.params = params;
    return next;
}

inline State submitted(const State& state) {
    auto next = state;
    next.in_flight = state.params;
    next.submitted_revision = state.revision;
    next.rendering = true;
    next.error.clear();
    return next;
}

inline State completed(const State& state) {
    auto next = state;
    next.displayed = state.in_flight;
    next.rendering = false;
    next.has_image = true;
    return next;
}

inline State failed_render(const State& state, const std::string& error) {
    auto next = state;
    next.rendering = false;
    next.error = error;
    return next;
}

inline State controls_toggled(const State& state) {
    auto next = state;
    next.show_controls = !state.show_controls;
    return next;
}

} // namespace bhr::workbench
