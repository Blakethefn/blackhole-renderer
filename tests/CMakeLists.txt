# Single doctest executable aggregating all unit tests.
add_executable(bhr_tests
    unit/test_main.cpp
    unit/test_hello.cpp
    unit/test_interop.cpp
    unit/test_params.cpp
    unit/test_image.cpp
    unit/test_kerr.cpp
    unit/test_geodesic.cpp
    unit/test_render.cpp
    integration/test_golden_schwarzschild.cpp
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
