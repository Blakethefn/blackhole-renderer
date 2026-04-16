# Black Hole Renderer — Design Spec

**Date:** 2026-04-16
**Status:** Approved for implementation planning
**Project:** `blackhole-renderer` (public GitHub repo)

## Overview

A physically accurate, GPU-accelerated Kerr black hole renderer with an interactive C++ workbench. The end result is visually similar to the Gargantua black hole from *Interstellar*, built on real general relativity: Kerr geodesic integration, Novikov-Thorne accretion disk, Doppler shift, gravitational redshift, relativistic beaming, and HDR starfield lensing.

The project is structured as a scientific tool: a reusable physics/rendering library plus a GUI workbench for exploring parameter space. Both integrator backends (semi-analytic Dexter-Agol and adaptive RK45) are included, so the user can compare methods.

## Goals

- Render a Gargantua-quality black hole on an RTX 3080 at 1920×1080 in under a second with the analytic integrator, under ~10 seconds with the adaptive RK45 integrator.
- Use real general relativity — no fake lensing shaders or color ramps.
- Provide an interactive workbench where physical parameters (spin, inclination, disk properties, camera) are exposed as sliders with a live low-res preview.
- Ship as a public GitHub repo that anyone can clone, build, and run with one command.
- Be educational — well-commented physics code with references to primary literature.

## Non-Goals

- Real-time animation at interactive framerates (> 30 fps). Full-resolution render is on-demand.
- Full radiative transfer (thick disks, polarization, synchrotron).
- Multi-black-hole systems or merging binaries.
- Non-Kerr metrics (Schwarzschild is the a=0 special case and IS supported).

## Architecture

```
blackhole-renderer/
├── lib/                         Core physics + rendering engine
│   ├── include/bhr/
│   │   ├── params.hpp          RenderParams, CameraParams, DiskParams
│   │   ├── image.hpp           Image buffer, PNG/EXR I/O
│   │   └── renderer.hpp        Public render() API
│   └── src/
│       ├── kerr.cu             Kerr metric, conserved quantities, ZAMO tetrad
│       ├── geodesic_rk45.cu    Cash-Karp adaptive RK45, second-order Mino-time ODE
│       ├── geodesic_geokerr.cu Dexter-Agol semi-analytic via Carlson R_F, R_D, R_J
│       ├── ferrari.cu          Closed-form quartic root solver
│       ├── elliptic.cu         Carlson symmetric elliptic integrals
│       ├── disk.cu             Novikov-Thorne thin disk model
│       ├── color.cu            Blackbody → CIE XYZ → sRGB
│       ├── starfield.cu        Equirectangular EXR sampling with bilinear filter
│       └── renderer.cu         Kernel launch, integrator dispatch, framebuffer assembly
├── app/                         GUI workbench
│   ├── src/
│   │   ├── main.cpp            SDL2 + OpenGL + ImGui bootstrap
│   │   ├── workbench.cpp       Bottom-bar UI, parameter panels
│   │   ├── viewport.cpp        GPU texture blit, progress bar, HUD
│   │   ├── presets.cpp         JSON save/load
│   │   └── hotkeys.cpp         Keyboard shortcuts
│   └── shaders/                Optional post-FX (tonemap, bloom) — deferred
├── external/                    Vendored or submoduled dependencies
│   ├── imgui/                  Dear ImGui (git submodule)
│   ├── tinyexr/                EXR loader (git submodule, header-only)
│   ├── stb/                    stb_image_write.h for PNG (git submodule)
│   ├── glad/                   OpenGL loader (vendored, single-file)
│   ├── nlohmann_json/          JSON for presets (git submodule, header-only)
│   └── doctest/                Test framework (git submodule, header-only)
├── assets/                      Gitignored, with README pointing to NASA SVS
│   └── README.md               Download instructions for Deep Star Maps 2020
├── tests/
│   ├── unit/                   doctest-based unit tests
│   └── golden/                 Reference PNGs for integration tests
├── docs/
│   ├── physics.md              The math, with primary-source references
│   ├── architecture.md         Code organization
│   ├── parameters.md           What each workbench control does
│   └── gallery/                Cool renders with parameter settings
├── .github/workflows/build.yml Linux compile-check CI
├── CMakeLists.txt              Top-level CMake
├── setup.sh                    One-command dependency setup and build
├── README.md                   Hero image, quick start, citations
├── LICENSE                     MIT
├── CITATION.cff                Academic citation metadata
├── CONTRIBUTING.md             Dev setup, coding style, PR process
└── .gitignore
```

### Data Flow (per render)

1. Workbench `RenderParams` struct updated when user changes a slider.
2. On RENDER button press (or live-preview tick), params copied via `cudaMemcpy` to device constant memory.
3. CUDA kernel launched: one thread per pixel.
4. Each thread:
   a. Computes camera-frame ray direction from pixel coords and FOV.
   b. Transforms ray to photon 4-momentum using the ZAMO tetrad at camera position.
   c. Computes conserved quantities (E, L_z, Q).
   d. Integrates geodesic backward using the selected integrator.
   e. At each step (or directly via analytic inversion), checks termination:
      - Horizon hit (`r ≤ r₊(1+ε)`) → black pixel.
      - Equatorial plane crossing within `[r_in, r_out]` → sample disk.
      - Ray escapes to `r > r_max` → sample starfield.
   f. Writes RGB color to framebuffer.
5. Framebuffer copied back to an OpenGL texture via CUDA-GL interop.
6. ImGui draws the viewport; user sees the render.

## Physics

### Kerr Metric (Boyer-Lindquist)

Line element components with `M=1` (geometrized units), mass absorbed into coordinate scaling:

- Σ = r² + a²cos²θ
- Δ = r² − 2r + a²
- A = (r² + a²)² − a²Δsin²θ
- Outer horizon: r₊ = 1 + √(1 − a²)
- Ergosphere: r_ergo = 1 + √(1 − a²cos²θ)

### Conserved Quantities

For a photon with 4-momentum p^μ:
- E = −p_t (energy at infinity)
- L_z = p_φ (axial angular momentum)
- Q = p_θ² + cos²θ[−a²E² + L_z²/sin²θ] — Carter's constant (photon form, m=0)

### Mino-Time Geodesic Equations

With dλ = dτ/Σ, the motion decouples:
- (dr/dλ)² = R(r) = [E(r²+a²) − aL_z]² − Δ[(L_z − aE)² + Q]
- (dθ/dλ)² = Θ(θ) = Q + cos²θ[a²E² − L_z²/sin²θ]
- dφ/dλ = a[E(r²+a²) − aL_z]/Δ − aE + L_z/sin²θ
- dt/dλ = (r²+a²)[E(r²+a²) − aL_z]/Δ − a²E sin²θ + aL_z

Second-order form used for ODE integration (no sign ambiguity at turning points).

### Integrator Backends

**1. RK45CashKarp** — Adaptive Cash-Karp Runge-Kutta 4(5), embedded 5th-order error estimator for step control. Integrates second-order Mino-time ODEs. Per-step tolerance ~10⁻⁸, tightened near horizon. Termination: horizon proximity, equatorial crossing, or r_max.

**2. GeokerrAnalytic** — Semi-analytic Dexter-Agol 2009 method:
   - Radial motion: `I_u = ∫ du/√U(u)` where u = 1/r and U is quartic/cubic. Roots via Ferrari's closed-form.
   - Polar motion: `I_μ = ∫ dμ/√M(μ)` where μ = cos θ and M is biquadratic.
   - φ(λ), t(λ): sums of u-integrals and μ-integrals, all reduced to Carlson R_F, R_D, R_J.
   - Classification: orbit type (plunging, deflected, escaping) determined from root structure.
   - Equatorial crossing: solve analytically for μ_f = 0.
   - ~5-500× faster than ODE, higher accuracy, no step control needed.

Both backends produce the same geodesic. GUI dropdown selects which to use; side-by-side comparison renders are possible.

### Camera and Observer

- **Camera position**: stationary observer at `(r_cam, θ_cam, φ_cam = 0)`, parameterized by distance and inclination sliders.
- **ZAMO tetrad**: locally non-rotating observer with 4-velocity `u^μ = (1/α, 0, 0, ω/α)`, where α = √(ΣΔ/A) is the lapse and ω = 2ar/A is the frame-dragging angular velocity. Used for the camera frame at all radii (including inside the ergosphere, where static observers fail).

### Accretion Disk (Novikov-Thorne)

Thin relativistic disk in the equatorial plane:
- **Inner edge**: defaults to r_ISCO(a). For a = 0.998, r_ISCO ≈ 1.237. For a = 0, r_ISCO = 6.
- **Outer edge**: user-configurable, default 20M.
- **Temperature profile**: `T(r) = T_peak · f(r; a)` where f is the Novikov-Thorne relativistic correction, asymptoting to the Shakura-Sunyaev Newtonian limit `∝ r^(-3/4)` at large r.
- **Orbit**: Keplerian prograde, angular velocity `Ω_K = 1/(r^(3/2) + a)` in geometrized units.
- **Disk 4-velocity**: `u^μ = u^t(1, 0, 0, Ω_K)` with `u^t = (−g_tt − 2Ω_K g_tφ − Ω_K² g_φφ)^(−1/2)`.

### Redshift and Beaming

Single redshift factor combines all effects:
- `g = (p_μ u^μ)_observer / (p_μ u^μ)_emitter`
- Observed blackbody temperature: `T_obs = g · T_emit`
- Observed specific intensity: `I_ν_obs = g³ · I_ν_emit` (from Lorentz-invariant I/ν³)
- Bolometric (integrated) intensity: `I_obs = g⁴ · I_emit`
- **Physics toggles** in the UI let the user enable/disable each effect independently for pedagogy.

### Color Mapping

Planck blackbody spectrum → CIE 1931 XYZ (via 1931 standard observer curves, tabulated or approximated) → sRGB (standard matrix transform + gamma). Brightness scales by `g⁴` if beaming is enabled. Output is tone-mapped via Reinhard for display; the saved PNG uses the same tone mapping with optional raw linear export.

### Starfield

- User supplies a 64K equirectangular EXR from NASA SVS 4851 (Deep Star Maps 2020), stored at a path of their choice (default: `/media/blakethefn/2000HDD1/dev/pics/<filename>.exr`).
- Loaded once via `tinyexr`. A 64K × 32K × 4-channel float32 EXR is ~32 GB in memory, which exceeds the 12 GB VRAM on the 3080. Strategy: on first load, downsample to 16K × 8K (~2 GB VRAM, mip-filtered) and cache the result on disk as `<source>.16k.exr`. Subsequent launches load the cached downsampled version directly. The full 64K source stays on disk and is only used for offline high-quality renders (invoked via a dedicated "Ultra export" option that accepts longer load times).
- When a ray escapes to r > r_max, direction `(θ_∞, φ_∞)` converted to equirectangular UV: `u = φ/(2π)`, `v = θ/π`. Bilinear sampled. Gravitational lensing is automatic — the escape direction is already bent.

## GUI Workbench

### Layout

- **Top menu strip**: File, Preset ▾, View, Help (right-aligned: GPU name and VRAM).
- **Viewport**: fills the center, displays the rendered framebuffer. HUD overlays in corners show render status, integration method, r_ISCO, horizon radius, ray count.
- **Bottom control bar** (155px tall): four color-coded parameter cards plus a render-action column.

### Parameter Cards

1. **Black Hole** (purple): spin a/M (0 to 0.998), mass (visual scale), inclination (0° to 90°).
2. **Accretion Disk** (orange): inner R (defaults to ISCO, auto-updates with spin), outer R, peak temperature, brightness.
3. **Camera** (green): distance (in M), FOV, resolution dropdown.
4. **Physics** (magenta): integrator dropdown (RK45 / geokerr), Doppler toggle, redshift toggle, beaming toggle, starfield toggle.

### Render-Action Column

- **RENDER** — full-res render with progress bar.
- **Live preview** toggle — 512×288 auto-render on parameter change.
- **Reset** — restore defaults.
- **Save preset** — write current params to `presets/*.json`.
- **Save PNG** — tone-mapped export of current full-res render.

### Hotkeys

- Space: render
- H: hide/show panels (clean viewport)
- S: save PNG
- 1-9: load preset
- R: reset params
- L: toggle live preview

### Presets

Bundled: Gargantua (a=0.998, i=85°), Schwarzschild (a=0, i=60°), Face-on (i=0°), Edge-on (i=90°), Thin-disk (r_out=40M), Photon-ring-closeup (r_cam=10M, FOV=20°). User presets saved as JSON in `presets/`.

## Build System

### CMake Top-Level

- Native CUDA language support (CMake 3.18+).
- CUDA architecture: `sm_86` (Ampere / RTX 3080) with PTX fallback for forward compatibility.
- Separate compilation of `.cu` files into `libblackhole`.
- `app/` links against the core lib.
- `tests/` optional, built when `BHR_BUILD_TESTS=ON`.

### Dependencies

| Package | Purpose | Source |
|---|---|---|
| SDL2 | Window, GL context, input | vcpkg |
| OpenGL (glad) | Viewport texture blit | vendored single-file in `external/glad/` |
| Dear ImGui | Workbench UI | git submodule |
| tinyexr | EXR starfield loading | git submodule (header-only) |
| stb_image_write | PNG export | git submodule (header-only) |
| nlohmann/json | Preset save/load | git submodule (header-only) |
| doctest | Test framework | git submodule (header-only) |
| CUDA Toolkit 12.x | GPU kernels | system install |

### Storage Layout

Heavy artifacts live on the HDD per the project-wide dev-environment convention:

```
/media/blakethefn/500SSD1/Projects/blackhole-renderer/   Source repo (lean)
/media/blakethefn/2000HDD1/dev/blackhole-renderer-deps/  Build cache, vcpkg
/media/blakethefn/2000HDD1/dev/pics/                     Star map EXR (user-provided)
```

`build/` in the repo is a symlink to `/media/blakethefn/2000HDD1/dev/blackhole-renderer-deps/build-cache/`.

### One-Command Setup

```bash
./setup.sh
# 1. git submodule update --init --recursive
# 2. vcpkg install sdl2 (via /media/blakethefn/2000HDD1/dev/vcpkg)
# 3. cmake -B build -DCMAKE_BUILD_TYPE=Release
# 4. cmake --build build -j
./build/blackhole-workbench
```

## Testing

### Unit Tests (doctest, runs in CI)

- Blackbody → sRGB at reference temperatures (3000K, 5778K, 10000K).
- r_ISCO formula: a=0 → 6, a=1 → 1, a=0.998 → ~1.237.
- Ferrari quartic solver: known polynomials, compared against numerical baselines.
- Carlson R_F, R_D, R_J: compared to closed forms (e.g., R_F(0,1,2) = K(1/√2)).
- Metric components at sample points (r, θ, a).
- Conserved-quantity extraction from camera rays.

### Integration Tests (local, CUDA required)

- Schwarzschild face-on (a=0, i=0°): expect symmetric photon ring. Compare against saved reference PNG.
- Gargantua preset: compare against saved golden PNG. Tolerance: PSNR ≥ 40 dB.
- Dual-integrator consistency: render same scene with RK45 and geokerr, PSNR between them ≥ 40 dB. Any lower indicates a bug.

### Physics Cross-Checks (manual, run on demand)

- Schwarzschild shadow radius = 2√27 M on face-on view. Pixel-count measurement.
- ISCO redshift factor matches Bardeen-Press-Teukolsky analytic value.
- Photon ring radius at a=0.998 matches published values (e.g., James et al. 2015 figures).

### CI

- GitHub Actions: Linux build-check on every PR. CUDA compile to PTX only (no GPU needed for build verification). Unit tests skipped on CI (they don't need GPU but CUDA runtime for compilation is enough).

## Public Repo Polish

### README.md

- Hero image: Gargantua-style render at the top.
- Pitch: "Physically accurate Kerr black hole renderer with accretion disk. Uses real general relativity — Kerr geodesics via Carlson elliptic integrals and adaptive RK45. Interactive workbench for exploring parameter space."
- Quick start commands.
- Screenshot gallery.
- Parameter reference (link to `docs/parameters.md`).
- Asset download instructions pointing to NASA SVS 4851.
- Citations and references section.
- License, acknowledgments.

### Citations (in README and CITATION.cff)

- James, von Tunzelmann, Franklin, Thorne 2015 — Class. Quantum Grav. 32, 065001 (Interstellar / DNGR)
- Chan, Psaltis, Özel 2013 — ApJ 777, 13 (GRay)
- Dexter, Agol 2009 — ApJ 696, 1616 (geokerr)
- Carter 1968 — Phys. Rev. 174, 1559 (Carter's constant)
- Bardeen, Press, Teukolsky 1972 — ApJ 178, 347 (ZAMO)
- Novikov, Thorne 1973 (thin disk)
- Shakura, Sunyaev 1973 — A&A 24, 337 (alpha-disk)
- NASA Scientific Visualization Studio, Goddard Space Flight Center — Deep Star Maps 2020

### License

MIT. Standard for physics/rendering projects, maximally permissive.

## Out of Scope for v1

- Thick disks, synchrotron emission, polarization.
- Binary black holes, merger visualizations.
- Hair rendering (post-Kerr modifications).
- Real-time animation.
- Python bindings (possible future addition).
- Non-Ampere GPU support (only need sm_86; sm_75/sm_80 fallback is trivial if someone asks).

## Open Decisions / Future

- Post-effects: bloom, tone mapping curves beyond Reinhard. Deferred to polish phase.
- Star map downsampling strategy for 64K EXR — compute once at startup and cache a 16K mip in VRAM.
- CLI rendering mode (`blackhole-cli --preset gargantua --resolution 3840x2160 --output out.png`) — nice future addition since the library is already CLI-ready.
