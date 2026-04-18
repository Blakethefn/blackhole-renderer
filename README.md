# Black Hole Renderer

GPU-accelerated Kerr black hole renderer and C++ workbench. The goal is a
physically grounded renderer for thin-disk black hole scenes: Kerr geodesics,
event-horizon hits, disk intersections, redshift and Doppler effects, and
starfield lensing.

> Status: early development. The core CUDA/C++ scaffolding is in place, but the
> final astrophysical renderer and polished workbench are still incomplete.

## Current State

Implemented pieces:

- CMake/CUDA project layout with a reusable `bhr` library and an SDL2/OpenGL
  workbench target.
- Kerr helpers for horizon radius, ISCO, metric terms, lapse, and frame
  dragging.
- Camera ray setup and an adaptive RK45 geodesic integration path.
- A synchronous `bhr::render()` API that writes RGBA8 pixels on the GPU and
  copies the image back to the host.
- A starter ImGui workbench using CUDA/OpenGL interop. The current viewport is
  an interop gradient scaffold, not the final black hole render view.
- Doctest-based unit and integration test targets.

Planned pieces:

- Semi-analytic Dexter-Agol / geokerr-style integrator for faster high-quality
  renders.
- Novikov-Thorne disk emission, blackbody color mapping, gravitational
  redshift, Doppler shift, and relativistic beaming.
- HDR starfield input and lensing of escaped rays.
- Presets, PNG/EXR export, gallery assets, and a fuller parameter workbench.

## Requirements

- Linux development environment.
- NVIDIA GPU and CUDA Toolkit 12.x with `nvcc` on `PATH`.
- CMake 3.24 or newer.
- Ninja.
- C++17-capable compiler.
- vcpkg with SDL2 available. `setup.sh` uses `VCPKG_ROOT` when set, or falls
  back to `/media/blakethefn/2000HDD1/dev/vcpkg` on the local workstation.
- Git submodules initialized for vendored/header-only dependencies.

## Quick Start

```bash
git clone --recursive https://github.com/Blakethefn/blackhole-renderer.git
cd blackhole-renderer
./setup.sh
./build/app/blackhole-workbench
```

Run tests after a successful build:

```bash
ctest --preset default
```

## Manual Build

Use this path when you want to control dependency setup yourself.

```bash
git submodule update --init --recursive
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default -j
ctest --preset default
```

Debug builds use the `debug` preset:

```bash
cmake --preset debug
cmake --build --preset debug -j
```

## Repository Layout

```text
app/      SDL2, OpenGL, ImGui, and CUDA interop workbench
lib/      Core physics, camera, image, geodesic, and render library
tests/    Doctest unit and integration tests
docs/     Project design notes and implementation specs
external/ Submodules and vendored third-party headers
```

Important entry points:

- `lib/include/bhr/renderer.hpp` - public render API.
- `lib/include/bhr/params.hpp` - camera, disk, and render parameter structs.
- `lib/include/bhr/kerr.hpp` - Kerr metric helper functions.
- `lib/include/bhr/geodesic.hpp` - photon state, conserved quantities, and
  integration types.
- `app/src/main.cpp` - current workbench bootstrap and CUDA/GL interop demo.

## Development Notes

- `setup.sh` expects `build/` to be a symlink to a larger dependency/build cache
  directory. If a real `build/` directory already exists, the script stops
  rather than overwriting it.
- The current renderer uses a simple disk/escape/horizon color model. Treat its
  output as a development diagnostic, not a finished scientific visualization.
- Starfield assets are not required yet. Future versions are expected to use
  large EXR maps stored outside the source repo.
- Most public-facing polish items are still open: screenshots, gallery renders,
  parameter docs, citation metadata, and CI.

## Roadmap

1. Replace the workbench gradient viewport with the `bhr::render()` output.
2. Tighten RK45 correctness and golden-image regression tests.
3. Add disk emission, color mapping, and relativistic intensity effects.
4. Add the semi-analytic integrator path and cross-check it against RK45.
5. Add starfield loading/downsampling and escaped-ray sampling.
6. Build out presets, exports, screenshots, and public documentation.

## References

- James, von Tunzelmann, Franklin, Thorne 2015 - *Gravitational lensing by
  spinning black holes in astrophysics, and in the movie Interstellar*
  (Class. Quantum Grav. 32, 065001)
- Dexter, Agol 2009 - *A Fast New Public Code for Computing Photon Orbits in a
  Kerr Spacetime* (ApJ 696, 1616)
- Chan, Psaltis, Ozel 2013 - *GRay: A Massively Parallel GPU-Based Code for Ray
  Tracing in Relativistic Spacetimes* (ApJ 777, 13)
- Carter 1968 - *Global Structure of the Kerr Family of Gravitational Fields*
  (Phys. Rev. 174, 1559)
- Bardeen, Press, Teukolsky 1972 - *Rotating Black Holes: Locally Nonrotating
  Frames, Energy Extraction, and Scalar Synchrotron Radiation* (ApJ 178, 347)
- Novikov, Thorne 1973 - thin accretion disk model
- Shakura, Sunyaev 1973 - alpha-disk model

## License

MIT
