# Single doctest executable aggregating all unit tests.
add_executable(bhr_tests
    unit/test_main.cpp
    unit/test_hello.cpp
    unit/test_interop.cpp
    unit/test_params.cpp
    unit/test_params_validate.cpp
    unit/test_presets.cpp
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

target_link_libraries(bhr_tests
    PRIVATE
        bhr::lib
        bhr_doctest
        CUDA::cudart
)

# doctest auto-discovery — each TEST_CASE becomes a CTest test.
include(${CMAKE_SOURCE_DIR}/external/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(bhr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
