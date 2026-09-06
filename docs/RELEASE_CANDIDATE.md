# 0.9.0-rc.1 release candidate

This is a local development candidate, not a published v1.0 release.

## Included

- CUDA RK45 renderer and explicitly approximate geokerr alternative.
- Headless PNG CLI, renderer benchmark, SDL2/OpenGL/ImGui workbench, and preview
  benchmark in the GUI archive.
- Reference and three validated prograde gallery presets.
- Runtime, build, validation, API, contribution, conduct, license, and third-
  party documentation.

The archive does not contain the NVIDIA driver, CUDA runtime/toolkit, system
OpenGL libraries, external starfields, source headers, or a supported standalone
C++ SDK/ABI.

## Candidate gates

- [x] Fresh Linux CUDA GUI and headless builds from explicit paths.
- [x] CUDA-free CPU contracts pass.
- [x] Temporary-prefix install and installed CLI execution pass.
- [x] TGZ package and SHA-256 checksum are generated and verified locally.
- [x] Golden PNG and existing image PSNR thresholds remain unchanged.
- [x] Exact Schwarzschild center-ray classification is repaired.
- [ ] All retained dual-integrator ray classifications pass.
- [ ] CUDA sanitizer completes an instrumented run.
- [ ] GitHub hosted CPU workflow is observed after publication.
- [ ] Self-hosted CUDA and CUDA/OpenGL workflows are configured and observed.
- [ ] Windows and macOS are implemented and tested (not current targets).

Because a physics gate remains red, this candidate is useful for testing and
handoff but does **not** meet a fully validated v1.0 release gate.

## Local packaging

```bash
cmake --preset linux-release -B "$HOME/.cache/blackhole-renderer/release" \
  -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build "$HOME/.cache/blackhole-renderer/release" -j
ctest --test-dir "$HOME/.cache/blackhole-renderer/release" --output-on-failure
cpack --config "$HOME/.cache/blackhole-renderer/release/CPackConfig.cmake"
```

CPack writes a versioned TGZ and `.sha256` file under `BUILD/packages/`. Review
the validation failure before deciding whether to publish anything.
