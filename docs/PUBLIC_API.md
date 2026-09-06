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

## Images and starfields

`bhr::Image` owns host RGBA8 pixels and supports PNG output. `bhr::Starfield`
owns CUDA texture/array resources, is move-only, and accepts the limited EXR
formats described in `bhr/starfield.hpp`. Callers must not replace or destroy a
starfield while an asynchronous render that samples it is in flight.

## Workbench boundary

The workbench's `Viewport` owns the persistent mapped PBO, alternating OpenGL
textures, CUDA event, and GL fences. It is an application implementation detail,
not a public library interface. A hidden-window integration test exercises its
resource lifetime, resize, state restoration, and last-image guarantees.
