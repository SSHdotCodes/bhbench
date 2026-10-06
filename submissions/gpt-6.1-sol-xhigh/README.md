# Black Hole Relativity Lab

A native C++17 / OpenGL 4.1 simulation with GPU ray tracing, a relativistic accretion disk, and an interactive 3D curvature view. It opens a GLFW window on your Mac. All implementation, shaders, build products, tests, and captures are contained in this directory.

The model is **Schwarzschild: an uncharged, nonrotating black hole**, with a geometrically thin, opaque disk whose self-gravity is negligible. The light paths solve the relativistic null-geodesic equations. This is a scientific visualization of a specified spacetime and an analytic accretion model, not a numerical evolution of Einstein's equations or a magnetohydrodynamic fluid solver.

## Launch

Double-click **Black Hole.app** in this folder. The app has already been built for this computer. It still uses the installed GLFW shared library.

Alternatively, double-click `run.command` to rebuild and launch, or use:

```sh
cd black-hole-cpp-sol6-1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
./build/black_hole
```

Build dependencies: a C++17 compiler, CMake, GLFW 3.3 or later, and an OpenGL 4.1 driver. CMake, GLFW 3.4, and the Apple compiler were already available on this machine. No other black hole project's code was consulted or used.

The native app bundle includes its shaders. Rebuilding refreshes the bundle. `run.command` resolves its own directory, so it can be launched from Finder without changing directories first.

## Controls

| Input | Action |
|---|---|
| Drag / arrow keys | Orbit the 3D camera, including elevation |
| Scroll | Zoom; the optical observer remains outside the disk |
| `1` | Lensed disk with live curvature inset |
| `2` / `G` | Full curvature view, optical inset, and causal diagram |
| `3` | Side-by-side light and geometry |
| `Space` | Pause disk patterns and falling test particles |
| `O` | Automatic camera orbit |
| `D` | Toggle disk to inspect the lensed star background and shadow |
| `L` | Toggle curved rays / Euclidean comparison |
| `B` | Toggle camera bloom |
| `C` | Cycle bolometric false color, frequency-shift diagnostic, and three-band Planck rendering |
| `T` | Toggle illustrative disk emissivity fluctuations |
| `Q` | Performance / balanced / high quality |
| `+` / `-` | Change exposure |
| `H` | Hide the interface |
| `P` | Save a full-window PPM screenshot in `captures/` |
| `R` | Reset camera, exposure, and animation clock |
| `Esc` | Close the window |

The controls on the right are also clickable. Balanced mode adapts internal ray resolution to GPU time; high quality uses native framebuffer resolution and a smaller integration step. The optical and curvature cameras share orbit and zoom input. Inset views update continuously.

## Physics and numerical method

### Units and observer

Distances use `GM/c²`; time uses `GM/c³`; the code sets `G = c = M = 1`. Thus the event horizon is at `r = 2`, the unstable circular photon orbit at `r = 3`, and the innermost stable circular orbit at `r = 6`. The critical impact parameter is `b = 3√3 ≈ 5.196152423`. The horizon's radius and the apparent shadow's size are different quantities.

The camera is an instantaneous **static observer** at finite Schwarzschild radius `R`. Each pixel begins as a direction in that observer's orthonormal tetrad. For local radial direction cosine `n_r` and transverse magnitude `n_t`, its impact parameter is

```text
b = R n_t / sqrt(1 - 2/R)
u(0) = 1/R
u'(0) = -n_r/b
```

Dragging moves between static observer locations. It does not add camera-motion aberration. The finite-observer shadow angle obeys `sin(alpha) = b_crit sqrt(1 - 2/R) / R`.

### Gravitational lensing

Spherical symmetry fixes each ray to a plane through the origin. The fragment shader integrates

```text
u'' = -u + 3u²,    u = 1/r
(u')² + u²(1 - 2u) = 1/b²
```

using fourth-order Runge-Kutta in orbital angle. Equatorial disk crossings are found at their exact angular location; cubic dense output estimates their radius. Escaping rays are advanced to `u = 0`, with a refined crossing, and then sample a stationary procedural sky at infinity. Rays reaching `r = 2` are captured. A ray that hits the iteration cap is marked violet rather than being silently classified as captured.

This produces gravitationally displaced background stars and higher-order images of the disk around the shadow. No hand-drawn photon-ring geometry or screen-space bending formula is used. The bright higher-order images come from the actual ray intersections. Camera bloom is applied afterward and is a separate, switchable optical effect.

Reference: [University of Vienna, Schwarzschild null-geodesic lecture notes](https://gravity.univie.ac.at/fileadmin/user_upload/i_gravity_physics/material/teaching/rt2/SS_2012/2012-04-24.pdf).

### Accretion disk and radiation

The disk extends from the zero-torque ISCO at `6M` to a finite outer edge at `24M`. Circular orbital angular velocity is `Ω = r^(-3/2)`. The radial emission profile uses the Schwarzschild specialization of the Novikov-Thorne / Page-Thorne conservation-law flux:

```text
x = sqrt(r), x0 = sqrt(6), a = sqrt(3)
J = x - x0 - (a/2) ln[(x-a)(x0+a) / ((x+a)(x0-a))]
F ∝ 1.5 J / [r^(5/2) (r-3)]
```

The common dimensional accretion-rate factor is removed. The disk is opaque: the first intersection with emitting material determines the intensity. Emission is isotropic in the disk rest frame. There is no limb-darkening prescription.

The emitted photon's conserved axial angular momentum `λ = Lz/E` determines the frequency ratio:

```text
g = sqrt(1 - 3/r) / [sqrt(1 - 2/R) (1 - Ω λ)]
I_observed,bolometric = g⁴ I_emitted,bolometric
```

This includes gravitational redshift, transverse Doppler shift, longitudinal Doppler shift, and beaming for the finite-radius observer. Specific intensity is transported without an extra distance attenuation factor.

The default warm palette is explicitly **false color of bolometric intensity**. The `C` diagnostic shows the frequency ratio. The third mode evaluates three narrow Planck bands at 610, 550, and 460 nm, using `Bν(gT)` and a nominal peak temperature of 100,000 K. These three bands are an illustrative spectral sampling, not calibrated human colorimetry. Exposure and tone mapping control the display, not the physical flux.

The small procedural emissivity perturbations rotate at the exact circular angular velocity and are evaluated at retarded emission time. Coordinate light travel time is integrated with Simpson quadrature on the dense orbit solution. The perturbations illustrate differential rotation; they are not a fluid turbulence calculation. Press `T` for a smooth steady disk.

Reference: [Page & Thorne (1974), Disk-Accretion onto a Black Hole, equations 11–12](https://articles.adsabs.harvard.edu/pdf/1974ApJ...191..499P).

### Curvature and the causal trapdoor

The wire grid uses the exact **Flamm embedding** of an exterior equatorial, constant-Schwarzschild-time spatial slice:

```text
z(r) = 2 sqrt[2M(r - 2M)] + constant
1 + (dz/dr)² = 1/(1 - 2M/r)
```

The height is an auxiliary embedding axis; it is not a physical direction of falling in four-dimensional spacetime. The grid stops at `2M`. The orange and gold rings show `3M` and `6M` in this spatial geometry, not luminous objects.

Radially falling test particles are parameterized by their proper time after release from rest at `R0 = 24M`:

```text
r = (R0/2)(1 + cos η)
τ = sqrt(R0³/8)(η + sin η)
```

Only their exterior paths are projected onto the embedding. The dots are repeated demonstrations of trajectories; their animation is not a simultaneous constant-Schwarzschild-time snapshot of a physical ensemble.

To explain the actual causal trap, the grid view also shows the two future radial null directions in **ingoing Painlevé-Gullstrand coordinates**:

```text
dr/dT = -sqrt(2M/r) ± 1
```

Outside the horizon an outgoing branch can move outward. At the horizon that branch has zero radial coordinate speed. Inside, both branches move toward smaller `r`. This causal fact is distinct from the shape of the spatial funnel.

Reference: [University of Tennessee, The Schwarzschild Solution](https://web.math.utk.edu/~freire/teaching/m568s12/m568s12Schwarzschild.pdf).

### Model scope

The spacetime and governing ray equations are exact for Schwarzschild; their integration and displayed images are numerical approximations. There is no spin, frame dragging, charged-hole physics, disk self-gravity, plasma scattering, magnetic field evolution, returning-radiation heating, or dynamic mass growth. The background is synthetic. Bloom, thermal false color, disk perturbations, and the animation time scale are visualization choices. Pixel resolution and finite angular steps limit very high-order images near the critical curve.

The disk animation advances at 8 geometric time units per wall-clock second. The infall demonstration advances its own proper-time parameter at 4.4 geometric units per second. No black hole mass or accretion rate is inferred from these display speeds.

## Verification

```sh
ctest --test-dir build --output-on-failure
./build/black_hole --validate-gpu
```

CPU tests verify the photon orbit, capture threshold, null first integral, convergence, weak-field bending, the zero-torque boundary, independent flux quadrature, redshift behavior, and the embedding metric. GPU tests read a floating-point render target and compare actual shader rays against an independent, finer double-precision C++ calculation, including disk radii, frequency shifts, and retarded times.

The GPU diagnostic uses an 80×60 ray grid and excludes a narrow interval `|b-b_crit| < 0.015`, where float roundoff can change the winding count. It is a sampled validation, not a universal error bound. Full results and performance measurements are in `VALIDATION.md`.

Useful command-line runs:

```sh
./build/black_hole --grid
./build/black_hole --split
./build/black_hole --seconds 10 --capture captures/example.ppm
./build/black_hole --hidden --seconds 10
```

`--seconds` closes after a bounded run. `--capture` saves after two seconds, before swapping the rendered back buffer. A normal launch runs until you close the window.

## Source map

- `src/main.cpp`: native window, GPU renderer, camera, controls, grid, screenshots, GPU verification.
- `src/physics.hpp`: independent double-precision physics reference.
- `shaders/raytrace.frag`: ray integration, disk intersections, radiation and sky.
- `shaders/post.frag`, `blur.frag`: HDR display mapping and optional camera bloom.
- `shaders/geometry.*`: the grid, particles and interface geometry.
- `tests/physics_tests.cpp`: analytic and numerical checks.
- `packaging/Info.plist`: native macOS app metadata.
- `captures/`: inspected screenshots and subsequent user captures.
