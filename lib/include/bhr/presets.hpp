#pragma once
/// @file bhr/presets.hpp
/// Shared workbench/CLI presets. JSON schema version 1 requires every field:
/// {"schema_version":1,"spin":0.9,"camera":{"r_cam":30,"theta_cam_deg":85,
/// "phi_cam_deg":0,"fov_deg":35,"width":256,"height":144},"disk":{
/// "r_inner":2.321,"r_outer":20,"peak_temp_K":40000,"brightness":1},
/// "enable_doppler":true,"enable_redshift":true,"enable_beaming":true,
/// "enable_starfield":false,"integrator":"rk45"}.
/// Starfield file paths are external assets, supplied separately by the UI/CLI.

#include "bhr/params.hpp"
#include <string>

namespace bhr {

/// A deterministic reference scene shared by the workbench and benchmark.
RenderParams workbench_preset();

/// Save validated parameters. On failure error contains a user-facing message.
bool save_preset(const RenderParams& params, const std::string& path, std::string& error);

/// Load and validate a complete preset. Leaves out unchanged on any failure.
/// Rejects unknown/missing fields, coercions, non-finite numbers and bad ranges.
bool load_preset(const std::string& path, RenderParams& out, std::string& error);

} // namespace bhr
