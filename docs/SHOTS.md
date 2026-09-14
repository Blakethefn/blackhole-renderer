# Shared scenes and deterministic shots

Plan 7 adds a CPU-only scene/shot foundation and selected-frame PNG rendering.
The existing RK45 renderer, complete v1 presets, projection and RGBA8 output are
unchanged. Fixed samples have **static disk emission**. Loop phase is metadata;
this is not an animated-loop or video exporter.

## Render selected frames

From the source repository after a headless CUDA build:

```bash
cmake --preset linux-headless -B "$HOME/.cache/blackhole-renderer/plan7-headless"
cmake --build "$HOME/.cache/blackhole-renderer/plan7-headless" -j 4

CLI="$HOME/.cache/blackhole-renderer/plan7-headless/app/blackhole-cli"
SHOT="presets/shots/reference.json"
OUT="$HOME/.cache/blackhole-renderer/plan7-samples"
mkdir -p "$OUT"
"$CLI" --shot-file "$SHOT" --shot-id fixed --frame 0 --output "$OUT/fixed-000.png"
"$CLI" --shot-file "$SHOT" --shot-id orbit --frame 0 --output "$OUT/orbit-000.png"
"$CLI" --shot-file "$SHOT" --shot-id orbit --frame 300 --output "$OUT/orbit-300.png"
"$CLI" --shot-file "$SHOT" --shot-id orbit --frame 599 --output "$OUT/orbit-599.png"
```

The fixture shares `presets/reference.json`'s physical values and contains:

| Shot | Frames | FPS | Duration | Camera |
|---|---:|---:|---:|---|
| `fixed` | 180 | 60/1 | 3 seconds | Scene camera: 30 M, 85°, 0° |
| `orbit` | 600 | 60/1 | 10 seconds | 40→25 M, 75→88° inclination, 170→190° azimuth |

Both output 256×144 with a 180-frame emission-loop metadata period. Radial and
inclination motion make the orbit samples differ even without an external sky.
Frame 300 is the chosen middle sample; an even frame count has no exact midpoint.
For larger images, copy the document and change the selected shot's `width` and
`height` (for example 1920×1080). The embedded preset's dimensions are preserved,
but shot dimensions determine the selected render.

All four CLI options are required exactly once in shot mode. `--frame` is zero
based and accepts only decimal nonnegative integers. `--params`, dimensions,
physical/effect/integrator and starfield overrides are incompatible with shot
mode. Legacy commands without shot selection retain their original precedence,
including ordered repeated overrides. `--help` prints help and exits as before.

The CLI prints exact rational timeline time and duration, optional phase, and
resolved rendering parameters. `Done in ...` is elapsed processing time, separate
from the timeline. FPS describes playback sampling, not live render throughput.
Unknown IDs, invalid indices/documents and required missing/invalid EXRs fail
before rendering with a nonzero exit and no new PNG. An existing output is not
removed on input failure. Rendering uses the original blocking `bhr::render`.

## Cinematic document version 1

The top-level object has exactly `format`, `schema_version`, `scene` and `shots`:

```json
{
  "format": "bhr.cinematic",
  "schema_version": 1,
  "scene": {
    "preset": {
      "schema_version": 1,
      "spin": 0.9,
      "camera": {"r_cam": 30, "theta_cam_deg": 85, "phi_cam_deg": 0, "fov_deg": 35, "width": 256, "height": 144},
      "disk": {"r_inner": 2.321, "r_outer": 20, "peak_temp_K": 40000, "brightness": 1},
      "enable_doppler": true, "enable_redshift": true,
      "enable_beaming": true, "enable_starfield": false, "integrator": "rk45"
    }
  },
  "shots": [{
    "id": "fixed", "frame_count": 180,
    "fps": {"numerator": 60, "denominator": 1},
    "width": 256, "height": 144, "loop_frames": 180,
    "camera": {"kind": "fixed"}
  }]
}
```

`scene.preset` is the complete strict existing v1 preset; its decoder and physical
validation are shared with the legacy loader. No physical property is duplicated
elsewhere. Optional `scene.starfield_exr` is a relative `.exr` string. All other
scene fields are rejected.

Every shot requires `id`, `frame_count`, `fps`, `width`, `height`, and `camera`.
`loop_frames` is optional; omit it to disable phase metadata (JSON null is invalid).
`fps` requires exactly integer `numerator` and `denominator`.
A fixed camera has exactly `{"kind":"fixed"}` and uses the scene pose/FOV.
An orbit camera has exactly:

```json
{
  "kind": "orbit",
  "start": {"r_cam": 40, "theta_cam_deg": 75, "phi_cam_deg": 170},
  "end": {"r_cam": 25, "theta_cam_deg": 88, "phi_cam_deg": 190}
}
```

Each endpoint has exactly those three numeric components. FOV is always the
scene's **vertical** FOV; the camera still looks inward using the existing BL
observer model. Orbit endpoints override the scene pose only.

## Timing and precision

For frame count `N`, FPS numerator `p`, denominator `q`, and index `i`:

- Valid indices are `0 <= i < N`.
- Duration is `N*q/p` seconds; frame time is `i*q/p` seconds.
- Optional emission phase is `(i mod L)/L` for `L = loop_frames`. The loop has
  samples `0/L` through `(L-1)/L`, with no duplicated endpoint.
- Orbit progress is `u = i/(N-1)` and interpolation weight is `u*u*(3-2*u)`.
  The first and final frames reach their respective endpoints exactly. Camera
  progress never wraps with emission phase.

`FrameTime` retains unreduced exact uint64 numerator/denominator pairs. Integer
multiplication is checked, and the allowed products are at most
`1,000,000 * 4,294,967,295`, below 2^52. `seconds()` explicitly converts to double;
non-binary rational fractions can round. For 30000/1001 FPS, frame 30000 has
exact time `30030000/30000` seconds, or 1001 seconds. No accumulated frame deltas,
wall clock, mutable playback cursor, or RNG participates in evaluation.

Pose/preset storage uses the renderer's floats. JSON numbers round to float under
the existing finite-float policy; values round-trip without further change.
Interpolation uses double intermediates and casts each result to float. The
radius/inclination domain is convex for fixed spin and smoothstep does not
overshoot. Both endpoints and every evaluated request use existing validation.
There is no silent clamping. Very large finite azimuths lose float angular
precision; use modest unwrapped degrees for useful shots.

Azimuth is explicitly unwrapped: 170→190 crosses the seam in the positive
20-degree direction; 190→170 moves negatively; 0→360 intentionally makes a full
rotation. No shortest-path normalization is applied. Repeated/out-of-order
sampling is deterministic and leaves the source unchanged. Byte equality is
verified on the same build/device/settings, not promised across GPUs/toolchains.

## Bounds and errors

| Input | Limit |
|---|---|
| JSON | At most 1 MiB, nesting depth at most 6 (root depth 0) |
| Shots/document | 1–64 |
| Shot ID | Unique, 1–64 ASCII letters/digits/underscore/hyphen |
| Frames/shot | 1–1,000,000; orbit requires at least 2 |
| FPS terms | 1–4,294,967,295 each; effective FPS at most 240 |
| Loop frames | 1–1,000,000; independent of shot count |
| Output dimensions | 1–16,384 each and existing checked byte-size validation |
| Asset reference | 1–1024 bytes, relative `.exr`, no `..`, backslash, colon or controls |
| Physics/poses | Existing `validation_error(RenderParams)` domain |

Reject unknown/missing/duplicate fields at every nesting level, duplicate IDs,
unknown versions/kinds, fractional integer fields, numeric strings, null optionals,
non-finite/out-of-range values, raw NULs, trailing junk and malformed/truncated JSON.
Integer range checks occur before narrowing. Duration is derived, so an independent
`duration` field is rejected. No transient GPU handles or absolute asset paths are
serialized. Later schema extensions need an explicit compatibility decision.

## Assets and transactional persistence

`starfield_exr`, when present, is relative to the document's containing directory,
not the process working directory. For example `assets/sky.exr` in
`/work/scene.json` resolves to `/work/assets/sky.exr`. Symlinks follow ordinary
filesystem semantics; this path policy is for portable documents, not a sandbox.
If changing process working directory between calls, retain an absolute document
path and pass it to `resolve_starfield`.

Authoring/save/load validates reference syntax but does not require the asset to
exist. When sampling is enabled, the field is mandatory and selected rendering
requires a regular file plus the existing single-part scanline HALF/FLOAT RGB EXR
validation/upload. Missing assets never trigger substitution or downloads.
An optional reference can be retained while sampling is disabled; it is not
loaded then. No external sky is bundled.

`parse_cinematic`/`load_cinematic` return a new immutable document or throw; an
active snapshot is never partially replaced. `save_cinematic` validates and
serializes completely, writes a uniquely/exclusively created same-directory
file, checks writes/flush/close, then atomically renames it over the destination.
Validation, write or rename failure leaves the old destination intact and removes
the temporary file. Files created this way have private POSIX permissions from
`mkstemp` (0600); replacement does not retain old inode metadata. Linux/POSIX
filesystems are supported. Crash/power-loss durability is not guaranteed: there
is no file/directory fsync protocol. The legacy preset save API is unchanged.

## CPU library API and explicit import

Include `bhr/shot.hpp`; no CUDA or OpenGL headers are needed. Link `bhr::cpu_support`
with `BHR_ENABLE_CUDA=OFF`, or `bhr::lib` in a CUDA build. Value members are const:
`Scene`, `Shot`, `CinematicDocument`, `FrameRate`, `BLPose` and `FrameSample` are
snapshots. Create a new snapshot to author changes. Resource ownership stays in
the runtime renderer. All APIs report failures through `std::exception` derivatives.

```cpp
bhr::RenderParams preset;
std::string error;
if (!bhr::load_preset("old-v1.json", preset, error))
    throw std::runtime_error(error);
const auto document = bhr::import_preset(preset); // requires disabled starfield
// For an enabled starfield: import_preset(preset, "assets/sky.exr").
// Import preserves every physical field, camera and output size, and creates
// a fixed 180-frame, 60/1 shot with a 180-frame loop metadata period.
bhr::save_cinematic(document, "scene.json");
const auto loaded = bhr::load_cinematic("scene.json");
const auto frame = bhr::evaluate_frame(loaded, "fixed", 42);
// frame.params -> existing renderer; frame.time/duration/loop_phase -> metadata.
```

`validate_cinematic`, `parse_cinematic`, `serialize_cinematic`, `load_cinematic`,
`save_cinematic`, `import_preset`, `evaluate_frame` and `resolve_starfield` are the
Plan 7 producer API. There is no import CLI, as the library API is sufficient.
JSON is an internal dependency; public headers expose no nlohmann values.

## Verification and next boundary

Tests cover rational timing, extremes, one/two-frame boundaries, smoothstep and
azimuth rules, immutable out-of-order sampling, strict parsing, exact v1 import,
relative assets, failed loads, partial writes and failed atomic replacement.
CPU option tests cover every legacy override and shot ambiguity. CUDA CLI tests
exercise actual fixed/legacy PNG byte parity and changing selected orbit samples.
The original golden image, numerical classifications and 25/15 dB PSNR gates
remain unchanged. See [validation](VALIDATION.md) for measured results.

Plan 8 owns cinematic look/radiance; Plan 9 owns animated emission and its eventual
seed contract. Plan 8's bloom-on fixed still is the accepted hero. This foundation has no stochastic consumer or seed field. Shading,
HDR/bloom, disk activity, sequences, caching, video encoding and GUI shot authoring
are deferred. The workbench continues to use its existing v1 presets and controls.

Implementation references: [nlohmann parser callbacks](https://json.nlohmann.me/features/parsing/parser_callbacks/)
for duplicate-key/depth rejection, and [POSIX rename](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html)
for atomic replacement. The repository's pinned dependencies are reused unchanged.
