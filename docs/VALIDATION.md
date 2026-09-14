# Validation matrix

This document defines what the 0.9.0-rc.1 candidate checks and what a successful
result means. It does not convert an unavailable GPU check into a pass.

## Verified local environment

The table below records the repaired Plan 6 baseline. Current Plan 7 results
and coverage follow in their own section.

- Ubuntu 24.04.4 x86_64, glibc 2.39
- CMake 3.28.3, Ninja 1.11.1, GCC/G++ 13.3.0
- CUDA Toolkit 12.0.140, NVIDIA driver 595.58.03
- NVIDIA GeForce RTX 3080 12 GB, CUDA architecture 86
- SDL2 2.32.10 and NVIDIA OpenGL on X11 for the GUI build

The earlier Plan 5 GCC/G++ 12.4 build remains valid evidence, but the fresh Plan
6 builds used the environment above. The archive is not claimed to run on older
glibc releases or non-sm_86 GPUs.

| Surface | Command | Hardware | Current local result |
|---|---|---|---|
| CPU contracts | `cmake --workflow --preset cpu-ci` | CPU only | 45/45 passed |
| CUDA headless | `ctest --test-dir BUILD --output-on-failure -LE presentation` | NVIDIA CUDA GPU | 74/74 passed |
| CUDA/OpenGL | `BUILD/tests/bhr_viewport_tests` | NVIDIA GPU + OpenGL 3.3 display | Passed locally |
| Install | `ctest --test-dir BUILD -R install_smoke --output-on-failure` | NVIDIA CUDA runtime | Passed locally |
| Package | `cpack --config BUILD/CPackConfig.cmake` | Build host | Local TGZ and SHA-256 generated |

The full CUDA GUI suite contains 75 registered tests after Plan 6 additions:
all 75 pass. The headless suite has 74 registered tests and all 74 pass. Artifact checksums are
recorded in the Plan 6 handoff because embedding a checksum inside its own
archive would be circular.

## Off-axis physics repair

The exact axial Schwarzschild radial failure remains fixed. The nine off-axis
Kerr rays now have explicit independently checked classifications; both
integrators pass them without skips, tolerances, or allow-lists.

The repair has two causes. RK45 now reflects Kerr polar coordinates at a chart
boundary rather than integrating through theta=0 or pi, and guards a false
inward-branch reversal only inside 1.5 horizon radii. Geokerr now brackets
sign-changing radial roots in the physically reachable interval before event
ordering; float Ferrari roots could otherwise acquire an imaginary component
and conceal a real turning point. The Schwarzschild path is intentionally left
unchanged to preserve its golden byte stream.

The unchanged image gates are:

- Schwarzschild RK45/geokerr PSNR: 27.0318 dB, threshold 25 dB.
- Kerr spin 0.9 PSNR: 29.7607 dB, threshold 15 dB.
- Schwarzschild golden PNG: byte-identical.

## Plan 7 scene/shot foundation — 2026-09-06

Verified implementation: `bf07b1d`. Fresh isolated builds use the environment above,
existing pinned dependencies, and the original numerical/golden tests.

| Surface | Result |
|---|---|
| CUDA-free Debug CPU | 55/55 passed |
| CUDA-free AddressSanitizer + UndefinedBehaviorSanitizer | 55/55 passed |
| Release headless CUDA | 85/85 passed |
| Release GUI/CUDA/OpenGL | 86/86 passed, including presentation |
| Selected-frame CLI | Byte-identical fixed/legacy PNGs; distinct orbit 0/300/599 PNGs |
| Install smoke | Passed in both CUDA builds; shot fixture/docs installed |

Schwarzschild golden bytes and strict off-axis classifications remain unchanged.
Measured dual-integrator PSNR is still 27.0318 dB (25 dB gate) and 29.7607 dB
(15 dB gate). No kernel, `RenderParams` layout, projection or golden asset changed.

Coverage is GCC 13 gcov executable-line coverage from a fresh Debug build with
`BHR_ENABLE_CUDA=OFF`, `--coverage`, and the complete CPU test suite:

| File | Covered lines | Coverage |
|---|---:|---:|
| `lib/src/shot.cpp` | 94/94 | 100% |
| `lib/src/shot_io.cpp` | 161/168 | 95.83% |
| `app/src/shot_options.hpp` | 26/26 | 100% |
| `lib/include/bhr/shot.hpp` | 1/1 | 100% |
| New CPU surface total | 282/289 | **97.58%** |
| Factored existing `lib/src/presets.cpp` | 102/102 | 100% |

This excludes third-party code, test code, the CUDA-dependent CLI render glue,
kernels and GUI. It is not project-wide or GPU coverage. Uncovered lines are
exceptional temporary-stream setup/cleanup and one field-set builder return line; real
partial-write and rename failures are exercised and preserve the old document.

Reproduce the measured CPU build and inspect per-file gcov JSON:

```bash
COVERAGE="$HOME/.cache/blackhole-renderer/plan7-coverage"
cmake -S . -B "$COVERAGE" -G Ninja -DBHR_ENABLE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=--coverage -Wall -Wextra -Wpedantic' \
  -DCMAKE_EXE_LINKER_FLAGS=--coverage
cmake --build "$COVERAGE" -j 4
ctest --test-dir "$COVERAGE" --output-on-failure
mkdir -p "$COVERAGE/report"
(cd "$COVERAGE/report" && gcov -j \
  "$COVERAGE/lib/CMakeFiles/bhr_cpu_support.dir/src/shot.cpp.gcno" \
  "$COVERAGE/lib/CMakeFiles/bhr_cpu_support.dir/src/shot_io.cpp.gcno" \
  "$COVERAGE/lib/CMakeFiles/bhr_cpu_support.dir/src/presets.cpp.gcno" \
  "$COVERAGE/tests/CMakeFiles/bhr_cpu_tests.dir/unit/test_shot.cpp.gcno" \
  "$COVERAGE/tests/CMakeFiles/bhr_cpu_tests.dir/unit/test_shot_cli.cpp.gcno")
```

Use a fresh build directory when measuring another revision to avoid mixing old
`.gcda` profiles. CPU sanitizers used a separate Debug build with
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`.

The headless and GUI suites were run with
`ctest --test-dir "$HOME/.cache/blackhole-renderer/plan7-headless" --output-on-failure`
and the equivalent `plan7-gui` path. GUI configuration used the already installed
SDL2 CMake package; no system packages/toolchains were installed.
[SHOTS.md](SHOTS.md) supplies the exact selected-frame commands and schema.

Compute Sanitizer 2022.4.1 was retried on a fixed selected-frame command with the
existing injection path. It exited 255 before the first instrumented API call
and could not find the exit code. CUDA sanitizer instrumentation remains
**unavailable**, even though CPU sanitizers and actual CUDA/OpenGL tests pass.

Only four 256×144 shot samples plus a legacy parity image were rendered for the
handoff, outside the source tree. The configured HDD mount was absent, so the
existing home cache was used. Static emission, POSIX atomic-save scope without
power-loss durability, and no cross-device bitwise guarantee remain limitations.
No video, cache, cinematic shading or GUI authoring is implemented here.

## Plan 8 cinematic appearance — current evidence

The following is implementation evidence, not final visual acceptance. The Plan 8
design was approved on 2026-09-07; user acceptance of the resulting stills remains
pending. Eleven actual CUDA RK45 captures, their saved v2 inputs, and exact ImGui
sRGB-framebuffer readbacks are available in the [still review board](plan8-review/stills/index.html).
All recorded captures have zero unknown, invalid, and clipped pixels; headless PNG
bytes exactly equal the actual ImGui framebuffer readback. Presentation explicitly
disables `GL_FRAMEBUFFER_SRGB` for the encoded-image draw and restores prior state.

| Surface | Current result | Scope / interpretation |
|---|---:|---|
| CPU contracts | 61/61 passed | CUDA-free Plan 8 test run. |
| CPU new-line coverage | 134/140 = **95.71%** (initial) | Final coverage report pending; neither value is project-wide or GPU coverage. |
| CPU ASan/UBSan | 61/61 passed | Separate CUDA-free sanitizer build. |
| Headless CUDA | 101/101 passed | Includes cinematic implementation checks; see `headless-implementation.log`. |
| GUI CUDA/OpenGL | 102/102 passed | Includes actual presentation; see `gui-implementation.log`. |
| Cinematic installed CLI / install smoke | Passed in headless and GUI builds | Confirms the installed selected-frame cinematic path. |
| CUDA Compute Sanitizer | Unavailable (historic) | Tooling exited before instrumentation in earlier work; it was not rerun or repaired without authorization. |

At 1920×1080, the evidence tool measured 20.0475 ms legacy GPU time, 20.1115 ms
radiance (0.32% shading overhead), 0.0563 ms display, 0.0931 ms bloom, and 20.2475
ms combined glow (two warmups, ten samples; RTX 3080 / CUDA 12.0.140 / driver
595.58.03). This is a measurement of the selected scene, not a 60 FPS claim. The
39,398,416-byte workspace, 8,294,400-byte RGBA target, and 33,554,432-byte sky are
measured allocations. The 106,130,464-byte conservative GUI-resize budget separately
accounts for concurrently retained preview resources; it is below the 268,435,456-byte
hard request limit, not a live-allocation measurement. Process peak RSS was 210,288
KiB.

Known visual/physics limitations remain deliberate: bloom is reviewed disabled and
does not conceal inner-boundary speckle; escape sampling is a finite BL-coordinate
star environment; no ray bundles, thick-volume transport, spectral transport, or
camera optics are modeled. RK45 remains the reference and Geokerr remains approximate.
See [Cinematic appearance](CINEMATIC_APPEARANCE.md) for the exact contract and
[measurement log](plan8-review/evidence/cinematic-measurements.log) for raw evidence.
The final implementation-suite logs are [headless](plan8-review/evidence/headless-implementation.log),
[GUI](plan8-review/evidence/gui-implementation.log), and [physics/image gates](plan8-review/evidence/physics-image-gates.log).

## CI interpretation

`cpu-contracts.yml` runs on a standard Ubuntu hosted runner without CUDA.
`gpu-validation.yml` is manual and requires explicitly labeled self-hosted
NVIDIA runners. The OpenGL job invokes the presentation binary directly so an
environment skip (exit 77) fails the job instead of masquerading as coverage.
No self-hosted runner has been registered and neither workflow has been run on
GitHub as part of local candidate preparation. The pinned Node 24 actions need a
self-hosted Actions Runner at least 2.327.1.

## Sanitizer status

Ordinary tests do not substitute for CUDA memory instrumentation. Compute
Sanitizer 2022.4.1 still exits before instrumentation. With only
`LD_LIBRARY_PATH` it cannot find `libsanitizer-collection.so`; with
`--injection-path /usr/lib/nvidia-cuda-toolkit/compute-sanitizer` it reports
"Target application terminated before first instrumented API call" and cannot
find the exit code. No CUDA sanitizer pass is claimed.
