# Validation matrix

This document defines what the 0.9.0-rc.1 candidate checks and what a successful
result means. It does not convert an unavailable GPU check into a pass.

## Verified local environment

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
find the exit code. No sanitizer pass is claimed.
