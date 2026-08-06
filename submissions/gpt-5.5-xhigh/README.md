# Black Hole C++ Codex

Realtime macOS/OpenGL simulation of a Schwarzschild black hole with curved-ray tracing, gravitational lensing, a slowed visible light-ray deflection demo, optional relativistically shaded accretion disk, photon/corona halo emission, and a warped spacetime grid.

## Build and run

```sh
cmake -S . -B build
cmake --build build --config Release
open build/BlackHoleSim.app
```

The app opens a native macOS window and renders on the GPU through OpenGL 4.1.

## Controls

- Drag mouse: orbit camera
- Scroll: zoom
- Space: pause animation
- `L`: toggle the visible thrown light ray
- `S`: toggle slow/normal light-ray pulse speed
- `D`: toggle accretion disk
- `G`: toggle spacetime grid
- `H`: toggle photon halo/corona
- `[` / `]`: decrease/increase geodesic steps
- `-` / `=`: decrease/increase exposure
- `R`: reset camera and quality
- `Q` or Escape: quit

## Physics model

- Units use `c = G = 1` and Schwarzschild radius `r_s = 1`.
- Light bending is traced in the Schwarzschild optical metric in isotropic coordinates:
  `n(rho) = (1 + r_s/(4 rho))^3 / (1 - r_s/(4 rho))`.
  This is the exact Fermat-principle spatial light path outside a static Schwarzschild horizon; the fragment shader integrates the transverse gradient of `log(n)` for each camera ray.
- The event horizon is at isotropic radius `rho = r_s / 4`.
- The photon sphere halo is centered at areal radius `R = 1.5 r_s`.
- The default visible light ray starts far from the hole with a slight transverse offset and is numerically stepped through the same Schwarzschild optical-metric acceleration as the lensing shader. The accretion disk is off by default in this mode so the bending is easier to see.
- The disk inner edge is the Schwarzschild ISCO at areal radius `R = 3 r_s`; the disk uses a thin-disk temperature falloff, gravitational redshift, and special-relativistic Doppler beaming from circular orbital velocity.
- The spacetime grid uses a Flamm-paraboloid embedding of the Schwarzschild equatorial spatial slice. It is drawn as a visual grid surface and ray-traced through the same curved light paths.

## Scope and approximations

This is a realtime educational renderer, not a full general-relativistic radiative-transfer solver. It intentionally models a non-spinning black hole, a thin emissive disk, approximate local opacity, procedural turbulence, and simplified halo emission. The ray bending basis and landmark radii are physically grounded, while the visual emission model is tuned for interactive clarity.
