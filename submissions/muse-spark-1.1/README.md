# Black Hole - Highly Accurate 3D GR Simulation in C++ / OpenGL

Realtime scientifically accurate black hole visualization featuring:

### 1. Gravitational Lensing - Ray Tracing in Curved Spacetime
- **Physics**: Solves null geodesics in Schwarzschild metric (G=c=1). For each pixel, a backward ray is traced from camera to infinity.
- **Equation**: Photon orbital equation `d²u/dφ² + u = 3Mu²` where `u=1/r`, derived from conserved energy `E` and angular momentum `L`. Impact parameter `b = L/E`.
- **Integration**: RK4 integration in orbital plane (spherical symmetry allows reduction of 3D trajectory to 2D plane through BH center, defined by `n = ro × rd`).
- **Capture condition**: `r < rs` → event horizon (black).
- **Escape condition**: `r > 80 rs` → sample starfield with lensing-distorted direction.

### 2. Accretion Disk
- **Geometry**: Thin equatorial disk in XZ plane, `r_in = 3 rs` (ISCO at 6M = 3rs), `r_out = 12 rs`.
- **Temperature profile** (Shakura-Sunyaev): `T(r) ∝ r^{-3/4} * (1 - √(r_in/r))^{1/4}` with peak ~10⁶ K.
- **Velocity**: GR-corrected Keplerian `v = √(M/(r-2M))` tangential, Lorentz factor `γ`.
- **Radiative transfer**:
  - Gravitational redshift: `g_grav = √(1 - rs/r)`
  - Doppler: `g_dopp = 1/[γ(1 - v·n)]`
  - Combined `g = g_grav * g_dopp`, `T_obs = g * T_emit`, `I_obs = g³ I_emit` (Liouville)
  - Blackbody color via realistic 1000-40000K approximation + `T⁴` bolometric scaling
- **Lensing**: Disk behind BH appears as secondary images above shadow due to ray bending; photon ring emerges naturally from near-critical impact parameter `b_c ≈ 3√3 M ≈ 2.6 rs`.

### 3. Spacetime Curvature Grid
Two modes demonstrating the "trapdoor":
- **Mode 1 - Flat lensed grid**: Infinite plane at `y=-3 rs` with grid lines. Rays intersect plane along curved geodesics → grid appears warped, lines bend toward BH and vanish into shadow (demonstrates light bending = curvature).
- **Mode 2 - Flamm Paraboloid**: Embedding diagram of Schwarzschild spatial slice: `y = -2√(rs(r-rs))`. This is the exact visualization of spatial curvature (the funnel). Grid pattern in (r, φ) coordinates draped on surface; intersection via ray tracing shows how the surface plunges into BH.

### Science notes
- Event horizon at `rs = 2M`. Photon sphere at `1.5 rs = 3M` (unstable circular photon orbits).
- Shadow apparent radius: `b_crit = 3√3 M ≈ 2.6 rs` – independent of distance.
- Uses natural units `G=c=1`, `M = rs/2`.

### Build & Run (macOS)
```bash
cd black-hole-cpp-muse
mkdir build && cd build
cmake ..
make -j8
./blackhole
```

Dependencies: GLFW (via brew or FetchContent), OpenGL 4.1 (macOS), GLM (header-only).

On macOS, OpenGL headers are system framework; on Linux, GLAD loader is fetched automatically.

### Controls
- Mouse drag: orbit
- Scroll / W-S: zoom
- A-D / Q-E: yaw/pitch
- G: toggle grid (0=off, 1=flat lensed, 2=Flamm funnel)
- D: toggle disk
- Space: auto-orbit
- R: reset camera
- Up/Down: exposure
- O/P: BH mass ±
- ESC: exit

### Realtime performance
Fragment shader performs ~150-300 RK4 steps per pixel on GPU. At 1280×800, modern M1/M2 GPU achieves 30-60 fps. Adaptive stepping reduces cost far from BH.

### Extensions
- Kerr metrics (spin) would require Carter constants and more complex integration.
- Interstellar used similar ray tracing with DNGR.
