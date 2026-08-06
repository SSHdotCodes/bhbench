# Schwarzschild black-hole ray tracer (C++ / OpenGL)

This is a self-contained, realtime OpenGL demonstration of a non-rotating black hole. The fragment shader integrates null rays on the GPU in Schwarzschild **isotropic coordinates**, so gravitational lensing is produced by the curved optical metric rather than by a post-process screen warp.

The visualization includes:

- a black-hole event horizon and shadow;
- strong-field gravitational lensing and the photon sphere at `r = 3M`;
- a thin Keplerian accretion disk beginning at the Schwarzschild ISCO, `r_ISCO = 6M`;
- gravitational redshift, special-relativistic Doppler beaming, and the invariant `I_nu / nu^3` intensity factor for disk emission;
- a photon-ring / inner-halo contribution;
- a warped spatial-slice embedding grid, shown as a visual “trapdoor in spacetime”;
- realtime camera orbit, zoom, exposure, toggles, pause, and reset.

The code uses geometric units `G = c = M = 1`. The thin-disk spectrum is intentionally mapped into display RGB instead of pretending that a visible monitor can show the disk’s full physical spectrum. The grid is an embedding-diagram visualization of curvature, not a literal extra dimension of the Schwarzschild solution.

## Build and run on macOS

The project expects GLFW and OpenGL. With Homebrew GLFW already installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/black_hole
```

The executable loads `black_hole.vert` and `black_hole.frag` from its working directory, so run it from this project directory as shown above.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Orbit the camera |
| W / S | Zoom in / out |
| G | Toggle warped spacetime grid |
| D | Toggle accretion disk |
| H | Toggle halo / photon ring |
| + / - | Exposure |
| Space | Pause / resume disk animation |
| R | Reset view |
| Esc | Quit |

