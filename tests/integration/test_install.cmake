if(NOT DEFINED BHR_BUILD_DIR OR NOT DEFINED BHR_INSTALL_ROOT)
  message(FATAL_ERROR "Install smoke test is missing a required path")
endif()

string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef install_id)
set(BHR_INSTALL_PREFIX "${BHR_INSTALL_ROOT}/${install_id}")
set(BHR_INSTALLED_CLI "${BHR_INSTALL_PREFIX}/bin/blackhole-cli")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BHR_BUILD_DIR}" --prefix "${BHR_INSTALL_PREFIX}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Install failed (${install_result})\n${install_output}\n${install_error}")
endif()

execute_process(
  COMMAND "${BHR_INSTALLED_CLI}" --help
  WORKING_DIRECTORY "${BHR_INSTALL_PREFIX}"
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_output
  ERROR_VARIABLE cli_error)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "Installed CLI failed (${cli_result})\n${cli_output}\n${cli_error}")
endif()
if(NOT "${cli_output}${cli_error}" MATCHES "blackhole-cli")
  message(FATAL_ERROR
    "Installed CLI help did not contain its program name\n${cli_output}\n${cli_error}")
endif()

foreach(required_path
    "${BHR_INSTALL_PREFIX}/share/blackhole-renderer/presets/reference.json"
    "${BHR_INSTALL_PREFIX}/share/blackhole-renderer/presets/shots/reference.json"
    "${BHR_INSTALL_PREFIX}/share/blackhole-renderer/presets/cinematic/scene.json"
    "${BHR_INSTALL_PREFIX}/share/blackhole-renderer/presets/cinematic/assets/stars-v1.exr"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/CINEMATIC_APPEARANCE.md"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/SHOTS.md"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/README.md"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/benchmarks/README.md"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/licenses/TinyEXR-and-OpenEXR-BSD-3-Clause.txt"
    "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/LICENSE")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Installed candidate is missing ${required_path}")
  endif()
endforeach()

execute_process(
  COMMAND "${BHR_INSTALLED_CLI}" --shot-file share/blackhole-renderer/presets/cinematic/scene.json
    --shot-id fixed --frame 0 --output installed-cinematic.png
  WORKING_DIRECTORY "${BHR_INSTALL_PREFIX}"
  RESULT_VARIABLE cinematic_result ERROR_VARIABLE cinematic_error TIMEOUT 30)
if(NOT cinematic_result EQUAL 0 OR NOT EXISTS "${BHR_INSTALL_PREFIX}/installed-cinematic.png")
  message(FATAL_ERROR "Installed cinematic frame failed: ${cinematic_error}")
endif()

file(READ "${BHR_INSTALL_PREFIX}/share/doc/blackhole-renderer/README.md" installed_readme)
if(NOT installed_readme MATCHES "## Quick start")
  message(FATAL_ERROR "Installed README is not the project quick-start document")
endif()

file(REMOVE_RECURSE "${BHR_INSTALL_PREFIX}")
