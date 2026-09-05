# Plan 5 validation — 2026-09-05

- Release build succeeds with GCC 12.4 / CUDA 12.0.140 / sm_86 and cached SDL2 2.32.10.
- Headless build succeeds with `BHR_BUILD_APP=OFF`, no SDL2 or OpenGL discovery.
- Full CTest: **70/72 pass** (2.93 s). Baseline before implementation: **42/44 pass**.
- The two failures are unchanged: the dual-integrator Schwarzschild center ray
  (RK45 escape versus geokerr horizon) and off-axis ray classification (5/9 agree).
- Golden Schwarzschild PNG remains byte-identical. Existing Schwarzschild
  ≥25 dB and Kerr ≥15 dB image-agreement gates pass without changes.
- Both integrators produce byte-identical host/device output. Tests also cover
  capacity/guard bytes, invalid dispatch, stale output, all effect toggles, and
  synthetic HDR starfield sampling.
- Hidden SDL/CUDA/OpenGL integration runs successfully: completed-texture parity,
  persistent texture reuse, resize, retention of the previous image, graphics
  failure handling, active-render shutdown, and GL upload-state restoration.
- CPU tests cover strict preset schema/type/duplicate-key checks, no partial state
  replacement, malformed CLI inputs, queued edits, menu visibility, and missing
  starfield assets. CLI process tests verify preset override order and effect flags.
- An R-only EXR fixture reproduced a segmentation fault before loader changes.
  The repaired loader passes 4 cases / 72 assertions covering channel/format
  rejection, finite radiance, HALF/FLOAT files, thin-image downsampling, and ownership.
- Known-good 512×288 RK45 output was rendered and visually inspected.
- CUDA memory instrumentation was **not completed**: the installed
  `compute-sanitizer` first could not locate its injection library. Supplying
  `/usr/lib/nvidia-cuda-toolkit/compute-sanitizer` and retrying with
  `--target-processes all` still failed before the first instrumented API call
  (exit 255, “couldn't find exit code”). No sanitizer pass is claimed; no system
  packages were installed or replaced.
- Final focused CPU suite: 23 cases / 674 assertions. gcov line coverage: params.hpp
  97.22%, redshift.hpp 100%, presets.cpp 100%, cli_options.hpp 100%, and
  workbench/state.hpp 97.22%. This is scoped CPU coverage, not GPU/UI coverage.
- No full-project coverage percentage or manual visible-window QA pass is claimed.
  The automated presentation benchmark measures real GL/ImGui work in a hidden
  window and reports UI event latency separately from renderer time.

Build artifacts and complete local validation logs:
`~/.cache/blackhole-renderer/plan5-build/evidence/`.
The original temporary build was cleared during the session; the final build and
measurement logs were regenerated in this persistent cache. The user-owned `build`
symlink and `imgui.ini` remain unchanged. No npm/Python dependencies are involved;
no npm/pip audit applies to this C++/CUDA-only change.
