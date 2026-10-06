# Schwarzschild black hole

A realtime general-relativistic view of a non-spinning black hole. Drag to orbit a static observer. The window shows null geodesics, a thin accretion disk, and Flamm's embedding of the equatorial slice.

Units are geometric: G = c = 1, M = 1, so the horizon is at r = 2M.

## Build

```sh
./build.sh
./build/blackhole
```

Needs a C++17 compiler, CMake, and GLFW (Homebrew: `brew install glfw cmake`). OpenGL 4.1, which is what macOS provides. `./build/bh_test` checks the geodesic integrator against analytic results.

## What you are seeing

- **Rays.** Each pixel is a past-directed null geodesic of the Schwarzschild metric, integrated with RK4 in Cartesian coordinates from a static observer's local frame. Rays that fall through r = 2M are black. The shadow's angular radius matches the critical impact parameter b = 3√3 M to a fraction of a percent.
- **Lensing.** Stars, a coordinate grid on the celestial sphere, and two background galaxies are sampled from the ray's outgoing direction. The grid wrapped around the shadow is the photon ring (r = 3M). Press L to switch those same rays to flat space: the shadow collapses to the Euclidean horizon and the grid straightens.
- **Disk.** A Novikov–Thorne thin disk from the ISCO (r = 6M) outward. Rest-frame flux comes from the relativistic integral, so F = 0 at the ISCO and the peak is near r ≈ 9.6M. Gas on circular geodesics Doppler-shifts and gravitationally redshifts the light by g = ω_camera / ω_emitter; surface brightness follows g⁴. Color is a blackbody at g T(r). The absolute scale is set so the flux peak is 8200 K, which puts this radial range in the visible band; the temperature ratios and g are not rescaled. A thin gaussian atmosphere is integrated along each ray, and a small bloom pass adds the glare of the bright inner disk.
- **Spacetime.** The inset is Flamm's paraboloid, the isometric embedding of the t = constant, equatorial slice. The throat is vertical at the horizon. Gold is the horizon, cyan is the photon sphere, orange is the ISCO. The moving dots are radial timelike geodesics released from rest.

Press C for a false-color view of g, or for a pure capture mask (black = captured). V switches between the observatory, the rays alone, and the embedding full screen.

## Controls

| Input | Action |
| --- | --- |
| Drag | Orbit the observer, or the funnel in the embedding view |
| Scroll | Camera radius, or funnel distance |
| 1 2 3 | Face-on, 66°, edge-on |
| V | Observatory / rays / embedding |
| L | Flat-space comparison |
| C | Realistic / g-factor / capture mask |
| D N B G | Disk, corona, bloom, sky grid |
| [ ] | Quality |
| - + | Exposure |
| Space | Pause the disk and the infall |
| R | Reset |
| F | Fullscreen |
| H | Hide the labels |
| Esc | Quit |
