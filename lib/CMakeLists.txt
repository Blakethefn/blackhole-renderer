add_library(libblackhole STATIC
    src/hello.cu
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

# Alias for downstream use
add_library(bhr::lib ALIAS libblackhole)
