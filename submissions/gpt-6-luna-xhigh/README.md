# Schwarzschild black hole

A realtime C++17 / OpenGL visualization. The main view integrates backward light rays through the Schwarzschild optical metric; a thin emitting disk and procedural star field are sampled along each ray. The side inset draws the Flamm embedding of the equatorial spatial slice.

## Build and run (macOS)

Dependencies: CMake, GLFW, GLEW, GLM, and Apple's OpenGL framework.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/black_hole.app
```

The macOS build creates an app bundle that opens a resizable desktop window and renders on the GPU. It targets OpenGL 4.1 core, which is the highest core profile provided by macOS. On other platforms, run `./build/black_hole` after building.

## Controls

- Left / right: orbit the camera
- Up / down: change viewing elevation
- `+` / `-`: zoom
- Space: pause / resume the disk's animated pattern
- `G`: show / hide the spacetime inset
- `R`: reset the camera
- Escape: quit

## Physical model

Units use `G = c = M = 1`. The ray paths use isotropic Schwarzschild coordinates, where the exact static optical index is

`n(rho) = (1 + 1/(2 rho))^3 / (1 - 1/(2 rho))`.

In this optical metric, spatial light paths satisfy `d(n t_hat)/ds = grad(n)`. The shader numerically integrates this equation backward from the camera and terminates rays at the horizon (`rho = 0.5 M`) or at a distant sky boundary. The Schwarzschild critical impact parameter is `3 sqrt(3) M`; the photon sphere is at areal radius `3 M`, the horizon at `2 M`, and the innermost stable circular orbit at `6 M`.

The disk inner edge is placed at the Schwarzschild ISCO. Its brightness uses a thin-disk radial profile, gravitational redshift, and an approximate orbital Doppler factor. The disk's turbulent brightness pattern is animated for readability; this is not a time-dependent GRMHD simulation. The star field is procedural. The Flamm surface `z = 2 sqrt(2 M (R - 2 M))` is an embedding diagram of one equatorial spatial slice, not a literal vertical dimension or a depiction of time evolution.

The simulation uses Schwarzschild rather than Kerr spacetime, and the emitting disk is an illustrative thin surface rather than a self-consistent radiation-transfer solution. Numerical ray integration is GPU shader code with finite step size, so features extremely close to the critical photon orbit are resolution-dependent.
