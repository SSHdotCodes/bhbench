# General Relativistic 3D Black Hole Simulation in C++ (OpenGL 4.1 GPU Accelerated)

A real-time, scientifically accurate 3D general-relativistic simulation of a **Schwarzschild & Kerr Black Hole** in C++17 and OpenGL 4.1, featuring:

1. **General Relativistic Null Geodesic Ray Tracing (Gravitational Lensing)**:
   - Integrates exact null geodesics ($ds^2 = 0$) in **3D Cartesian Kerr-Schild coordinates** $(t, x, y, z)$ using an adaptive **4th-order Runge-Kutta (RK4)** Hamiltonian solver on the GPU:
     $$g_{\mu\nu} = \eta_{\mu\nu} + f k_\mu k_\nu, \qquad g^{\mu\nu} = \eta^{\mu\nu} - f k^\mu k^\nu$$
     $$H(\mathbf{x}, \mathbf{p}) = \frac{1}{2}\left(|\mathbf{p}|^2 - 1 - f(\mathbf{k}\cdot\mathbf{p} + 1)^2\right) = 0, \qquad f = \frac{2 M r^3}{r^4 + a^2 y^2}$$
   - Captures exact strong-field relativistic phenomena:
     - **Event Horizon** $r_+ = M\left(1 + \sqrt{1 - a_*^2}\right)$
     - **Ergosphere / Static Limit** $r_{\text{ergo}}(\theta) = M\left(1 + \sqrt{1 - a_*^2 \cos^2\theta}\right)$
     - **Prograde & Retrograde Photon Spheres** $r_{\text{ph}}^\pm = 2M\left[1 + \cos\left(\frac{2}{3}\arccos(\mp |a_*|)\right)\right]$
     - **Asymmetric D-Shaped Kerr Shadow** ($b_{\text{crit}}^\pm$) and **Lense-Thirring Frame Dragging**
     - **Einstein Ring** lensing of background stars and galactic structure

2. **Relativistic Accretion Disk + Multi-Order Lensed Halos**:
   - **Bardeen-Press-Teukolsky (1972) Innermost Stable Circular Orbit** $r_{\text{ISCO}}(a_*)$
   - **Novikov-Thorne / Page-Thorne Relativistic Temperature Profile**:
     $$T(r) = T_0 \left(\frac{r_{\text{ISCO}}}{r}\right)^{3/4} \left(1 - \sqrt{\frac{r_{\text{ISCO}}}{r}}\right)^{1/4}$$
   - **Relativistic Keplerian Angular Velocity & Liouville $g^4$ Beaming**:
     $$\Omega_K(r) = \frac{\sqrt{M}}{r^{3/2} + a\sqrt{M}}, \qquad I_{\text{obs}} = g^4 I_{\text{emit}}, \qquad T_{\text{obs}} = g\,T_{\text{emit}}(r)$$
     combining relativistic Doppler shift and gravitational redshift into physical Planckian blackbody emission.
   - **Multi-Order Relativistic Halos**:
     - $n = 0$: Direct disk image + gravitationally lensed upper arch from the far side of the disk.
     - $n = 1$: Secondary lensed halo arch wrapping underneath the South pole of the black hole shadow.
     - $n \ge 2$: Exponentially demagnified photon sub-rings grazing the critical curve $b = b_{\text{crit}}$.

3. **Spacetime Curvature: The "Trapdoor in Spacetime" Grid**:
   - Isometric **Flamm's Paraboloid** embedding ($dz/dr = \sqrt{r_s / (r - r_+)} \implies z(r) = -2\sqrt{r_s(r - r_+)}$) plunging into a vertical relativistic throat at $r \le r_+$.
   - **Kerr Frame-Dragging Twist**: Radial coordinate spokes twist azimuthally around the spin axis as $a^*$ increases.
   - **Gullstrand-Painlevé River Model**: Animated infalling wave pulses accelerate along the grid lines into the trapdoor throat.
   - **Color-Coded Relativistic Landmark Rings**: Event Horizon ($r_+$, red), Ergosphere ($r_{\text{ergo}}$, gold), Photon Sphere ($r_{\text{ph}}$, cyan), and ISCO ($r_{\text{ISCO}}$, green), plus real-time **3D Null Geodesic Photon Beams** showing weak deflection, photon-sphere orbits, and capture into the trapdoor.

---

## Build & Run

```bash
make
./black_hole_sim
```

Or with CMake:
```bash
mkdir -p build && cd build
cmake ..
make -j
./black_hole_sim
```

## Interactive Controls

| Key / Input | Action |
|---|---|
| **Mouse Left-Drag** or **WASD / Arrows** | Orbit camera around the black hole |
| **Mouse Scroll** or **Z / X** | Zoom camera in / out ($4.2M$ to $55M$) |
| **Q / E** | Decrease / Increase Kerr spin parameter $a^* = a/M \in [-0.995, +0.995]$ |
| **T / G** | Increase / Decrease accretion disk peak temperature ($K$) |
| **R / F** | Increase / Decrease spacetime trapdoor funnel depth |
| **D** | Cycle relativistic Doppler beaming ($100\% \to 135\% \to 0\%$) |
| **1** | Toggle 3D Spacetime Curvature ("Trapdoor in Spacetime") grid |
| **2** | Toggle 3D Null Geodesic photon beam trajectories |
| **3** | Toggle gravitationally lensed in-shader spacetime grid |
| **4** | Toggle relativistic accretion disk |
| **5** | Toggle Blandford-Znajek relativistic polar magnetospheric jets |
| **6** | Preset: *Interstellar* Gargantua ($a^* = 0.88$) + Trapdoor |
| **7** | Preset: Deep "Trapdoor in Spacetime" Funnel & Geodesics |
| **8** | Preset: EHT M87* Polar Jet + Photon Ring ($a^* = 0.94$) |
| **9** | Preset: Exact Schwarzschild Non-Rotating ($a^* = 0.00$) |
| **0** | Preset: Extreme Kerr Thorne Limit ($a^* = 0.995$, D-shaped shadow) |
| **B** | Cycle render resolution scale ($75\% \to 100\% \to 55\%$) |
| **O** | Toggle smooth camera auto-orbit |
| **Space** | Pause / Resume time evolution |
| **H** | Toggle scientific telemetry HUD |
| **Esc** | Quit |
