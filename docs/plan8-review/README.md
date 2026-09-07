# Plan 8 design review evidence

Open [index.html](index.html) in a browser for the visual reference board. The
[proposal](../CINEMATIC_APPEARANCE_PROPOSAL.md) is **pending design acceptance**.
Every local black-hole PNG here is unedited legacy renderer output, not the new
cinematic pipeline. External reference images are linked, not bundled.
[scene-v2.proposal.json](scene-v2.proposal.json) is a complete design candidate,
not a loadable current fixture; its proposed star asset does not exist yet.

Source tested: `abc3d21`, with Plan 7 implementation `bf07b1d`. Source code,
legacy fixtures/goldens and thresholds are unchanged in this documentation batch.

## Reproduce the builds

Executed from the repository with existing dependencies and CUDA 12.0.140:

```bash
cmake --preset linux-headless -B "$HOME/.cache/blackhole-renderer/plan8-headless"
cmake --build "$HOME/.cache/blackhole-renderer/plan8-headless" -j 4
ctest --test-dir "$HOME/.cache/blackhole-renderer/plan8-headless" --output-on-failure

cmake --preset cpu-tests -B "$HOME/.cache/blackhole-renderer/plan8-cpu"
cmake --build "$HOME/.cache/blackhole-renderer/plan8-cpu" -j 4
ctest --test-dir "$HOME/.cache/blackhole-renderer/plan8-cpu" --output-on-failure

cmake --preset linux-release -B "$HOME/.cache/blackhole-renderer/plan8-gui" \
  -DSDL2_DIR="$HOME/.cache/blackhole-renderer/plan5-cached-sdl2/share/sdl2"
cmake --build "$HOME/.cache/blackhole-renderer/plan8-gui" -j 4
ctest --test-dir "$HOME/.cache/blackhole-renderer/plan8-gui" --output-on-failure
```

CPU 55/55, headless 85/85, GUI 86/86 pass; GUI presentation actually ran.
Logs: [CPU](evidence/cpu-ctest.log), [headless](evidence/headless-ctest.log),
[GUI](evidence/gui-ctest.log). Golden bytes and strict repaired ray tests pass.
[Fresh PSNR readback](evidence/psnr.log): Schwarzschild 27.0318 dB against 25 dB;
Kerr 29.7607 dB against 15 dB. No threshold changes.
Build warnings in unchanged CUDA/third-party sources remain; there were no errors.
CPU sanitizers and coverage were not rerun for this design-only batch. No CUDA
instrumentation pass is claimed; Plan 7 recorded failure before instrumentation.

## Reproduce the selected baseline stills

Saved v1 JSON is authoritative. Original source: `presets/reference.json` and
`presets/shots/reference.json`; variations are enumerated in the proposal.
This loop merely reproduces the finite review samples; it adds no frame exporter.

```bash
CLI="$HOME/.cache/blackhole-renderer/plan8-headless/app/blackhole-cli"
REVIEW="docs/plan8-review/baselines"
OUT="$HOME/.cache/blackhole-renderer/plan8-reproduce"
mkdir -p "$OUT"
for preset in "$REVIEW"/*.json; do
  stem="${preset##*/}"
  stem="${stem%.json}"
  if test "$stem" = reference-shots-1024; then continue; fi
  "$CLI" --params "$preset" --output "$OUT/$stem.png"
  cmp "$REVIEW/$stem.png" "$OUT/$stem.png"
done
"$CLI" --shot-file "$REVIEW/reference-shots-1024.json" --shot-id fixed --frame 0 --output "$OUT/fixed-000.png"
"$CLI" --shot-file "$REVIEW/reference-shots-1024.json" --shot-id orbit --frame 0 --output "$OUT/orbit-000.png"
"$CLI" --shot-file "$REVIEW/reference-shots-1024.json" --shot-id orbit --frame 300 --output "$OUT/orbit-300.png"
"$CLI" --shot-file "$REVIEW/reference-shots-1024.json" --shot-id orbit --frame 599 --output "$OUT/orbit-599.png"
cmp "$OUT/fixed-000.png" "$OUT/incl-85.png"
for stem in fixed-000 orbit-000 orbit-300 orbit-599; do
  cmp "$REVIEW/$stem.png" "$OUT/$stem.png"
done
(cd docs/plan8-review && sha256sum -c SHA256SUMS)
```

All 15 PNGs were inspected and their exact `(255,0,255)` unknown-hit pixels counted.
See [pixel counts](evidence/pixel-counts.csv). There are 2 at inclination 75°, 3
at 85°/fixed, and 10 at orbit frame 599; all other samples have zero. Zero unknown
pixels does not prove subpixel accuracy: inner-boundary speckling is still visible.
No recoloring, cleanup, glow, masking or physics edits were applied.
The new curated 1080p composition command completed in 0.156 seconds including
the CLI's processing scope; this single run is not an isolated GPU benchmark.

## Measured legacy benchmark

```bash
"$HOME/.cache/blackhole-renderer/plan8-headless/app/blackhole-benchmark" \
  --resolution 1920x1080 --integrator rk45 --warmup 2 --frames 10
```

[Raw results](evidence/baseline-benchmark-1080.log): GPU mean 146.639 ms,
min 145.700 / max 147.745 ms, synchronized wall mean 146.948 ms. Allocation and
warmup are excluded; no PNG/readback/window. Scene is the existing workbench preset,
not the curated composition. GPU: RTX 3080 12 GiB, sm_86; driver 595.58.03.
Future cinematic radiance/display/bloom times and peak resources are unmeasured.

## Board checks and local artifact storage

Playwright loaded every reference/local image and exercised inclination/orbit
selectors. Mobile layout has no horizontal overflow; no page JavaScript errors.
All 15 documented PNG reproductions and SHA-256 comparisons pass; see
[reproduction log](evidence/reproduction.log).
Desktop screenshot visually inspected; reference photography is not
repackaged as a runtime asset. The local screenshot and source-reference research
copies are at `$HOME/.cache/blackhole-renderer/plan8-review/`.
The configured HDD mount is absent; isolated builds use the existing home cache.
No local environment, dependency or system configuration was changed.

The board's references require network access; local baseline images and the proposal
remain usable offline. No server is required to open the HTML file.
The repository includes only original renderer images, saved settings, logs, checksums
and design documentation. Nothing was pushed, tagged, released or published.
