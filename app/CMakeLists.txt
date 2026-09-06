# Persistent presentation is also used by the hidden-window integration test.
if(BHR_BUILD_APP)
add_library(bhr_workbench_viewport STATIC src/workbench/viewport.cpp)
target_include_directories(bhr_workbench_viewport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bhr_workbench_viewport PUBLIC bhr::lib bhr_glad OpenGL::GL)

add_executable(blackhole-workbench
    src/main.cpp
    src/workbench/ui_panels.cpp
)

target_link_libraries(blackhole-workbench
    PRIVATE
        bhr::lib
        bhr_workbench_viewport
        bhr_imgui
        bhr_glad
        SDL2::SDL2
        SDL2::SDL2main
        OpenGL::GL
)

set_target_properties(blackhole-workbench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)

add_executable(blackhole-preview-benchmark src/preview_benchmark.cpp)
target_link_libraries(blackhole-preview-benchmark PRIVATE bhr_workbench_viewport bhr_imgui SDL2::SDL2 SDL2::SDL2main)
set_target_properties(blackhole-preview-benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)
endif()

# The headless CLI remains available with BHR_BUILD_APP=OFF.
add_executable(blackhole-cli
    src/cli.cpp
)

target_link_libraries(blackhole-cli
    PRIVATE bhr::lib
)

set_target_properties(blackhole-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)

add_executable(blackhole-benchmark src/benchmark.cpp)
target_link_libraries(blackhole-benchmark PRIVATE bhr::lib CUDA::cudart)
set_target_properties(blackhole-benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)

include(GNUInstallDirs)
install(TARGETS blackhole-cli blackhole-benchmark
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(BHR_BUILD_APP)
    install(TARGETS blackhole-workbench blackhole-preview-benchmark
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
