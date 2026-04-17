# GUI workbench (existing)
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

set_target_properties(blackhole-workbench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)

# CLI renderer (new)
add_executable(blackhole-cli
    src/cli.cpp
)

target_link_libraries(blackhole-cli
    PRIVATE bhr::lib
)

set_target_properties(blackhole-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app
)
