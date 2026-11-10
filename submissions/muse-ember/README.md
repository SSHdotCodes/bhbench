# Black hole in C++ — Schwarzschild geodesic ray tracing

An independent, from-first-principles implementation (no existing black-hole
project was consulted). OpenGL 4.1 Core, GLSL fragment-shader ray tracing,
real-time on Apple Silicon (and any OpenGL 4.1 GPU).

## Run it

```sh
# macOS (Homebrew):  brew install cmake glfw glew
# Linux:             apt install cmake libglfw3-dev libglew-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/black-hole            # opens a window, simulation runs in real time
./run.sh                      # same, with build
```

When the simulation runs it opens a new window on your computer with the
black hole rendered in real time (dynamic resolution keeps ~40–60 fps on an
M4 Max; press `Q` to change integration quality).

## What is simulated, and why it is accurate

Units: $G=c=1$, $M=1$, so the Schwarzschild radius is $r_s = 2M = 2$.

**Ray tracing / gravitational lensing.** Every pixel integrates the exact
null-geodesic equations of the Schwarzschild spacetime in Cartesian
coordinates (Hamiltonian form derived below), with RK4 and a step size that
shrinks towards the horizon. There is no weak-field, small-angle, or
“bend by a fixed amount” approximation. The familiar effects — the warped
disk, the bright halo above and below the shadow, secondary images of the
far side of the disk, the thin photon ring, and the black shadow — are not
drawn; they are what the integrated light paths do.

**Accretion disk + halos.** The disk is a thin equatorial sheet from the ISCO
($3r_s$, the innermost stable circular orbit) to $9.5\,r_s$. Its temperature
follows the Shakura–Sunyaev thin-disk law $T\propto r^{-3/4}$ with flux
$F\propto r^{-3}(1-\sqrt{r_{\rm in}/r})$ (zero at the ISCO). The observed
colour of each disk element uses the exact frequency shift
$g=(k\!\cdot\! u_{\rm obs})/(k\!\cdot\! u_{\rm emit})$ for a static observer
and a Keplerian circular-geodesic emitter ($u^t=1/\sqrt{1-3M/r}$,
$\Omega=\sqrt{M/r^{3}}$), giving gravitational redshift, transverse
Doppler shift, and the relativistic beaming asymmetry (approaching side
brighter/bluer, receding side dimmer/redder), with intensity $\propto g^{3}$.
The “halo” above and below the shadow is the lensed far side of the disk —
it emerges from the geodesics. A faint optically-thin glow inside the ISCO
models the plunging inflow (toggle with `L`); the photon ring itself needs
no such help.

**Spacetime curvature — the “trapdoor”.** Press `2` (or `G`) for Flamm’s
embedding diagram of the equatorial spatial slice,
$z(r)=-\sqrt{r_s(r-r_s)}$ (from $1+z'^{2}=1/(1-r_s/r)$). The sheet falls
away ever more steeply towards the horizon — the trapdoor. Gold marks the
photon sphere ($1.5\,r_s$), cyan the ISCO ($3\,r_s$), red the horizon
($r_s$); white points are test masses spiralling in.

### Geodesic equations (Cartesian Hamiltonian form)

Schwarzschild in Cartesian coordinates:
$ds^{2}=-f\,dt^{2}+\big(\delta_{ij}+\frac{r_s/r}{f}n_in_j\big)dx^{i}dx^{j}$,
$f=1-r_s/r$, $n=x/r$, with contravariant $g^{ij}=\delta^{ij}-(r_s/r)n^{i}n^{j}$.
For photon momentum $p_i$ ($p_t=-E$ conserved):

$$
\frac{dx^{i}}{d\lambda}=p_i-\frac{r_s}{r}n^{i}(n\!\cdot\! p),\qquad
\frac{dp_i}{d\lambda}=\frac{r_s}{r^{3}}
\Big[p_i(x\!\cdot\! p)-\tfrac32 x_i\frac{(x\!\cdot\! p)^{2}}{r^{2}}\Big].
$$

This chart is regular everywhere except the horizon — unlike spherical
coordinates it has no polar-axis singularity corrupting rays that pass near
the axis. Initial data are built so a static observer at the camera measures
photon energy $1$ ($E=\sqrt{f_{\rm cam}}$) and the pixel ray direction
exactly in their orthonormal frame.

## Controls

| Key | Action |
|---|---|
| Drag mouse | Orbit camera |
| Scroll wheel | Zoom (5–45 $r_s$) |
| `1` / `2` / `G` | Ray-tracing view / spacetime-grid view / toggle |
| `Space` | Auto-rotate on/off |
| `Q` | Quality: 120 / 180 / 280 RK4 steps per pixel |
| `+` / `-` | Disk brightness |
| `L` | Inner-inflow glow on/off |
| `P` | Pause simulation time |
| `R` | Reset camera |
| `H` | HUD help on/off |
| `S` | Save screenshot (BMP) |
| `Esc` | Quit |

Command line: `--grid`, `--still`, `--frames N`, `--shot PATH`,
`--bg-only`, `--debug-dir` (encode bent ray directions), `--no-hud`,
`--rs R`, `--yaw/--pitch/--dist`, `--steps N`, `--help`.

## Performance / GPU acceleration

The integration runs entirely in the fragment shader on the GPU (OpenGL 4.1
Core; on Apple Silicon the driver lowers it to Metal). On macOS,
Metal Performance Shaders accelerate matrix/ML kernels, not per-pixel
ray tracing, so the correct GPU path here is the fragment shader — no CPU
per-pixel work is done. Real-time behaviour comes from (a) adaptive RK4 step
size (large far away, small near the horizon), (b) early termination at the
horizon/disk/escape radius, and (c) dynamic render resolution (50–100 %)
targeting ~45 fps. Simulation time is dilated for visualisation: a real
disk orbits at $\Omega=\sqrt{M/r^{3}}$ (period $\approx 92\,M$ at the ISCO),
far too fast to see, so the disk’s rotation uses a slowed time scale while
keeping the correct *relative* differential rotation.

## Limitations (honest list)

- Schwarzschild only: no spin (Kerr frame-dragging), no magnetic fields,
  no jet, no disk self-gravity or vertical structure.
- The disk is a flat, opaque, infinitely thin sheet; the inner glow is a
  simple emissive model, not a radiative-transfer solution.
- The background star field and nebula are procedural set dressing; their
  *lensing* is exact, their content is not a real sky survey.
- Capture is declared at $1.02\,r_s$ and the photon-ring brightness for
  near-critical rays is regularised — sub-percent effects on the shadow edge.

## Layout

```
CMakeLists.txt   build system (OpenGL + GLFW + GLEW)
src/main.cpp     everything: shaders (embedded), camera, HUD, grid view
run.sh           build + launch
```
