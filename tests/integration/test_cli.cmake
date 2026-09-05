if(NOT DEFINED CLI OR NOT DEFINED BENCHMARK OR NOT DEFINED TEST_OUTPUT_DIR)
    message(FATAL_ERROR "CLI, BENCHMARK, and TEST_OUTPUT_DIR are required")
endif()
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

function(expect_failure name expected_error)
    set(output "${TEST_OUTPUT_DIR}/${name}.png")
    file(REMOVE "${output}")
    execute_process(COMMAND "${CLI}" ${ARGN} --output "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(status EQUAL 0 OR EXISTS "${output}" OR NOT stderr MATCHES "${expected_error}")
        message(FATAL_ERROR "${name}: status=${status}, expected error '${expected_error}', got: ${stdout}${stderr}")
    endif()
endfunction()

function(expect_render name)
    execute_process(COMMAND "${CLI}" ${ARGN} --output "${TEST_OUTPUT_DIR}/${name}.png"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status EQUAL 0 OR NOT EXISTS "${TEST_OUTPUT_DIR}/${name}.png")
        message(FATAL_ERROR "${name}: render failed (${status}): ${stdout}${stderr}")
    endif()
    if(NOT stderr MATCHES "Rendering 64x36")
        message(FATAL_ERROR "${name}: explicit preview resolution did not apply: ${stderr}")
    endif()
endfunction()

expect_failure(float_suffix "finite number" --spin 0.9junk)
expect_failure(float_nan "finite number" --spin nan)
expect_failure(resolution_suffix "WxH format" --resolution 256x144junk)
expect_failure(resolution_overflow "WxH format" --resolution 4294967552x144)
expect_failure(invalid_disk "Disk outer radius" --disk-inner 25)
expect_failure(missing_preset "Cannot open preset" --params "${TEST_OUTPUT_DIR}/missing.json")
expect_failure(missing_starfield "Failed to load starfield" --starfield "${TEST_OUTPUT_DIR}/missing.exr")
expect_failure(reenabled_starfield "Failed to load starfield" --no-starfield --starfield "${TEST_OUTPUT_DIR}/missing.exr")
expect_failure(invalid_integrator "requires rk45 or geokerr" --integrator other)

set(preset [=[{
  "schema_version": 1,
  "spin": 0.9,
  "camera": {"r_cam":30,"theta_cam_deg":85,"phi_cam_deg":0,"fov_deg":35,"width":256,"height":144},
  "disk": {"r_inner":2.321,"r_outer":20,"peak_temp_K":40000,"brightness":1},
  "enable_doppler":true,"enable_redshift":true,"enable_beaming":true,"enable_starfield":false,
  "integrator":"rk45"
}]=])
file(WRITE "${TEST_OUTPUT_DIR}/reference.json" "${preset}")
expect_render(override_before --resolution 64x36 --params "${TEST_OUTPUT_DIR}/reference.json")
expect_render(override_after --params "${TEST_OUTPUT_DIR}/reference.json" --resolution 64x36)
file(SHA256 "${TEST_OUTPUT_DIR}/override_before.png" before_hash)
file(SHA256 "${TEST_OUTPUT_DIR}/override_after.png" after_hash)
if(NOT before_hash STREQUAL after_hash)
    message(FATAL_ERROR "CLI overrides must produce the same image before or after --params")
endif()
expect_render(disabled_asset --params "${TEST_OUTPUT_DIR}/reference.json" --resolution 64x36
    --starfield "${TEST_OUTPUT_DIR}/missing.exr" --no-starfield)
file(SHA256 "${TEST_OUTPUT_DIR}/disabled_asset.png" disabled_hash)
if(NOT before_hash STREQUAL disabled_hash)
    message(FATAL_ERROR "Disabling a starfield must produce the same image as omitting the asset")
endif()
expect_render(disabled_effects --params "${TEST_OUTPUT_DIR}/reference.json" --resolution 64x36
    --no-doppler --no-redshift --no-beaming)
file(SHA256 "${TEST_OUTPUT_DIR}/disabled_effects.png" effects_hash)
if(before_hash STREQUAL effects_hash)
    message(FATAL_ERROR "Disabling all relativistic effects should change the reference image")
endif()
string(REPLACE "\"enable_starfield\":false" "\"enable_starfield\":true" enabled_preset "${preset}")
file(WRITE "${TEST_OUTPUT_DIR}/enabled-starfield.json" "${enabled_preset}")
expect_failure(preset_without_asset "supply --starfield PATH" --params "${TEST_OUTPUT_DIR}/enabled-starfield.json")
file(WRITE "${TEST_OUTPUT_DIR}/invalid.json" "{\"schema_version\":1.5}")
expect_failure(invalid_preset "Cannot load preset" --params "${TEST_OUTPUT_DIR}/invalid.json")

foreach(invalid_option "--frames;0" "--frames;1junk" "--resolution;64x36junk" "--integrator;other")
    execute_process(COMMAND "${BENCHMARK}" ${invalid_option}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(status EQUAL 0 OR NOT stderr MATCHES "Error:")
        message(FATAL_ERROR "Benchmark accepted '${invalid_option}': ${stdout}${stderr}")
    endif()
endforeach()
message(STATUS "CLI invalid input, preset override parity, disabled assets/effects, and benchmark parsing passed")
