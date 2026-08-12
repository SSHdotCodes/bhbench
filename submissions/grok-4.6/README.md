# Schwarzschild black hole — realtime geodesic ray tracer

A from-scratch C++ / OpenGL 4.1 simulation of a non-rotating black hole.
Every light path is a **null geodesic of the Schwarzschild metric**, integrated
on the GPU. The window that opens is a live camera, not a pre-rendered clip.

Geometric units: \(G = c = 1\). Mass \(M = 1\), so the numbers on screen are
in units of \(M\).

| Surface | Radius |
|---|---|
| Event horizon | \(r_s = 2M\) |
| Photon sphere | \(r = 3M\) |
| Innermost stable circular orbit (ISCO) | \(r = 6M\) |
| Critical impact parameter | \(b_{\rm crit} = 3\sqrt{3}\,M \approx 5.196\,M\) |

## What you are looking at

**Gravitational lensing.** Each pixel traces a Hamiltonian null geodesic
backward from a static observer

\[
H = \tfrac12\Big[-\frac{E^2}{\alpha} + \alpha\,p_r^2 + \frac{p_\theta^2}{r^2}
+ \frac{L_z^2}{r^2\sin^2\theta}\Big],\qquad \alpha = 1 - \frac{2M}{r}.
\]

Photons with \(b < b_{\rm crit}\) fall in. Photons that skim \(b \approx b_{\rm crit}\)
wind near the photon sphere and produce the bright photon ring and higher-order
images of the far side of the disk. Stars behind the hole form an Einstein ring.

**Accretion disk.** A Novikov–Thorne thin disk in the equatorial plane:
optically thick, inner edge at the ISCO, Keplerian four-velocity
\(\Omega = \sqrt{M/r^3}\), \(u^t = 1/\sqrt{1-3M/r}\). The fluid-frame flux is
\(F \propto r^{-3}(1-\sqrt{r_{\rm ISCO}/r})\). Specific intensity is transferred
with the invariant \(I_\nu/\nu^3\), i.e. a gravitational + Doppler factor
\(g^3\). That is why the approaching side is bright and blue and the receding
side is dim and red. The halo is an optically thin corona stacked on the same
geodesics, plus HDR bloom on the photon ring.

**Spacetime curvature.** The equatorial spatial slice of Schwarzschild embeds
in Euclidean 3-space as Flamm's paraboloid \(z^2 = 4 r_s(r-r_s)\). The cyan
wireframe is that embedding, dropped so distant space sits at \(y=0\) and the
horizon is the bottom of the throat — the textbook “trapdoor in spacetime”.
Gold ring: photon sphere. Green ring: ISCO. Red ring: horizon. Orange curves
on the fabric are sample null geodesics (captured, critical, and scattered).

This is Schwarzschild, not Kerr: no frame dragging, no spin-dependent ISCO.
The disk still orbits. A spinning hole would need the Kerr geodesic equations.

## Build and run

Needs a C++17 compiler, CMake, GLFW 3, GLM, and OpenGL 4.1 (macOS ships this).

```bash
cd black-hole-cpp-grok46
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/black_hole
```

A native window opens on the machine that runs the binary.

## Controls

| Input | Action |
|---|---|
| Left-drag | Orbit |
| Right-drag | Pan |
| Scroll / Z X | Zoom |
| W S F, arrows, J L, Q E | Move target |
| G D H T B | Toggle grid, disk, halo, stars, bloom |
| Space | Pause disk rotation |
| R | Reset camera |
| 1 2 3 4 | Quality (faster → sharper) |
| - = | Exposure |
| Esc | Quit |

Default quality (key `2`) is meant to hold realtime on an Apple Silicon GPU.
The window is 1280×800. On a Retina Mac the compositor reports a 2× drawable;
the sim sizes its viewport from the window so the image is not cropped.

## Physics references

- Misner, Thorne & Wheeler, *Gravitation* — Schwarzschild geodesics, Flamm embedding
- Chandrasekhar, *The Mathematical Theory of Black Holes* — photon sphere, \(b_{\rm crit}\)
- Novikov & Thorne (1973) — thin accretion disks
- Luminet (1979), A&A 75, 228 — first theoretical image of a Schwarzschild disk
- Poisson & Will, *Gravity* — Hamiltonian formulation used in the integrator
