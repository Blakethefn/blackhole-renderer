# Original static sky v1

`stars-v1.exr` is an original project asset under the repository MIT license.
It contains 1,400 deterministic finite Gaussian stars in a 2048×1024 HALF RGB
equirectangular image. RGB values are relative linear sRGB/D65 radiance; the
background is zero. It uses no astronomical catalog, downloaded image or film asset.

Reproduce using the pinned TinyEXR implementation and the project generator:

```sh
cmake --build BUILD --target generate-cinematic-stars
BUILD/app/generate-cinematic-stars presets/cinematic/assets/stars-v1.exr
```

The construction, fixed integer hash and original color distribution are in
`tools/generate_cinematic_stars.cpp`. Floating transcendental results may vary
between toolchains; the checked-in asset is the rendering reference. The loader
checks the source dimensions before decode and bounds the CUDA upload separately.
