# Black Hole 3D Simulation (C++ / OpenGL)

A scientifically-inspired 3D black hole simulation with real-time rendering.

## Features

- **Gravitational Lensing** (Ray Tracing) - Light rays are bent by the black hole's gravity using geodesic integration in curved spacetime
- **Accretion Disk** - Hot particle disk orbiting the black hole with temperature gradients
- **Spacetime Curvature Grid** - Deformable grid showing the "trapdoor in spacetime" caused by the black hole's mass
- **Photon Sphere Glow** - Visual representation of the photon capture region
- **3D Starfield** - Background of lensed stars

## Controls

- **Left Mouse Drag** - Rotate camera around the black hole
- **Scroll Wheel** - Zoom in/out
- **G** - Toggle spacetime grid
- **D** - Toggle accretion disk
- **S** - Toggle starfield
- **L** - Toggle gravitational lensing visualization
- **R** - Reset camera
- **SPACE** - Auto-rotate camera
- **ESC** - Exit
- **1/2/3** - Change black hole mass (5, 10, 20 solar units)

## Building

```bash
cd build
cmake ..
make -j$(sysctl -n hw.logicalcpu)
```

### Dependencies
- GLFW3
- GLEW
- OpenGL (macOS built-in)
- C++17 compiler

### macOS Install
```bash
brew install glfw glew
```

## Running

```bash
./blackhole_sim
```

## Physics

The simulation uses the Schwarzschild metric for a non-rotating black hole:
- **Schwarzschild radius**: Rs = 2GM/c²
- **Photon sphere**: 1.5 Rs
- **Innermost Stable Circular Orbit (ISCO)**: 3 Rs
- **Gravitational lensing**: Light deflection via geodesic integration through curved spacetime
- **Accretion disk**: Temperature follows T ~ r^(-3/4) scaling with Doppler Doppler

## Scientific Accuracy Notes

This simulation is **visually inspired** by general relativity but uses simplified
models for real-time rendering. The gravitational lensing uses ray-marching
through approximated Schwarzschild geodesics, not full GR ray tracing.
The accretion disk uses Keplerian orbital mechanics with artistic color mapping.

For production scientific visualization, use dedicated GR ray-tracing tools
such as Geodesic Viewer or BHAC.
