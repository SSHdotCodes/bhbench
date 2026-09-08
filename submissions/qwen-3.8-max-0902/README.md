# Schwarzschild Black Hole — real-time GPU geodesic ray tracer (C++ / OpenGL)

A scientifically accurate, real-time 3D black-hole simulation. Every pixel is a
light ray integrated backwards along an **exact null geodesic of the
Schwarzschild metric**; the accretion disk, its lensed "halo" images, the
shadow, the photon ring and the warped star field all emerge from the
geodesic integration — nothing is faked with sprites or billboards.

Units: `G = c = 1`, Schwarzschild radius `r_s = 2M = 1` world unit.

## Build & run

```sh
make          # needs: brew install glfw   (Apple Silicon or Intel Mac)
./blackhole   # opens a window, runs in real time
```

Requirements: macOS, GLFW 3 (`brew install glfw`). Rendering uses OpenGL 4.1
core, which on macOS is backed by Apple's Metal driver (GPU-accelerated).

Headless screenshots (for testing):

```sh
./blackhole --shot out.ppm --mode 1 --frames 90   # modes 1|2|3
```

## Controls

| input        | action                                            |
|--------------|---------------------------------------------------|
| drag         | orbit camera                                      |
| scroll       | zoom                                              |
| `1` / `2` / `3` | ray-traced view / spacetime grid / split-screen |
| `G`          | cycle views                                       |
| `Space`      | pause time                                        |
| `D`          | toggle accretion disk                             |
| `=` / `-`    | exposure                                          |
| `[` / `]`    | geodesic integration steps (accuracy vs speed)    |
| `R`          | reset camera                                      |
| `Esc`        | quit                                              |

## Physics

### 1. Gravitational lensing (ray tracing)
Light rays obey the relativistic Binet equation for Schwarzschild null
geodesics,

    d²u/dφ² = −u + (3/2) r_s u²,   u = 1/r,

integrated per-pixel with adaptive-step RK4 in the ray's orbital plane
(a plane through the origin, which is totally geodesic by spherical symmetry).
Consequences reproduced exactly: light deflection ≈ 4M/b in the weak field,
the unstable photon orbit at r = 1.5 r_s, the critical impact parameter
b_crit = (3√3/2) r_s and hence the shadow, multiple winding (photon-ring
substructure), and Einstein-ring-like distortion of the background star field.

### 2. Accretion disk + halos
Razor-thin equatorial disk from the exact Schwarzschild ISCO (r = 6M = 3 r_s)
to 13 r_s. Ray/disk-plane crossings are found analytically inside each
integration step and composited front-to-back (semi-transparent disk), so
rays that wind around the hole produce the higher-order lensed images of the
disk — the bright "halo" arcs above and below the shadow.

* Temperature: Shakura–Sunyaev thin disk,
  `T(r) = T_d (r/r_in)^{-3/4} (1 − √(r_in/r))^{1/4}` (zero torque at ISCO).
* Redshift/beaming: for a circular Keplerian emitter (4-velocity
  `u^t = (1−3M/r)^{-1/2}`, `Ω = √(M/r³)`) and observer at infinity, the exact
  frequency ratio uses the photon's conserved axial angular momentum:
  `g = ν_obs/ν_em = 1 / [u^t (1 − Ω L_z/E)]`, with `L_z/E` carried from the
  camera along the ray. This combines gravitational redshift, transverse and
  radial Doppler shift exactly; the approaching side is brighter and bluer
  (relativistic beaming, `I_obs = g⁴ I_em`, observed blackbody at `T_obs = g·T`).
* Gas texture is frozen into the flow (pattern co-rotates at Ω(r)).

### 3. Spacetime curvature — the "trapdoor"
View 2 renders **Flamm's paraboloid**, the exact isometric embedding of a
t = const equatorial slice of Schwarzschild spacetime,

    z(r) = 2 √( r_s (r − r_s) ),

as a wireframe grid: vertical tangent at the horizon (the throat), flattening
towards the asymptotic plane — the classic funnel/trapdoor shape. Test
particles inspiral on Keplerian orbits (`Ω = √(M/r³)`) mapped onto the curved
surface. Horizon (r = r_s) and photon-sphere (r = 1.5 r_s) rings are marked.

## Real-time performance
The ray tracer renders into an adaptive-resolution FBO (blitted to the
window), so the frame rate stays at vsync (60 fps): internal scale steps
between 0.4× and 1.0× of the framebuffer based on a frame-time EMA.
Typical cost: ~10–14 ms/frame at 1600×900 on Apple Silicon (M4 Max),
i.e. real time with headroom. `[` / `]` trade geodesic step count for speed.

## Files
* `main.cpp` — application + all GLSL shaders (single translation unit)
* `Makefile`
