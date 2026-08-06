# Schwarzschild black-hole ray tracer (C++ / OpenGL)

This is a real-time, GPU fragment-shader renderer of a non-rotating black hole. It opens a native GLFW window and uses OpenGL 3.3; the expensive work runs once per pixel on the graphics processor.

## Build and run (macOS)

```sh
cd black-hole-cpp-codexterraxhigh
cmake -S . -B build
cmake --build build -j
./build/black_hole
```

`cmake` uses a locally installed GLFW when available, otherwise it downloads GLFW during configuration. If configuration cannot find an OpenGL development environment, install GLFW with `brew install glfw` and rerun the commands.

## Controls

| Input | Action |
| --- | --- |
| Left mouse drag | Orbit the observer around the hole |
| Scroll | Change field of view |
| `Space` | Pause / resume slow camera orbit |
| `G` | Show / hide the spacetime embedding grid |
| `Esc` | Quit |

## Physics model

Units use `G = c = M = 1`, so the event horizon is at `r = 2` and the innermost stable circular orbit is at `r = 6`.

For every display pixel, the fragment shader follows the photon in its orbital plane using the null-geodesic equation of the Schwarzschild metric:

```text
d²u/dφ² + u = 3u²,        u = 1/r
```

It uses fourth-order Runge--Kutta steps, detects capture at the event horizon, and tests the curved ray against the equatorial accretion disk. The disk is bounded by the ISCO, has a Novikov--Thorne-inspired radial temperature profile, relativistic Doppler beaming from Keplerian orbital motion, gravitational redshift, turbulent density structure, and a faint lensing halo. Background stars are evaluated only after a ray escapes to a distant celestial sphere.

The lower-left inset is an actual line mesh of Flamm's paraboloid, the isometric embedding of a constant-time equatorial spatial slice of the Schwarzschild solution:

```text
z(r) = -2 sqrt(2 (r - 2))
```

It is a visualization of spatial curvature rather than a literal funnel in four-dimensional spacetime. The main image is limited to a static, uncharged, non-rotating (Schwarzschild) black hole; Kerr frame dragging, radiative transfer/scattering, finite disk thickness, and general-relativistic MHD are deliberately outside this compact real-time model.
