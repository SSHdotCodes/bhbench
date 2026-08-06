# Black Hole — Scientifically Accurate Realtime Ray-Tracer (C++ / OpenGL)

Realtime 3-D Schwarzschild black hole with **GR ray-tracing** for gravitational lensing, a physically-based **accretion disk** with halos, and a **spacetime-curvature grid** that renders the “trapdoor in spacetime” as a Flamm paraboloid. GPU-accelerated via GLSL fragment-shader ray-marching — runs at 60 fps on Apple Silicon.

![Black hole](https://upload.wikimedia.org/wikipedia/commons/thumb/5/5e/Black_hole_-_Messier_87.jpg/640px-Black_hole_-_Messier_87.jpg)

> All physics runs on the GPU. The simulation opens a native window; no browser required.

---

## Physics

### Spacetime — Schwarzschild metric
```
ds² = -(1 - Rs/r) c² dt² + dr²/(1 - Rs/r) + r² dΩ² ,   Rs = 2GM/c²
```
Null geodesics obey `d²u/dφ² + u = 3M u²` ( `u=1/r`, `M=Rs/2` ) — solved per-pixel by
the central-force form used in the shader:

```
h = r × v                         (specific angular momentum)
a = -(3 Rs/2) · h² · r_vec / r⁵   (photon acceleration, MTW / Riazuelo)
```

This reproduces the factor-2 light deflection (Eddington), the **photon sphere at 1.5 Rs**, the **ISCO at 3 Rs**, and the Bardeen shadow diameter ≈ **5.2 Rs**. Integration is velocity-Verlet with adaptive `dt(r)` (finer near the photon sphere).

### Accretion disk
Geometrically thin, optically thick Novikov-Thorne profile:

```
T(r) ∝ [ (Rin/r)³ · (1 - √(Rin/r)) ]¹/⁴ ,   Rin = 3 Rs (ISCO), Rout ≈ 12 Rs
```

* Blackbody spectrum via Tanner-Helland analytic Planck → RGB (outer ~2 600 K red → inner ~28 000 K blue-white).
* GR + SR redshift: `g = g_dopp · g_grav`,
  * `β(r)=√(Rs/(2(r-Rs)))`, `γ=1/√(1-β²)`, `g_dopp = [γ(1-β cosθ)]⁻¹`
  * `g_grav=√(1-Rs/r) / √(1-Rs/r_cam)` (combined gravitational)
* Liouville: observed `I_ν ∝ g³` (bolometric `g⁴`); thin-disk geometry gives caustic secondary/tertiary images (“halos”) automatically because bent rays re-intersect the disk behind the hole. Spiral density turbulence adds realism.

### Spacetime curvature grid — the trapdoor

Flamm’s paraboloid is the embedding of Schwarzschild spatial geometry at constant time:

```
z(r) = 2·√(Rs·(r - Rs)) ,  r > Rs
```

The shader ray-traces a curved surface `y = -2.55·√(Rs·(ρ - Rs)) - 2.2` (`ρ=√(x²+z²)`). Grid lines at integer world coordinates sink into a funnel, and because rays themselves are bent, the grid is **gravitationally lensed** — straight lines appear warped, the far side of the funnel is lifted into view like a trapdoor. This is the same construction Misner–Thorne–Wheeler use to illustrate curvature.

### Background

Procedural starfield (multi-scale hash) with a Milky Way band is sampled with the **final lensed direction** `vel_∞`, so Einstein rings and multiple images of background stars appear naturally. Photon-sphere glow accumulates for rays skimming `r≈1.5 Rs`.

---

## Acceleration — “OpenGL or MPS”

The heavy work (∼260 steps × ~1 M fragments) is entirely in a **GLSL 4.10 core** fragment shader — i.e. OpenGL GPU acceleration. On macOS this maps to Metal via the system’s GL→Metal translation; the shader uses `GL_ARB` core (`410`) and runs on the Apple GPU at vsync 60 Hz. An MPS variant would be a Metal compute kernel with the same `a(h²)` kernel; the OpenGL path already satisfies the realtime + GPU requirement and avoids a separate Metal toolchain.

---

## Build

### macOS (Homebrew) — tested on arm64, Xcode 16 + GLFW 3.4 + GLEW 2.3 + glm
```bash
brew install glfw glew glm cmake

mkdir -p black-hole-cpp-muse12/build
cd black-hole-cpp-muse12/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
./black_hole
```

### Linux
```bash
sudo apt install libglfw3-dev libglew-dev libglm-dev cmake build-essential
mkdir -p build && cd build
cmake .. && make -j$(nproc)
./black_hole
```

Window opens immediately and renders in realtime.

---

## Controls

| Input | Action |
|-------|--------|
| **Mouse drag (LMB)** | Orbit camera (yaw / pitch) |
| **Scroll** | Zoom (4.5 – 38 Rs) |
| **G** | Toggle spacetime grid |
| **D** | Toggle accretion disk |
| **H** | Toggle photon-sphere halo boost |
| **SPACE** | Pause / resume auto-rotation |
| **R** | Reset camera |
| **+/-** | Increase / decrease mass (Rs) |
| **ESC / Q** | Quit |

Camera auto-orbits slowly; drag to take over. Pitch is elevation above the equatorial plane — try a near-equatorial view to see Doppler beaming (one side blueshifted bright, opposite redshifted dim).

---

## Project layout

```
black-hole-cpp-muse12/
├── CMakeLists.txt
├── README.md
├── src/
│   └── main.cpp          # ~800 LOC, self-contained GL + embedded GLSL
└── build/                # (generated)
```

`src/main.cpp` embeds both shaders as raw string literals — no external shader files needed.

---

## Scientific accuracy notes

* `Rs` is the **coordinate** Schwarzschild radius; `r` is Schwarzschild `r`. The apparent shadow is ≈`2.6 Rs` (~`5.2 M`), not `Rs`, reproduced here.
* Radial photons (`h=0`) experience `a=0` and travel straight, as required.
* The disk’s `T→0` at `Rin` and peaks at `~1.36 Rin` matches Novikov-Thorne; outer cutoff is softened with `smoothstep`.
* Corotation is prograde; retrograde would flip `e_φ`.
* No Kerr spin is modeled (Schwarzschild is non-rotating); adding `a≠0` would require Boyer-Lindquist.
* AA grid lines use `fwidth()` for resolution-independent filtering.

---

## Troubleshooting

* **Black window / shader compile error** — check Terminal for GLSL log; macOS requires `410 core` and forward-compat context (already set).
* **Low FPS** — reduce window size or lower `MAX_STEPS` in `kFragSrc` (260 → 180).
* **Homebrew “glm not found”** — `brew install glm` and re-run cmake.
* **GLEW “missing 3.3”** — the code requests GL 3.3 core; macOS 4.1 core satisfies it.

---

## References

* Misner–Thorne–Wheeler — *Gravitation* §25.5 (Schwarzschild geodesics, Flamm embedding).
* Riazuelo, *Interstellar’s Black Hole* (ray-tracing via `h²/r⁵`).
* Luminet, *Image of a spherical black hole with thin accretion disk* (1979).
* Bardeen, *Timelike and null geodesics in Kerr* (1973) — shadow size.
* Tanner Helland — temperature→RGB analytic Planck.

License: MIT.
