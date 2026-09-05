#pragma once
#include "state.hpp"
#include "viewport.hpp"
#include "bhr/starfield.hpp"
#include <string>

namespace bhr::workbench {

struct Controls {
    char preset_path[1024] = "scene.json";
    char starfield_path[1024] = "";
    bool continuous = false;
    std::string message;
};

State draw_controls(const State& state, Controls& controls,
                    const Viewport& viewport, Starfield& starfield,
                    const char* device_name);
void draw_viewport(const State& state, const Viewport& viewport);

} // namespace bhr::workbench
