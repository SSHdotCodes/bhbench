// kerr_shared.h
//
// Kerr null-geodesic core, written ONCE and compiled twice:
//   * as C++17 with T = double  -> the CPU validation suite (selftest.cpp)
//   * as Metal Shading Language with T = float -> the real-time GPU ray tracer
// so the shader's physics is literally the code that the test-suite verifies.
//
// Units: G = c = M = 1.  Boyer-Lindquist coordinates (t, r, theta, phi), spin a = J/M in [0, 1).
//
// --- Method -----------------------------------------------------------------------------
// A photon has conserved E (=1 by normalisation), L = p_phi and Carter's constant Q.  In Mino time
// (d tau_affine = Sigma d lambda) the geodesic separates:
//
//     (dr/dl)^2   = R(r)  = (r^2 + a^2 - a L)^2 - Delta [ Q + (L - a)^2 ]
//     (dmu/dl)^2  = M(mu) = Q - (Q + L^2 - a^2) mu^2 - a^2 mu^4          (mu = cos theta)
//     dphi/dl     = a (r^2 + a^2 - a L)/Delta - a + L/(1 - mu^2)
//     dt/dl       = (r^2 + a^2)(r^2 + a^2 - a L)/Delta + a (L - a (1 - mu^2))
//
// We integrate in the variable u = 1/r.  With s = a^2 - a L and K = Q + (L - a)^2,
//     (du/dl)^2 = F(u) = (1 + s u^2)^2 - K u^2 (1 - 2u + a^2 u^2),
// and the SECOND-order forms
//     u''  = F'(u)/2  = (2s - K) u + 3K u^2 + 2 (s^2 - a^2 K) u^3
//     mu'' = M'(mu)/2 = -(Q + L^2 - a^2) mu - 2 a^2 mu^3
// are low-order polynomials: smooth through turning points (no sign flips), regular on the polar axis (mu
// is a good coordinate there, theta is not), regular at infinity (u = 0) and at the horizon (u = 1/r_+),
// and O(1) everywhere -- unlike r(l), which is hyperbolic (r'' ~ 2 r^3) in the far field.
// Rays are traced BACKWARD from the camera, so the integration parameter is s = -lambda_photon.
//
// Coordinate time along the ray grows like r; we integrate the regularised  t_hat = t + |dr|-variation
// (which is O(log r)) and subtract the accumulated |Delta r| analytically, so light-travel delays stay accurate.
// ---------------------------------------------------------------------------------------------
#pragma once

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#define BH_REF thread
#else
#include <cmath>
#include <algorithm>
using std::sqrt;
using std::sin;
using std::cos;
using std::fabs;
using std::min;
using std::max;
using std::atan2;
using std::acos;
using std::exp;
using std::log;
using std::pow;
#define BH_REF
#endif

#define BH_INLINE inline

namespace bh {

template <class T>
struct Ray {
    T u, vu;    // u = 1/r and du/ds
    T mu, vmu;  // cos(theta) and dmu/ds
    T w, vw;    // w = sin^2(theta) tracked as its own variable (accurate at the poles where 1 - mu^2 is not)
    T phi;      // azimuth
    T that;     // regularised coordinate time  (t = that - total variation of r)
    T L, Q;     // constants of motion of the (forward) photon, E = 1
};

template <class T>
struct TraceCfg {
    T a;          // spin
    T r_plus;     // outer horizon
    T r_isco;     // inner edge of the disk
    T r_out;      // outer edge of the disk
    T r_far;      // escape radius (flat-space extrapolation beyond it)
    T c;          // step-size coefficient (smaller = more accurate)
    int max_steps;
    int disk;     // 1: intersect the accretion disk
    T disk_h;     // half-thickness of the disk photosphere as |cos theta| (aspect ratio H/r); >= 1e-3 enforced
    T halo;       // amplitude of the optically thin hot corona ("halo"); 0 disables
    T nu_cam;     // camera-measured photon energy per unit energy at infinity (for the corona's redshift)
};

template <class T>
struct TraceOut {
    int status;     // 0 = captured by horizon, 1 = escaped to infinity, 2 = hit the disk, 3 = step limit
    int steps;
    int crossings;  // equatorial-plane crossings before the terminal event (image order n)
    int surf;       // which disk surface was hit: 0 top face, 1 bottom face, 2 outer rim, 3 inner wall
    T r_hit, phi_hit, t_hit, vmu_hit;  // disk hit (t_hit = coordinate time offset, negative = in the past)
    T L;            // photon angular momentum (per unit energy)
    T dx, dy, dz;   // asymptotic direction of the ray (unit vector) if escaped
    T halo_e;       // corona: integral of  j g^3 Sigma dlambda  along the ray (observed intensity, arbitrary units)
    T halo_g;       // corona: emission-weighted mean frequency ratio g
};

// ---------------------------------------------------------------------------------------------
// Geodesic equations (second order form in u = 1/r)
// ---------------------------------------------------------------------------------------------
template <class T>
BH_INLINE void rhs(T a, const BH_REF Ray<T>& y, BH_REF Ray<T>& d)
{
    const T u = y.u, u2 = u * u, a2 = a * a;
    const T s = a2 - a * y.L;
    const T K = y.Q + (y.L - a) * (y.L - a);
    const T mu = y.mu;
    const T om = max(y.w, T(1e-14));            // sin^2(theta), accurate arbitrarily close to the polar axis
    d.u = y.vu;
    d.vu = (T(2) * s - K) * u + T(3) * K * u2 + T(2) * (s * s - a2 * K) * u2 * u;
    d.mu = y.vmu;
    d.vmu = -(y.Q + y.L * y.L - a2) * mu - T(2) * a2 * mu * mu * mu;
    // w = 1 - mu^2:  w'' = -2 M(w) + 2 (1 - w) M_w(w),   M(w) = -L^2 + (Q + L^2 + a^2) w - a^2 w^2
    {
        const T q = y.Q + y.L * y.L + a2;
        const T Mw = -y.L * y.L + q * y.w - a2 * y.w * y.w;
        d.w = y.vw;
        d.vw = T(2) * ((T(1) - y.w) * (q - T(2) * a2 * y.w) - Mw);
    }
    const T D = T(1) - T(2) * u + a2 * u2;           // Delta / r^2
    const T invD = T(1) / D;
    const T Pr2 = T(1) + s * u2;                      // P / r^2
    d.phi = -(a * Pr2 * invD - a + y.L / om);         // backward parameter: sign flip
    const T tdot = -((T(1) / u2 + a2) * Pr2 * invD + a * (y.L - a * om));
    const T vr = -y.vu / u2;                          // dr/ds
    d.that = tdot + fabs(vr);                         // regularised: removes the O(r^2) light-cone growth
    d.L = T(0);
    d.Q = T(0);
}

template <class T>
BH_INLINE Ray<T> axpy(const BH_REF Ray<T>& y, const BH_REF Ray<T>& k, T h)
{
    Ray<T> o = y;
    o.u += h * k.u;
    o.vu += h * k.vu;
    o.mu += h * k.mu;
    o.vmu += h * k.vmu;
    o.w += h * k.w;
    o.vw += h * k.vw;
    o.phi += h * k.phi;
    o.that += h * k.that;
    return o;
}

template <class T>
BH_INLINE void rk4(T a, BH_REF Ray<T>& y, T h)
{
    Ray<T> k1, k2, k3, k4;
    rhs(a, y, k1);
    Ray<T> yt = axpy(y, k1, T(0.5) * h);
    rhs(a, yt, k2);
    yt = axpy(y, k2, T(0.5) * h);
    rhs(a, yt, k3);
    yt = axpy(y, k3, h);
    rhs(a, yt, k4);
    const T h6 = h / T(6);
    y.u += h6 * (k1.u + T(2) * (k2.u + k3.u) + k4.u);
    y.vu += h6 * (k1.vu + T(2) * (k2.vu + k3.vu) + k4.vu);
    y.mu += h6 * (k1.mu + T(2) * (k2.mu + k3.mu) + k4.mu);
    y.vmu += h6 * (k1.vmu + T(2) * (k2.vmu + k3.vmu) + k4.vmu);
    y.w += h6 * (k1.w + T(2) * (k2.w + k3.w) + k4.w);
    y.vw += h6 * (k1.vw + T(2) * (k2.vw + k3.vw) + k4.vw);
    y.phi += h6 * (k1.phi + T(2) * (k2.phi + k3.phi) + k4.phi);
    y.that += h6 * (k1.that + T(2) * (k2.that + k3.that) + k4.that);
}

// Radial and polar "potentials" (diagnostics / initial conditions).
template <class T>
BH_INLINE T potential_F(T a, T u, T L, T Q)          // (du/dl)^2 = R/r^4
{
    const T s = a * a - a * L;
    const T K = Q + (L - a) * (L - a);
    return (T(1) + s * u * u) * (T(1) + s * u * u) - K * u * u * (T(1) - T(2) * u + a * a * u * u);
}
template <class T>
BH_INLINE T potential_R(T a, T r, T L, T Q)
{
    const T P = r * r + a * a - a * L;
    const T K = Q + (L - a) * (L - a);
    return P * P - (r * r - T(2) * r + a * a) * K;
}
template <class T>
BH_INLINE T potential_M(T a, T mu, T L, T Q)
{
    const T m2 = mu * mu;
    return Q - (Q + L * L - a * a) * m2 - a * a * m2 * m2;
}

// Adaptive step.  Base: phase increment c of the polar/radial oscillation (w = its angular frequency in Mino
// time); limited so that u changes by at most 70% per step (keeps the regularised time accurate and never
// overshoots u = 0), by 0.08 in absolute terms (resolves the strong-field region), and near the polar axis.
template <class T>
BH_INLINE T step_size(T c, const BH_REF Ray<T>& y)
{
    const T w = sqrt(max(y.Q + y.L * y.L, T(0))) + T(1);
    T h = c / w;
    const T av = fabs(y.vu) + T(1e-6);
    h = min(h, T(0.7) * y.u / av);
    h = min(h, T(0.08) / av);
    // resolve passages close to the polar axis, where phi swings by up to pi within a tiny lambda interval
    // (the constraint tightens as sin(theta)^3 below ~0.15 rad and relaxes quadratically above it)
    const T sth = sqrt(max(y.w, T(1e-10)));
    const T rel = max(T(1), (sth / T(0.15)) * (sth / T(0.15)));
    h = min(h, T(0.07) * sth * rel / (sqrt(max(y.Q + y.L * y.L, T(0))) + T(0.05)));
    return h;
}

// Constraint projection.  RK4 conserves the first integrals (du/dl)^2 = F(u), (dmu/dl)^2 = M(mu) only to O(h^4);
// near a turning point that error is comparable to the (tiny) value of the potential itself, and for rays that skim
// the polar axis (w = sin^2 theta ~ L^2/q ~ 1e-7) it destroys the azimuth.  After every step we therefore rescale the
// velocities back onto the constraint shell (signs are kept).  Turning points become exact; what remains is a small
// phase (timing) error.  Near the equator mu is the accurate variable, near the poles w = sin^2(theta) is;
// the two are re-synchronised in both directions.
template <class T>
BH_INLINE T sgn_(T x) { return x < T(0) ? T(-1) : T(1); }

template <class T>
BH_INLINE void project_state(T a, BH_REF Ray<T>& y)
{
    const T a2 = a * a;
    const T F = potential_F(a, y.u, y.L, y.Q);
    y.vu = sgn_(y.vu) * sqrt(max(F, T(0)));
    if (fabs(y.mu) < T(0.7)) {
        const T M = potential_M(a, y.mu, y.L, y.Q);
        y.vmu = sgn_(y.vmu) * sqrt(max(M, T(0)));
        y.w = T(1) - y.mu * y.mu;
        y.vw = -T(2) * y.mu * y.vmu;
    } else {
        const T q = y.Q + y.L * y.L + a2;
        y.w = min(max(y.w, T(1e-30)), T(1));
        const T Mw = -y.L * y.L + q * y.w - a2 * y.w * y.w;
        y.vw = sgn_(y.vw) * sqrt(max(T(4) * (T(1) - y.w) * Mw, T(0)));
        const T m = sgn_(y.mu) * sqrt(max(T(1) - y.w, T(0)));
        y.mu = m;
        y.vmu = (m != T(0)) ? -y.vw / (T(2) * m) : T(0);
    }
}

// One integrator step: RK4 followed by the constraint projection.
template <class T>
BH_INLINE void step_ray(T a, BH_REF Ray<T>& y, T h)
{
    rk4(a, y, h);
    project_state(a, y);
}

// ---------------------------------------------------------------------------------------------
// Camera: a ZAMO (zero-angular-momentum observer) at (r0, theta0).  The view direction n is given in
// the observer's orthonormal frame (e_r, e_theta, e_phi).  The forward photon arriving from
// direction n travels along -n, so  L = -sqrt(g_phiphi) E_z n_phi,  E = alpha E_z + omega L = 1.
// Returns false if no photon with E>0 at infinity can arrive from that direction (ergoregion).
// nu_cam = photon energy measured by the camera per unit energy at infinity (gravitational blueshift).
// ---------------------------------------------------------------------------------------------
template <class T>
BH_INLINE bool init_ray_zamo(T a, T r0, T theta0, T phi0, T nr, T nth, T nph,
                             BH_REF Ray<T>& y, BH_REF T& nu_cam)
{
    const T mu = cos(theta0);
    const T s = max(sin(theta0), T(1e-4));
    const T s2 = s * s;
    const T r2 = r0 * r0, a2 = a * a;
    const T Sigma = r2 + a2 * mu * mu;
    const T Delta = r2 - T(2) * r0 + a2;
    const T A = (r2 + a2) * (r2 + a2) - a2 * Delta * s2;
    const T sg = sqrt(A * s2 / Sigma);       // sqrt(g_phiphi)
    const T lapse = sqrt(Sigma * Delta / A);
    const T omega = T(2) * a * r0 / A;
    const T den = lapse - omega * sg * nph;
    if (!(den > T(1e-5))) return false;
    const T Ez = T(1) / den;
    const T pth = sqrt(Sigma) * Ez * nth;    // dtheta/ds
    const T vr = sqrt(Sigma * Delta) * Ez * nr;
    y.u = T(1) / r0;
    y.vu = -y.u * y.u * vr;
    y.mu = mu;
    y.vmu = -s * pth;
    y.w = s2;
    y.vw = -T(2) * mu * y.vmu;
    y.phi = phi0;
    y.that = T(0);
    y.L = -sg * Ez * nph;
    // Rays in the camera's meridian plane have L = 0 exactly: they pass THROUGH the polar axis, where phi jumps by
    // pi (0/0 in dphi/dl = L/sin^2 theta).  The map is continuous in L, so nudge |L| to >= 5e-4 (negligible: a
    // 1e-5 rad camera-angle change) and the jump is then integrated correctly and matches both neighbours.
    if (fabs(y.L) < T(5e-4)) y.L = (y.L < T(0)) ? T(-5e-4) : T(5e-4);
    y.Q = pth * pth + mu * mu * (y.L * y.L / s2 - a2);
    nu_cam = Ez;
    return true;
}

// Ray from explicit constants of motion (used by validation tests).  Starts moving inward.
template <class T>
BH_INLINE bool init_ray_constants(T a, T r0, T mu0, T L, T Q, T vmu_sign, BH_REF Ray<T>& y)
{
    const T u0 = T(1) / r0;
    const T F = potential_F(a, u0, L, Q);
    const T M = potential_M(a, mu0, L, Q);
    if (F < T(0) || M < T(0)) return false;
    y.u = u0;
    y.vu = sqrt(F);
    y.mu = mu0;
    y.vmu = vmu_sign * sqrt(M);
    y.w = T(1) - mu0 * mu0;
    y.vw = -T(2) * mu0 * y.vmu;
    y.phi = T(0);
    y.that = T(0);
    y.L = L;
    y.Q = Q;
    return true;
}

// Asymptotic direction (unit vector, world frame with z = spin axis) from the ray state at large r.
template <class T>
BH_INLINE void escape_direction(T a, const BH_REF Ray<T>& y, BH_REF T& dx, BH_REF T& dy, BH_REF T& dz)
{
    Ray<T> d;
    rhs(a, y, d);
    const T r = T(1) / y.u;
    const T st = sqrt(max(y.w, T(1e-14)));
    const T ct = y.mu;
    const T cp = cos(y.phi), sp = sin(y.phi);
    // components of the coordinate velocity in the local orthonormal frame, scaled by u^2 to stay O(1)
    T ar = -d.u;                               // (dr/ds) u^2 = -du/ds
    T at = (-d.mu / st) * y.u;                 // r dtheta/ds * u^2 = dtheta/ds * u
    T ap = st * d.phi * y.u;                   // r sin(theta) dphi/ds * u^2
    (void)r;
    const T n = sqrt(ar * ar + at * at + ap * ap);
    ar /= n; at /= n; ap /= n;
    dx = ar * st * cp + at * ct * cp - ap * sp;
    dy = ar * st * sp + at * ct * sp + ap * cp;
    dz = ar * ct - at * st;
}

// Cubic Hermite interpolation on [0,1] with slopes wrt u.
template <class T>
BH_INLINE T hermite(T p0, T p1, T m0, T m1, T x)
{
    const T x2 = x * x, x3 = x2 * x;
    return (T(2) * x3 - T(3) * x2 + T(1)) * p0 + (x3 - T(2) * x2 + x) * m0 +
           (T(-2) * x3 + T(3) * x2) * p1 + (x3 - x2) * m1;
}
template <class T>
BH_INLINE T hermite_dx(T p0, T p1, T m0, T m1, T x)
{
    const T x2 = x * x;
    return (T(6) * x2 - T(6) * x) * p0 + (T(3) * x2 - T(4) * x + T(1)) * m0 +
           (T(-6) * x2 + T(6) * x) * p1 + (T(3) * x2 - T(2) * x) * m1;
}

// ---------------------------------------------------------------------------------------------
// Kerr circular equatorial orbits (prograde), M = 1
// ---------------------------------------------------------------------------------------------
template <class T>
BH_INLINE T horizon_radius(T a) { return T(1) + sqrt(max(T(1) - a * a, T(0))); }

template <class T>
BH_INLINE T isco_radius(T a)
{
    const T a2 = a * a;
    const T z1 = T(1) + pow(T(1) - a2, T(1) / T(3)) * (pow(T(1) + a, T(1) / T(3)) + pow(max(T(1) - a, T(0)), T(1) / T(3)));
    const T z2 = sqrt(T(3) * a2 + z1 * z1);
    return T(3) + z2 - sqrt((T(3) - z1) * (T(3) + z1 + T(2) * z2));
}

// Keplerian angular velocity  Omega = 1/(r^{3/2} + a)
template <class T>
BH_INLINE T omega_kepler(T r, T a) { return T(1) / (r * sqrt(r) + a); }

// u^t of the circular orbit  u^t = (r^{3/2} + a) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} + 2a))
template <class T>
BH_INLINE T ut_kepler(T r, T a)
{
    const T sr = sqrt(r);
    const T r15 = r * sr;
    return (r15 + a) / (sr * sqrt(sr) * sqrt(max(r15 - T(3) * sr + T(2) * a, T(1e-12))));
}

// Frequency ratio g = nu_obs/nu_emit for gas on a circular orbit seen by a photon (E=1, angular momentum L)
// arriving with camera-measured energy nu_cam.
template <class T>
BH_INLINE T disk_redshift(T r, T a, T L, T nu_cam)
{
    return nu_cam / (ut_kepler(r, a) * (T(1) - omega_kepler(r, a) * L));
}

// Optically thin hot corona around the hole: a fat torus-like atmosphere, emissivity j ~ r^-5/2 exp(-mu^2/2 sigma^2),
// co-rotating on Keplerian orbits outside the ISCO and with the local frame-dragging (ZAMO) inside it.  The observed
// intensity from optically thin gas is  I = int j g^3 d(affine parameter)  (j/nu^2 and I/nu^3 are Lorentz invariants); the
// affine parameter is Sigma dlambda.  Returns j g^3 Sigma and stores g.
template <class T>
BH_INLINE T halo_integrand(const BH_REF TraceCfg<T>& cfg, T L, T u, T mu, BH_REF T& g_out)
{
    g_out = T(0);
    if (u < T(1) / T(40) || fabs(mu) > T(0.85)) return T(0);
    const T r = T(1) / u, a = cfg.a;
    const T sig = T(0.30);
    const T prof = exp(-mu * mu / (T(2) * sig * sig));
    const T rf = r / T(26);
    const T j = cfg.halo * prof * u * u * sqrt(u) * exp(-rf * rf);
    const T Sigma = r * r + a * a * mu * mu;
    T nu_em;
    if (r >= cfg.r_isco) {
        nu_em = ut_kepler(r, a) * (T(1) - omega_kepler(r, a) * L);
    } else {
        const T Delta = r * r - T(2) * r + a * a;
        const T s2 = max(T(1) - mu * mu, T(1e-4));
        const T A = (r * r + a * a) * (r * r + a * a) - a * a * Delta * s2;
        const T lapse = sqrt(max(Sigma * Delta / A, T(1e-8)));
        nu_em = (T(1) - T(2) * a * r / A * L) / lapse;                       // ZAMO
    }
    if (!(nu_em > T(1e-3))) return T(0);
    const T g = min(cfg.nu_cam / nu_em, T(6));
    g_out = g;
    return j * g * g * g * Sigma;
}

// ---------------------------------------------------------------------------------------------
// Trace one ray until it is captured, escapes, or hits the thin equatorial disk r in [r_isco, r_out].
// ---------------------------------------------------------------------------------------------
template <class T>
BH_INLINE void trace_ray(const BH_REF TraceCfg<T>& cfg, Ray<T> y, BH_REF TraceOut<T>& out)
{
    out.status = 3;
    out.steps = 0;
    out.crossings = 0;
    out.surf = 0;
    out.r_hit = out.phi_hit = out.t_hit = out.vmu_hit = T(0);
    out.L = y.L;
    out.dx = out.dy = out.dz = T(0);
    out.halo_e = out.halo_g = T(0);
    T halo_eg = T(0);
    T hw0 = T(0), hg0 = T(0);
    if (cfg.halo > T(0)) hw0 = halo_integrand(cfg, y.L, y.u, y.mu, hg0);
    const T a = cfg.a;
    const T u_cap = T(1) / (cfg.r_plus + T(0.01));
    const T u_far = T(1) / cfg.r_far;
    T tv = T(0);  // accumulated |delta r|

    for (int i = 0; i < cfg.max_steps; ++i) {
        const T h = step_size(cfg.c, y);
        Ray<T> n = y;
        step_ray(a, n, h);
        out.steps = i + 1;

        if (n.u > u_cap) {
            out.status = 0;
            if (cfg.halo > T(0)) {   // finish the corona integral to the horizon (heavily redshifted: small)
                T hg1;
                const T hw1 = halo_integrand(cfg, y.L, min(n.u, T(1) / cfg.r_plus), n.mu, hg1);
                out.halo_e += T(0.5) * h * (hw0 + hw1);
                halo_eg += T(0.5) * h * (hw0 * hg0 + hw1 * hg1);
                out.halo_g = (out.halo_e > T(0)) ? halo_eg / out.halo_e : T(1);
            }
            return;
        }
        const T r_old = T(1) / y.u;

        if (cfg.disk != 0) {
            // The disk is an opaque wedge |cos(theta)| < h between r_isco and r_out: its photosphere is the cone pair
            // mu = +h (top face) and mu = -h (bottom face), closed by the outer rim and the inner wall.  A step can cross
            // several of these surfaces, so take the earliest valid one (Hermite root of each).
            const T hh = max(cfg.disk_h, T(1e-3));
            const T u_out = T(1) / cfg.r_out, u_in = T(1) / cfg.r_isco;
            const bool ctop = (y.mu - hh) * (n.mu - hh) < T(0);
            const bool cbot = (y.mu + hh) * (n.mu + hh) < T(0);
            const bool crim = (y.u - u_out) * (n.u - u_out) < T(0);
            const bool cin = (y.u - u_in) * (n.u - u_in) < T(0);
            if (ctop || cbot || crim || cin) {
                Ray<T> k0, k1;
                rhs(a, y, k0);
                rhs(a, n, k1);
                const T m0 = h * k0.mu, m1 = h * k1.mu;
                T xbest = T(2);
                int kind = -1;
                for (int e = 0; e < 4; ++e) {
                    if (e == 0 && !ctop) continue;
                    if (e == 1 && !cbot) continue;
                    if (e == 2 && !crim) continue;
                    if (e == 3 && !cin) continue;
                    T x;
                    if (e < 2) {
                        const T sh = (e == 0) ? hh : -hh;
                        x = (y.mu - sh) / ((y.mu - sh) - (n.mu - sh));
                        for (int it = 0; it < 4; ++it) {
                            const T f = hermite(y.mu - sh, n.mu - sh, m0, m1, x);
                            const T fp = hermite_dx(y.mu - sh, n.mu - sh, m0, m1, x);
                            if (fabs(fp) > T(1e-12)) x = x - f / fp;
                            x = min(max(x, T(0)), T(1));
                        }
                        const T rr = T(1) / hermite(y.u, n.u, h * k0.u, h * k1.u, x);
                        if (rr < cfg.r_isco || rr > cfg.r_out) continue;
                    } else {
                        const T ue = (e == 2) ? u_out : u_in;
                        x = (y.u - ue) / ((y.u - ue) - (n.u - ue));
                        for (int it = 0; it < 4; ++it) {
                            const T f = hermite(y.u - ue, n.u - ue, h * k0.u, h * k1.u, x);
                            const T fp = hermite_dx(y.u - ue, n.u - ue, h * k0.u, h * k1.u, x);
                            if (fabs(fp) > T(1e-12)) x = x - f / fp;
                            x = min(max(x, T(0)), T(1));
                        }
                        if (fabs(hermite(y.mu, n.mu, m0, m1, x)) >= hh) continue;
                    }
                    if (x < xbest) {
                        xbest = x;
                        kind = e;
                    }
                }
                if (kind >= 0) {
                    const T ue = (kind == 2) ? u_out : (kind == 3) ? u_in : T(0);
                    out.status = 2;
                    out.surf = kind;
                    out.r_hit = (kind >= 2) ? T(1) / ue : T(1) / hermite(y.u, n.u, h * k0.u, h * k1.u, xbest);
                    out.phi_hit = hermite(y.phi, n.phi, h * k0.phi, h * k1.phi, xbest);
                    out.t_hit = hermite(y.that, n.that, h * k0.that, h * k1.that, xbest) - (tv + fabs(out.r_hit - r_old));
                    out.vmu_hit = y.vmu + (n.vmu - y.vmu) * xbest;
                    if (cfg.halo > T(0)) {   // corona along the partial step up to the surface (Simpson on [0, xbest])
                        T hgm, hg1;
                        const T xm = T(0.5) * xbest;
                        const T wm = halo_integrand(cfg, y.L, hermite(y.u, n.u, h * y.vu, h * n.vu, xm), hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, xm), hgm);
                        const T w1 = halo_integrand(cfg, y.L, hermite(y.u, n.u, h * y.vu, h * n.vu, xbest), hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, xbest), hg1);
                        out.halo_e += h * xbest * (hw0 + T(4) * wm + w1) / T(6);
                        halo_eg += h * xbest * (hw0 * hg0 + T(4) * wm * hgm + w1 * hg1) / T(6);
                        out.halo_g = (out.halo_e > T(0)) ? halo_eg / out.halo_e : T(1);
                    }
                    // count the equatorial crossing inside this step if it happened before the hit; rim / inner-wall
                    // hits on the lower half are the direct image of the rim (the crossing happened outside the disk)
                    if (y.mu * n.mu < T(0) && y.mu / (y.mu - n.mu) < xbest) out.crossings += 1;
                    if (kind >= 2 && hermite(y.mu, n.mu, m0, m1, xbest) < T(0) && out.crossings > 0) out.crossings -= 1;
                    return;
                }
            }
        }
        if (y.mu * n.mu < T(0)) out.crossings += 1;

        if (cfg.halo > T(0)) {   // Simpson rule over the step, midpoint from cubic Hermite interpolation of (u, mu)
            T hgm, hg1;
            const T um = hermite(y.u, n.u, h * y.vu, h * n.vu, T(0.5));
            const T mm = hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, T(0.5));
            const T hwm = halo_integrand(cfg, y.L, um, mm, hgm);
            const T hw1 = halo_integrand(cfg, y.L, n.u, n.mu, hg1);
            out.halo_e += h * (hw0 + T(4) * hwm + hw1) / T(6);
            halo_eg += h * (hw0 * hg0 + T(4) * hwm * hgm + hw1 * hg1) / T(6);
            hw0 = hw1;
            hg0 = hg1;
            out.halo_g = (out.halo_e > T(0)) ? halo_eg / out.halo_e : T(1);
        }
        tv += fabs(T(1) / n.u - r_old);
        y = n;
        if (y.u < u_far && y.vu < T(0)) {
            out.status = 1;
            escape_direction(a, y, out.dx, out.dy, out.dz);
            return;
        }
    }
}

}  // namespace bh
