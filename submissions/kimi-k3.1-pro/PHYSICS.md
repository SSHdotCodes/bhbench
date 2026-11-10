# Physical and numerical model

## 1. Schwarzschild geometry

The simulation uses geometrized units `G = c = M = 1` and the exterior metric

```text
ds^2 = -(1 - 2/r) dt^2 + (1 - 2/r)^(-1) dr^2
       + r^2 (dtheta^2 + sin^2(theta) dphi^2).
```

The black hole is non-spinning and electrically neutral. Relevant radii are:

| Feature | Schwarzschild radius coordinate |
|---|---:|
| Event horizon | `r = 2` |
| Photon sphere | `r = 3` |
| Critical impact parameter | `b_c = 3 sqrt(3)` |
| Marginally stable circular orbit (ISCO) | `r = 6` |

These are coordinate radii in units of the black-hole mass, not Euclidean distances measured by a local ruler.

## 2. Null-geodesic ray tracing

Spherical symmetry confines each null geodesic to a plane. For `u = 1/r` and azimuth `phi` in that plane, the orbit equation is

```text
d^2u/dphi^2 + u = 3u^2.
```

The fragment shader integrates this equation backward from the observer with fourth-order Runge–Kutta. No weak-field bending approximation is used in the renderer.

For a static observer at `r_o`, define `f_o = 1 - 2/r_o`. Let the initial unit direction in the observer's orthonormal frame have radial component `n_r` and transverse magnitude `n_t`. The conserved impact parameter and initial conditions are

```text
b     = r_o n_t / sqrt(f_o)
u_o   = 1/r_o
u'_o  = -n_r / b.
```

The last expression follows from `p^r/E = n_r` in the static orthonormal frame. It also satisfies the null conserved-energy invariant

```text
(u')^2 + u^2 (1 - 2u) = 1/b^2.
```

The local direction used for sky sampling and disk redshift is reconstructed from

```text
n_backward ∝ [-u' / (u sqrt(f))] e_r + e_phi.
```

Rays reaching `u >= 1/2` are captured. Escaped rays are evaluated against a procedural celestial sphere at infinity. Integration is limited to `8 pi` radians and 480 RK4 steps per ray; this bounds the cost of arbitrarily long paths asymptotically approaching the photon sphere. Step size is reduced in the strong-field region.

## 3. Accretion disk

The disk lies in the equatorial plane and extends from `r = 6` to `r = 16`. Every sign change of the orbital plane's height relative to the disk plane is interpolated to a disk crossing. Multiple crossings along one ray generate higher-order and halo images without adding an artificial ring texture.

Matter follows circular Schwarzschild geodesics with

```text
Omega = r^(-3/2)
v     = r Omega / sqrt(f) = 1/sqrt(r - 2)
gamma = 1/sqrt(1 - v^2) = sqrt((r - 2)/(r - 3)),
```

where `v` is measured in the local static orthonormal frame. At the ISCO, `v = 1/2` and `gamma = 2/sqrt(3)`.

If `n` is the physical photon direction from the emitter toward the observer, the total frequency shift is

```text
g = nu_observed / nu_emitted
  = sqrt(f_e / f_o) / [gamma (1 - v e_velocity dot n)].
```

This includes both gravitational and special-relativistic Doppler shifts. The backward ray's spatial direction is reversed before evaluating `n`.

The emitted radial flux uses the zero-torque thin-disk factor

```text
F(r) ∝ (r_in/r)^3 [1 - sqrt(r_in/r)].
```

Its maximum occurs at `r = (49/36) r_in`, approximately `8.167M` for `r_in = 6M`. Display temperature is proportional to `F^(1/4)`, normalized with an illustrative `T_max = 7000 K`.

For blackbody emission, invariance of `I_nu/nu^3` gives `I_nu,obs = g^3 I_nu,em(nu/g)`. Integrated bolometric intensity therefore transforms as `g^4`, while observed temperature transforms as `T_obs = g T_em`. The renderer applies both. A compact three-primary Planck sample supplies display hue; it is not a full CIE spectral integration.

The sheet has fixed display opacity `0.88`, allowing higher-order images to remain visible. This is a radiative-visualization approximation, not a solution of the full disk radiative-transfer equation.

## 4. Spacetime-grid visualization

The grid view embeds the `t = constant`, equatorial (`theta = pi/2`) spatial metric

```text
dl^2 = (1 - 2/r)^(-1) dr^2 + r^2 dphi^2
```

into three-dimensional Euclidean space. The embedding height is

```text
z(r) = 2 sqrt(2 (r - 2)),       (M = 1)
```

because `1 + (dz/dr)^2 = (1 - 2/r)^(-1)`. The mesh is translated vertically only to place its outer rim at a convenient display height.

This is **Flamm's spatial embedding**, not a Newtonian potential well and not an embedding of four-dimensional spacetime. The radial and azimuthal grid lines illustrate how the curved spatial geometry differs from a flat plane.

## 5. Validation

`tests/physics_tests.cpp` checks:

- the stationary photon-sphere solution;
- the critical impact parameter;
- ISCO speed and Lorentz factor;
- static gravitational redshift;
- the disk-flux maximum;
- the null conserved-energy invariant;
- capture for `b < b_c` and escape for `b > b_c`;
- light deflection against the weak-field expansion

```text
Delta phi = 4/b + 15 pi/(4 b^2) + 128/(3 b^3) + O(b^-4).
```

The deflection test uses effectively infinite endpoints and interpolates the final endpoint crossing to avoid finite-distance and step-quantization biases.

## 6. Deliberate limits

- Schwarzschild only: no spin, frame dragging, or Kerr ISCO structure.
- Stationary disk: no turbulence, MHD, vertical structure, or time-dependent retarded emission.
- Finite ray count per pixel and finite winding angle, required for realtime rendering.
- Procedural rather than cataloged background stars.
- Illustrative display temperature, opacity, exposure, and tone mapping.
- No claim of full astrophysical radiative transfer or GRMHD accuracy.
