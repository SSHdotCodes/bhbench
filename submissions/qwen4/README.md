# Schwarzschild Black Hole — Relativistic Ray Tracer (C++ / OpenGL)

A real-time, scientifically grounded 3D simulation of a non-rotating
(Schwarzschild) black hole. Every pixel is a **null geodesic integrated through
curved spacetime** on the GPU — gravitational lensing, the accretion disk, the
photon ring and the shadow all emerge from the geodesic equation, not from
image-space tricks.

Runs natively on macOS / Apple Silicon (OpenGL 4.1, which Apple implements on
top of Metal, so the ray tracing executes on the GPU).

## Run

```sh
brew install glfw          # once
make
./blackhole                # opens a window, real-time (from a terminal)
make launch                # or: build BlackHole.app and open it via LaunchServices
make test                  # numerical validation against known GR results
```

`make launch` is the robust way to keep the window open independently of the
terminal: the app runs inside the macOS GUI session (Quit with Esc/Q or ⌘Q).

CLI options: `--selftest`, `--capture out.png --frames N`, `--scale 0.75`,
`--incl 80 --azim 35 --dist 26` (camera), `--time 8`, `--exposure 1.2`,
`--no-grid`, `--no-disk`, `--no-hud`.

## Controls

| Input | Action |
|---|---|
| Left-drag | Orbit camera (inclination / azimuth) |
| Wheel | Zoom (5 – 80 RS) |
| `G` / `D` / `S` / `B` | Toggle spacetime grid / disk / stars / bloom |
| `1` `2` `3` | Quality: high / medium / fast (step size + step count) |
| `0` | Toggle automatic resolution scaling (targets ~60 fps) |
| `[` / `]` | Exposure down / up |
| `Space` | Pause disk evolution |
| `P` | Save `capture.png` |
| `F` | Fullscreen |
| `R` | Reset camera |
| `Esc` / `Q` | Quit |

## Physics model (units G = c = 1, RS = 2M = 1)

### 1. Ray tracing = exact null geodesics

Schwarzschild metric:

```
ds² = -(1 - RS/r) dt² + (1 - RS/r)⁻¹ dr² + r² dΩ²
```

For photons, the orbit equation (Binet form, u = 1/r) is

```
d²u/dφ² + u = (3/2) RS u²
```

which is equivalent to motion under the central acceleration

```
a(x) = -(3/2) RS h² x / r⁵ ,   h = |x × v|  (conserved)
```

in areal-radius Cartesian coordinates. The **spatial path is the exact GR
photon trajectory** (the parameterization along it is not affine, which is
irrelevant for imaging). Each fragment integrates this ODE with RK4 and an
adaptive step `dl = clamp(0.08 r, 0.02, 0.6)`. Rays terminate at the horizon
(r < RS → black), at the disk (first equatorial crossing inside the annulus —
optically thick), or at infinity (sample the sky along the escaped direction,
so the background star field is genuinely lensed: Einstein rings and tidal
star arcs appear near the shadow).

Key radii that emerge automatically:

| Quantity | Value |
|---|---|
| Event horizon | r = RS = 1 |
| Photon sphere | r = 1.5 RS |
| ISCO (disk inner edge) | r = 3 RS = 6M |
| Shadow / critical impact parameter | b_c = (3√3/2) RS ≈ 2.598 RS |

### 2. Accretion disk + relativistic halo

- Optically thick, geometrically thin disk on circular Keplerian orbits,
  r ∈ [3, 12] RS. Emissivity structure advects with the Keplerian angular
  velocity Ω(r) = √(M/r³) (differential rotation).
- **Shakura–Sunyaev zero-torque temperature profile**
  T(r) ∝ r^(−3/4) (1 − √(r_in/r))^(1/4), peak-normalized; T → 0 at the ISCO.
- **Full relativistic frequency shift** computed in the local static
  orthonormal frame at the emission event:

  ```
  g ≡ ν_obs/ν_emit = √(1 - RS/r) / [ γ (1 - β·n̂) ]
  β = √( M / (r - RS) )   (Keplerian speed vs. static observer; 0.5c at ISCO)
  ```

  combining gravitational redshift, transverse Doppler and line-of-sight
  Doppler. The photon direction n̂ is converted from the coordinate basis to
  the static frame (radial component divided by √(1−RS/r)).
- By Liouville's theorem (I_ν/ν³ invariant), bolometric intensity scales as
  **g⁴** — relativistic beaming makes the approaching side dramatically
  brighter. The observed spectrum is a blackbody at T_obs = g·T_emit, mapped
  to RGB with a Planckian-locus fit. The "halo" arching over the shadow is the
  lensed image of the disk's far side; the thin bright ring at the shadow edge
  is the photon ring (geodesics that wind near r = 1.5 RS).
- HDR pipeline: linear RGBA16F scene → bright-pass → separable Gaussian bloom
  → exposure → ACES tonemap → gamma.

### 3. Spacetime curvature grid ("trapdoor")

The wireframe is **Flamm's paraboloid**, the exact isometric embedding of the
t = const, equatorial spatial slice of Schwarzschild into Euclidean 3-space:

```
y(r) = -2 √( RS (r - RS) )
```

Rings + radial spokes are drawn on that surface and depth-tested against the
ray-traced scene (the opaque disk occludes them; lines behind the shadow
silhouette are faded). Note this is an *embedding diagram* of spatial
geometry, rendered in coordinate space — a visualization aid, not a literal
fabric, and not itself lensed.

### Approximations (stated honestly)

- Non-rotating hole (no Kerr frame dragging; shadow is circular).
- Thin, optically thick disk: no volumetric radiative transfer, no
  self-irradiation, no limb darkening; blackbody emission.
- Procedural background sky (for lensing visualization), not a real catalog.
- Disk turbulence is a passive advected pattern, not MHD.
- Simulation time is in geometric units, played back at 6× for visibility
  (the absolute timescale of a real hole depends on its mass).

## Numerical validation (`./blackhole --selftest`)

The same integrator is run on the CPU in double precision and checked against
analytic GR:

- Weak-field deflection at b = 50 RS vs. α = 2RS/b + (15π/16)(RS/b)² — **0.1% error**
- Conserved angular momentum drift — **< 1e-14**
- Capture threshold by bisection vs. b_c = 3√3/2 RS — **0.0007% error**
- Static-observer orbital speed at ISCO — **exactly 0.5 c**
- Strong-field winding for b = b_c + 0.001 — **> 2π accumulated turning** (photon ring)

## Files

```
src/main.cpp        window, camera, render passes, HUD, CLI
src/selftest.h      CPU geodesic integrator + GR validation
src/gl_helpers.h    shader loading, PNG capture (zlib)
src/math.h          vec/mat helpers
src/font.h          5x7 HUD font
shaders/raytrace.frag  geodesic integrator, disk radiative model, lensed sky
shaders/grid.*       Flamm paraboloid wireframe
shaders/bright/blur/composite.frag  bloom + tonemap
shaders/text.*       HUD
```

Render passes per frame: geodesic ray trace (HDR + depth) → embedding grid
(depth-tested) → bloom (¼-res ping-pong) → ACES composite → HUD.
