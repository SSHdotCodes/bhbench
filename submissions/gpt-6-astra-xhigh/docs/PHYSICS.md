# Equations and assumptions

All lengths below are in `GM/c²` and times in `GM/c³`; the code sets `G = c = M = 1`. One real second advances coordinate time by 20 geometric time units at the 1× setting. This is a visualization clock; no particular physical black-hole mass is asserted.

## Null geodesics

With `f = 1 - 2/r`, the Schwarzschild metric is

```text
ds² = -f dt² + dr²/f + r²(dtheta² + sin²(theta) dphi²).
```

Spherical symmetry confines each photon to a plane through the origin. In that plane, using `u = 1/r`, `q = du/dphi`, and conserved impact parameter `b = |L/E|`:

```text
du/dphi = q
dq/dphi = -u + 3u²
q² + u² - 2u³ = 1/b²
d(lookback time)/dphi = 1 / [b u²(1-2u)]
```

The two spatial equations follow by differentiating the conserved null energy relation. Unlike using a Newtonian attraction on a light ray, these equations include the full Schwarzschild null-orbit dynamics. Standard metric and geodesic background: [Carroll, Lecture Notes on General Relativity](https://arxiv.org/abs/gr-qc/9712019).

The GPU evolves `(u,q,t)` using RK4. The angular cap is 0.045, 0.025, or 0.012 radians for Fast, Balanced, or Fine. A second cap, `h <= 0.12 u/|q|`, bounds the fractional radial change. Turning points do not require a sign-flipping branch: `q` evolves continuously through zero.

The path is reconstructed as

```text
x(phi) = [e0 cos(phi) + e1 sin(phi)] / u(phi),
```

where `e0` points from the hole to the observer, and `e1` is the initial tangential ray direction. The renderer finds equatorial-plane crossings analytically in `phi` and takes an RK4 substep to that angle, rather than interpolating a coarse straight chord through the disk.

Rays terminate at `r ≈ 2.0002M`, escape beyond `2000M`, or exhaust 1,800 integration steps. Escaping directions receive a flat-space asymptotic correction `atan(u / -q)`; the remaining far-field GR correction is small but nonzero. Unresolved rays after the step budget contribute only emission accumulated so far. Extremely fine critical structure is limited by pixel sampling, the step budget, and 32-bit GPU arithmetic. There is no claim of resolving infinitely many photon subrings.

The unstable circular orbit solves `-u+3u² = 0`, giving `r = 3M`. Substitution into the null invariant gives `b_critical = sqrt(27)M`. The critical curve is a capture boundary for illumination from infinity; the visible dark area with an emitting disk need not equal that entire boundary. Lensed images and the terminology of photon/lensing rings are discussed by [Gralla, Holz & Wald (2019)](https://arxiv.org/abs/1906.00873).

## Camera

A static observer is held at radius `r_o`, outside the disk. Screen directions are expressed in the observer's local orthonormal frame. If `mu` is the cosine relative to the outward radial basis and `sin(alpha)` the tangential magnitude:

```text
b = r_o sin(alpha) / sqrt(1 - 2/r_o)
u0 = 1/r_o
q0 = -mu/b.
```

The vertical field of view is 35°. Its tangent is supplied as a precomputed high-accuracy constant to avoid platform-dependent constant trigonometric approximations. A 1,000-pixel GPU readback test independently checks this camera mapping and the capture mask. Inclination is measured from the disk's normal. Orbiting the camera changes its position, but each frame depicts a static observer; observer-motion aberration is not included.

## Disk flux and frequency transfer

The disk is opaque, two-sided, infinitesimally thin, and lies at `y = 0`, with emission at `6M <= r <= 28M`. The first emitting intersection terminates the ray; crossings through the central hole or beyond the disk continue. The outer edge tapers between 25M and 28M.

For the Schwarzschild zero-torque disk, setting `x = sqrt(r)`, `x0 = sqrt(6)`, and `a = sqrt(3)`:

```text
J(r) = x - x0 - (a/2) ln{ [(x-a)/(x+a)] / [(x0-a)/(x0+a)] }
F(r) proportional to (3/2) J(r) / [r^(5/2) (r-3)].
```

This follows from the stationary thin-disk energy/angular-momentum balance with zero torque at the ISCO. The flux maximum is approximately `0.00017188421` in the chosen omitted-prefactor convention, at `r ≈ 9.551M`. The emission is normalized by that maximum; it does not prescribe a mass accretion rate. Source: [Page & Thorne (1974), Disk-Accretion onto a Black Hole I](https://articles.adsabs.harvard.edu/pdf/1974ApJ...191..499P).

Circular emitters have `Omega = r^(-3/2)` and `u_emitter^t = 1/sqrt(1-3/r)`. Define `lambda` as the signed axial angular momentum divided by energy of the **future-directed** photon:

```text
g = nu_observer / nu_emitter
  = sqrt(1-3/r) / [sqrt(1-2/r_o) (1-Omega lambda)].
```

The backward-traced spatial ray has the opposite signed axial impact parameter, which is why the shader denominator uses `1 + Omega*bPhi`. Disk azimuth is `atan(z,x)`, with the corresponding axial convention explicitly accounted for. Surface specific intensity obeys invariant `I_nu/nu³`; bolometric intensity therefore receives `g^4`.

The texture is a bounded procedural emissivity modulation advected at the local circular `Omega`, evaluated at `t_observer - lookback time`. It represents illustrative inhomogeneity, not evolved turbulence. Mean radial velocities and the plunging flow are neglected.

Thermal mode applies an amber false-color temperature palette to bolometric emission. Spectral mode computes samples of `B_nu(g T)` at 610, 550, and 460 nm with `T = 82000 K * (normalized local flux)^0.25`; the identity `g³ B_(nu/g)(T) = B_nu(gT)` implements specific-intensity transfer. Detector response, absolute radiometric units, and atmospheric/interstellar extinction are not modeled. An exposure multiplier and ACES-style display curve compress the radiance for an ordinary monitor.

## Halo

The optional corona is a prescribed static emissivity distribution, tapered away below 2.7M, centered roughly around 5M, and truncated at 16M. Its bolometric transfer is accumulated as

```text
dI_observer = g_static^4 * j(r,y) * dl_static
g_static = sqrt[(1-2/r)/(1-2/r_o)]
dl_static = r² dphi / [b sqrt(1-2/r)].
```

The integration uses midpoint spatial samples. Absorption and scattering are neglected; disk opacity still terminates the ray. A static corona needs physical support that this model does not solve. Foreground coronal emission can appear over a captured direction; the horizon itself emits no light. Bloom is a separate blurred bright-pass image, not an additional gravitational effect.

## Exterior spatial grid

At constant Schwarzschild `t` and on the equatorial plane:

```text
dl² = dr²/(1-2/r) + r² dphi².
```

A surface of revolution embedded in Euclidean three-space has radial metric coefficient `1 + (dz/dr)²`. Matching coefficients gives

```text
dz/dr = sqrt[2/(r-2)]
z(r) = 2 sqrt[2(r-2)] + constant.
```

The renderer uses the exact scale in all three displayed coordinates, shifting the height so the outer rim at 30M is zero. It stops at the 2M horizon boundary. The embedding does not extend the Schwarzschild constant-time spatial slice into the interior and does not establish the causal one-way nature of the horizon by itself. Null rays shown on it are coordinate-mapped **spacetime** trajectories, not geodesics of this embedded spatial surface. Their animated markers move by sample index for clarity.

## Validation limits

The double-precision reference and GPU solver are separate C++ and GLSL implementations of the same ODE. Agreement between them alone is insufficient, so the suite also uses analytic invariants, the capture threshold, weak-field bending, the embedded metric, and step convergence. Numerical readback is performed at all quality levels. The camera has an additional analytic end-to-end check.

Captured-ray endpoint angles depend on the discrete horizon crossing and are reported for diagnostics, not used as scattering angles. Escape-angle comparisons, invariant errors, and capture classifications determine the GPU pass criteria. A smaller step does not guarantee smaller error at every impact parameter because floating-point accumulation eventually competes with truncation error.
