# Kerr black hole: ray-traced lensing in real time (C++ / Metal)

A spinning (Kerr) black hole simulated from the Einstein equations' geodesics and rendered in a
Metal window at real-time frame rates on Apple silicon. It includes:

- **Gravitational lensing** from ray tracing light through the exact Kerr metric.
- **An accretion disk** modelled as a Novikov-Thorne thin disk, with its gravitational and Doppler
  redshift, rendered through the same rays (so the lensed back side and the photon-ring "halo" come out
  of the physics, not an effect).
- **A spacetime "trapdoor" view**: the equatorial spacetime embedded as a rubber sheet with a grid.
- **Validation**: a double-precision test suite of 25 checks against analytic results.

Everything is built from the equations; no other black hole project was consulted.

## Quick start

```sh
make test                                   # 25 physics checks (double precision, CPU)
make                                        # builds bin/blackhole, bin/bh_cpu, bin/test_physics
./bin/blackhole                             # opens the window: lensing view
./bin/blackhole --view sheet                # opens the spacetime sheet view
./bin/blackhole --snapshot frame.png --width 1600 --height 1000 --spin 0.998 --pitch 0.04
./bin/bh_cpu --out cpu.png --width 800 --height 500 --spin 0.9   # CPU reference renderer
```

Requires macOS with Metal (tested on macOS 27.0, Apple M4 Max, with the Xcode toolchain).

## Renders (`renders/`)

| file | what it shows |
|---|---|
| `lensing_a0.90.png` | default view, a = 0.9: shadow shifted to the retrograde side, bright prograde photon ring |
| `lensing_a0.998_edge_on.png` | near-extremal spin, nearly edge-on: lensed back side arches over and under the shadow |
| `lensing_schwarzschild_a0.png` | a = 0: circular shadow; the sky grid repeats around it as higher-order photon-ring images |
| `lensing_a0.90_grid_sky.png` | the latitude/longitude sky grid, lensed by the hole |
| `spacetime_sheet_a0.90.png` | rubber-sheet "trapdoor": equatorial Kerr geometry, horizon (red), photon orbit (yellow), ISCO (orange) |
| `spacetime_sheet_a0.998.png` | the same for a = 0.998: a narrow throat; the three rings nearly coincide |
| `cpu_reference_a0.90.png` | the double-precision CPU renderer, for comparison with the GPU |

## What is simulated

**Metric.** Kerr in Kerr-Schild Cartesian coordinates (G = c = M = 1):
`g = eta + H l l`, `H = 2 r^3 / (r^4 + a^2 z^2)`, `l_mu = (1, (r x + a y)/(r^2+a^2), (r y - a x)/(r^2+a^2), z/r)`,
with `r` the Kerr-Schild radius. These coordinates are regular across the horizon and the axis, so no
coordinate singularity needs special handling. The inverse metric is exact: `g^{mu nu} = eta - H l^mu l^nu`.
All first derivatives are analytic (`src/kerr_core.h`) and checked against finite differences.

**Light.** Photons follow null geodesics as the Hamiltonian flow of `(1/2) g^{mu nu} p_mu p_nu`:
`dx^i/dl = p_i - H l_i q`, `dp_i/dl = (1/2)[dH/dx^i q^2 + 2 H q p_j dl_j/dx^i]`, with `q = l^mu p_mu`.
Energy `E = -p_t` and the axial angular momentum `L = y p_x - x p_y` are conserved; we normalise `E = 1`.
Integration is RK4 with an adaptive step: each step moves a photon about `eta |x|` in space and changes
its momentum by about `eta |p|`. The second limit matters near the horizon, where the momentum grows
(see the notes below).

Camera rays come from a static observer's orthonormal frame (Gram-Schmidt in the full metric), so
aberration and the camera's own gravitational redshift are exact for a static observer.

**Capture.** A photon is captured when its radius drops below
`r_capture = r_+ + (r_ph+ - r_+)/2`, where `r_ph+` is the prograde photon orbit. An escaping photon's
closest approach is at least `r_ph+`, so it can never be inside `r_capture`. I checked this numerically:
over about 70,000 escaping rays per spin, the smallest radius reached stayed above `r_ph+` (for a = 0.9 the
minimum was 1.68, against `r_ph+` = 1.56 and `r_capture` = 1.50). This avoids integrating
into the horizon, where the momentum grows in these coordinates, without changing any outcome.

**Accretion disk.** Thin, optically thick Novikov-Thorne disk around the equatorial plane `z = 0`
(Page & Thorne 1974), extending from the prograde ISCO (Bardeen formula) to `r = 16 M`:

- Circular orbits: `Omega = 1/(r^{3/2}+a)`, `E`, `L`, `u^t` (Bardeen, Press & Teukolsky 1972).
- Flux: `F(r) = Mdot f(r) / (4 pi r)`, with `f = -Omega'/(E - Omega L)^2 * int_{r_isco}^r (E - Omega L) L' dr'`.
- The first crossing of the disk with `r >= r_isco` absorbs the ray, which is the optically thick
  assumption. Rays that cross inside the ISCO continue into the hole.
- Redshift from the emitter to a static camera: `g = u^t_cam / (u^t_disk (1 - Omega L))`.
  Observed intensity `I_obs = g^4 I_emit` (bolometric). Observed temperature `T_obs = g T_emit`.
- Colour: a blackbody at `T_obs`. The chromaticity uses a standard polynomial fit, so colours are an
  sRGB approximation of the Planck spectrum.

**Spacetime sheet.** The equatorial `t = const` slice of Kerr has induced metric
`dl^2 = (r^2/Delta) dr^2 + rho(r)^2 dphi^2`, with `rho^2 = r^2 + a^2 + 2a^2/r`. Embedding it in
cylindrical coordinates gives `dY/dr = sqrt(r^2/Delta - rho'^2)`. For `a = 0` this reproduces Flamm's
paraboloid, which the tests check. The horizon ring, photon orbit and ISCO are marked in colour.

**Physical scale.** For a 10 M_sun black hole accreting at 1e-8 M_sun/yr with a = 0.9, the peak
disk temperature is 6.3e6 K (X-ray), and the radiative efficiency `1 - E_isco` is 15.6%. Both appear in
the window title. The colours use a display temperature (7500 K peak, `--tpeak`) because the physical
disk is X-ray-bright and the eye sees nothing at 1e7 K.

## Validation (`make test`)

All checks run in double precision on the CPU. Current output:

```
[PASS] metric inverse g^{mu a} g_{a nu} = delta     max error 7.122e-16
[PASS] l_mu null: sum l_i^2 = 1 (eta-null)          max error 5.551e-16
[PASS] Kerr-Schild r satisfies implicit equation    max residual 4.441e-16
[PASS] analytic gradients match finite differences  max relative error 6.153e-10
[PASS] escaping photon stays null (a=0.9, eta=0.02) max |g p p| 3.197e-08, min r 3.514
[PASS] photon L = y p_x - x p_y conserved           max |dL| 1.871e-08
[PASS] static observer triad orthonormal, u-orthogonal max error 2.220e-16
[PASS] pixel rays are null (after E = 1 normalisation) max residual 7.394e-16
[PASS] ISCO r = 6M for a = 0 (Schwarzschild)        r = 6.000000000000
[PASS] ISCO r = 2.32088 for a = 0.9                 r = 2.32088304
[PASS] ISCO efficiency a=0: 1 - E = 1 - sqrt(8/9)   E = 0.942809041582
[PASS] L_isco = 2 sqrt(3) for a = 0                 L = 3.464101615138
[PASS] circular orbit: u.u = -1, E = -u_t, L = u_phi max error 1.704e-15
[PASS] dE/dr vanishes at the ISCO (a = 0.9)         dE/dr = -1.665e-11
[PASS] NT energy identity, a = 0.0                  integral 0.057195 vs 1 - E_isco 0.057191
[PASS] NT energy identity, a = 0.9                  integral 0.155756 vs 1 - E_isco 0.155753
[PASS] rubber sheet reproduces Flamm's paraboloid (a=0) max |dY| 2.984e-13
[PASS] prograde photon orbit: closed form = root    max error 1.776e-15
[PASS] Schwarzschild critical b = 3 sqrt(3) M       b_c = 5.196152
[PASS] Kerr a=0.9 prograde critical L (co-rotating) L_c = 2.844421 vs formula 2.844421
[PASS] Kerr a=0.9 retrograde critical L (counter)   L_c = -6.832319 vs formula -6.832319
[PASS] prograde photons get closer than retrograde  |L_pro| = 2.8444 < |L_retro| = 6.8323
[PASS] float32 vs float64 outcomes agree (GPU precision) 0 of 1681 pixels disagree
[PASS] float32 vs float64 escape directions         max |dn|_1 5.484e-04
[PASS] Schwarzschild ISCO u^t = sqrt(2)             u^t = 1.414213562373

25 passed, 0 failed
```

What these establish:

- The **critical impact parameters** match the analytic values for Schwarzschild (`3 sqrt 3`) and
  Kerr (prograde and retrograde). The prograde/retrograde match checks the sign of frame dragging.
- The **Novikov-Thorne energy identity** `int E f dr = 1 - E_isco` holds to 3e-6 for `a = 0` and `a = 0.9`.
  It checks the flux normalisation against conservation of energy.
- The **GPU path** (float32, the only precision Apple GPUs support) agrees with the double-precision
  CPU path. At `a = 0.9` the two renders differ by a mean of 0.014/255, with 29 of 256,000 pixels above
  2 levels and a maximum of 10 levels (`--eta 0.03`). The differences sit at the photon ring.

Not done: I did not compare against an external reference code or published images. The checks above
are analytic and conservation tests.

## Performance

Measured on an M4 Max (40-core GPU), GPU time per frame, lensing view, default `eta = 0.03`:

| size | GPU time |
|---|---|
| 1200 x 750 | 17.5 ms (about 57 fps) |
| 1600 x 1000 | 31 ms (about 32 fps) |

The window uses adaptive resolution: the render scale drops when a frame takes more than 22 ms and
recovers below 12 ms, and the frame is upscaled to the window. The spacetime sheet is a raster pass
(about 0.4 ms).

## Controls (window must be focused)

| key | action |
|---|---|
| `1` / `2` | lensing view / spacetime sheet |
| `[` / `]` | decrease / increase spin a (0 to 0.998) |
| `b` | cycle background: stars, sky grid, both |
| `d` | toggle the accretion disk |
| `+` / `-` | exposure |
| `p` | pause / resume the automatic camera orbit |
| `r` | reset the camera |
| drag / scroll / pinch | orbit / zoom |
| `q` | quit |

Command-line options. Both programs take `--spin`, `--pitch`, `--yaw`, `--dist`, `--bg stars|grid|both`,
`--nodisk`, `--exposure`, `--gain`, `--tpeak`, `--eta`, `--steps`, `--width` and `--height`.
`bin/bh_cpu` also takes `--out FILE` and `--fov`. `bin/blackhole` also takes `--view sheet`,
`--snapshot FILE` and `--frames N`.

## Notes and limitations

- **Metal rather than MPS or OpenGL.** Metal Performance Shaders is a library of pre-built kernels
  (matrix, convolution), not a ray tracer, so the tracer is written as a Metal compute kernel. OpenGL on
  macOS tops out at 4.1, which has no compute shaders, and it is deprecated.
- **"The halos."** I read this as the lensed photon-ring images: the higher-order images that wrap
  around the shadow. They come out of the ray tracing (see the Schwarzschild render).
- **Stationary disk.** The disk is static; there are no orbiting features and no time dependence.
  Making it time-dependent would need light-travel-time integration, which is not implemented.
- **Thin disk only.** Only the Novikov-Thorne disk is modelled. The plunging region emits nothing, as in
  the model. No gas, dust or radiative transfer through a corona.
- **Star background** is a procedural starfield, not a catalogue. It is not redshifted or given spectra.
- **Colours** use a display temperature (7500 K peak) and an sRGB blackbody fit. Brightness is physical
  up to the display gain and exposure. The physical T_max is in the title bar.
- **Sheet view** is the equatorial slice only. It is not a full 3D spacetime grid.
- **Photon-capture criterion.** Photons inside `r_capture` are declared captured without being tracked to
  the horizon. The argument for this is given above; the critical-impact checks confirm it at the
  shadow edge.
- **Step size.** Tests use `eta = 0.02`. The app defaults to `eta = 0.03`, which the critical-impact
  checks show is accurate to about 1e-7 (the shadow edge does not move at the image's resolution).
  Use `--eta 0.02` for the strictest runs.

## Files

```
src/kerr_core.h     metric, derivatives, Hamiltonian RK4, photon tracer (C++ and Metal)
src/shading.h       disk emission with redshift, blackbody colour, starfield, sky grid (C++ and Metal)
src/shared_types.h  structs shared byte-for-byte between the host and the shaders
src/disk_model.h    Novikov-Thorne disk: circular orbits, ISCO, flux table, T_max (host, double)
src/funnel_model.h  rubber-sheet embedding and mesh (host, double)
src/scene.h         camera, static-observer frame and per-frame uniforms
src/app_macos.mm    Cocoa window, Metal pipelines, input, snapshot mode
src/cpu_render.cpp  double-precision CPU reference renderer (same tracer)
src/png_out.cpp     PNG writer (ImageIO)
shaders/bh.metal    trace kernel, tone-mapping blit, sheet raster shaders
tests/test_physics.cpp  the 25 validation checks
renders/            example images
```
