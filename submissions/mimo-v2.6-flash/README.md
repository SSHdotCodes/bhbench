# Black Hole Simulator (C++ / OpenGL)

A realtime, physically-grounded simulation of a **Schwarzschild black hole**, GPU ray-traced
in a fragment shader. It opens a window on your machine and renders at 100+ fps.

Three views:

| key | view | what you see |
|-----|------|--------------|
| `1` | **Gravitational lensing** | Every pixel is an integrated photon geodesic: the black-hole shadow, the photon ring, a Novikov–Thorne accretion disk with Doppler beaming and gravitational redshift, an optically thin disk halo, and a lensed star field / Milky Way. |
| `2` | **Spacetime curvature** | The "trapdoor in spacetime": the Flamm paraboloid — an *exact* isometric embedding of the Schwarzschild spatial slice — shaded by Gaussian curvature, with the horizon / photon-sphere / ISCO rings and two infalling test particles (proper time vs. Schwarzschild coordinate time). |
| `3` | **Geodesic gallery** | Exact null geodesics for a range of impact parameters against their flat-space references, showing deflection and capture at $b_c = 3\sqrt3\,M$. |

---

## Build & run

Requirements: CMake ≥ 3.20, a C++17 compiler, GLFW 3, OpenGL (4.1 core / 3.3 core),
zlib (optional, for PNG capture).

```bash
cd black-hole-cpp-mimolocalfast
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/blackhole            # opens the window
```

On macOS everything is pre-installed via Homebrew (`brew install cmake glfw`);
on Linux install `libglfw3-dev` and `zlib1g-dev`.

### Useful commands

```bash
./build/blackhole --selftest                 # physics validation suite, prints a report
./build/blackhole --headless --out shot.png  # render offscreen to a file, no window
./build/blackhole --mode 2                   # start in the spacetime-sheet view
./build/blackhole --mass 4.3e6 --mdot 1e-3   # Sgr A*-like parameters, in Msun and Msun/yr
./build/blackhole --physical                 # true physical colours (X-ray hot disk)
./build/blackhole --help                     # all options
```

## Controls

| input | action |
|-------|--------|
| `1` `2` `3` | switch view |
| mouse drag | orbit camera (yaw / pitch) |
| scroll wheel | zoom in / out |
| `←` `→` `↑` `↓` | yaw / pitch |
| `W` / `S` | move closer / farther |
| `[` / `]` | ray-trace resolution scale (quality vs. speed) |
| `;` / `'` | fewer / more RK4 steps per ray |
| `-` / `=` | exposure |
| `T` | toggle true physical colours ↔ display (film-response) colours |
| `H` `K` `N` | toggle halo / disk / star field |
| `B` | show the mirrored second exterior (Einstein–Ros bridge illustration), view 2 |
| `Space` | pause (disk turbulence / infall animation) |
| `R` | reset camera |
| `Esc` | quit |

The window title shows the live FPS, render resolution, ray scale and step budget.

---

## What is actually simulated

Everything below is derived in [docs/PHYSICS.md](docs/PHYSICS.md).

**Ray tracing / lensing.** Each pixel launches one photon from the camera and integrates the
*exact* Schwarzschild null-geodesic equation

$$\frac{d^2u}{d\phi^2} + u = 3Mu^2,\qquad u = 1/r$$

with adaptive RK4 in double-style-accurate float arithmetic, using the first integral
$1/b^2 = (du/d\phi)^2 + u^2(1-2Mu)$ for the impact parameter $b = L/E$. Rays that cross the
horizon return black (the shadow), rays that escape sample a lensed star field, rays that cross
the disk plane are resolved by bisection to sub-pixel accuracy. No approximations such as the
weak-field deflection formula are used anywhere in the image.

**Accretion disk.** A geometrically thin, optically thick **Novikov–Thorne** disk ($a=0$) with
its inner edge at the ISCO, $r_\mathrm{in}=6M$:

$$F(r)=\frac{3GM\dot M}{8\pi r^3}\Big(1-\sqrt{r_\mathrm{in}/r}\Big),\qquad
T_\mathrm{eff}(r)=\Big(\frac{F}{\sigma}\Big)^{1/4}$$

Emission from the moving gas is shifted and beamed by the invariant factor

$$g=\frac{\nu_\mathrm{obs}}{\nu_\mathrm{em}}=\frac{\sqrt{1-r_s/r}}{1-\Omega\,L_z/E},
\qquad \Omega=\sqrt{M/r^3},\qquad I_\mathrm{obs}=g^4 I_\mathrm{em}$$

which produces the gravitational redshift, the relativistic Doppler shift and the beaming
(the approaching side is several times brighter and bluer) automatically. Disk turbulence is
advected with the local Keplerian angular velocity, so the pattern shears differentially.

**Halo.** An optically thin, flared atmosphere around the disk, volume-integrated along the
curved ray with $g^4$-weighted emission and Beer–Lambert extinction.

**Colour.** Blackbody spectra are integrated on the CPU against the CIE 1931 colour-matching
functions and converted to linear sRGB (a 1024-entry LUT). Because a real thin disk around a
stellar-mass hole peaks in the X-rays, a *display response* maps the spectrum into the visible
band exactly like a camera exposure would — press `T` for the true physical colours.

**Spacetime sheet (view 2).** $z(r)=2\sqrt{r_s(r-r_s)}$ is an **isometric** embedding of the
$t=\mathrm{const}$ Schwarzschild slice ($1+z'^2 = 1/(1-r_s/r)$, verified in the self test), so
the grid compression you see is real spatial curvature, shaded by $K=-r_s/(2r^3)$. Two test
particles fall radially: one frozen in Schwarzschild coordinate time (never quite reaches
$r_s$), one in proper time (crosses in finite time) — the "frozen star" vs. free fall.

## Verification

```bash
./build/blackhole --selftest
```

13 checks, including: the weak-field deflection series
$\alpha = \tfrac{4M}{b}+\tfrac{15\pi}{4}(\tfrac{M}{b})^2+\tfrac{128}{3}(\tfrac{M}{b})^3$
to $10^{-5}$ relative, the capture threshold $b_c=3\sqrt3 M$ to $7\times10^{-8}$, the
photon-sphere fixed point, the redshift/Doppler identity, the Flamm isometry and its Gaussian
curvature, the Novikov–Thorne profile, the colour LUT against known blackbody sRGB values, and
the production integrator against a 100× finer reference step.

`tools/analyze_frame.py` measures a rendered frame (shadow radius vs. the analytic
$\sin\theta = b_c\sqrt{1-r_s/r_o}/r_o$, Doppler asymmetry, background structure);
`tools/ascii_preview.py` prints a frame as ASCII art.

## Performance

Fragment-shader ray tracing on the GPU (no denoising, one sample per pixel, HDR + bloom):

| resolution | ray scale | frame rate |
|-----------|----------|-----------|
| 2880×1800 | 1.0 | ~167 fps |
| 3456×2160 | 1.0 | ~265 fps |
| 2560×1440 | 0.75 | ~545 fps |

(measured on Apple M4 Max; with vsync on, the window runs at your display refresh rate.)

## Layout

```
CMakeLists.txt
src/        main.cpp (app, input, render passes)  physics.cpp/.hpp (geodesics, disk, colour)
            scene.cpp/.hpp (Flamm mesh, geodesics gallery)  shader.cpp/.hpp  selftest.cpp
            image_io.cpp/.hpp (PNG)  math3d.hpp
shaders/    raytrace.frag (the simulation)  grid.*  lines.*  fullscreen.vert
            brightpass.frag  blur.frag  composite.frag
docs/       PHYSICS.md (full derivations)
tools/      analyze_frame.py  ascii_preview.py
```

## Limitations

* **Schwarzschild only** (non-rotating). Kerr frame dragging, the ergosphere and a displaced
  shadow are not modelled.
* The camera is a *static* observer (held at fixed $r$, not in free fall).
* The disk is razor-thin at $z=0$ with the ISCO inner edge; no vertical structure, no
  self-irradiation, no magnetic/turbulent transport beyond the advected noise pattern.
* The halo uses a Keplerian emitter for its $g$-factor; inside $r\lesssim3M$ no circular
  orbits exist, so the halo density is truncated at the ISCO.
* Float32 integration with an adaptive step; near-critical rays that exhaust the step budget
  fall back to sampling the background along their current direction (this only affects a thin
  band around the critical curve).
