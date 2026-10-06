# black-hole-cpp-ml4 — real-time Schwarzschild black hole ray tracer (C++ / OpenGL)

A real-time, scientifically grounded 3D black hole simulation. Running the
program opens a window with the simulation rendering live on the GPU.

## Features

- **Gravitational lensing via ray tracing** — every pixel casts a light ray
  that is integrated as an *exact Schwarzschild null geodesic* (RK4, adaptive
  step) in its orbital plane, using the conserved energy `E` and angular
  momentum `L`. This reproduces the black hole shadow (photon capture at
  `b < 3√3 M`), the photon ring, and the lensed, Einstein-ringed starfield.
- **Accretion disk** — geometrically thin, Keplerian disk from the ISCO
  (`r_in = 3 rs`) outward, with:
  - relativistic Doppler beaming `D = 1 / (γ(1 − β·k))`, intensity ∝ `D³`,
  - Doppler-shifted blackbody spectrum (`T ∝ r^-3/4`, observed `T·D`),
  - Shakura–Sunyaev `r^-3` surface brightness falloff,
  - turbulent fbm texture, plus a volumetric **halo** glow above/below the
    disk plane.
- **Spacetime curvature** — a wireframe grid drawn on the **Flamm
  paraboloid**, the exact embedding of the Schwarzschild equatorial spatial
  slice (`z(r) = 2√(rs(r − rs))`), making the "trapdoor" of spacetime
  visible, with a glowing event-horizon rim and photon-sphere ring.
- **Real-time** — the ray tracer is a fragment shader executing on the GPU
  (on macOS, OpenGL is executed by the Metal-backed driver, i.e. Apple
  Silicon / Apple GPU acceleration). Runs at ~60 FPS at 1080p-class render
  targets on an M4 Max; includes a `--scale` render-resolution option.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requires CMake, a C++17 compiler, and GLFW (`brew install glfw` on macOS).

## Run

```sh
./build/blackhole                 # opens the simulation window, live at ~60 FPS
```

The renderer auto-selects a render scale on high-DPI (retina) displays to stay
realtime; use `--scale 1.0` for maximum sharpness or lower values for more FPS.

Controls: **left-drag** orbit · **scroll** zoom · **R** reset camera ·
**G** toggle spacetime grid · **H** toggle disk halo · **SPACE** pause ·
**ESC** quit.

### Options

```
--width W --height H   window size (default 1280x720)
--scale S              render resolution scale (default: auto)
--az A --el E --dist D initial camera (azimuth, elevation, distance)
--no-halo              disable the disk halo
--no-grid              disable the spacetime grid
--debug-hits           false-color map: blue=escaped ray, red=captured,
                       green=disk hit, yellow=still orbiting
--test N OUT.ppm       headless: render N frames, write a PPM screenshot
```

## Headless test / screenshot

```sh
./build/blackhole --test 3 frame.ppm   # renders 3 frames, writes a PPM image
```

(The `--debug-hits` view is handy for verifying the physics: the red capture
region has angular radius `asin(3√3 M / 2d)`, matching theory.)

## Physics notes

Units use `G = c = 1`, `rs = 2M`. Each camera ray is a null geodesic; by
spherical symmetry it stays in the plane with normal `n = ro × rd`. In plane
polar coordinates `(r, φ)` with `f = 1 − rs/r`:

```
r̈   = rs (ṙ² − E²) / (2 r² f) + f L² / r³
φ̈   = −2 ṙ φ̇ / r        L = r² φ̇ = const,   E = f ṫ = 1 (affine gauge)
```

The initial tangent is normalized with the null condition
`E² = ṙ² + f r² φ̇²`. Disk gas moves at the Keplerian speed
`v = √(M/r) = √(rs / 2r)`. The grid surface is the Flamm embedding of the
`θ = π/2` slice, `ds² = f⁻¹ dr² + r² dφ²  →  z(r) = 2√(rs(r − rs))`.
