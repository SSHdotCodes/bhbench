# Kerr Black Hole — 3D General Relativistic Raytracer & Spacetime Curvature Simulation

A real-time, scientifically accurate 3D Kerr & Schwarzschild black hole simulation written in C++20, Metal Compute, and Apple **Metal Performance Shaders (MPS)** (`MPSImageGaussianBlur`).

## Scientific & Physical Features

1. **Exact Kerr Null Geodesic Raytracing (`G = c = 1`, geometric units in `M`)**:
   - Initializes photon constants $(E, L_z, \mathcal{Q})$ in the exact **Locally Non-Rotating Frame (ZAMO)** of Bardeen, Press & Teukolsky (1972).
   - Integrates the second-order Mino-time Kerr geodesic equations using **RK4** in $u = 1/r$ and a singularity-free 3D unit-sphere representation $(\mathbf{q}, \dot{\mathbf{q}}, \phi_{\text{FD}})$ (zero polar coordinate singularities).
   - Captures exact gravitational lensing of background stars/Milky Way (Einstein rings, multiple images), secondary/tertiary photon-ring crossings, and the asymmetric D-shaped Kerr shadow at high spin $a/M \in [0, 0.998]$.

2. **Relativistic Accretion Disk & Halos**:
   - Inner edge at the exact analytic prograde **ISCO** ($r_{\text{ISCO}}(a)$), shrinking from $6M$ (Schwarzschild) down to $\approx 1.24M$ ($a = 0.998M$).
   - **Novikov–Thorne** radial temperature profile $T(r) \propto r^{-3/4}(1 - \sqrt{r_{\text{ISCO}}/r})^{1/4}$ with differentially sheared Keplerian spiral plasma turbulence.
   - Exact gravitational redshift & relativistic Doppler beaming factor $g = E_{\text{obs}} / [u^t(E - \Omega_K L_z)]$, shifting Planckian blackbody color ($T_{\text{obs}} = g T_{\text{em}}$) and boosting intensity ($\propto g^{3.4}$) on the approaching side.
   - Multi-pass **MPS (`MPSImageGaussianBlur`)** tight & wide HDR optical bloom halos + grazing photon-sphere corona.

3. **Spacetime Curvature ("Trapdoor in Spacetime") Grid**:
   - 3D mesh built from the exact **Kerr equatorial spatial embedding** ($dz/dr = \sqrt{g_{rr} - 1}$, reducing identically to **Flamm's Paraboloid** at $a = 0$) plus a causal trapdoor throat below $r_+$.
   - Visualizes Kerr frame-dragging twist near the ergosphere, glowing physical radius markers ($r_+$, $r_{\text{ISCO}}$, ergosphere), infalling geodesic wavefronts, and depth occlusion against the raytraced shadow and disk.

## Build & Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/black_hole_sim
```

## Controls

- **Mouse Drag**: Orbit 3D camera around the black hole
- **Scroll / Trackpad Pinch**: Zoom in/out ($3.4M \leftrightarrow 65M$)
- **`1`–`4`**: Camera presets (Inclined Gargantua, Edge-On Lensing, Polar Top-Down, Close Photon Skim)
- **`Space`**: Pause / Resume orbital & geodesic animation
- **`G`**: Toggle Spacetime Curvature ("Trapdoor") Grid
- **`D`**: Toggle Accretion Disk
- **`B`**: Toggle Background (Deep Space Milky Way vs Einstein Calibration Grid)
- **`H`**: Show / Hide HUD control sidebar
- **`R`**: Reset simulation defaults
- **`Esc`**: Quit
