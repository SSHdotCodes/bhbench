# Black Hole Simulation (C++ / OpenGL)

Real-time Schwarzschild black hole visualization with GPU ray tracing, gravitational lensing, accretion disk, and spacetime curvature grid.

## Physics

- **Ray tracing**: Null geodesics in Schwarzschild spacetime integrated via RK4 in a GLSL fragment shader, using the full Christoffel-symbol geodesic equation.
- **Gravitational lensing**: Emerges naturally from geodesic bending (Einstein rings, photon sphere at 1.5× rₛ).
- **Accretion disk**: Novikov–Thorne thin-disk emissivity with ISCO at 3 rₛ, Shakura–Sunyaev temperature profile T ∝ r⁻³/⁴, and relativistic Doppler beaming.
- **Corona/halo**: Exponential vertical falloff above/below the disk plane.
- **Spacetime grid**: Flamm paraboloid embedding z = 2√(rₛ(r − rₛ)) showing the "trapdoor" geometry.

Units: geometric (G = c = 1), rₛ = 2M = 2.

## Requirements

- macOS with OpenGL 4.1+ (or Linux with OpenGL 4.1+)
- CMake 3.16+
- GLFW 3, GLM (via Homebrew: `brew install glfw glm`)

## Build & Run

```bash
cd black-hole-cpp-composer
mkdir build && cd build
cmake ..
cmake --build .
./black-hole-sim
```

## Controls

| Input | Action |
|-------|--------|
| Mouse drag | Orbit camera |
| Scroll | Zoom |
| G | Toggle spacetime grid |
| R | Reset camera |
| Escape | Quit |