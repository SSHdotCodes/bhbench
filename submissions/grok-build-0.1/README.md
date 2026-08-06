# Scientifically Accurate Black Hole 3D Simulation (C++ + OpenGL)

Realtime GR ray-traced black hole with:
- Null geodesic ray tracing (RK4) for gravitational lensing
- Relativistic thin accretion disk with Doppler + gravitational redshift + beaming
- Photon ring / strong lensing glow
- Spacetime grid using Flamm's paraboloid embedding + inflow animation ("trapdoor")
- Procedural background stars with deflection

## Requirements (macOS)
- CMake >= 3.12
- GLFW (brew install glfw)
- GLM (brew install glm)
- Xcode command line tools / Apple clang
- OpenGL (provided by macOS)

## Build & Run

```bash
cd black-hole-cpp-grokbuild
mkdir build && cd build
cmake ..
cmake --build . -j
./blackhole
```

Or from project root after build:
```bash
./build/blackhole
```

The simulation opens a window immediately and runs in realtime (VSync on).

## Controls (realtime interactive)

**Camera:**
- Mouse drag: orbit around black hole (azimuth/polar)
- Shift + mouse drag: dolly (change distance)
- Mouse scroll: zoom in/out
- W/S : dolly in/out
- A/D : rotate azimuth
- Q/E : change polar angle

**Physics / Viz parameters (live adjustable):**
- R : reset camera + defaults
- P : pause / resume time (grid inflow animation)
- +/- : accretion disk brightness
- [ / ] : ray march step size (smaller = more accurate but slower)
- 1 / 2 : decrease / increase max integration steps (quality vs perf)
- 3 / 4 : exposure down / up
- 5 / 6 : spacetime grid warp amount (embedding strength)
- 7 / 8 : grid line alpha
- Left/Right arrows : move disk inner radius (ISCO)
- Up/Down arrows : move disk outer radius

## Scientific notes (accuracy)

- Units: G = c = M = 1 → rs = 2M = 2.0
- Photon sphere: 3M = 1.5 rs
- Innermost stable circular orbit (ISCO): 6M = 3 rs (default disk inner)
- Integrator: 4th-order Runge-Kutta on Schwarzschild geodesic equations using Christoffel symbols
- Conserved E and Lz used
- Disk: Keplerian 4-velocity + exact g-factor frequency shift
- Intensity boost ~ g^3.8 (approximates beaming + lensing)
- Disk temperature profile ~ r^(-3/4)
- Grid: Flamm paraboloid z(r) = ±2 √[rs (r − rs)] + time-dependent inflow
- Background sampled at asymptotic escape direction of each geodesic

The ray tracer naturally produces:
- Einstein ring / photon ring
- Strongly lensed top + bottom sides of the disk
- Doppler asymmetry (beaming)
- Gravitational redshift
- Light bending around horizon

Performance: On modern Mac GPUs you can run 60–120 steps comfortably at 720p–1080p. Reduce steps or increase step size for higher FPS.

Enjoy exploring the trapdoor in spacetime!
