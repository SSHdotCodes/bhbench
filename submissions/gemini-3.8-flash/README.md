# Real-Time Scientifically Accurate Black Hole Simulator (C++ / Metal)

This repository contains an autonomous, scientifically grounded C++ implementation of a spinning **Kerr Black Hole** simulated in real-time via hardware-accelerated **Apple Metal Compute Shaders** and **GLFW**.

---

## 1. Physics & Mathematical Formulation

### A. General Relativity Geodesic Integration (Ray-Tracing & Gravitational Lensing)
The trajectory of light rays (null geodesics) backwards from the camera into the black hole is governed by Einstein's field equations.
- **Metric**: Kerr metric in Boyer-Lindquist / Cartesian representation with dimensionless spin parameter $a \in [0, 0.998]$.
- **Null Geodesics**: Light deflection is computed using an adaptive Runge-Kutta 4th Order (RK4) integrator over the relativistic light equation:
  $$\frac{d^2 \mathbf{x}}{d\lambda^2} = -\frac{3 M |\mathbf{L}|^2}{r^5} \mathbf{x} + \mathbf{v} \times \mathbf{B}_{\text{gm}}$$
  where $\mathbf{L} = \mathbf{x} \times \mathbf{v}$ is the photon angular momentum vector, and $\mathbf{B}_{\text{gm}} = \frac{2M}{r^3}[3(\mathbf{J}\cdot\hat{\mathbf{n}})\hat{\mathbf{n}} - \mathbf{J}]$ describes the **Lense-Thirring frame-dragging** (gravitomagnetism) caused by the spinning black hole singularity.
- **Event Horizon & Shadow**: Photons falling inside the outer Kerr event horizon:
  $$r_+ = M + \sqrt{M^2 - a^2}$$
  are irreversibly captured, rendering the central black hole shadow.
- **Innermost Stable Circular Orbit (ISCO)**: The accretion disk inner boundary dynamically snaps to the prograde ISCO radius:
  $$Z_1 = 1 + (1 - a^2/M^2)^{1/3} \left[(1+a/M)^{1/3} + (1-a/M)^{1/3}\right]$$
  $$Z_2 = \sqrt{3 a^2/M^2 + Z_1^2}$$
  $$r_{\text{ISCO}} = M \left(3 + Z_2 - \sqrt{(3-Z_1)(3+Z_1+2Z_2)}\right)$$

### B. Accretion Disk Physics, Relativistic Beaming & Halos
- **Page-Thorne / Novikov-Thorne Temperature Profile**:
  $$T_{\text{eff}}(r) \propto \left[\frac{M}{r^3} \left(1 - \sqrt{\frac{r_{\text{ISCO}}}{r}}\right)\right]^{1/4}$$
- **Relativistic Doppler & Gravitational Redshift**: Keplerian orbital velocities $v_\phi = \Omega r$ reach significant fractions of $c$. The observed frequency shift factor $g$ combines special relativistic Doppler shifting with general relativistic gravitational time dilation:
  $$g = \frac{\sqrt{1 - 2M/r}}{\gamma (1 - \mathbf{v}\cdot\mathbf{n})}$$
- **Relativistic Beaming (Doppler Boosting)**: Radiation intensity from the approaching disk side is strongly boosted by $I_{\text{obs}} = g^{3.5} I_{\text{emit}}$, while the receding side is dimmed and reddened.
- **Gravitational Halos & Secondary Images**: Photon rays bending over and under the black hole create the iconic Einstein rings and secondary images of the rear accretion disk.
- **Blackbody Color**: Thermal emission is converted from temperature to RGB using Planckian blackbody spectrum approximations.

### C. Spacetime Curvature & "Trapdoor in Spacetime" (Flamm's Paraboloid)
- Visualizes spatial geometry embedding according to Flamm's paraboloid:
  $$z(r) = - 2.5 \sqrt{2M(r - 2M)}$$
- An embedded polar coordinate spacetime grid renders the funnel-shaped gravity well ("trapdoor") plunging into the horizon.

---

## 2. Requirements & Building

- **Platform**: macOS (Apple Silicon M-series or Intel with Metal support)
- **Compiler**: Clang++ with C++17 support
- **Build System**: CMake 3.20+
- **Libraries**: GLFW 3 (`brew install glfw`)

### Building:
```bash
cmake -B build -S .
cmake --build build
```

---

## 3. Running the Simulation

Run the compiled executable:
```bash
./build/black_hole_sim
```
This opens a native window running the real-time simulation at high resolution and 60+ FPS.

### Interactive Controls:
- **Left Mouse Click + Drag**: Orbit / rotate 3D camera around the black hole
- **Right Mouse Click + Drag**: Pan camera
- **Scroll Wheel**: Smooth zoom in / out
- **Up / Down Arrow**: Increase / decrease Kerr black hole spin parameter $a$
- **Left / Right Arrow**: Adjust outer radius of accretion disk
- **+ / -**: Adjust vertical offset of spacetime curvature funnel
- **L**: Toggle Gravitational Lensing (Null Geodesic Curvature vs. Flat Space)
- **G**: Toggle Spacetime Curvature Grid ("Trapdoor" Flamm Paraboloid)
- **D**: Toggle Accretion Disk
- **C**: Cycle Accretion Disk Spectrum Mode:
  1. *Realistic Blackbody + Relativistic Doppler/Gravitational Shifts*
  2. *False-Color Temperature Map*
  3. *Relativistic $g$-factor Map (approaching blue-shift vs receding red-shift)*
- **Spacebar**: Toggle automatic camera orbit
- **H**: Toggle HUD display
- **R**: Reset camera and physical parameters
- **ESC**: Exit
