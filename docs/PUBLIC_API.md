# Public API contracts

Candidate version: **0.9.0-rc.1**. The headers document usable C++ interfaces,
but this candidate does not promise a stable binary ABI or ship a supported
standalone development package.

## Rendering

`bhr::render_device(params, starfield, pixels, capacity_bytes, stream)` validates
the scene and launches asynchronously into caller-owned, packed RGBA8 CUDA
memory. It performs no allocation, synchronization, or readback. The caller
keeps the output buffer, CUDA mapping, stream, and starfield alive until stream
or event completion and checks asynchronous execution errors.

`bhr::render(params, image)` and its starfield overload are blocking host-image
wrappers over the same kernel. Failure leaves `image` empty and reports a
diagnostic.

## Scene and preset data

`bhr::RenderParams` is trivially copyable and validated before rendering. Spin
is currently prograde only (`0` through `0.999`). RK45 is the reference/default;
`kGeokerr` selects the documented approximate backend.

`bhr::save_preset` and `bhr::load_preset` use the strict version-1 JSON schema in
`bhr/presets.hpp`. Loading is transactional: invalid input does not partially
replace active parameters. External starfield paths are intentionally not
embedded in preset JSON.

`bhr/shot.hpp` adds immutable CPU-only `Scene`, `Shot`, `CinematicDocument` and
`FrameSample` snapshots. The separate strict `bhr.cinematic` v1 format embeds a
complete legacy preset. `evaluate_frame` resolves exact rational frame time and a
fixed or smooth unwrapped BL orbit into the existing `RenderParams`. Load/import
return new values; save uses atomic replacement. See [SHOTS.md](SHOTS.md) for the
producer API, bounds, relative assets and selected-frame CLI. Runtime GPU ownership
and the legacy preset APIs remain unchanged.

## Images and starfields

`bhr::Image` owns host RGBA8 pixels and supports PNG output. `bhr::Starfield`
owns CUDA texture/array resources, is move-only, and accepts the limited EXR
formats described in `bhr/starfield.hpp`. Callers must not replace or destroy a
starfield while an asynchronous render that samples it is in flight.

## Cinematic appearance v1 and render document v2

`AppearanceV1` is immutable, validated appearance data. `CinematicRenderDocument`
wraps an unchanged `CinematicDocument` with that appearance; strict
`bhr.cinematic` v2 requires it. `parse_cinematic_v2`, `load_cinematic_v2`, and
`save_cinematic_v2` handle v2 only. `upgrade_cinematic` explicitly returns a new
v2 value; v1 parsing remains strict and invalid v2 never falls back to v1.
`parse_render_document`/`load_render_document` dispatch a supported version while
preserving the legacy v1 producer contract.

`CinematicRequest` combines an evaluated immutable `RenderParams` snapshot with
`AppearanceV1`. `CinematicDeviceTarget` is a caller-owned, 16-byte aligned CUDA
workspace. `render_radiance_device`, `bloom_device`, `display_device`, and
`render_cinematic_device` are allocation-free asynchronous operations ordered on one
stream; they neither copy nor synchronize. The target, output RGBA8 storage,
starfield, and stream must remain valid until completion. Shapes, capacities,
alignment, overlap, assets, appearance ranges, and the 256 MiB request budget are
validated before dispatch. `render_cinematic` is the blocking host-image wrapper and
rejects an invalid-radiance frame rather than returning a partial image.

The radiance target has packed top-left row-major `alignas(16) RadiancePixel`
(`float R,G,B,A`, 16 bytes/pixel): nonnegative finite linear sRGB/Rec.709-primary,
D65 relative artistic radiance in `[0, 65504]`, with opaque alpha exactly one.
`PixelStatus` records valid, unknown, invalid, and clipped pixels. Unknown/invalid
pixels display as diagnostic magenta and are excluded from bloom; clipping and invalid
counts are exposed in `FrameDiagnostics`.

`Viewport::submit(CinematicRequest, Starfield)` provides the corresponding workbench
submission boundary. It retains the existing last-good image behavior. The ImGui image
is drawn into an sRGB framebuffer with `GL_FRAMEBUFFER_SRGB` disabled for that draw
and restored afterwards, preventing a second transfer encoding.

## Animated emission v1 and render document v3

`EmissionV1` is explicit, bounded artistic disk activity. `AnimatedRenderDocument`
wraps the unchanged scene/shots and appearance values with it; strict
`bhr.cinematic` v3 requires a version-1 emission object and `loop_frames` on every
shot. `AnimatedCinematicRequest` adds an exact rational `EmissionPhase` to the
immutable render snapshot. `render_animated_radiance_device`, `render_animated_device`,
and `render_animated` use actual disk-hit Boyer–Lindquist `r`/`phi` coordinates and
never depend on prior frame order, wall time or a finished framebuffer. See
`docs/ANIMATED_EMISSION.md` for field ranges and the temporal contract.

## Workbench boundary

The workbench's `Viewport` owns the persistent mapped PBO, alternating OpenGL
textures, CUDA event, and GL fences. It is an application implementation detail,
not a public library interface. A hidden-window integration test exercises its
resource lifetime, resize, state restoration, and last-image guarantees.
