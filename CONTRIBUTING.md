# Contributing

This project welcomes focused fixes, tests, documentation improvements, and
well-supported rendering research. It is a CUDA-first Linux project; Windows,
macOS, CPU rendering, animation, and new physics models are not current release
targets.

## Before opening a change

1. Search existing issues and code for prior work.
2. Explain the physical or software contract being changed.
3. Add a failing regression test before the implementation when practical.
4. Keep RK45 as the reference integrator unless independent evidence supports a
   different decision. Geokerr remains approximate.
5. Do not weaken PSNR thresholds or replace golden images only to accept a new
   result. A changed golden needs an explained correctness argument and review.

## Local checks

The portable, CUDA-free contract suite is the fastest starting point:

```bash
cmake --workflow --preset cpu-ci
```

On a Linux NVIDIA system, configure a fresh CUDA build and run all tests:

```bash
cmake --preset linux-release -B "$HOME/.cache/blackhole-renderer/contrib" \
  -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build "$HOME/.cache/blackhole-renderer/contrib" -j
ctest --test-dir "$HOME/.cache/blackhole-renderer/contrib" --output-on-failure
```

Use `linux-headless` when SDL2/OpenGL are unavailable. Presentation tests need a
real OpenGL 3.3 display and CUDA/OpenGL interop on the same NVIDIA GPU. State any
checks you could not run. See [validation](docs/VALIDATION.md) for the current
development candidate's physics and platform evidence.

## Code and review expectations

- C++17/CUDA 12.x, clear names, shallow control flow, and explicit errors.
- Validate external input at CLI, preset, file, CUDA, and graphics boundaries.
- Do not add secrets, telemetry, downloaded assets, or new dependencies without
  a concrete need and license review.
- Preserve caller ownership and asynchronous lifetime contracts in the renderer
  and viewport APIs.
- Use focused conventional commits such as `fix:`, `feat:`, `test:`, or `docs:`.

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
