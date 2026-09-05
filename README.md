# Black Hole Renderer

CUDA Kerr black-hole renderer with an SDL2/OpenGL/Dear ImGui workbench and a
headless PNG CLI. The workbench uses the same render kernel as the CLI.
RK45 is the default reference integrator; geokerr is an experimental approximate
alternative. This remains a development renderer with known physics limitations.

## Workbench

Run `blackhole-workbench` from the build's `app/` directory. The rendered image
fills the viewport. **F1** opens or hides the floating **Renderer Controls** menu;
the image remains visible with the menu hidden.

- Camera distance, inclination, azimuth, field of view, and black-hole spin.
- Disk radii, temperature, brightness, Doppler shift, time dilation/redshift,
  and relativistic beaming. Spin edits raise the disk inner radius to ISCO when needed.
- Preview sizes: 256×144, 512×288, 1024×576, and 1920×1080.
- RK45 or explicitly labelled approximate geokerr integration.
- HDR EXR starfield loading and sampling toggle; JSON preset save/load by path.
- Known-good preset, explicit re-render, and optional continuous preview.

**R** resets the scene, **Space** switches integrators, **1–4** select preview
resolution, and **Esc** exits. Scene shortcuts respect text-entry focus; F1
always toggles the controls. Invalid scene combinations display a reason and
retain the last completed image. A preset requesting a starfield waits for its
asset or for sampling to be disabled.

Rendering runs only when the scene changes or continuous preview is enabled.
The UI reports rendering status, the completed image's resolution/integrator,
CUDA-event kernel time, and time from submission to completed presentation
texture. UI FPS is reported separately. The target is at least 3 completed
preview frames/s; reproducible measurements are in [benchmarks](benchmarks/README.md).

## Architecture and API

`bhr::render_device(params, starfield, pixels, capacity_bytes, stream)` validates
and asynchronously launches into caller-owned packed RGBA8 CUDA memory. It does
not allocate, synchronize, or copy pixels. Its CUDA status covers validation and
launch errors; the caller must check stream/event completion for execution errors.
Keep the buffer mapped/alive and the starfield alive until completion. See
[renderer.hpp](lib/include/bhr/renderer.hpp) for the complete contract.

The blocking `bhr::render(params, Image&)` and starfield overload call this same
core, then copy to host. Failures leave an empty image and report a diagnostic.
`RenderParams` remains trivially copyable; validation, invalidation revisions,
in-flight snapshots, and display status are separated in the workbench.

The viewport owns a persistent CUDA-registered pixel-unpack buffer and two GL
textures. It polls a CUDA completion event before unmapping, then polls the GL
upload fence before publishing the new texture. OpenGL draws the previous
completed texture while CUDA works. Normal steady-state frames perform no
allocation or GPU-to-CPU copy. Resizing waits for the current submission and
retains the previous image until the replacement is complete. Shutdown drains
work and releases resources before the GL context and starfield are destroyed.

## Build

Requires Linux, an NVIDIA GPU, CUDA Toolkit 12.x, CMake 3.24+, Ninja, a compatible
C++17 compiler, and initialized vendored submodules. SDL2 development files and
OpenGL are required for the GUI; the CLI/core tests can build without them.

With vcpkg configured:

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default -j
ctest --preset default
```

Or use already installed dependencies and a separate build directory:

```bash
cmake -S . -B /path/to/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=86 \
  -DCMAKE_TOOLCHAIN_FILE= -DCMAKE_PREFIX_PATH=/path/to/sdl2
cmake --build /path/to/build -j
ctest --test-dir /path/to/build --output-on-failure
```

CUDA 12.0 needs a supported host compiler; on this workstation specify
`-DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12
-DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12`.
For headless builds add `-DBHR_BUILD_APP=OFF`; `blackhole-cli`,
`blackhole-benchmark`, and core/CLI tests remain available without SDL2/OpenGL.

The local `build` symlink and HDD vcpkg path may be unavailable. Do not run
`setup.sh` to repair them implicitly. Plan 5 verification used existing cached
SDL2 2.32.10 files and an isolated build at
`~/.cache/blackhole-renderer/plan5-build`; no system dependencies were installed
and the existing symlink and `imgui.ini` were untouched.

## CLI and presets

```bash
/path/to/build/app/blackhole-cli --resolution 512x288 --output schwarzschild.png
/path/to/build/app/blackhole-cli --params presets/reference.json --output kerr.png
/path/to/build/app/blackhole-cli --params presets/reference.json \
  --resolution 1024x576 --no-beaming --output no-beaming.png
/path/to/build/app/blackhole-cli --params presets/reference.json \
  --starfield /path/to/stars.exr --output stars.png
```

Use `--help` for all options. Explicit options override a preset regardless of
where `--params` appears; conflicting explicit options apply in command order.
Malformed numbers, invalid scenes, missing assets, and failed renders return a
nonzero status. PNG is the supported output format.

The GUI and CLI share the strict version-1 JSON schema documented in
[presets.hpp](lib/include/bhr/presets.hpp). All scene fields are saved, including
toggles and resolution; external starfield paths are supplied separately. Presets
are loaded and validated before replacing the active scene. Save/load is explicit;
there is no automatic startup/exit preset write or native file-dialog dependency.

## Verification and limitations

Tests cover parameter boundaries, host/device byte parity on both integrators,
effect flags, preset round trips, CLI errors/overrides, state invalidation, and
hidden-window CUDA/GL lifetime/resize/readback parity. The hidden-window test
returns an explicit skip when a compatible display/GPU is unavailable.

- The existing full suite has two known dual-integrator ray-classification
  failures, including the Schwarzschild center ray. They are retained as failing
  tests. Golden-image and image-PSNR gates are also retained.
- Geokerr uses approximate radial integration/inversion and approximate escape
  directions. Historical Schwarzschild/Kerr image agreement is about 27/18 dB,
  below the original 35/30 dB objectives. Do not interpret it as exact physics.
- The disk is a simplified thin-disk model; blackbody RGB saturates above 40000 K.
  Frequency shift assumes an observer at infinity. Separating redshift/time
  dilation from directional Doppler is a diagnostic approximation, documented in
  [redshift.hpp](lib/include/bhr/redshift.hpp). Beaming uses the selected shift's
  fourth power. Turning off starfield sampling retains the dim-blue diagnostic
  escape background.
- Large starfield files are decoded on explicit load; loading may pause the UI.
  Interactive rendering after upload remains asynchronous. Supported EXR formats
  are described in [starfield.hpp](lib/include/bhr/starfield.hpp).
- Benchmark timings apply to the stated scene and environment. Hidden-window
  presentation measurements exclude monitor scanout; they are separate from
  renderer-only CUDA measurements.

The next rendering-quality priority is fixing RK45 ray classification and then
improving disk-crossing agreement without relaxing the image-regression gates.

## References

- James, von Tunzelmann, Franklin, Thorne (2015), *Gravitational lensing by spinning
  black holes in astrophysics, and in the movie Interstellar*, CQG 32, 065001.
- Dexter, Agol (2009), *A Fast New Public Code for Computing Photon Orbits in a
  Kerr Spacetime*, ApJ 696, 1616.
- Chan, Psaltis, Ozel (2013), *GRay: A Massively Parallel GPU-Based Code for Ray
  Tracing in Relativistic Spacetimes*, ApJ 777, 13.
- Bardeen, Press, Teukolsky (1972), *Rotating Black Holes*, ApJ 178, 347.

## License

MIT
