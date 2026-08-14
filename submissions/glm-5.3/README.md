# Schwarzschild Black Hole — Real-Time Geodesic Ray Tracer (C++ / OpenGL)

A scientifically grounded, real-time 3D simulation of a non-rotating
(Schwarzschild) black hole. Every pixel is a photon trajectory integrated
through curved spacetime on the GPU. No fake post-processed "lens" effects —
the bending, the shadow, the halos and the disk images all emerge from the
geodesic equation itself.

Run it and a window opens with the simulation running in real time.

```
./run.sh          # builds (if needed) and launches
```

Headless render (no window, writes a BMP): `./build/blackhole --offscreen W H FRAMES OUT.bmp [--mode 0|1|2]`

## What is simulated

| Feature | Physics |
| --- | --- |
| **Gravitational lensing (ray tracing)** | Each camera ray is a null geodesic of the Schwarzschild metric, integrated per-pixel (velocity-Verlet, adaptive step). The Cartesian form used, `d²x/dλ² = −(3/2) rs h² x/r⁵` with conserved `h = |x × v|`, is derived from the exact photon Binet equation `u″ + u = (3/2) rs u²`. It reproduces the photon sphere at `1.5 rs`, the capture shadow with critical impact parameter `b = 3√3 GM/c² ≈ 2.598 rs`, Einstein rings, and all higher-order images. |
| **Accretion disk + halos** | Geometrically thin, optically thick Keplerian (Shakura–Sunyaev) disk from the ISCO (`3 rs`) outward. Temperature profile `T(r) ∝ r^(−3/4) (1 − √(r_in/r))^(1/4)`. Orbital speed `β = √(M/(r−2M))` reaches **0.5 c at the ISCO**. Emission uses the relativistic invariant `I_ν/ν³`: observed temperature is `g·T` and bolometric intensity scales as `g⁴T⁴`, with total shift `g = √(1−rs/r) · √(1−β²)/(1−β·n̂)` (gravitational redshift × Doppler, with local-frame aberration). Colors are Planck blackbody. The vertical "halos" above/below the hole and the photon ring are not drawn — they are the far side and higher-order images of the disk, lensed by the geodesics. |
| **Spacetime curvature (trapdoor)** | The spacetime grid is the exact **Flamm paraboloid** embedding of the equatorial spatial slice, `z(r) = 2√(rs(r−rs))` for `r ≥ rs` — the "crater/trapdoor" geometry. ISCO (cyan) and photon-sphere (amber) reference circles are marked; matter streaks fall down the funnel. The grid is itself rendered through the geodesic tracer, so it is lensed and wraps around the hole. |
| **Real time** | GPU ray tracing via OpenGL 4.1 core (fullscreen fragment shader), adjustable integrator budget (320/560/900 steps) and render resolution scale. Typically 60 fps on Apple Silicon. |

Units: `rs = 1`, `G = c = 1`, so `M = 1/2`. Lengths on screen are in
Schwarzschild radii.

## Controls

| Input | Action |
| --- | --- |
| mouse drag | orbit camera |
| scroll | zoom (2.2 – 60 rs) |
| `R` | reset camera |
| `G` | cycle view: realistic → spacetime grid → combined |
| `V` / `B` | toggle accretion disk / star field |
| `SPACE` | pause time, `,` `.` slow/accelerate time |
| `[` `]` | disk peak temperature (2500–20000 K) |
| `-` `=` | exposure |
| `1` `2` `3` | integrator quality (steps/accuracy) |
| `F2` / `F3` | render resolution scale down/up |
| `Esc` | quit |

CLI: `./build/blackhole [--mode 0|1|2] [--quality 0|1|2] [--res 0..3] [--benchmark SECONDS]`

## Build & run

Requirements: macOS, Xcode command line tools, CMake ≥ 3.16, GLFW 3 (`brew install glfw`).

```sh
./run.sh                 # cmake release build + run
# or manually:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/blackhole
# fallback without cmake:
./build.sh && ./blackhole
```

Run from the project root (it loads `shaders/blackhole.frag`).

## Files

```
src/main.cpp              window, camera, render pipeline, controls
src/physics_test.cpp      CPU validation of the geodesic integrator vs exact GR
shaders/blackhole.frag    all physics: geodesic integrator, disk, grid, stars
screenshots/              sample renders (realistic / grid / combined)
CMakeLists.txt / build.sh / run.sh
```

## Validation

`src/physics_test.cpp` re-implements the shader's integrator on the CPU and
checks it against exact general relativity (run `./build/physics_test`):

| Quantity | Exact GR | This integrator |
| --- | --- | --- |
| shadow critical impact parameter `3√3 M` | 2.59808 rs | 2.59803 rs |
| photon-sphere periapsis of critical ray | 1.5 rs | 1.5108 rs |
| weak-field deflection `4M/b + (15π/4)(M/b)²` at b = 40 rs | 0.05184 rad | 0.05170 rad |
| marginally stable orbit (ISCO) `6M` | 3 rs | 3.0000 rs |

GPU output is verified by rendering offscreen and measuring pixels: the
photon-ring diameter matches the predicted `2·b_crit·√(1−rs/r₀)/r₀` angular
size, and the disk shows the relativistic beaming asymmetry (approaching side
several times brighter than the receding side, as in the EHT images).

## Accuracy notes & idealizations

- Exact Schwarzschild *photon trajectories*; no weak-field approximation.
- Non-rotating metric (Schwarzschild, not Kerr) — no frame dragging.
- Disk: steady, thin, prograde circular geodesic orbits; turbulence is
  procedural but sheared by the true Keplerian `Ω(r) = √(M/r³)`.
- Redshift combines the local static-frame Doppler with gravitational
  redshift (exact for the emitter model used here).
- The grid embeds the *spatial* equatorial slice (Flamm); a full spacetime
  embedding would require 4 dimensions.
- Starfield is procedural; its lensing (Einstein arcs, shadow silhouette)
  is exact for whatever directions are sampled.
