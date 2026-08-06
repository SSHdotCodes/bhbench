# SolxHigh Schwarzschild Black Hole

A real-time C++17/OpenGL 4.1 visualization of a non-rotating (Schwarzschild) black hole. The main view traces null geodesics on the GPU; it does not fake lensing with a screen-space distortion. It includes a relativistically shifted accretion disk, an optically thin emitting halo/photon ring, a lensed procedural star field, and an exact Flamm-paraboloid spacetime-grid inset.

## Run on this Mac

Double-click `run.command`, or use Terminal:

```sh
./scripts/build_and_run.sh
```

After the first build, the native app is at:

```text
build/black-hole-sim.app
```

Dependencies are CMake, a C++17 compiler, and GLFW 3.3+. On a new Homebrew-based Mac:

```sh
brew install cmake glfw
```

## Controls

| Input | Action |
|---|---|
| Left-drag | Orbit the camera |
| Scroll | Zoom |
| `R` | Reset camera |
| `D` | Toggle accretion disk |
| `H` | Toggle emitting halo/photon shell |
| `G` | Toggle Flamm spacetime-grid inset |
| `Q` | Cycle fast/balanced/high geodesic quality |
| Space | Pause disk flow |
| Escape | Quit |

The current frame rate and feature state appear in the title bar. Balanced mode is the default; the verified Mac configuration runs it at the display-limited 60 FPS.

## Scientific model

The code uses geometrized units, `G = c = 1`, and sets the Schwarzschild radius

```text
r_s = 2GM/c^2 = 1.
```

Every Schwarzschild null geodesic lies in a plane through the origin. A camera ray is converted from the orthonormal frame of a static observer into the conserved photon energy `E` and angular momentum `L`. The shader integrates the exact radial and angular equations

```text
d²r/dλ² = (L²/r³)(1 - 3M/r)
dφ/dλ  = L/r²
```

with an adaptive velocity-Verlet method. This produces the correct horizon at `r = r_s`, unstable photon sphere at `r = 1.5 r_s = 3M`, critical impact parameter `b_crit = 3√3 M`, higher-order disk images, Einstein-ring structure, and background gravitational lensing.

The disk is optically thick and geometrically thin, with:

- its inner edge at the Schwarzschild ISCO, `r = 3 r_s = 6M`;
- a zero-torque thin-disk radial flux factor proportional to `r^-3(1-sqrt(r_ISCO/r))`;
- Keplerian angular speed `Ω = sqrt(M/r³)`;
- black-body visible-band color computed from Planck samples;
- gravitational plus special-relativistic frequency shift
  `g = sqrt(1-3M/r)/(1-Ωλ)`, where `λ = L_z/E`;
- bolometric intensity transformed by `g^4`, consistent with invariance of `I_ν/ν³`.

The disk's absolute temperature scale is a display-oriented supermassive-black-hole choice because temperature also requires a physical mass and accretion rate. Its radial dependence, relativistic shift, beaming, and intensity transform are the scientific parts of the model. Turbulence modulates emissivity but does not alter the geodesics.

The halo is optically thin emissive plasma concentrated around the disk corona and `r = 1.5 r_s`. Rays that orbit near the unstable photon sphere accumulate more path emission, forming the photon ring rather than receiving a painted screen-space outline.

## What the spacetime grid means

The bottom-right inset renders the exact Flamm embedding of the equatorial, constant-Schwarzschild-time spatial slice:

```text
z(r) = 2 sqrt(r_s (r-r_s)) + constant.
```

The constant only moves the surface vertically. This embedding makes the rapidly increasing proper radial distance near the horizon legible as a “trapdoor.” It is a visualization of intrinsic spatial curvature—not a claim that gravity is a literal funnel in another physical dimension, and not a visualization of time curvature. Press `G` to compare the ray-traced scene with and without it.

## Validation and limitations

`ctest` independently checks the photon-sphere condition, analytical critical impact parameter, and capture/scatter behavior on both sides of the separatrix. Run it with:

```sh
ctest --test-dir build --output-on-failure
```

This is a Schwarzschild model. It intentionally does not include black-hole spin, Kerr frame dragging, magnetohydrodynamics, disk self-gravity, absorption-line radiative transfer, or polarization. Those require a different metric and substantially more physical parameters; presenting them without those inputs would be less scientifically honest.

## Project layout

```text
src/main.cpp                 GLFW window, controls, OpenGL setup
shaders/black_hole.frag      GPU geodesics, disk, halo, sky, Flamm inset
tests/geodesic_tests.cpp     independent capture-boundary validation
scripts/build_and_run.sh     configure, build, test, and launch
run.command                  macOS double-click launcher
```

