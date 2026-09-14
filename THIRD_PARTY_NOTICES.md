# Third-party notices

The source tree vendors pinned revisions. The release archive contains their
compiled code where noted, but does not redistribute the NVIDIA driver, CUDA
runtime/toolkit, system OpenGL libraries, or an external EXR starfield.

| Dependency | Pinned revision/version | Use | License |
|---|---|---|---|
| Dear ImGui | `6f7b5d0ee2fe9948ab871a530888a6dc5c960700` | GUI, compiled into workbench | MIT |
| TinyEXR | `6c8742cc8145c8f629698cd8248900990946d6b1` | EXR loading, compiled into binaries | BSD-3-Clause; includes BSD-3-Clause OpenEXR code |
| miniz | TinyEXR vendored revision | PNG/EXR compression support, compiled into binaries | MIT |
| stb | `f0569113c93ad095470c54bf34a17b36646bbbb5` | PNG writing, compiled into binaries | MIT or public domain; this distribution uses MIT |
| nlohmann/json | `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` | Preset parsing, compiled into binaries | MIT |
| doctest | `ae7a13539fb71f270b87eb2e874fbac80bc8dda2` | Tests only | MIT |
| glad | generated with 0.1.36 | OpenGL loader, compiled into GUI binaries | Generated code offered as public domain, WTFPL, or CC0; Khronos specification notices may also apply |
| Khronos platform header | EGL Registry commit recorded in `khrplatform.h` | OpenGL platform types compiled into GUI binaries | MIT |
| SDL2 | 2.32.10 at vcpkg baseline `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4`; locally verified with 2.32.10 | Window/input layer, linked according to the selected SDL2 package | zlib |

The package installs the complete upstream notices for ImGui, stb,
nlohmann/json, doctest, TinyEXR/OpenEXR, miniz, Khronos, and SDL2 in
`share/doc/blackhole-renderer/licenses/`. The source copies are also retained in
their vendored files. SDL's notice must remain with any separately redistributed
SDL binary.

The black-hole renderer itself remains licensed under [MIT](LICENSE).

## Plan 8 authored asset

`presets/cinematic/assets/stars-v1.exr` is an original deterministic sparse-star
environment authored for this repository and distributed under this project's MIT
license. It is not an external sky, film frame, NASA image, or downloaded runtime
asset. The Plan 8 review-board references are editorial links only and are not bundled
or used by the renderer.
