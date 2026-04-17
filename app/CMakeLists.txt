add_executable(blackhole-workbench
    src/main.cpp
)

target_link_libraries(blackhole-workbench
    PRIVATE
        bhr::lib
        bhr_imgui
        bhr_glad
        SDL2::SDL2
        SDL2::SDL2main
        OpenGL::GL
)

# Keep the binary next to the source tree for easy invocation
set_target_properties(blackhole-workbench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)
