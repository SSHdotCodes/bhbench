# Schwarzschild Lab

A clean-room, real-time macOS C++20/OpenGL 4.1 simulation of a nonrotating black hole. It combines exact Schwarzschild null-geodesic ray tracing, a relativistically shifted analytic accretion disk, an optically thin emissive halo, a procedural lensed sky, and a separate Flamm-paraboloid spacetime-curvature view.

No existing black-hole project or source tree was inspected while making this project.

## Run it on this Mac

Double-click `run-black-hole.command`, or run:

```bash
cd black-hole-cpp-solultra
./run-black-hole.command
```

The launcher configures and builds the native app, then opens:

```text
build/schwarzschild_lab.app
```

The local prerequisites are already present: Apple Clang, CMake, Ninja, Homebrew GLFW 3.4, and the macOS OpenGL framework.

For a manual build:

```bash
cmake --fresh -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build --parallel
open build/schwarzschild_lab.app
```

## Controls

| Input | Action |
|---|---|
| `1` | Full observer/ray-traced view |
| `2` | Full spatial-curvature embedding view |
| `3` | Synchronized split view |
| Left drag | Orbit the observer; disables auto-orbit |
| Scroll | Change static observer radius |
| `Space` | Pause/resume the disk emissivity animation |
| `A` | Toggle slow auto-orbit |
| `D` | Toggle the optically thick disk |
| `H` | Toggle the optically thin halo |
| `G` | Toggle the embedding grid lines |
| `L` | Toggle exact ray bending / straight-path diagnostic |
| `B` | Toggle display bloom (a camera effect, not physics) |
| `Q` | Cycle Performance, Balanced, High, Reference quality |
| `[` / `]` | Decrease/increase exposure |
| `V` | Toggle vertical sync |
| `F` | Toggle fullscreen |
| `R` | Reset simulation and render parameters |
| `Esc` | Quit |

In the curvature view, cyan marks the horizon boundary/minimal circle at `r=2M`, gold marks the photon sphere at `r=3M`, and green marks the Schwarzschild ISCO at `r=6M`.

## What is physically modeled

The renderer uses geometric units with `G=c=M=1`, where `M` means `GM_phys/c^2`. The geometry is therefore scale invariant. For one solar mass, one `M` is about 1.477 km and one time unit `GM/c^3` is about 4.926 microseconds.

### Exact Schwarzschild light paths

Spherical symmetry confines every light ray to a plane. For each pixel, the GPU integrates the exact Schwarzschild null-orbit system

```text
dr/dlambda     = v
dv/dlambda     = b^2/r^3 (1 - 3M/r)
dphi/dlambda   = b/r^2
```

with fourth-order Runge-Kutta and an adaptive step bounded in both fractional radius and azimuth. The exact first integral

```text
v^2 + (1 - 2M/r)b^2/r^2 = 1
```

is used to control numerical drift. Rays are captured at the horizon and sampled from the procedural celestial sphere when they escape. Near-critical rays may loop around the photon sphere, producing strongly lensed and higher-order disk images.

The critical asymptotic impact parameter is `b_crit = 3 sqrt(3) M`. For the finite static observer, its angular radius obeys

```text
sin(alpha_shadow) = 3 sqrt(3) M sqrt(1 - 2M/r_observer) / r_observer.
```

The dark image is the **black-hole shadow/capture region**, not a direct picture of the event horizon. Comparing its screen size directly with the horizon's areal radius would mix distinct geometric quantities.

### Accretion disk and frequency shift

The equatorial disk is thin, optically thick, and extends from the Schwarzschild ISCO at `6M` to `22M`. The first disk crossing along each backward-traced ray terminates radiative transfer. Its fast emissivity proxy is the Newtonian zero-torque profile with its inner edge placed at the relativistic ISCO:

```text
F(r) proportional to r^-3 (1 - sqrt(6M/r)).
```

Gas follows circular Schwarzschild geodesics with `Omega=sqrt(M/r^3)`. The renderer uses the invariant frequency-shift factor for a static observer,

```text
g = 1 / [sqrt(f_observer) u^t (1 - Omega xi)],
u^t = 1/sqrt(1 - 3M/r),
```

which combines gravitational redshift, transverse Doppler shift, and line-of-sight relativistic beaming. Bolometric intensity is transformed with `g^4`, consistent with invariance of `I_nu/nu^3`. The bright/dim asymmetry across the disk is therefore physical within this Schwarzschild thin-disk model.

The animated fine structure is an emissivity tracer rotating at the local orbital frequency. It is not a hydrodynamic or magnetohydrodynamic evolution, and it uses one global animation phase rather than retarded emission times; higher-order images therefore omit light-travel-time delays in that non-axisymmetric texture.

### Halo and lensed sky

The halo is integrated along the curved rays as simplified, optically thin emissivity concentrated toward the equatorial region and inner radii. Static-emitter gravitational shifting and the corresponding local proper-path factor are included. The prescribed emitters are treated as static even though maintaining static plasma close to a horizon would require divergent acceleration; this is a phenomenological visualization, not a physical accretion-flow solution. It does not include absorption, scattering, polarization, or frequency-dependent plasma transport.

The sky is procedural. A compact demonstration source is kept directly behind the hole so an Einstein ring is easy to see, and a star field plus faint galactic band makes lens mapping legible. It is not an astronomical catalog or cosmological sky model.

### Spacetime-curvature view

View `2` renders the exact Flamm embedding of the exterior equatorial, constant-Schwarzschild-time spatial slice:

```text
dl^2 = dr^2/(1 - 2M/r) + r^2 dphi^2
z(r) = 2 sqrt(2M(r - 2M)) + constant,  r >= 2M.
```

The plotted single exterior sheet ends at a minimal circular boundary of radius `2M`; it never narrows to a point. That circle becomes a throat only if the doubled Einstein-Rosen spatial extension is shown. Its vertical depth is an artificial embedding coordinate. It is **not** a literal funnel in three-dimensional space, the flow of time, the black-hole interior, or a traversable tunnel. The separate and split views are intentional so this educational diagram is not confused with the observer's physical sky. The gold and green circles are reference overlays, not intrinsic features of the embedding surface.

## Accuracy and tests

Run the regression suite with:

```bash
ctest --test-dir build --output-on-failure
```

It checks:

- `r_h=2M`, `r_ph=3M`, `r_ISCO=6M`, and `b_crit=3 sqrt(3)M`;
- capture below and escape above the critical impact parameter;
- the static-observer launch constraint and finite-observer shadow-angle mapping;
- null first-integral drift along captured and escaping rays;
- convergence to the weak-field deflection `4M/b` at `b=100M`;
- the static Schwarzschild gravitational-redshift relation;
- approaching/transverse/receding Doppler-factor ordering;
- near-critical range budgets for the Balanced, High, and Reference GPU presets;
- equality of the Flamm surface metric and the equatorial spatial metric.

The final build and shader were validated in the native window on an Apple M4 Max. At a `1280x823` internal render size, Balanced mode measured roughly 85–105 FPS across the observer, curvature, and split views. Performance depends on window size, Retina scaling, view, and quality preset; Reference mode intentionally prioritizes near-critical convergence over frame rate.

## Honest scope and limitations

- This is classical general relativity on a fixed, uncharged, nonrotating Schwarzschild spacetime. It does not simulate Kerr spin or frame dragging.
- Photons are test particles. Disk and radiation do not back-react on the metric.
- The disk uses exact Schwarzschild circular kinematics and redshift but a Newtonian zero-torque emissivity proxy rather than the full Page-Thorne/Novikov-Thorne relativistic flux. It is not GRMHD: turbulence, magnetic fields, self-gravity, plunging-region emission, and detailed plasma microphysics are omitted.
- Disk color is an exposure-controlled thermal visualization. Without a specified mass, accretion rate, spectral band, and color-management pipeline, it is not a unique naked-eye prediction.
- A bright rim can combine unresolved higher-order disk images, the aligned background source, and halo emission. It should not automatically be interpreted as a luminous physical surface at the photon sphere.
- Finite step budgets affect the thinnest near-critical images. `Q` exposes the convergence/performance tradeoff.
- Geometric optics omits diffraction, wave optics, polarization, quantum effects, Hawking radiation, singularity physics, and quantum gravity.
- `L` is a visualization diagnostic that straightens only the spatial ray paths. It deliberately leaves the disk emission/redshift pipeline visible for comparison, so the `L-off` image is not a self-consistent alternate universe.
- Bloom is explicitly a display effect and can be disabled with `B`.

## Project layout

```text
black-hole-cpp-solultra/
├── CMakeLists.txt
├── README.md
├── run-black-hole.command
├── src/
│   ├── main.cpp       # GLFW window, controls, HDR target, realtime loop
│   └── shaders.hpp    # GPU geodesics, disk/halo transfer, sky, embedding
└── tests/
    └── physics_tests.cpp
```
