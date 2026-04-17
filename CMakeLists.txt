cmake_minimum_required(VERSION 3.24)

# Toolchain file from vcpkg (set via preset or -DCMAKE_TOOLCHAIN_FILE)
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(blackhole-renderer LANGUAGES C CXX CUDA)

# --- Global settings ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

# Ampere (RTX 3080) + PTX fallback for forward compat
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
endif()

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options -----------------------------------------------------------------
option(BHR_BUILD_TESTS "Build unit tests" ON)
option(BHR_BUILD_APP "Build the GUI workbench" ON)

# --- External dependencies ---------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# Header-only or vendored externals — add them as INTERFACE targets
add_library(bhr_stb INTERFACE)
target_include_directories(bhr_stb INTERFACE ${CMAKE_SOURCE_DIR}/external/stb)

add_library(bhr_tinyexr INTERFACE)
target_include_directories(bhr_tinyexr INTERFACE ${CMAKE_SOURCE_DIR}/external/tinyexr)

add_library(bhr_json INTERFACE)
target_include_directories(bhr_json INTERFACE ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include)

add_library(bhr_doctest INTERFACE)
target_include_directories(bhr_doctest INTERFACE ${CMAKE_SOURCE_DIR}/external/doctest/doctest)

# glad (vendored, compiled)
add_library(bhr_glad STATIC ${CMAKE_SOURCE_DIR}/external/glad/src/glad.c)
target_include_directories(bhr_glad PUBLIC ${CMAKE_SOURCE_DIR}/external/glad/include)

# Dear ImGui (compiled here — no CMakeLists in upstream)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(bhr_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(bhr_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(bhr_imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# --- Subdirectories ----------------------------------------------------------
add_subdirectory(lib)

if(BHR_BUILD_APP)
    add_subdirectory(app)
endif()

if(BHR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Summary -----------------------------------------------------------------
message(STATUS "----------------------------------------")
message(STATUS "blackhole-renderer ${CMAKE_PROJECT_VERSION}")
message(STATUS "  C++ standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA arch:        ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:            ${BHR_BUILD_TESTS}")
message(STATUS "  App:              ${BHR_BUILD_APP}")
message(STATUS "----------------------------------------")
