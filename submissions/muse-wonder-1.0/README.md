# Black Hole — Realtime Schwarzschild Ray Tracing (C++ / OpenGL)

A highly scientifically-grounded, realtime 3D black hole simulation. No textures, no fake lensing post-effect: every pixel integrates a real null geodesic through Schwarzschild spacetime.

## Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/blackhole
```

A 1280×800 OpenGL window opens immediately. Drag to orbit.

## Physics (units Rs = 1, G = c = 1, M = 1/2)

| Feature | Implementation |
|---|---|
| Metric | Schwarzschild. Horizon `r=1`, photon sphere `r=1.5`, ISCO `r=3` |
| Ray tracing / lensing | Per-pixel null-geodesic integration `d²x/dλ² = -3/2 · h² x / |x|^5`, `h = |x × v|` conserved. Leapfrog (kick-drift-kick), adaptive step `dt ∝ (r - Rs)`. Rays with `r < 1` are captured (true shadow), rays skimming `r≈1.5` orbit (photon ring), background stars are sampled with the *bent* exit direction — so lensing, Einstein ring and higher-order disk images emerge, they are not drawn |
| Accretion disk | Thin Novikov–Thorne disk `r ∈ [3, 9] Rs`. `T⁴ ∝ r⁻³(1-√(rin/r))`. Keplerian `Ω = √(M/r³)`, orbital `β = 1/√(2(r-1))` (β=0.5 at ISCO). Observed intensity `I = g³ Iemit` with `g = g_grav · g_Doppler`, `g_grav = √(1-Rs/r)`. Blackbody colour from shifted T. Differential rotation + procedural turbulence in the co-rotating frame. Multiple plane crossings accumulate front-to-back, giving the lensed top/bottom disk images above and below the shadow |
| Halos | Photon-sphere glow from minimum approach distance along each geodesic + faint scattered-light halo |
| Spacetime grid ("trapdoor") | Flamm's paraboloid — the exact embedding of the Schwarzschild `t=const, θ=π/2` spatial slice into Euclidean 3-space: `z(r) = 2√(Rs(r-Rs))`, rendered as a polar wireframe funnel under the hole. Flat far away, steep throat at the horizon |
| Tonemapping | ACES filmic + gamma, starfield sampled along lensed rays |

Toggle lensing with `L` to see the same scene with straight rays — the shadow shrinks, the Einstein ring and secondary disk images vanish, which demonstrates that they are purely relativistic.

## Controls

| Key | Action |
|---|---|
| Drag / scroll | Orbit / zoom (4–35 Rs) |
| `D` `G` `L` | Toggle disk / grid / gravitational lensing |
| `1` `2` `3` | Black hole only / grid only / combined view |
| `Q` | Cycle geodesic steps 160 → 280 → 420 (quality vs FPS) |
| `+` / `-` | Exposure |
| `Space` | Pause disk rotation |
| `R` | Reset camera |
| `P` | Save current frame to `/tmp/bh_live.ppm` |
| `Esc` | Quit |

Headless verification modes (used for the test captures in development):

```sh
./build/blackhole --capture out.ppm theta phi radius disk grid lens view steps time W H
./build/blackhole --bench 90 1280 800   # measured: 9.1 ms/frame ≈ 110 FPS on this machine
```

FPS and current toggles are shown live in the window title.

## Performance

The geodesic march runs entirely in the fragment shader (GPU-parallel, OpenGL — on Apple Silicon this executes on the Metal-backed GL driver). 280 steps/pixel at 1280×800 is realtime on an M-series GPU; press `Q` down to 160 steps if needed. No CPU per-frame physics, no allocations in the render loop.

## Files

- `src/main.cpp` — window, orbit camera, grid geometry, render loop
- `shaders/blackhole.frag` — geodesic integrator, disk shading, lensed starfield
- `shaders/grid.vert` — Flamm embedding displacement

## Limitations / extensions

Schwarzschild (non-spinning). A Kerr extension would replace the integrator with the Carter-constant form and add frame-dragging + an ergosphere. Disk is optically-thin single-scattering accumulation; full radiative transfer and returning radiation are approximated by the `g³` law.
