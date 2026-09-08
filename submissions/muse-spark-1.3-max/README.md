# Black Hole — Realtime GPU Simulation (C++ / OpenGL)

A scientifically grounded, realtime 3D black-hole simulation written from scratch
in C++17 + OpenGL 4.1 (GLSL). No engine, no black-hole reference code — just
general relativity, a geodesic integrator, and the GPU.

All work lives in this folder (`black-hole-cpp-musemax`). The binary opens a
native window and runs the simulation in realtime. Press `S` anytime to save
a PPM screenshot (convert with `sips -s format png shot.ppm --out shot.png`).

## Views

| Key | View | What it shows |
|-----|------|---------------|
| `1` | Geodesic ray tracing | Fullscreen GR ray tracer: lensing, shadow, disk |
| `2` | Spacetime grid | Flamm's paraboloid "trapdoor", light rays, particles |
| `3` | Split | Both side by side |

## Features

- **Ray-traced gravitational lensing** — every pixel integrates a
  Schwarzschild null geodesic (orbital-plane reduction, velocity Verlet, exact
  in geometric units M = 1). Background star field, galaxy band, and nebulae
  are sampled along the bent ray, producing Einstein rings and higher-order
  images for free.
- **Accretion disk** — Novikov-Thorne/Shakura-Sunyaev-style thin disk with the
  flux profile F(r) ~ r^-3 (1 - sqrt(r_isco/r)), turbulent advected structure,
  and the full GR redshift factor g = (k·u_obs)/(k·u_emit): gravitational
  redshift plus relativistic Doppler beaming (one side brighter/bluer).
  Temperature maps to a blackbody ramp; brightness ~ (g·T)^3.5.
- **Photon-ring halo** — rays near the critical curve b = 3√3 M pile up into
  the glowing photon ring; a narrow analytic halo term adds the bloom.
- **Spacetime curvature ("trapdoor")** — Flamm's paraboloid embedding
  w(r) = 2√(rs(r-rs)) rendered as a lit funnel + wireframe grid, meeting a
  flat reference grid at large r. Draped on it: the glowing disk, the photon
  ring (r = 3M), the spin-dependent ISCO ring, CPU-traced bent light rays
  (cyan = escapes, ember = captured), and test particles including plunging
  orbits and GR perihelion-precessing ellipses.
- **Realtime + interactive** — orbit/zoom camera, auto-orbit, 3 quality
  presets, adjustable spin, pause, screenshots, live FPS/status overlay.

## Physics model (honest version)

- Light bending integrates **exact Schwarzschild null geodesics**. Verified:
  capture threshold b_crit = 3√3 M to 8e-5, weak deflection 4M/b to ~2%,
  shadow angular size, gravitational + transverse redshifts (see
  `--selftest`, 19 checks, all passing).
- Spin `a/M` drives the **Kerr disk model** (Bardeen ISCO, Kerr Ω, Doppler
  velocities) while bending stays Schwarzschild. At spin 0 the whole render
  is fully self-consistent GR; at high spin the disk inner edge moves inward
  (Interstellar-style) with bending as the controlled approximation. Disk
  inner edge is clamped ≥ 2.5M so it never enters the ray tracer's horizon.
- Disk pattern advection speed and particle motion use an illustrative time
  rate (real geometric-time motion would be imperceptible/aliasing).
- Embedding-diagram vertical scale is compressed 0.55x for framing; noted
  in-app. Particle precession uses the weak-field 6πM/a(1-e²) formula.

## Build

Requirements: CMake 3.16+, a C++17 compiler, GLFW3, glm headers.
macOS needs only Xcode + Homebrew packages (OpenGL 4.1 is system-provided);
Linux additionally needs GLEW.

```bash
brew install cmake glfw glm        # macOS (once)
./build.sh                         # builds ./build/blackhole
./build/blackhole                  # opens the realtime window
```

Linux: `apt install cmake libglfw3-dev libglm-dev libglew-dev` then `./build.sh`.

## Run

```bash
./build/blackhole                  # realtime window (1280x800, resizable)
./build/blackhole --selftest       # 19 physics unit tests, no window
./build/blackhole --screenshot out.ppm [W H]
                                   # hidden-window render + checks to PPM
```

`--screenshot` also verifies the GPU image itself: face-on shadow radius vs
the analytic sin α = (b_crit/r)√(1-rs/r) prediction (±20%), and disk
left/right Doppler asymmetry (> 1.25x).

## Controls

| Input | Action |
|-------|--------|
| drag / wheel | orbit / zoom |
| `1` `2` `3` | lensed / spacetime-grid / split view |
| `Q` | quality LOW → MED → HIGH (steps 160/288/448) |
| `+` / `-` (`Shift` = coarse) | spin a/M ± (rebuilds ISCO + disk) |
| `X` | spin = 0 (exact self-consistent GR) |
| `R` / `Space` | auto-orbit / hold time |
| `O` / `F` | overlay / fullscreen |
| `S` | save screenshot (`screenshot_NNN.ppm`) |
| `H` / `Esc` | help / quit |

## Files

```
shaders/raytrace.frag   GPU null-geodesic integrator + disk + sky (the core)
shaders/raytrace.vert   fullscreen triangle
shaders/grid.*          lit/unlit/fresnel shader for the embedding scene
shaders/overlay.*       pixel-space text shader
src/physics.hpp         constants, Bardeen ISCO, CPU geodesic mirror, disk model
src/physics.cpp         19 scientific self-tests
src/spacetime.*         Flamm funnel, disk, rings, rays, particles
src/overlay.*           dependency-free 5x7 font overlay
src/camera.hpp          orbit camera
src/main.cpp            app, input, modes, screenshot verification
CMakeLists.txt build.sh .gitignore
```

## Performance notes

Cost per pixel ≈ steps × ~40 FLOP. MED (288 steps) at 1280×800 ≈ 30 GFLOP
per frame — Apple Silicon iGPUs sustain this at 60 Hz; larger/retina windows
may prefer LOW, or windowed (non-fullscreen) sizes. `Q` switches live.

## References (theory, not code)

Bardeen-Press-Teukolsky 1972 (ISCO); Luminet 1979 (disk imaging);
Page-Thorne 1974 (thin-disk flux); Misner-Thorne-Wheeler (Flamm paraboloid);
Einstein 1915 (4M/b deflection). All code derived from these from scratch.
