# Schwarzschild black hole: real-time C++ ray tracer

This is a native macOS OpenGL 4.1 application. Launching it opens an interactive window with a ray-traced black hole and accretion disk on the left, and a 3D spacetime-grid visualization on the right. It was built and run on an Apple M4 Max.

## Run

Requirements: CMake, GLFW 3, a C++17 compiler, and macOS OpenGL. On a Mac with Homebrew, install the first two with `brew install cmake glfw`.

```sh
cd black-hole-cpp-sol6
./run.sh
```

The script builds the app in `build/` and starts it immediately. For a direct run after building, use `./build/black-hole`.
The build also refreshes `BlackHole.app`, which can be opened from Finder or with `open BlackHole.app`.

| Input | Action |
| --- | --- |
| Left drag | Orbit the camera around the black hole |
| Mouse wheel or trackpad scroll | Zoom |
| `[` / `]` | Lower / raise render resolution |
| `D`, `H`, `G` | Toggle disk, halo, or grid |
| Space | Pause the disk animation and grid rotation |
| `R` | Reset the camera |
| `S` | Save `black-hole-screenshot.ppm` in this folder |
| Esc | Quit |

For a one-shot image, run `./build/black-hole --capture image.ppm`. To print live frame-rate samples and exit after five seconds, run `./build/black-hole --benchmark 5`.

## Physics

We use geometrized units, `G = c = M = 1`, and a **nonspinning Schwarzschild black hole**. Consequently, the event horizon is at `r = 2`, the photon sphere is at `r = 3`, the critical impact parameter is `3√3`, and the disk's innermost stable circular orbit (ISCO) is at `r = 6`. The finite observer starts at `r = 39`.

Each image pixel starts a photon in the observer's local orthonormal frame. A fourth-order Runge-Kutta integrator traces its null geodesic backwards through the Schwarzschild metric, using the Cartesian-coordinate Hamiltonian

```text
H = 1/2 [-E²/f + |p|² - 2M(x·p)²/r³],   f = 1 - 2M/r,   H = 0.
```

Rays that reach the horizon are black. Rays that escape sample a fixed celestial sphere; its stars are deliberately synthetic so repeated, distorted star images reveal lensing. Rays meeting the equatorial disk sample the first optically thick disk crossing, including the lensed upper arc and thin higher-order images.

The disk occupies `6M ≤ r ≤ 18M`. Its circular geodesic angular velocity is `Ω = √(M/r³)`. Its radial emission follows the **zero-torque Page–Thorne thin-disk flux profile** for a Schwarzschild hole, normalized for display. The observed disk energy shift includes gravitational and orbital Doppler factors, and the bolometric brightness is multiplied by `g⁴`. The approaching side is therefore brighter. The moving filaments, visible colors, flux normalization, and faint optically thin halo are illustrative rendering choices rather than a solved gas or radiative-transfer simulation.

The right-hand grid is a genuine 3D mesh of **Flamm's paraboloid**, with embedding height `z(r) = 2√[2M(r−2M)]`. It represents a two-dimensional *spatial* equatorial slice at fixed Schwarzschild time. The vertical plotting direction is not a physical fourth dimension or a force dragging objects down. Orange, blue, and gold rings mark the horizon, photon sphere, and ISCO.

This model does not include black-hole spin, frame dragging, an evolving magnetized plasma, disk self-gravity, or spectral radiative transfer. The disk cutoff at `18M`, corona height, and celestial sky are visualization choices. Spatial steps are adaptive, but the 500-step cap can leave very near-critical rays dark after they spend a long time near the photon sphere.

## Verification

- Built successfully with AppleClang and ran in a visible GLFW window on an Apple M4 Max.
- At full Retina resolution (3000 × 1700 framebuffer), the five-second benchmark reported approximately 64–75 frames per second after startup on that machine. Use `[` to lower resolution on slower GPUs.
- A separate numerical check of the same Hamiltonian and step rule captured rays with impact parameter `b = 5.19M` and let `b = 5.20M` escape, bracketing the analytic threshold `3√3 M ≈ 5.19615M`. The maximum null-Hamiltonian residual along the `b = 5.20M` escaping ray was below `4×10⁻⁶` in the chosen units.

## References

- [Aarhus University general relativity notes: Schwarzschild geodesics](https://phys.au.dk/~fedorov/backup/GTR/05/note10.htm)
- [Page & Thorne, *Disk-Accretion onto a Black Hole. I* (1974)](https://articles.adsabs.harvard.edu/pdf/1974ApJ...191..499P)
- [University of Tennessee notes: Flamm's paraboloid](https://web.math.utk.edu/~freire/teaching/m568s12/m568s12Schwarzschild.pdf)
- [University of Colorado: Schwarzschild geometry and embedding diagram](https://jila.colorado.edu/~ajsh/bh/schwp.html)
