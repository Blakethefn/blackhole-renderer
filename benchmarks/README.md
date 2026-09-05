# RTX 3080 preview measurements — 2026-09-05

Measured locally with Release/sm_86, GCC 12.4, CUDA 12.0.140, NVIDIA RTX 3080
12GB, driver 595.58.03, SDL2 2.32.10, X11 and NVIDIA OpenGL. No other task
benchmarks ran concurrently. Each scene uses two warmup frames followed by ten
measured frames. Raw samples and complete scene settings are in the adjacent
[measurement logs](2026-09-05-rtx3080/).

The deterministic scene is `bhr::workbench_preset()` / `presets/reference.json`:
spin 0.9, camera radius 30 M/inclination 85°/azimuth 0°/FOV 35°, disk inner 2.321 M,
outer 20 M, temperature 40000 K, brightness 1, all disk effects enabled, starfield off.

| Integrator | Resolution | GPU mean (ms) | Preview mean (ms) | Completed previews/s | Max UI interval (ms) | Max event latency (ms) |
|---|---|---:|---:|---:|---:|---:|
| rk45 | 256x144 | 0.514 | 50.168 | 19.933 | 16.726 | 17.000 |
| rk45 | 512x288 | 1.237 | 50.166 | 19.933 | 16.726 | 16.000 |
| rk45 | 1024x576 | 4.306 | 50.163 | 19.935 | 16.728 | 17.000 |
| geokerr | 256x144 | 1.218 | 50.165 | 19.934 | 16.730 | 16.000 |
| geokerr | 512x288 | 1.416 | 50.174 | 19.930 | 16.732 | 17.000 |
| geokerr | 1024x576 | 67.099 | 112.030 | 8.926 | 16.729 | 17.000 |

All measured previews met the ≤333ms / ≥3FPS target. RK45 remains the default:
it is the correctness reference and was faster in this scene. This performance
does not establish physics accuracy or guarantee timings for other scenes.

`blackhole-preview-benchmark` uses the production Viewport, a hidden 1280×720
SDL/OpenGL window, ImGui rendering of the previous image, and a paced 60 Hz UI
loop. Preview time runs from submission through publication after the GL upload
fence. Completed-frame throughput includes dispatch and publication overhead.
The three polled GPU/GL phases add UI ticks, explaining the roughly 50 ms floor
at this pacing. This is GPU presentation readiness, not monitor scanout or a
visible-window vsync measurement. Cold allocation and shader initialization are
excluded by warmup. A separate SDL thread injects heartbeat events every 16 ms;
event latency measures how quickly the UI consumes them while rendering.

`blackhole-benchmark` measures only the shared render core on a persistent device
buffer, with CUDA events and synchronized wall time. Its larger FPS figures
exclude OpenGL presentation, UI pacing, startup, readback, and PNG output; do not
use those figures as workbench FPS.

Reproduce (run each command sequentially on an otherwise idle GPU):

```bash
/path/to/build/app/blackhole-preview-benchmark --integrator rk45 --resolution 1024x576 --warmup 2 --frames 10 --ui-hz 60
/path/to/build/app/blackhole-preview-benchmark --integrator geokerr --resolution 1024x576 --warmup 2 --frames 10 --ui-hz 60
/path/to/build/app/blackhole-benchmark --integrator rk45 --resolution 1024x576 --warmup 2 --frames 10
```

Repeat at 256x144 and 512x288 for the full table. Source resolution is independent
of the window size. Baseline image gates remained unchanged: Schwarzschild
PSNR 27.0318 dB, Kerr a=0.9 PSNR 18.1293 dB, golden PNG byte-identical. The two
pre-existing ray-classification failures remain visible in the full suite.
