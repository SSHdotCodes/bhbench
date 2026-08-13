# General Relativistic Kerr Black Hole Real-Time Simulation (C++ / Metal)

A scientifically accurate, real-time 3D General Relativistic Kerr Black Hole simulation implemented from first principles in C++ and Apple Metal.

## Core Scientific & Astrophysical Features

1. **General Relativistic Ray-Tracing (Null Geodesics in Kerr Spacetime):**
   - Implements 4th-order Runge-Kutta (RK4) integration with adaptive step-size scaling.
   - Accurately captures gravitational light bending, photon sphere ($r = 3M$ for Schwarzschild), frame dragging (Lense-Thirring effect) parameterized by dimensionless Kerr spin parameter $a/M$, and the shadow of the outer event horizon ($r_+ = M + \sqrt{M^2 - a^2}$).

2. **Accretion Disk & Relativistic Kinematics:**
   - Keplerian orbital dynamics with angular velocity $\Omega_K = \frac{\sqrt{M}}{r^{3/2} + a\sqrt{M}}$.
   - Relativistic Doppler boosting and beaming ($I_{obs} = g^4 I_{emit}$).
   - Gravitational and kinematic redshift invariant: $g = \frac{\sqrt{1 - 2M/r}}{\gamma (1 - \mathbf{v}\cdot\mathbf{n})}$.
   - Multi-temperature blackbody color emission via Page-Thorne / Novikov-Thorne radial temperature profile: $T(r) \propto (M/r^3)^{1/4} [1 - \sqrt{r_{in}/r}]^{1/4}$.
   - Turbulent MHD plasma spirals advected dynamically via differential Keplerian shear.

3. **3D Spacetime Curvature & Trapdoor Funnel:**
   - Real-time visualization of Flamm's Paraboloid embedding $z(r) = -2\sqrt{2M(r - 2M)}$ illustrating the visual "trapdoor in spacetime".
   - Warped equatorial proper-distance metric grid demonstrating radial coordinate compression near the horizon ($dr / \sqrt{1 - 2M/r}$).

4. **Corona / Halos & Celestial Lensing:**
   - Comptonized X-ray / UV spherical corona halo glow with power-law radial density falloff.
   - High-fidelity Milky Way celestial skybox and starfield gravitationally magnified and distorted into Einstein rings.
   - Switchable coordinate sphere lensing grid for direct geometric deflection analysis.

5. **Diagnostic Visualizations & Scientific Modes:**
   - **Mode 0:** Full Relativistic Ray-Traced Photorealistic Rendering.
   - **Mode 1:** Spacetime Curvature & Geodesic Deflection Heatmap.
   - **Mode 2:** Relativistic Doppler Redshift / Blueshift Map (g-factor).

---

## Build & Run Instructions

### Prerequisites
- macOS on Apple Silicon (M1/M2/M3/M4) or Intel with Metal support
- Xcode Command Line Tools (`clang++`, `xcrun metal`)
- CMake 3.15+

### Building

```bash
cd black-hole-cpp-gemini37flash
mkdir -p build && cd build
cmake ..
make -j8
```

### Running

To launch the real-time interactive simulation window:
```bash
./BlackHoleSim
```

### Interactive Controls
- **Left-Click + Drag:** Orbit camera around the black hole (azimuth and elevation).
- **Right-Click + Drag:** Pan camera target.
- **Scroll Wheel / Trackpad Pinch:** Zoom in / out (radial distance from black hole).
- **ImGui Control Panel:**
  - Adjust black hole spin ($a/M$), event horizon, and ISCO.
  - Tweak accretion disk inner/outer radii, scale height, opacity, and Doppler beaming.
  - Toggle and customize the 3D Spacetime Trapdoor Grid (Flamm paraboloid).
  - Switch between Photorealistic, Deflection Heatmap, and Doppler Shift visualization modes.
  - Enable/disable auto-orbit and adjust HDR exposure and RK4 integration steps.
