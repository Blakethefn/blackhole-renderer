# Animated disk emission contract

Plan 9 adds an explicit `bhr.cinematic` schema version 3 for deterministic active-disk
emission. Version 1 scene documents, version 2 cinematic appearance documents, legacy
presets and static rendering remain unchanged. The v3 path is an artistic scene-time
approximation; it is not GRMHD and does not model retarded emission time, scattering,
fluid dynamics or geometry changes.

## Selected-frame usage

The reference fixture is `presets/cinematic/scene-animated.json` and includes a fixed
180-frame shot and a 600-frame moving orbit. Render a frame with:

```bash
BUILD=/path/to/blackhole-renderer-build
"$BUILD/app/blackhole-cli" --shot-file presets/cinematic/scene-animated.json \
  --shot-id fixed --frame 90 --output fixed-090.png
```

The workbench accepts the same selected-frame options and presents the selected frame
read-only. The normal legacy workbench and v2 cinematic workbench paths remain static.

## EmissionV1

`EmissionV1` is immutable-at-request-boundary validated data:

| Field | Range / meaning |
|---|---|
| `enabled` | Boolean activity switch |
| `seed` | Deterministic `uint32` phase seed |
| `amplitude` | `[0, 0.25]`, spatial lane modulation |
| `radial_modulation` | `[0, 0.12]`, bounded radial breathing |
| `flow_strength` | `[0, 1]`, inner-fast periodic advection strength |

The accepted reference values are `enabled=true`, seed `7`, amplitude `.18`, radial
modulation `.06`, and flow strength `1`. Invalid, non-finite or out-of-range values
are rejected before rendering.

## Spatial and temporal semantics

At the actual equatorial disk hit, both RK45 and Geokerr provide Boyer–Lindquist radius
`r` and azimuth `phi`. The pure emission function derives:

```text
u = clamp((r - r_inner) / (r_outer - r_inner), 0, 1)
v = phi / (2*pi)
flow(u) = 0.20 + 0.75*sqrt(1-u)
v' = v - flow_strength*flow(u)*sin(2*pi*phase)/(2*pi)
```

Two bounded spatial harmonics and a lower-frequency radial term multiply the existing
Plan 8 emissivity. Phase is an exact rational `numerator/denominator` at the API
boundary and is evaluated as `(frame_index mod loop_frames)/loop_frames`. Animated
documents require `loop_frames > 0` for every shot. The phase function is periodic, so
phase zero and the wrapped endpoint have matching value and first derivative; frame
`loop_frames` is not emitted as a duplicate endpoint.

The material pattern is sampled from ray-derived hit coordinates, never from a finished
screen image. It therefore appears at corresponding direct and lensed disk locations.
No frame-order state, wall clock, RNG or prior framebuffer participates in rendering.

## Versioned document shape

Schema v3 keeps the v2 `scene`, `shots` and `appearance` objects and adds:

```json
"emission": {
  "schema_version": 1,
  "enabled": true,
  "seed": 7,
  "amplitude": 0.18,
  "radial_modulation": 0.06,
  "flow_strength": 1.0
}
```

The parser rejects missing/unknown fields, wrong versions, invalid values and animated
shots without loop metadata. The v1/v2 parsers do not accept or silently discard this
field.

## Verification and limitations

The pure CPU contract tests cover bounds, seed changes, azimuth wrapping, phase value
and first-derivative continuity. CUDA tests cover deterministic phase changes, exact
wrap behavior, unchanged ray classifications and the v3 CLI/install path. Plan 9's
remaining gate is visual acceptance of the activity strength and loop seam. The
renderer still uses the existing approximate Geokerr geometry and Plan 8's documented
shadow-edge/finite-escape-sky limitations.

The dedicated CUDA-event benchmark measured the animated path at 444–452 ms versus
462–466 ms for the static path in three steady 1920×1080 runs; the difference is within
system/cache variation and no added GPU cost was observed. See
`docs/plan9-review/evidence/animated-emission-benchmark.log`. This is not a 60 FPS claim.
