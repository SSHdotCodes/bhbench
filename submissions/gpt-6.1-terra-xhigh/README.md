# Schwarzschild Observatory

A native macOS C++/OpenGL visualization of a non-rotating black hole. The GPU traces light rays through Schwarzschild spacetime, including the lensed images of an opaque, thin accretion disk and a procedural background sky. The second view shows the corresponding equatorial spatial geometry as a Flamm paraboloid.

## Run

On this Mac, double-click `Launch.command`, or run:

```sh
cd /Users/sshpro/.t3/scratch/2026-10-26-make-a-highly-scientifically-accurate-ac37fe3d/black-hole-cpp-terra-6-1
./Launch.command
```

The launcher builds the program if needed and opens a separate window. It requires macOS, CMake, Xcode command-line tools, and GLFW 3 (`brew install cmake glfw` if needed). The app uses Apple's OpenGL 4.1 implementation. To build and test manually:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/black-hole
```

The shader files are loaded from the source directory configured by CMake, so rebuild after moving this folder. `./build/black-hole --geometry` opens directly in the embedding view. `./build/black-hole --benchmark` runs 180 frames with vsync disabled and reports the resulting throughput; the result depends on window visibility, display and GPU scheduling.

## Controls

- **1**: ray-traced view; **2**: spatial curvature view
- **Drag**: rotate the observer; **scroll**: zoom
- **D**: toggle disk; **Space**: pause animation; **R**: reset camera
- **Esc**: close the window

The GPU renders at half the framebuffer width and height and upscales to support interactive performance on Retina displays.

## Model and interpretation

Distances use geometric units, `G = c = M = 1`. The event horizon is at `r = 2M`, the unstable photon sphere at `r = 3M`, and the innermost stable circular orbit of massive particles is at `r = 6M`. A static observer sits at `r = 30M`. For each pixel a local direction is converted to Schwarzschild coordinate initial conditions. The shader integrates the exact Schwarzschild null-orbit equation in its equivalent Cartesian form, `d²x/dλ² = -3 M L² x/r⁵`, using a finite-step velocity-Verlet integrator. Rays terminate at the horizon, at an outer sky boundary, or at the first disk crossing. Near-critical rays have the highest numerical error; integration and rendering resolution limit the visibility of very narrow higher-order images. A ray exceeding the iteration limit is rendered black.

The disk lies in the equatorial plane from `6M` to `22M`. It is treated as optically thick and consists of circular Keplerian emitters. The renderer uses the locally measured orbital speed, gravitational frequency shift and relativistic Doppler factor, with a `g⁴` bolometric intensity factor. The radial emissivity, color palette, animated texture and background stars are illustrative choices, not a physical plasma or spectral model. The disk does not evolve through magnetohydrodynamics, radiative transfer or self-gravity, and the hole has no spin. Lensed disk images can form bright arcs or halos, but there is no separate simulated emitting corona. Tone mapping and finite pixel resolution affect their appearance.

The curvature view plots `z(r) = 2 sqrt(2M(r - 2M))`, with a vertical translation, for `r ≥ 2M`. This is an isometric embedding of **one spatial, equatorial, constant-Schwarzschild-time slice**, not a literal shape of four-dimensional spacetime or the inside of the black hole. The colored rings mark the horizon and photon-sphere radii; the latter is an orbital property of light, not a luminous physical surface. Its grid is a coordinate visualization and its outer edge is a display cutoff.

The CPU test reproduces the renderer's numerical update and checks the unstable circular photon orbit, capture on either side of the critical impact parameter `3 sqrt(3) M`, and the weak-field bending scale `4M/b`. It does not prove pixel-perfect convergence or validate the disk's illustrative emission model.

## References

- David Tong, [General Relativity: Geodesics in Spacetime](https://www.damtp.cam.ac.uk/user/tong/gr/grhtml/S1.html), especially the null effective potential and light-bending equation.
- Gourgoulhon, [Geometry and physics of black holes](https://relativite.obspm.fr/blackholes/bholes.pdf), for Schwarzschild geometry and spatial embeddings.
