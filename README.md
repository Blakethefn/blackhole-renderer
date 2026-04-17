# Black Hole Renderer

A physically accurate, GPU-accelerated Kerr black hole renderer with an interactive C++ workbench. Uses real general relativity — Kerr geodesics, Novikov-Thorne accretion disk, Doppler shift, gravitational redshift, and HDR starfield lensing.

> **Status:** in early development. This README will be expanded as the project takes shape.

## Quick start

```bash
git clone --recursive https://github.com/<user>/blackhole-renderer.git
cd blackhole-renderer
./setup.sh
./build/app/blackhole-workbench
```

## References

- James, von Tunzelmann, Franklin, Thorne 2015 — *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar* (Class. Quantum Grav. 32, 065001)
- Dexter, Agol 2009 — *A Fast New Public Code for Computing Photon Orbits in a Kerr Spacetime* (ApJ 696, 1616)
- Chan, Psaltis, Özel 2013 — *GRay: A Massively Parallel GPU-Based Code for Ray Tracing in Relativistic Spacetimes* (ApJ 777, 13)

## License

MIT
