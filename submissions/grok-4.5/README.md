# Schwarzschild Black Hole — Real-time GR Ray Tracer (C++ / OpenGL)

A scientifically grounded **real-time** black-hole simulation written in modern C++ with OpenGL 4.1 (macOS-compatible).

## Physics

| Feature | Implementation |
|--------|------------------|
| Spacetime | Schwarzschild metric, geometric units \(G = c = 1\) |
| Horizon | \(r_s = 2M\) |
| Photon sphere | \(r_{\mathrm{ph}} = 3M\) (unstable circular null orbit) |
| ISCO | \(r_{\mathrm{ISCO}} = 6M\) (thin-disk inner edge) |
| **Ray tracing / lensing** | Per-pixel **null geodesic** integration of the exact planar orbit equation \(\frac{d^2u}{d\varphi^2} + u = 3Mu^2\) (\(u=1/r\)) with RK4. Spherical symmetry ⇒ each ray integrated in its orbital plane. Critical impact parameter \(b_c = 3\sqrt{3}\,M\). |
| **Accretion disk** | Optically thin equatorial disk from ISCO → outer radius; Page–Thorne–style emissivity; gravitational redshift \(\sqrt{1-r_s/r}\); Keplerian Doppler; Liouville \(I_\nu/\nu^3\) intensity scaling; multiple images from strong deflection |
| **Photon ring / halos** | Bright rings from rays that wind near the photon sphere (\(b \approx b_c\)) |
| **Spacetime curvature** | **Flamm’s paraboloid** embedding of the equatorial spatial slice: \(z(r)=2\sqrt{r_s(r-r_s)}\) — the classic “trapdoor in spacetime” — plus polar grid lines and a curvature cage |

## Build

### Dependencies (macOS / Homebrew)

```bash
brew install cmake glfw glm
```

OpenGL ships with macOS (4.1 core profile).

### Compile & run

```bash
cd black-hole-cpp-grok45
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/black_hole_sim
```

A window opens with the simulation running in real time (vsync enabled).

## Controls

| Input | Action |
|-------|--------|
| Mouse drag | Orbit camera |
| Scroll | Zoom |
| `G` | Toggle spacetime grid |
| `F` | Toggle Flamm surface |
| `D` | Toggle accretion disk |
| `Space` | Pause disk animation |
| `1` / `2` / `3` | Quality: fast / normal / high |
| `+` / `-` | Change mass \(M\) |
| `R` | Reset camera & mass |
| `Esc` | Quit |

## Project layout

```
black-hole-cpp-grok45/
  CMakeLists.txt
  README.md
  include/     camera, mesh, shader, physics headers
  src/         main loop, OpenGL resources
  shaders/     geodesic ray tracer + spacetime grid GLSL
```

## Notes on accuracy & performance

- Integration uses the **exact** Schwarzschild null-orbit ODE (not a Newtonian deflection approximation).
- Finite camera distance is supported; impact parameter uses \(|\mathbf{x}\times\hat{\mathbf{k}}|\).
- Quality tier `1` targets interactive rates on Apple Silicon / modern GPUs; tier `3` increases RK4 steps for sharper rings.
- macOS OpenGL is capped at **4.1**, so acceleration is via **fragment-shader ray marching** (highly parallel on the GPU). No CUDA/Metal compute is required.

## References (equations)

1. Misner, Thorne & Wheeler — *Gravitation* (Schwarzschild geodesics, Flamm embedding).  
2. Chandrasekhar — *The Mathematical Theory of Black Holes* (null orbits, \(b_c=3\sqrt{3}M\)).  
3. Novikov & Thorne / Page & Thorne — thin accretion disk structure.  
4. Luminet (1979) — pioneering disk image with secondary rings.
