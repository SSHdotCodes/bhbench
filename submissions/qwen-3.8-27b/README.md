# Black Hole — Real-Time Geodesic Ray Tracer

A real-time, interactive 3D Schwarzschild black hole simulation built in C++
with Metal (Apple GPU) and Metal Performance Shaders (MPS) for bloom.

## Build & Run

```sh
make        # compiles src/app.mm + src/sim.cpp -> ./black-hole
./black-hole
```

Requires macOS with an Apple Silicon (or AMD) GPU and Metal. No external
dependencies; shaders are embedded C strings compiled at runtime.

## Controls

| Input          | Action                                    |
|----------------|-------------------------------------------|
| drag           | orbit camera                              |
| scroll         | zoom (camera distance)                    |
| `1`            | lens view (ray-traced hole only)          |
| `2`            | trapdoor view (curved-spacetime grid)     |
| `3`            | hybrid (ray-traced hole + grid)           |
| `G`            | toggle spacetime grid                     |
| `Space`        | pause / resume photon particles           |
| `R`            | reset camera                              |
| `A`            | toggle adaptive resolution                |
| `H`            | toggle HUD                                |
| `+` / `-`      | internal resolution ±10%                  |

The HUD shows live FPS, internal resolution scale, and active mode. Adaptive
resolution (on by default) lowers internal render resolution when FPS < 47 and
restores it above 56.

## Rendering pipeline (per frame)

1. **Geodesic ray tracing** (Metal compute, 1000-step RK4 per pixel): each
   pixel's ray is integrated through the Schwarzschild metric as a null
   geodesic. Rays that cross the equatorial plane inside the disk radius are
   shaded with a Doppler-shifted blackbody; rays reaching r ≤ 2 go black
   (shadow); rays escaping to r > 85 sample a procedural starfield.
2. **Bloom/halo** (MPS): ray-traced image is downsampled to 1/4 res, Gaussian
   blurred with `MPSImageGaussianBlur` (self-tested at startup, with a
   compute-shader fallback), and composited additively.
3. **Tonemap**: ACES filmic fit + gamma 2.2.
4. **Spacetime grid** (modes 2/3 or `G`): the Flamm-paraboloid embedding of
   the equatorial spatial slice, z(r) = 2·sqrt(2(r−2)), drawn as a translucent
   cyan grid with labeled circular markers at the horizon (r=2, red), photon
   sphere (r=3, white), ISCO (r=6, orange) and disk edge (r=30), plus a dark
   "throat cap" at the funnel bottom.
5. **Photon geodesic particles**: 128 photons launched from r∈[55,90] with
   impact parameters clustered around the critical value b_crit = 3√3; their
   RK4-integrated trajectories are drawn as fading trails on the funnel
   (orange = captured, cyan = deflected/escaped).
6. **HUD**: bitmap-font text rendered to a texture on the CPU each frame.

## Physics (units: G = c = 1, M = 1)

- Metric (Schwarzschild): ds² = −(1−2/r)dt² + (1−2/r)⁻¹dr² + r²dΩ²
- Event horizon r = 2, photon sphere r = 3, ISCO r = 6
- Null geodesics integrated in second-order form (no square-root turning-point
  handling needed):
  - r̈ = (E²−ṙ²)/(f³r²) − (r/f)(θ̇² + sin²θ·φ̇²)
  - θ̈ = ṙθ̇/r − sinθ·cosθ·φ̇²
  - φ̈ = ṙφ̇/r + cotθ·θ̇φ̇,  with f = 1 − 2/r
- Conserved quantities E = f·ṫ, L_θ = r²θ̇, L_z = r²sin²θ·φ̇
- Accretion disk: T(r) = 1.5·10⁷·(6/r)^0.75 K (Shakura–Sunyaev profile),
  rendered through blackbody color; relativistic Doppler factor
  g = √f / (γ(1 − v·n_φ)), v = √(1/(r−2)) (Keplerian speed in a local static
  frame), intensity ∝ g⁴ (beaming + redshifted blackbody).
- Gravitational lensing and the photon ring emerge naturally from the same
  geodesic integrator — no separate lensing model.

## Honest simplifications

- Schwarzschild (non-spinning) hole; no Kerr frame-dragging or disk
  precession.
- The "trapdoor" is the Flamm-paraboloid embedding of the equatorial spatial
  slice — a visualization, not the full 4D geometry; its height is scaled by
  0.55 so the outer rings fit on screen.
- Disk thickness is zero (thin-disk model); Doppler uses the local static
  observer frame, not the full photon 4-momentum projection.
- Blackbody colors are shifted ×1.05e-3 into the visible band (the real disk
  peaks in X-rays/UV).
- The starfield is procedural (hash noise), not a real sky.

## Files

```
src/app.mm       Obj-C++ shell: window, MTKView, Metal pipeline, MPS bloom, input
src/sim.hpp/cpp  C++ core: camera math, Flamm mesh, particle geodesics, HUD font
src/shaders.hpp  MSL source (ray tracer, blur, composite, grid, HUD)
Makefile         build
```
