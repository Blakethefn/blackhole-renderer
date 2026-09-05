# Single doctest executable aggregating all unit tests.
add_executable(bhr_tests
    unit/test_main.cpp
    unit/test_hello.cpp
    unit/test_interop.cpp
    unit/test_params.cpp
    unit/test_params_validate.cpp
    unit/test_presets.cpp
    unit/test_cli_options.cpp
    unit/test_workbench_state.cpp
    unit/test_starfield.cpp
    unit/test_image.cpp
    unit/test_kerr.cpp
    unit/test_geodesic.cpp
    unit/test_render.cpp
    unit/test_blackbody.cpp
    unit/test_redshift.cpp
    unit/test_disk_physics.cpp
    unit/test_elliptic.cpp
    unit/test_ferrari.cpp
    unit/test_geokerr.cpp
    integration/test_golden_schwarzschild.cpp
    integration/test_dual_integrator_rays.cpp
    integration/test_dual_integrator_psnr.cpp
    integration/test_workbench_render_parity.cpp
)
target_include_directories(bhr_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/src)

target_link_libraries(bhr_tests
    PRIVATE
        bhr::lib
        bhr_doctest
        CUDA::cudart
)

# doctest auto-discovery — each TEST_CASE becomes a CTest test.
include(${CMAKE_SOURCE_DIR}/external/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(bhr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME cli_contract COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:blackhole-cli>
    -DBENCHMARK=$<TARGET_FILE:blackhole-benchmark>
    -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-contract
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_cli.cmake)
set_tests_properties(cli_contract PROPERTIES TIMEOUT 120)

if(BHR_BUILD_APP)
    add_executable(bhr_viewport_tests integration/test_viewport.cpp)
    target_link_libraries(bhr_viewport_tests PRIVATE bhr_workbench_viewport bhr_doctest SDL2::SDL2)
    add_test(NAME workbench_viewport COMMAND bhr_viewport_tests)
    set_tests_properties(workbench_viewport PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
