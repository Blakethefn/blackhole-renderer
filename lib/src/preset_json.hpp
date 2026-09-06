#pragma once
// Internal value codec shared by strict v1 presets and nested cinematic scenes.
// Not a public dependency or a second physical validation authority.
#include "bhr/params.hpp"
#include <nlohmann/json.hpp>
namespace bhr::detail {
nlohmann::json preset_to_json(const RenderParams& params);
RenderParams preset_from_json(const nlohmann::json& json);
}
