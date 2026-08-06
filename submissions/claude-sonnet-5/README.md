# Black Hole Simulator

A real-time, GPU ray-traced visualization of a (non-rotating, Schwarzschild)
black hole in C++ and OpenGL 4.1 core. Built from first principles — every
photon path is obtained by numerically integrating the actual general-
relativistic geodesic equation per pixel, every frame.

It has two views, toggled with `TAB`:

- **Raytrace** — gravitational lensing, an accretion disk with realistic
  Doppler beaming / gravitational redshift, and the black hole's shadow,
  all falling out of one geodesic integrator.
- **Spacetime Grid** — the Flamm's-paraboloid embedding diagram: an exact
  GR visualization of how the black hole curves space into a funnel (the
  "trapdoor" look), with the accretion disk's radial extent marked on it.

Both run live, interactively, typically at 60+ fps on Apple Silicon.

## Build & run

Requires CMake, a C++17 compiler, and Homebrew's `glfw` and `glm`
(`brew install glfw glm`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/black_hole_sim
```

A window opens immediately and starts rendering.

## Controls

| Input         | Action                                          |
|---------------|--------------------------------------------------|
| Left-drag     | Orbit the camera                                  |
| Scroll        | Zoom                                              |
| `TAB` / `M`   | Toggle Raytrace view ↔ Spacetime Grid view        |
| `L`           | Toggle gravitational lensing on/off (A/B compare) |
| `SPACE`       | Pause / resume disk motion                        |
| `=` / `-`     | Increase / decrease render resolution             |
| `]` / `[`     | Increase / decrease ray-march quality (steps)     |
| `R`           | Reset the current view's camera                   |
| `ESC`         | Quit                                              |

The `L` toggle is worth trying first: it switches the same scene between
curved (geodesic) and straight-line ray tracing, so you can directly see
what gravitational lensing is doing to the image.

## The physics

Everything is computed in geometrized units (`G = c = 1`), with the
Schwarzschild radius `rs = 2M` as the unit of length (`rs = 1` by default;
`M` is therefore `0.5`).

### Geodesic ray tracing (`shaders/blackhole.frag`)

A photon trajectory around a non-spinning black hole is always planar — the
plane through the black hole, the ray's origin, and its initial direction.
Within that plane, using `u = 1/r`, the *exact* null-geodesic equation is

```
d²u/dφ² = -u + (3/2) rs u²
```

For each screen pixel, the shader builds that plane, sets the initial
`(u, du/dφ)` from the camera ray, and integrates this ODE with RK4,
stepping in the angle `φ` with an adaptive step size (finer near the
photon sphere, coarser far away). At every step it checks three things:

1. **Captured** — `r` has dropped to `rs`: the pixel is black (event horizon).
2. **Disk crossing** — the trajectory crossed the equatorial plane (`y=0`)
   within the disk's radius range: shade and accumulate that point's
   emission (see below). The loop keeps going after a hit, so a single
   pixel can pick up *multiple* disk crossings — this is what produces the
   lensed secondary image of the disk's far side arcing above and below
   the shadow.
3. **Escaped** — `r` has grown large: sample a procedural starfield with
   the photon's final direction.

No special-casing is needed for the photon ring or multiple images — they
are an emergent result of integrating the real equation far enough.

A `straightMarch` fallback (flat-space ray/plane and ray/sphere
intersection) handles purely radial rays and is also what `L` switches to
for the lensing-off comparison.

### Accretion disk shading

At each disk crossing, radius `r`:

- **Temperature** follows the Shakura–Sunyaev zero-torque thin-disk flux
  profile, `F(r) ∝ (r/Rin)⁻³ (1 − √(Rin/r))`, which is exactly zero at the
  inner edge and peaks just outside it — the standard boundary condition
  for a disk terminating at the ISCO. The normalized flux drives a
  blackbody temperature, remapped into the visible range (real disk
  temperatures, ~10⁵–10⁷ K, all sit far blue of the visible Planckian
  locus; this remap is an explicit visualization choice, not a physics
  claim).
- **Gravitational redshift**: `sqrt(1 - rs/r)`.
- **Relativistic Doppler beaming**: disk material orbits at the exact
  Schwarzschild circular-geodesic speed `v = sqrt(M/(r-rs))` (relative to
  a local static observer); combined with the photon's local arrival
  angle this gives a combined shift factor `g`, and observed brightness
  scales as `g⁴` — the well-known relativistic beaming result, which is
  why one side of the disk is dramatically brighter/bluer (approaching)
  than the other (receding).
- Inner disk edge = ISCO = `3·rs` (= `6M`), the innermost stable circular
  orbit for Schwarzschild.

Color temperature → RGB uses the standard Planckian-locus polynomial
approximation (Tanner Helland's fit).

### Spacetime curvature grid (`shaders/grid.vert`)

The equatorial slice of Schwarzschild spacetime embeds exactly into flat
3D space via **Flamm's paraboloid**:

```
z(r) = 2 √(rs (r − rs)),   r ≥ rs
```

This is not an artistic rubber-sheet metaphor — it's the literal exact
embedding diagram. The grid mesh displaces a flat polar grid by `-z(r)` in
its vertex shader, producing the funnel that sinks steeply toward the
horizon (`r = rs`, highlighted in red) and flattens out far away, with the
accretion disk's radial range highlighted in orange for context.

## Known approximations & limitations

- **Schwarzschild only** (no spin/Kerr) — chosen because the photon
  trajectory stays exactly planar, which is what makes a real-time
  per-pixel ODE integration tractable.
- Disk emission uses a static thin-disk flux/temperature *profile*; it is
  not a fluid/MHD simulation, so there's no genuine turbulent structure —
  the subtle mottling is decorative procedural noise, not physics.
- Display temperatures are rescaled into the visible blackbody range (see
  above) rather than using physical disk temperatures.
- Finite step count near the critical impact parameter (the boundary of
  the photon ring) can show a thin aliasing artifact, since the true
  winding number diverges there for any finite-precision integrator —
  increasing ray-march quality (`]`) narrows it but can't eliminate it
  without supersampling.

## Project layout

```
CMakeLists.txt
src/
  main.cpp      window/GL context, input, render loop, FBO pipeline
  Camera.{h,cpp}    spherical orbit camera
  Shader.{h,cpp}    GLSL program loader
  GridMesh.{h,cpp}  procedural polar grid for the spacetime-curvature view
shaders/
  fullscreen.vert        fullscreen-quad pass-through
  blackhole.frag         geodesic ray tracer + disk + starfield
  grid.vert / grid.frag  Flamm's paraboloid grid
```
