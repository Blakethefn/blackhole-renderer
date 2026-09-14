# Plan 8 appearance proposal — design approved; final visual acceptance pending

Prepared 2026-09-06 and approved for implementation on 2026-09-07. The approval is
the design go-ahead for the bounded Plan 8 direction below; it is **not** final visual
acceptance of the produced stills. That final gate remains pending while the selected
frame / async presentation question awaits the user's answer. Start with the
[historical design board](plan8-review/index.html) and the [actual still board](plan8-review/stills/index.html).

## Approved design direction

The approved direction is one **warm ivory / amber** look: a nearly edge-on, centered disk spanning
about 63% of a 16:9 frame, readable static lanes, a dark shadow, sparse lensed stars,
and a small highlight glow. Retain Doppler/redshift/beaming and the existing RK45
geometry. The warm palette and glow are explicitly artistic approximations.
It authorizes batches A–C below, including the bounded optional bloom design. Initial
implementation and every morphology comparison have bloom disabled.

This approval does not accept the current smooth legacy images as the finished
look. Final acceptance requires inspecting actual cinematic stills after implementation.
The gate comes from `.astra/PLAN8_PROMPT.md`, “design and visual review gate before
implementation”, and `.astra/obsidian-plans/plan8-cinematic-radiance-and-look.md`.

## Visual evidence and reference board

The board distinguishes external art direction, actual unchanged renderer output,
and proposed color swatches. No generated art or recolored PNG is labeled a render.

| Reference | Use in this proposal | Boundary |
|---|---|---|
| [DNEG, Interstellar](https://www.dneg.com/our-work/interstellar) | Bright ivory disk, large dark surround, optical glow | Film still is reference-only; spacecraft/planet are outside scope |
| [James et al., 2015, Fig. 16, p. 28](https://arxiv.org/pdf/1502.03808#page=28) | Narrow foreground band, lensed upper/lower portions, restrained version of veiling glow | Film image omits frequency/brightness shifts; our recommendation retains them |
| [NASA Goddard / Jeremy Schnittman, SVS 13326](https://svs.gsfc.nasa.gov/13326/) | Readable disk lanes and black surround | Use detail organization as inspiration, not its orange grade or a new physical oracle |

Film replication is not promised. The current camera looks radially inward with a
ZAMO tetrad; it has no independent target/roll or transported moving-observer velocity.
The renderer traces individual rays into a first-hit thin disk, uses an observer-at-
infinity frequency factor and samples the sky at finite escape BL coordinates.
It does not have DNGR ray bundles, a thick volume, spectral transport or camera optics.

Actual fresh baselines in [plan8-review/baselines](plan8-review/baselines/):

| Files | What changes | Finding |
|---|---|---|
| `incl-60/75/85` | 60°, 75°, 85°; spin .9; r=30 M; vertical FOV 35° | Disk crops heavily; broad smooth blue-gray surfaces dominate |
| `spin-0-incl-85`, `spin-099-incl-85` | Spin 0 / .99 and corresponding valid inner radii | Shadow/lensed morphology differs; a painted circular mask is unacceptable |
| `fixed-000`, `orbit-000/300/599` | Unmodified Plan 7 evaluation at 1024×576 | Fixed matches `incl-85` bytes; orbit geometry changes |
| `composition-cool/warm/amber` | Spin .6; r=55 M; FOV 40°; 85°; 40000/12000/6000 K | Existing controls establish framing and a warm direction but cannot add detail or proper display encoding |
| `composition-88`, `composition-09-88` | Inclination 88° at spin .6 / .9 | Thin foreground band and room for stars; .6 recommended |
| `composition-1080` | `composition-88` at 1920×1080 | Full-resolution legacy composition proof; no cinematic shading |

Every PNG has a saved v1 preset or shared v1 shot input, process log and SHA-256.
All effects remain enabled and no external sky is loaded in these baselines.
The warm composition proof uses 6000 K / brightness 2 through legacy shading;
the proposed cinematic scene instead uses 40000 K / brightness 1, explicit appearance
temperature scale .15 and exposure +1 EV. They are not equivalent color pipelines.

**Visible limitations:** speckling remains around inner boundaries. Exact magenta
unknown-hit pixels occur in `incl-75` (2), `incl-85`/`fixed-000` (3), and `orbit-599`
(10), out of 589824 pixels each. The proposed composition has zero magenta pixels
at both captured resolutions, but still shows boundary speckling. Existing gates pass;
that does not certify every ray. Do not hide unknown hits behind bloom or change the
completed physics repairs. Any geometry repair beyond Plan 8 needs separate scope.

## Recommended appearance and scene

- Scene: spin .6; disk 3.83–20 M, 40000 K, brightness 1; camera r=55 M,
  inclination 88°, azimuth 0°, vertical FOV 40°. Review at 1024×576; final at 1920×1080.
- Palette: warm ivory highlights, muted amber outer disk; no blue-gray blanket.
  Retain brightness asymmetry. Keep the shadow black before optical glow.
- Static detail: at most ±18% modulation, evaluated using disk-hit radius and azimuth,
  with broad curved lanes; fade emission over the outermost 10% of the radial span.
  No opacity/geodesic changes. The radial fade is an artistic emissivity taper.
- Environment: an original, deterministic 2048×1024 linear RGB EXR of sparse,
  finite-footprint stars; no nebula, automatic sky download or external asset dependency.
- Exposure: start at +1 EV, fixed for every selected frame. No auto-exposure or
  per-frame normalization. Start bloom off, then review strength .06 / threshold 1.

## Radiance and display contract (proposed v1)

**Storage:** tightly packed, top-left origin, row-major, 16-byte aligned four-float
pixels (`R,G,B,A`, 16 bytes/pixel). R/G/B are nonnegative finite linear-light sRGB /
Rec.709-primary, D65 values in **relative artistic radiance units**, range [0,65504].
They may exceed 1. Alpha is always 1, opaque coverage, never emission or hit classification.
No premultiplication distinction arises with alpha 1. Precision is float32, not RGBA8
and not half. Cap only at the declared high bound using overflow-safe intermediates.
Count saturation and invalid pixels; do not silently turn NaN or unknown rays black.

**Sources:** retain the existing temperature, frequency factor, log-temperature
emissivity proxy and enabled g^4 beaming. Construct the cinematic value *before*
legacy tone mapping or quantization. For the approximate blackbody color, deliberately
interpret the existing Helland approximation as encoded sRGB and decode it using the
piecewise sRGB inverse. Its current “linear” comment is not a reliable color contract.
Apply `.15 * g * T_emit` only to the artistic color temperature; never scale the
temperature that drives intensity or change the ray calculation. Keep the legacy
helper/output behavior intact. EXR input is explicitly declared linear sRGB/D65 by
the new appearance version; the loader does not infer primaries from an arbitrary EXR.

Relative disk intensity proposal: `1.5 * brightness * log(max(T_emit,1))/log(40000)`
times the existing enabled beaming factor, nonnegative static detail and outer taper.
Multiply by decoded RGB. This is an emission proxy, not a Stefan–Boltzmann or SI model.
Sky samples use an independent .15 radiance multiplier and enter the same display path.
Unknown rays retain an explicit magenta diagnostic via a separate pixel-status buffer;
nonfinite cinematic shading marks a failed/invalid frame. Diagnostics bypass tone/glow.

**Display order:** exposure → optional linear bloom → per-channel Reinhard → sRGB
encoding → nearest-integer RGBA8. Exposure multiplies radiance by `2^exposure_ev`,
validated to [-16,+16]. Reinhard is `x/(1+x)` on nonnegative exposed channels;
this simple curve deliberately desaturates very bright mixed colors. It is not ACES
or a claim to reproduce an IMAX response. Encode `12.92*y` for `y <= .0031308`,
otherwise `1.055*y^(1/2.4)-.055`, clamp to [0,1], then `floor(255*y+.5)`.
Apply this transfer exactly once. Zero EV preserves input radiance before the curve;
it is not an identity mapping from HDR to bytes. No dither is proposed for this version.

One CPU-testable `BHR_HD` display function is used by CUDA. Headless needs no GL.
The viewport presents those encoded bytes with `GL_RGBA8`; explicitly disable and
restore `GL_FRAMEBUFFER_SRGB` during the image draw to avoid a second encoding.
Byte parity tests precede a real framebuffer capture at 1:1 size. PNG output gets a
cinematic-only sRGB chunk with valid CRC through the existing PNG/miniz machinery;
the legacy writer remains byte-identical. No color-managed monitor/HDR display claim.

Independent color checks: sRGB encode .0031308 → .040449936, encode .18 →
.461356130, encode .5 → .735356983; inverse .04045 → .003130804954. With EV 0,
radiance 1 → Reinhard .5 → byte 188; EV +1 → 2/3 → byte 213. Test black, scale,
primaries, thresholds, negative/NaN/infinite rejection, cap/overflow, alpha and no
double encoding. These expectations come from the declared equations and
[W3C's sRGB reference conversion](https://www.w3.org/TR/css-color-4/#color-conversion-code).

## Additive API and explicit schema migration

Leave `RenderParams`, `Scene`, `CinematicDocument`, `FrameSample`, v1 preset parsing,
`parse_cinematic`/`load_cinematic` and legacy rendering signatures unchanged.
Suggested CPU-only additions (names can be finalized within the accepted design):

```cpp
struct AppearanceV1; // immutable validated color, detail and bloom values
struct CinematicRenderDocument {
    const CinematicDocument scene_shots;
    const AppearanceV1 appearance;
};
struct CinematicRequest {
    const RenderParams params; // from unchanged evaluate_frame
    const AppearanceV1 appearance;
};
// Explicit new v2 parser/serializer/atomic save; old parser still rejects v2.
CinematicRenderDocument parse_cinematic_v2(const std::string&);
CinematicRenderDocument upgrade_cinematic(const CinematicDocument&, const AppearanceV1&);
```

Add a CUDA-only target API beside these CPU types: `RadianceTarget` (pointer,
capacity, width/height), `PixelStatusTarget`, `CinematicWorkspace` (checked reduced
buffers), `render_radiance_device(request, starfield, target, status, stream)`, and
`display_device(appearance, target, status, workspace, rgba8, capacity, stream)`.
Provide a blocking `render_cinematic` wrapper that returns a new image or explicit
failure. Validate shape, alignment, capacities, overlap and resource budget before
dispatch. Async calls allocate/copy/synchronize nothing; caller owns all resources
through completion. Device writes are owned output operations; shared snapshots remain
immutable. Exposure-only fixtures can call display without tracing rays.

The [complete draft scene](plan8-review/scene-v2.proposal.json) saves the proposed
physical settings, appearance and both shot kinds. It is unsupported by the current
binary; its original star asset will be created only after design acceptance.
The **new** strict `bhr.cinematic` schema v2 retains `scene` and `shots` unchanged
and requires one top-level `appearance` object. The embedded preset stays v1.
Example appearance (proposal data, unsupported by the current executable):

```json
{
  "schema_version": 1,
  "working_space": "linear-srgb-d65",
  "radiance_model": "relative-disk-v1",
  "temperature_scale": 0.15,
  "disk_detail": 0.18,
  "outer_fade_fraction": 0.1,
  "star_intensity": 0.15,
  "exposure_ev": 1.0,
  "tone_map": "reinhard-rgb-v1",
  "output_transfer": "srgb",
  "bloom": {"enabled": false, "strength": 0.06, "threshold": 1.0}
}
```

Every displayed field is required. Reject extras, duplicates, wrong types, nulls and
unknown versions/enums. Proposed numeric ranges: temperature scale [.01,4], detail
[0,.3], fade [0,.25], stars [0,16], bloom strength [0,.15], threshold [.1,16].
Keep 1 MiB and depth-6 limits, checked integer/float parsing and transactional save.
Use shared internal field codecs where warranted, not stringify/reparse with weakened
duplicate detection. An explicit upgrade returns a new value; no silent rewrite or
automatic v1 appearance. CLI `--shot-file` dispatches by strict supported document
version; v1 invokes the exact old renderer, v2 evaluates the same frames then uses
the additive request. Invalid v2 must never fall back to v1.

The new fixture will live at `presets/cinematic/scene.json`, with a document-relative
`assets/stars-v1.exr`; `scene.preset.enable_starfield=true` still requires that asset.
Missing/invalid assets fail, never substitute a sky. An explicit downgrade must reject
appearance loss unless separately requested. No downgrade command is needed for Plan 8.
Keep selected-frame CLI argument exclusivity. A workbench **startup selected-frame
option** and `Viewport::submit(CinematicRequest, ...)` provide presentation parity;
no timeline, shot editing or appearance-control expansion is proposed.

Plan 9's future spatial handoff is disk-hit `r` in M and wrapped BL `phi` in radians,
with `u=(r-r_inner)/(r_outer-r_inner)` and `v=fract(phi/(2*pi))`. Static detail is
periodic in v, resolution independent in source coordinates, and uses no clock/phase.
The evaluator's exact time/loop metadata stays untouched. This is a proposed contract,
to freeze only after implementation and final review.

## Bloom and resource budget

Bloom is a display approximation, not a corona. Threshold exposed radiance by
`rgb * max(Y-threshold,0)/max(Y,epsilon)` using Rec.709 luminance; average valid
source texels into ceil(W/4)×ceil(H/4). Use two float4 ping-pong buffers, separable
Gaussian blur with sigma 2 / radius 6 reduced pixels, normalized fixed weights,
clamped edges (no wrap), then bilinear upsample and add `strength * bloom` before
tone mapping. Radius is fixed in this version (about 8 full-resolution pixels sigma).
Handle partial edge boxes and one-pixel images. Exclude diagnostic pixels from bloom
input and overwrite their output with diagnostics. No full-resolution blur buffer.

| 1920×1080 allocation | MiB |
|---|---:|
| Float4 radiance | 31.64 |
| RGBA8 target / GUI PBO | 7.91 |
| One byte/pixel status | 1.98 |
| Two quarter-dimension float4 bloom buffers | 3.96 |
| 2048×1024 float4 sky texture | 32.00 |
| Two GUI RGBA8 textures | 15.82 |
| Total steady GUI image resources | 93.31 |

Headless image resources: 77.49 MiB GPU plus 7.91 MiB host PNG pixels, excluding
decode staging/context overhead. Target <=128 MiB GUI image resources at 1080p
including temporary resize texture; hard checked cinematic resource budget 256 MiB
per request with clear rejection rather than overflow/OOM. Keep the last good image
on a failed allocation/launch/load. Reject overlapping device buffers.
Use an additive cinematic asset-load limit (source <=8 Mi pixels, upload width <=2048)
checked before decoding; legacy loader limits stay intact. Track decoder CPU peak
separately; target <512 MiB for the bundled sky and one frame. These are budgets,
not measured cinematic consumption. No packages or dependency upgrades are needed.

At 1080p, target display <=2 ms, bloom <=3 ms and additional shading <=10% of the
same-scene legacy trace cost, measured separately with CUDA events. Missing a target
requires reporting/tradeoff review, not weakening quality. Measure combined GPU time,
end-to-end host/GUI time and peak resources after implementation. No 60 FPS claim.
Fresh legacy workbench-preset baseline: **146.639 ms GPU mean**, 145.700–147.745 ms,
10 frames after 2 warmups, RTX 3080 / CUDA 12.0.140 / driver 595.58.03 / Release sm_86.
This is a different camera from the proposed curated scene; it is not its forecast.

## Research, reuse and assets

GitHub repository/code searches found `google/filament`, `ebruneton/black_hole_shader`
and other renderers. Consulted Filament's
[pinned ToneMapper.cpp](https://github.com/google/filament/blob/b073ca02937446d1cac77fd34c781708afde8500/filament/src/ToneMapper.cpp)
(Apache-2.0) and [vendor rendering guide](https://google.github.io/filament/main/filament.html)
for HDR/post-process separation. No shader/math copied. Reuse our existing Reinhard
equation, [Reinhard et al.'s operator](https://www.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf),
existing CPU/CUDA helpers, strict JSON/persistence, TinyEXR/miniz, PNG and viewport ownership.
The [Helland source](https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html)
confirms an approximate bounded color conversion, not calibrated emission.

Inspected `vcpkg.json`, submodule SHAs and `THIRD_PARTY_NOTICES.md`; checked
[TinyEXR registry metadata at our vcpkg baseline](https://github.com/microsoft/vcpkg/blob/04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4/ports/tinyexr/vcpkg.json).
Registry 3.2.0 is not the vendored revision; retain the existing pinned submodule.
No new runtime libraries, npm/pip packages, toolchain changes or installs. npm/pip
audits are inapplicable. Dependency adoption would require exact-version/license review.

External reference images are not bundled with the repository, runtime or release.
The board loads DNEG/NASA images from their source URLs and links the paper. Local
research copies stay in the home cache. Fig. 16 is Warner Bros., with CC BY-NC-ND 3.0
terms printed in the caption; retain its attribution/title/journal/DOI if separately
sharing that figure. DNEG's site retains its own rights. NASA image credit is
Goddard Space Flight Center/Jeremy Schnittman; see its
[media guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/).
These sources grant no license to incorporate film textures into the renderer.
The proposed original static star catalog/EXR and disk detail will be project MIT
assets with provenance, deterministic construction parameters and checksums recorded.

## Implementation batches and verification after acceptance

One owner handles rendering and integration. A: CPU types, strict v2 migration,
validation, independent color tests first, target/capacity contracts. B: pre-tone
linear shading, shared display/PNG encoding and exact legacy dispatch. C: approved
static lanes/star asset, then optional bloom and actual still iteration. Each batch
gets a scoped conventional commit after review and its relevant tests.

Retain all existing tests and 25/15 dB gates unchanged. Measure >=80% new CPU line
coverage; run CPU ASan/UBSan, CUDA-free/headless/GUI builds, malformed-schema/failed-save
tests, fixed/orbit determinism, resource failures and actual presentation/readback.
Compare identical headless/GUI pixels and framebuffer orientation/color at 1:1.
Inspect several inclinations/spins with glow off/on, preserve shadow classification
and diagnostic visibility, and measure costs above. Record unavailable CUDA sanitizer
instrumentation honestly; do not install or repair tooling under this assignment.

## Fresh design-stage validation and continuation

Rebuilt unchanged `abc3d21` in new `plan8-{cpu,headless,gui}` directories. CPU Release
55/55, headless 85/85, GUI 86/86 including actual presentation and install smoke pass.
Legacy golden bytes and repaired ray/PSNR tests pass at unchanged thresholds.
This historical baseline predates the approved implementation. Current CPU coverage,
CPU sanitizer rerun, cinematic parity, HDR/bloom costs, and actual captures are
recorded in [Validation](VALIDATION.md) and the [still review board](plan8-review/stills/index.html).
Final user visual acceptance remains pending. See [reproduction and logs](plan8-review/README.md).

`build` and `imgui.ini` are preserved. The configured HDD mount is absent; small
artifacts/builds use the existing home cache, and baseline review images are about
3 MiB in the repository. No push, publication or Plans 9–14 work.

Design approval record: **approved 2026-09-07**. Final visual acceptance record:
**pending**. Record the user's actual decision in the TaskVault task/output; do not
infer it from silence.
