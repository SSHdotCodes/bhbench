# Physics and numerics

All lengths are in geometric units with

$$M \equiv \frac{GM_\bullet}{c^2} = 1,\qquad r_s = 2M,\qquad
r_\mathrm{ph} = 3M,\qquad r_\mathrm{ISCO} = 6M,\qquad b_c = 3\sqrt3\,M .$$

The metric (signature $-+++$, $\theta=\pi/2$ for the photon plane):

$$ds^2 = -\Big(1-\frac{r_s}{r}\Big)c^2dt^2 + \frac{dr^2}{1-r_s/r} + r^2 d\phi^2 .$$

---

## 1. Null geodesics — what the ray tracer integrates

For a photon in the orbital plane, with $u = 1/r$ and conserved $E=-p_t$, $L=p_\phi$,
$b \equiv L/E$:

$$\Big(\frac{du}{d\phi}\Big)^2 = \frac{1}{b^2} - u^2\Big(1 - \frac{2M}{r}\cdot r\Big)
    = \frac{1}{b^2} - u^2 + 2Mu^3 .$$

Differentiating once gives the **orbit equation** actually integrated in `raytrace.frag`:

$$\boxed{\ \frac{d^2u}{d\phi^2} + u = 3Mu^2\ }$$

which is exact — no weak-field expansion, no thin-lens approximation. Initial conditions come
from the camera ray direction $\hat d$ at radius $r_0$:

$$u_0 = \frac{1}{r_0},\qquad
\frac{du}{d\phi}\Big|_0 = -\,u_0\,\frac{\hat d\cdot\hat r}{|\hat d - (\hat d\cdot\hat r)\hat r|},
\qquad
\frac{1}{b^2} = \Big(\frac{du}{d\phi}\Big)^2 + u_0^2 (1-2Mu_0).$$

The plane basis is $\hat e_1 = \hat r$, $\hat e_2$ = normalised transverse velocity; the
photon's angular-momentum direction is $\hat e_3 = \hat e_1\times\hat e_2$, so

$$\frac{L_z}{E} = b\,(\hat e_3\cdot\hat z).$$

**Integrator.** Classic RK4 with the adaptive step

$$h(u) = \frac{h_\mathrm{max}}{1 + 6u + 25u^2},\qquad h_\mathrm{max}=0.2 .$$

The dominant truncation error is the RK4 phase error $\sim h^5/120$, which is scale-invariant
in the far field (where $u\to0$ but the polar angle still advances), so $h_\mathrm{max}$ is
capped; the $u$-dependent terms tighten the step near the photon sphere where orbits wind.
For outgoing rays the step is additionally clamped so $u$ never steps through zero
($r\to\infty$ is an escape, not a singularity), and the exit state is interpolated back to
exactly $r=r_\mathrm{far}$ so the entry/exit are symmetrical.

**Termination.** captured ($u \ge 1/r_s$ → black), escaped ($u\le 1/r_\mathrm{far}$ and
outgoing → background), disk hit (plane crossing, bisected 8× on the true trajectory), or
step budget exhausted (background sampled along the current direction).

### Validation (from `--selftest`)

| check | result |
|-------|--------|
| $\alpha$ vs. $\frac{4M}{b}+\frac{15\pi}{4}\frac{M^2}{b^2}+\frac{128}{3}\frac{M^3}{b^3}$, $b=50\ldots1000M$ | rel. error $10^{-5}$ … $3\times10^{-4}$ |
| capture threshold vs. $3\sqrt3 M$ | $|\Delta b_c| = 7\times10^{-8}$ |
| circular photon orbit $u=1/3$ after $\phi=6$ | $\Delta u = 0$ (to double-rounding) |
| first integral $(du/d\phi)^2+u^2-2Mu^3 = 1/b^2$ conserved along a trace | drift $<10^{-13}$ |
| production step vs. 100× finer reference (renderer geometry) | max $|\Delta\alpha| = 7\times10^{-4}$ rad |
| independent elliptic quadrature of $2\!\int du/\sqrt{V}$ vs. RK4 | agree to $10^{-6}$ |
| direct asymptotic-direction angle vs. $\Delta\phi-\pi$ | agree to $10^{-9}$ |

The shadow radius in a rendered frame is also measured by
`tools/analyze_frame.py` against the analytic static-observer result
$\sin\theta = \dfrac{b_c\sqrt{1-r_s/r_o}}{r_o}$ (agreement ≈1 %, limited by the
halo glow in front of the hole).

---

## 2. Accretion disk (Novikov–Thorne, $a=0$)

Steady, geometrically thin, optically thick disk; energy released by viscous torques;
zero-torque inner boundary at the ISCO. The emergent flux (Page 1974, exactly the
$a\to0$ limit of Page–Thorne):

$$F(r) = \frac{3GM_\bullet\dot M}{8\pi r^3}\Big(1-\sqrt{\frac{r_\mathrm{in}}{r}}\Big),
\qquad r\ge r_\mathrm{in}=6M .$$

$F$ peaks at $r = \frac{49}{36}r_\mathrm{in} = 8.167M$; the effective temperature follows
from Stefan–Boltzmann, $T_\mathrm{eff}=(F/\sigma)^{1/4}$.

For the defaults ($M_\bullet = 10\,M_\odot$, $\dot M = 10^{-7}\,M_\odot\,\mathrm{yr}^{-1}$,
0.45 Eddington):

$$T_\mathrm{peak} = 6.15\times10^{6}\ \mathrm{K} \;(\approx 0.53\ \mathrm{keV}),$$

which is the physically expected value for a stellar-mass X-ray binary inner disk.

**Ray/disk intersection.** The disk is the plane $z=0$. Each RK4 step tests for a sign change
of $z$; on a hit, the crossing angle is refined by 8 bisection steps *re-integrating the
geodesic*, giving sub-pixel accuracy in the crossing radius (needed because the ISCO edge and
the photon ring are only a few pixels apart). The first hit wins (the disk is opaque), which
is exactly what produces the primary image, the secondary image arcing over the shadow, and
the higher-order images crowding into the critical curve.

---

## 3. Redshift, Doppler factor and beaming

For gas on a circular Keplerian orbit, $u^\mu = u^t(1,0,0,\Omega)$,
$u^t = (1-r_s/r)^{-1/2}$, $\Omega=\sqrt{M/r^3}$, the frequency ratio measured by a static
observer at infinity is

$$g = \frac{E}{E_\mathrm{em}} = \frac{E}{u^t(E-\Omega L_z)}
   = \frac{\sqrt{1-r_s/r}}{1-\Omega b(\hat e_3\cdot\hat z)} .$$

Because $I_\nu/\nu^3$ is a Lorentz invariant, the bolometric specific intensity transforms as

$$I_\mathrm{obs} = g^4 I_\mathrm{em},\qquad T_\mathrm{obs} = g\,T_\mathrm{em}.$$

`--selftest` checks this expression against the independent special+general-relativistic
decomposition $g=\sqrt{1-r_s/r}\cdot\gamma\sqrt{(1\pm\beta)/(1\mp\beta)}$ with
$\beta = \Omega r/\sqrt{1-r_s/r}$: exact to $10^{-9}$.

The same $g$ is used for every disk pixel, so the bright/blueshifted approaching side and the
dim/redshifted receding side fall out of the physics rather than being painted on. Switching
`--no-doppler` removes only the $1/(1-\Omega b_z)$ term, and the frame's left/right intensity
ratio collapses to exactly 1.000 — used by `tools/` to verify beaming.

**Display response.** The physical spectrum is far outside the visible band. Rather than
lying about the physics, the spectrum is mapped into the visible the way a camera exposure
maps scene radiance onto a sensor: $T_\mathrm{disp} = gT_\mathrm{eff}/S$ with
$S = T_\mathrm{peak}/6500\,\mathrm{K}$, so the *relative* radial temperature structure and
all $g$-factor shifts are preserved, while the absolute colour lands in the visible band.
`--physical` (or `T`) sets $S=1$ and shows the true, blue-white X-ray disk.

---

## 4. Colour science

`buildBlackbodyLUT()` integrates, for 1024 logarithmically spaced temperatures from 300 K to
$10^7$ K:

1. Planck's law $B_\lambda(\lambda,T)$, evaluated in log-space (the visible band is deep in the
   Wien tail at low $T$, so naive evaluation underflows);
2. the CIE 1931 2° colour-matching functions (3-lobe Gaussian fit, Wyman–Shirley–Wang 2013);
3. XYZ → linear sRGB (D65 matrix), normalised to unit peak.

The shader samples the LUT with $\ln T$, so both the disk and the star colours share one
consistent colour model.

---

## 5. Background

Equal-area cells on the direction sphere in $(\phi,\cos\theta)$ so the stellar density is
uniform; per cell a hashed position, luminosity function ($\mathrm{mag}\propto \xi^7$, so most
stars are faint), colour temperature drawn from the same blackbody LUT, plus a Milky Way band
with fbm structure and dust lanes. Escaping rays sample it in their asymptotic direction, so
the field is lensed by the black hole — Einstein rings and the dragging of the critical curve
appear without any special-casing.

---

## 6. Spacetime sheet (view 2)

The **Flamm paraboloid** is the exact isometric embedding of the $t=\mathrm{const}$
Schwarzschild spatial slice into Euclidean 3-space:

$$z(r) = 2\sqrt{r_s(r-r_s)}\quad\Longrightarrow\quad
1 + z'^2 = \frac{1}{1-r_s/r} \ \checkmark$$

(the self-test verifies this identity and the induced Gaussian curvature by finite
differences). Hence

$$K = -\frac{r_s}{2r^3},$$

which is what the colour ramp encodes (log scale). The grid is spaced uniformly in
$s=\sqrt{r-r_s}$ so rings crowd correctly toward the throat; the funnel is drawn from
$r=r_s(1+10^{-4})$ outward, and the rings mark the horizon ($2M$, the throat),
the photon sphere ($3M$) and the ISCO ($6M$).

**Infall demo.** A particle released from rest at $r_0=40M$:

* proper time: $(dr/d\tau)^2 = r_s/r \Rightarrow r(\tau) = \big[\tfrac32(r_0^{3/2}-\sqrt{r_s}\tau)\big]^{2/3}$
  — reaches the horizon in finite proper time $\tau = \tfrac23(r_0^{3/2}-r_s^{3/2})/\sqrt{r_s}$;
* Schwarzschild coordinate time: $dr/dt = -(1-r_s/r)\sqrt{r_s/r}$ — the solution asymptotes to
  $r_s$ and never crosses (integrated once at startup into a table).

Two markers slide down the funnel in lock-step: the proper-time one disappears through the
throat, the coordinate-time one hangs just above it forever. That *is* the "trapdoor in
spacetime".

`B` toggles a mirrored copy below, which is the standard illustration of the maximal-extension
Einstein–Rosen bridge. Caveat: the exact embedding is only guaranteed for the exterior
$r\ge r_s$; the mirror is an illustration of the second asymptotic region, not an additional
exact embedding of the interior slice (the $t=\mathrm{const}$ slicing is not spacelike inside
the horizon in Schwarzschild coordinates).

---

## 7. Approximations and limitations

* **Static camera.** Held at fixed $r$ (not free fall), so no aberration from observer
  motion is modelled.
* **Escape radius.** Rays terminate at $r=300M$; the neglected deflection beyond that is
  $\lesssim Mb^2/r_\mathrm{far}^2 \approx 3\times10^{-3}$ rad for the largest relevant $b$.
* **Halo path element.** Uses $dl = |d\vec x|/\sqrt{1-r_s/r}$ — exact for radial motion,
  an approximation for the transverse part; the $g^4$ factor dominates the error budget.
* **Halo emitter.** Uses the Keplerian $g$-factor, valid only for $r>3M$; the density is
  therefore truncated at the ISCO.
* **float32** state with RK4: fine to $\sim10^{-4}$ rad in the deflection (see table), but
  rays that wind many times near the critical curve eventually exhaust the step budget.
* No Kerr rotation, no self-gravity, no radiative transfer between disk and halo
  (emission is added, scattering is not), no polarization.
