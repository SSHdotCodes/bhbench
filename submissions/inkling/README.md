# Black Hole Simulation - Scientific Visualization

A highly scientifically accurate 3D real-time black hole simulation built from scratch in C++ with OpenGL and MPS acceleration support.

## Scientific Features

### 1. Schwarzschild Metric & Spacetime Curvature
- Implements the Schwarzschild metric: `ds² = -(1-2M/r)dt² + (1-2M/r)⁻¹dr² + r²dθ² + r²sin²θdφ²`
- Visualizes the "trapdoor in spacetime" via a curved grid mesh that bends downward near the black hole using the metric curvature factor.

### 2. Ray-Tracing with Gravitational Lensing
- Traces photon paths through curved spacetime using approximate geodesic integration.
- Applies Einstein's deflection formula: `α = 2Rs / b` (where `Rs = 2GM/c²` is the Schwarzschild radius and `b` is the impact parameter).
- Renders a procedurally generated starfield that is gravitationally lensed around the black hole event horizon.

### 3. Accretion Disk (Shakura-Sunyaev Thin Disk)
- Simulates a thin accretion disk with temperature profile `T ∝ r^(-3/4)`.
- Includes gravitational redshift near the event horizon (`z = 1/√(1-Rs/r) - 1`).
- Includes Doppler beaming approximation and thermal emission visualization.
- Features inner hot halo (white-yellow), middle region (orange-red), and outer cool region.

### 4. Real-Time Performance
- OpenGL 3.3 Core Profile rendering at 60 FPS.
- Orbiting camera around the black hole.
- MPS (Metal Performance Shaders) framework support for Apple Silicon GPU acceleration.

## Build & Run

```bash
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
./black_hole_simulation
```

When run, it opens a new 1280x800 window with the real-time simulation.

## Project Structure

- `src/` - C++ source files (Physics, Renderer, Window, Shaders)
- `shaders/` - GLSL shaders for spacetime curvature, accretion disk, and ray tracing
- `build/` - Compiled binary and shader copies
- `CMakeLists.txt` - Build configuration with OpenGL, GLFW, GLEW, MPS

## Scientific Accuracy Notes

- Schwarzschild radius set to `2.0` (geometric units, M=1).
- Event horizon at `r = 2M`.
- Innermost Stable Circular Orbit (ISCO) at `r = 6M`.
- Photon trajectories integrated using 4th-order Runge-Kutta approximation for null geodesics.
- Temperature and emission profiles based on standard thin accretion disk theory.
