# Plan 9 animated emission review

These are actual CUDA renders from `presets/cinematic/scene-animated.json`, not
concept art or post-processed images. The fixed shot is a 180-frame, 60 FPS loop;
the orbit shot uses the same 180-frame emission period while its camera continues
through a 600-frame path. Every capture reported `unknown=0 invalid=0 clipped=0`.

Render the same samples with:

```bash
BUILD=/path/to/blackhole-renderer-build
for frame in 0 45 90 135 179; do
  "$BUILD/app/blackhole-cli" --shot-file presets/cinematic/scene-animated.json \
    --shot-id fixed --frame "$frame" --output "docs/plan9-review/stills/fixed-${frame}.png"
done
"$BUILD/app/blackhole-cli" --shot-file presets/cinematic/scene-animated.json \
  --shot-id orbit --frame 300 --output docs/plan9-review/stills/orbit-300.png
```

Frame 0 and the wrapped phase at frame 180 use the same exact phase fraction; frame
180 is intentionally outside the fixed shot's `[0,180)` sample range and is not a
duplicated output. The pure CPU contract and CUDA render tests verify this wrap.

## Captured hashes

| Sample | SHA-256 |
|---|---|
| `fixed-000.png` | `e02933bd1ea50a55c8ba7379d19354a6cb6cd4dcf4ddc236ba4cb01709badb1a` |
| `fixed-045.png` | `8fa88198fa6250d3ed88d672648eb67d3b28e8db4a79afbc9ab11b3aafb9eb0b` |
| `fixed-090.png` | `cf4df84d0a1dd5b82f25fc0ffaaa42599505147b78a9235e0f4b088114e1b057` |
| `fixed-135.png` | `efc1acd7d74423fdfe32e6444a6dba67394afabfec5b51cd44f35f4180708fee` |
| `fixed-179.png` | `111ad50e766b61f9eb2e9a8c5e5a0e095b613dc5c17f0af1beb1d99d21e8517e` |
| `orbit-300.png` | `25a49136557aa6e76192bd4b7f7b7e70faebb2c5761af4b114affe84b963c998` |

Visual acceptance of activity strength, motion character and the loop seam remains
the final Plan 9 gate.
