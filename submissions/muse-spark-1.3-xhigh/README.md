# Black Hole — scientifically accurate realtime 3D simulation (C++ / OpenGL)

Realtime Schwarzschild black hole in `black-hole-cpp-muse-1-3`, written from
first principles (no other black-hole projects consulted). The GPU integrates
exact null-geodesic tracks, so lensing, the shadow, the photon ring and the
disk's higher-order images **emerge** from the physics instead of being painted on.

## Physics core

Geometrized units `G = c = 1`, scene units `Rs = 1` (`M = 0.5`):

| Quantity | Value | Meaning |
|---|---|---|
| Horizon | `r = Rs = 1` | capture condition `r < Rs` |
| Photon sphere | `r = 3M = 1.5` | unstable light orbits, photon ring |
| ISCO / disk inner edge | `r = 6M = 3` | Novikov–Thorne flux goes to 0 here |
| Disk outer edge | `r = 9` | display cutoff |
| Shadow radius | `b_c = 3√3 M ≈ 2.598` | critical impact parameter |

* **Ray tracing / lensing** (`shaders/blackhole.frag`): backward-traced null
  geodesics in the spatial-track form of `d²u/dφ² + u = 3Mu²`,
  `h = r × v`, `a = −1.5·Rs·|h|²·r⃗ / r⁵`, adaptive step
  `dt = clamp((r − 0.92·Rs)·0.35, 0.02, 0.55)`. Rays with `r < Rs` are captured
  (shadow); rays reaching `r > 40` moving outward sample the background sky.
  Press **B** to compare against straight-line (no-bending) rays.
* **Accretion disk**: thin equatorial plane, Novikov–Thorne flux
  `F ∝ r⁻³(1 − √(rIn/r))`, `T ∝ F^¼` mapped through a blackbody palette,
  Keplerian shear advects `fbm` turbulence, inner hot rim, front-to-back
  blending so lensed secondary images show through. Relativistic
  `g = √(1−Rs/r) / (γ(1 − β⃗·p̂))`, `I_obs = g³I_emit` gives one side
  blueshifted/brighter, the other redshifted/dimmer (**D** toggles it).
* **Halos**: the thin photon ring at `~2.6 Rs`, the Einstein ring of the
  background, and the disk's halo-like images above/below the shadow are all
  higher-order (`n ≥ 1`) geodesic images — no sprite hacks.
* **Spacetime curvature** (view `2`/`G`): Flamm's paraboloid embedding of the
  Schwarzschild equatorial slice, `w(r) = 2√(Rs(r − Rs))`, drawn as a polar
  grid. Color encodes the Kretschmann scalar `K = 48M²/r⁶` (log-mapped).
  Red = horizon, yellow = photon sphere, orange = ISCO; the animated dot is an
  infalling probe. This is the "trapdoor in spacetime" view.
* **Realtime**: fullscreen-triangle GLSL raymarcher (GPU = OpenGL acceleration
  path requested), adaptive `dt`, user step budget (`+`/`-`, default 220,
  auto-quality holds 30–60 fps), VSync on.

## Build & run (macOS, Apple Silicon tested)

```bash
brew install glfw cmake
cmake -S black-hole-cpp-muse-1-3 -B black-hole-cpp-muse-1-3/build
cmake --build black-hole-cpp-muse-1-3/build -j
./black-hole-cpp-muse-1-3/build/blackhole        # opens the simulation window
./black-hole-cpp-muse-1-3/build/blackhole --test # headless physics self-test
```

Linux needs `libglfw3-dev`; the CMake falls back to system `OpenGL::GL`.

## Controls

mouse-drag orbit · wheel zoom · **1** BH view · **2**/**G** spacetime grid ·
**B** bending · **D** doppler/redshift · **K** disk · **+**/**-** steps ·
**[**/**]** step size · **A** auto-quality · **Space** pause · **R** reset ·
**P** screenshot (`/tmp/*.ppm`) · **H** help · **ESC** quit.

## Files

```
black-hole-cpp-muse-1-3/
  CMakeLists.txt
  src/main.cpp      # window, camera, funnel mesh, render loop
  src/physics.h     # constants + CPU mirror of the geodesic integrator
  shaders/fullscreen.vert
  shaders/blackhole.frag  # lensing + disk + sky
  shaders/funnel.vert / funnel.frag
```

## Verification

* `blackhole --test`: Flamm values, ISCO speed `~0.5c`, `b_c ≈ 2.598`,
  disk flux cutoff, and the key lensing claim — a `b = 2 Rs` ray is **captured
  with** bending but **escapes without** it.
* Build with `-Wall -Wextra`; shaders are `#version 410 core` (macOS max).
