if(NOT BHR_ENABLE_CUDA)
    add_library(bhr_cpu_support STATIC
        src/appearance.cpp
        src/emission.cpp
        src/image.cpp
        src/presets.cpp
        src/shot.cpp
        src/shot_io.cpp
    )
    target_include_directories(bhr_cpu_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(bhr_cpu_support PUBLIC bhr_stb PRIVATE bhr_json)
    add_library(bhr::cpu_support ALIAS bhr_cpu_support)
    return()
endif()

add_library(libblackhole STATIC
    src/hello.cu
    src/appearance.cpp
    src/emission.cpp
    src/image.cpp
    src/geodesic_rk45.cu
    src/geodesic_geokerr.cu
    src/cinematic_renderer.cu
    src/renderer.cu
    src/starfield.cu
    src/presets.cpp
    src/shot.cpp
    src/shot_io.cpp
)

# Output name: "blackhole" → libblackhole.a
set_target_properties(libblackhole PROPERTIES
    OUTPUT_NAME blackhole
    POSITION_INDEPENDENT_CODE ON
    CUDA_SEPARABLE_COMPILATION ON
)

target_include_directories(libblackhole
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(libblackhole PUBLIC bhr_stb bhr_tinyexr CUDA::cudart PRIVATE bhr_json)

# Alias for downstream use
add_library(bhr::lib ALIAS libblackhole)
