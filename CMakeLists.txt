cmake_minimum_required(VERSION 3.25)

option(BHR_ENABLE_CUDA "Build the CUDA renderer and command-line applications" ON)

set(BHR_VERSION "0.9.0")
set(BHR_CANDIDATE_SUFFIX "rc.1")

# Toolchain file from vcpkg (set via preset or -DCMAKE_TOOLCHAIN_FILE)
if(BHR_ENABLE_CUDA AND DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

if(BHR_ENABLE_CUDA)
    project(blackhole-renderer VERSION ${BHR_VERSION} LANGUAGES C CXX CUDA)
else()
    project(blackhole-renderer VERSION ${BHR_VERSION} LANGUAGES C CXX)
endif()

# --- Global settings ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(BHR_ENABLE_CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

    # The candidate defaults to the verified RTX 3080 architecture. Users can
    # pass CMAKE_CUDA_ARCHITECTURES explicitly for another NVIDIA GPU.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 86)
    endif()
endif()

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options -----------------------------------------------------------------
option(BHR_BUILD_TESTS "Build unit tests" ON)
option(BHR_BUILD_APP "Build the GUI workbench" ON)
if(NOT BHR_ENABLE_CUDA AND BHR_BUILD_APP)
    message(STATUS "Disabling BHR_BUILD_APP because BHR_ENABLE_CUDA is OFF")
    set(BHR_BUILD_APP OFF CACHE BOOL "Build the GUI workbench" FORCE)
endif()

# --- External dependencies ---------------------------------------------------
if(BHR_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()
if(BHR_ENABLE_CUDA AND BHR_BUILD_APP)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

# Header-only or vendored externals — add them as INTERFACE targets
add_library(bhr_stb INTERFACE)
target_include_directories(bhr_stb INTERFACE ${CMAKE_SOURCE_DIR}/external/stb)

if(BHR_ENABLE_CUDA)
    add_library(bhr_tinyexr STATIC
        ${CMAKE_SOURCE_DIR}/external/tinyexr/deps/miniz/miniz.c
    )
    target_include_directories(bhr_tinyexr PUBLIC
        ${CMAKE_SOURCE_DIR}/external/tinyexr
        ${CMAKE_SOURCE_DIR}/external/tinyexr/deps/miniz
    )
    set_target_properties(bhr_tinyexr PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(bhr_json INTERFACE)
target_include_directories(bhr_json INTERFACE ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include)

add_library(bhr_doctest INTERFACE)
target_include_directories(bhr_doctest INTERFACE ${CMAKE_SOURCE_DIR}/external/doctest/doctest)

# GUI dependencies are unnecessary for headless rendering and core tests.
if(BHR_ENABLE_CUDA AND BHR_BUILD_APP)
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
endif()

# --- Subdirectories ----------------------------------------------------------
add_subdirectory(lib)

if(BHR_ENABLE_CUDA)
    add_subdirectory(app)
endif()

if(BHR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BHR_ENABLE_CUDA)
    include(GNUInstallDirs)
    install(FILES presets/shots/reference.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets/shots)
    install(FILES presets/cinematic/scene.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets/cinematic)
    install(FILES presets/cinematic/scene-animated.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets/cinematic)
    install(FILES presets/cinematic/assets/stars-v1.exr presets/cinematic/assets/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets/cinematic/assets)
    install(FILES presets/reference.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets)
    install(FILES
        presets/gallery/schwarzschild.json
        presets/gallery/kerr-moderate.json
        presets/gallery/kerr-high-prograde.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/blackhole-renderer/presets/gallery)
    install(FILES
        README.md
        LICENSE
        CONTRIBUTING.md
        CODE_OF_CONDUCT.md
        THIRD_PARTY_NOTICES.md
        docs/SHOTS.md
        docs/CINEMATIC_APPEARANCE.md
        docs/ANIMATED_EMISSION.md
        docs/PUBLIC_API.md
        docs/RELEASE_CANDIDATE.md
        docs/VALIDATION.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES benchmarks/README.md benchmarks/validation.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/benchmarks)
    install(FILES
        gallery/README.md
        gallery/workbench.png
        gallery/schwarzschild.png
        gallery/kerr-moderate.png
        gallery/kerr-high-prograde.png
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/gallery)
    install(FILES external/imgui/LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
        RENAME imgui-MIT.txt)
    install(FILES external/stb/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
        RENAME stb-MIT-or-public-domain.txt)
    install(FILES external/nlohmann_json/LICENSE.MIT
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
        RENAME nlohmann-json-MIT.txt)
    install(FILES external/doctest/LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
        RENAME doctest-MIT.txt)
    install(FILES
        docs/licenses/TinyEXR-and-OpenEXR-BSD-3-Clause.txt
        docs/licenses/Khronos-MIT.txt
        docs/licenses/miniz-MIT.txt
        docs/licenses/SDL2-zlib.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)

    set(CPACK_PACKAGE_NAME "blackhole-renderer")
    set(CPACK_PACKAGE_VENDOR "Blake Paulson")
    set(CPACK_PACKAGE_CONTACT "Blake Paulson")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CUDA Kerr black-hole renderer development candidate")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}-${BHR_CANDIDATE_SUFFIX}")
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" BHR_PACKAGE_SYSTEM)
    string(REPLACE ";" "-sm" BHR_PACKAGE_CUDA_ARCHITECTURES
        "${CMAKE_CUDA_ARCHITECTURES}")
    set(BHR_PACKAGE_PLATFORM
        "${BHR_PACKAGE_SYSTEM}-${CMAKE_SYSTEM_PROCESSOR}-cuda${CUDAToolkit_VERSION_MAJOR}-sm${BHR_PACKAGE_CUDA_ARCHITECTURES}")
    if(BHR_BUILD_APP)
        set(BHR_PACKAGE_FLAVOR "${BHR_PACKAGE_PLATFORM}")
    else()
        set(BHR_PACKAGE_FLAVOR "${BHR_PACKAGE_PLATFORM}-headless")
    endif()
    set(CPACK_PACKAGE_FILE_NAME
        "blackhole-renderer-${CPACK_PACKAGE_VERSION}-${BHR_PACKAGE_FLAVOR}")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
    set(CPACK_GENERATOR TGZ)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()

# --- Summary -----------------------------------------------------------------
message(STATUS "----------------------------------------")
message(STATUS "blackhole-renderer ${CMAKE_PROJECT_VERSION}")
message(STATUS "  C++ standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  Candidate:        ${PROJECT_VERSION}-${BHR_CANDIDATE_SUFFIX}")
message(STATUS "  CUDA enabled:     ${BHR_ENABLE_CUDA}")
if(BHR_ENABLE_CUDA)
    message(STATUS "  CUDA arch:        ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:            ${BHR_BUILD_TESTS}")
message(STATUS "  App:              ${BHR_BUILD_APP}")
message(STATUS "----------------------------------------")
