# Cinematic appearance contract

Plan 8 defines an additive, versioned appearance path. It does not alter legacy
presets, legacy `bhr.cinematic` v1 documents, their parser, or legacy RGBA8 output.
The Plan 8 design was approved on 2026-09-07; final user visual acceptance remains
pending and Plan 8 is therefore not complete.

## Versions and documents

`AppearanceV1` is immutable validated data. A strict `bhr.cinematic` v2 document
keeps the v1 `scene` and `shots` fields and adds a required top-level `appearance`
object. It declares `schema_version: 1`, `working_space: linear-srgb-d65`,
`radiance_model: relative-disk-v1`, `tone_map: reinhard-rgb-v1`, and
`output_transfer: srgb`. All fields are required; unknown versions/enums, extras,
duplicates, nulls, bad types, and invalid ranges are rejected. V2 never falls back to
v1. `upgrade_cinematic` is explicit and returns a new document; no automatic rewrite
or lossy downgrade exists.

`--shot-file FILE --shot-id ID --frame INDEX --output FILE.png` remains the selected
frame CLI boundary for both fixed and orbit shots. The workbench supports the matching
startup selection. Both use the same deterministic `evaluate_frame` result; static
emission has no clock, loop phase, seed, or per-frame normalization.

The document-relative star asset is required when `enable_starfield` is true. Its
loader accepts the supported EXR forms only, rejects more than four channels, and
predicts the bounded upload pixel count before decoding; missing or invalid assets
fail rather than substituting a sky.

## Radiance and display

`RadiancePixel` is a 16-byte, 16-byte-aligned float32 `R,G,B,A` value, packed
top-left-origin row-major. RGB is finite, nonnegative linear sRGB/Rec.709-primary,
D65 relative artistic radiance in `[0, 65504]`; HDR values above one are allowed.
Alpha is always one opaque coverage, never classification or emission. Overflow clips
at the declared ceiling and increments diagnostics. Unknown rays and invalid shading
are status-tracked, remain diagnostic magenta in display output, and never silently
become black.

The fixed display order is: multiply by `2^exposure_ev` (EV range `[-16,16]`),
optional linear bloom, per-channel Reinhard `x/(1+x)`, sRGB encode once, then nearest
RGBA8 byte `floor(255*x + .5)`. The encode is `12.92*x` at or below `.0031308`,
otherwise `1.055*x^(1/2.4)-.055`. Thus EV 0 is not an HDR-to-byte identity; radiance
1 maps to byte 188 and EV +1 maps to 213. The approximate blackbody helper is treated
as encoded sRGB and decoded before it becomes cinematic radiance.

For a disk hit, intensity is the artistic emission proxy
`1.5 * brightness * log(max(T_emit,1))/log(40000) * beaming * detail_taper`.
`beaming` is the existing enabled `g^4` factor; `detail_taper` is bounded static
emissivity detail plus the outer fade. The color lookup uses `.15 * g * T_emit` only
as its artistic temperature scale. It never changes the temperature used for
intensity, ray tracing, or the 16-point legacy baseline. Sky radiance independently
uses `star_intensity` (the reviewed value is `.15`) and joins the same display path.

When enabled, bloom first thresholds exposed valid radiance by Rec.709 luminance,
downsamples to `ceil(width/4) × ceil(height/4)`, runs normalized separable Gaussian
blur at sigma 2 and radius 6 reduced pixels with clamped edges, bilinearly upsamples,
and adds `strength * bloom` before tone mapping. Unknown and invalid diagnostics are
excluded from bloom input and overwritten by diagnostic output. Disabled bloom adds no
glow.

The actual ImGui parity evidence presents those already encoded bytes into an sRGB
FBO. `GL_FRAMEBUFFER_SRGB` is disabled for the image draw and restored afterwards;
headless bytes and framebuffer readback match exactly for every recorded still.

## Asynchronous ownership and resources

`render_radiance_device`, `bloom_device`, `display_device`, and
`render_cinematic_device` allocate, copy, and synchronize nothing. They execute in
order on the supplied stream. The caller owns the aligned workspace, RGBA8 output,
starfield, and stream until that stream/event completes. Validation checks dimensions,
capacity, alignment, overlaps, starfield bounds, appearance ranges, and the 256 MiB
request budget before dispatch. `render_cinematic` is the blocking wrapper and rejects
invalid-radiance frames instead of returning partial output.

`CinematicBuffer` owns one reusable workspace and a separate pinned diagnostics
record. `read_diagnostics_async` is the explicit device-to-host diagnostics read; the
render/display pipeline itself performs no host transfer. Launch setup clears only the
device diagnostics counter asynchronously.

At 1920×1080, the evidence run allocated a 39,398,416-byte workspace, 8,294,400-byte
RGBA output, and 33,554,432-byte 2048×1024 float4 sky. The separately calculated
conservative GUI-resize budget—including simultaneous retained preview resources—is
106,130,464 bytes, below the 268,435,456-byte hard request limit; it is not a direct
measurement of live allocation. Recorded process peak RSS was 210,288 KiB. These
measurements are not a 60 FPS claim.

## Candidate static spatial handoff for Plan 9

The implemented candidate source coordinates are the actual disk-hit Boyer–Lindquist radius `r` in M
and wrapped azimuth `phi` in radians: `u=(r-r_inner)/(r_outer-r_inner)` and
`v=fract(phi/(2*pi))`. Detail is periodic in `v`, resolution-independent in these
source coordinates, and has no time or phase input. Plan 8 only applies bounded static
emissivity detail and outer taper; it does not change geodesics, opacity, temperature,
or frequency shift. This becomes a frozen Plan 9 handoff only after final visual
acceptance; that decision remains pending.

## Limitations

This is artistic relative emission, not calibrated physical radiance or film/IMAX
replication. RK45 remains the reference geometry; Geokerr remains approximate. Some
physics directions still need future scope: inner-boundary speckle is not hidden by
bloom, escape rays sample a finite escape-BL sky rather than a transported optical
environment, and there is no ray-bundle, thick-volume, spectral, or camera-optics
model. CUDA Compute Sanitizer remains historically unavailable and was not rerun or
repaired under Plan 8 authorization.

See the [actual still review board](plan8-review/stills/index.html), [measurements](plan8-review/evidence/cinematic-measurements.log), and [proposal](CINEMATIC_APPEARANCE_PROPOSAL.md).
