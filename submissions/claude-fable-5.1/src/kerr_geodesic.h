// kerr_geodesic.h — Kerr null-geodesic integrator shared by the Metal GPU
// shaders and the CPU verification harness. Compiles as both MSL and C++17.
//
// Formulation
// -----------
// Boyer–Lindquist coordinates (t, r, θ, φ), geometric units G = c = M = 1.
// Photon Hamiltonian H = ½ g^{μν} p_μ p_ν with conserved E = -p_t, L = p_φ:
//
//     2 Σ H = Δ p_r² + p_θ² − P(r)²/Δ + (L − a E sin²θ)² / sin²θ,
//     P(r) = (r² + a²) E − a L,   Σ = r² + a² cos²θ,   Δ = r² − 2r + a².
//
// We integrate in Mino time λ' (dλ' = dλ / Σ), which removes the 1/Σ factor:
//     dr/dλ' = Δ p_r,  dθ/dλ' = p_θ,
//     dφ/dλ' = a P/Δ + (L − aE sin²θ)/sin²θ,
//     dt/dλ' = (r²+a²) P/Δ + a (L − aE sin²θ),
//     dp_r/dλ' = −½ ∂F/∂r + 2 r H,   dp_θ/dλ' = −½ ∂F/∂θ − 2 a² cosθ sinθ H,
// where F = 2ΣH.  The H terms vanish for exact null rays; keeping them makes
// the flow the exact Hamiltonian flow of the slightly-off-shell numeric state.
// Rays are traced BACKWARD from the camera (negative Mino-time steps) with the
// arriving photon's momentum, so frame dragging is handled with the right sign.
//
// θ is allowed to leave [0, π] when a ray with L = 0 passes exactly over a pole;
// all formulas are analytic in θ and the pseudo-Cartesian map handles the sign.
#ifndef KERR_GEODESIC_H
#define KERR_GEODESIC_H

#ifdef __METAL_VERSION__
#  define KG_THREAD thread
#  define KG_SIN(x)  metal::precise::sin(x)
#  define KG_COS(x)  metal::precise::cos(x)
#  define KG_SQRT(x) metal::precise::sqrt(x)
#  define KG_ABS(x)  metal::abs(x)
#  define KG_MIN(a,b) metal::min(a,b)
#  define KG_MAX(a,b) metal::max(a,b)
#else
#  include <cmath>
#  define KG_THREAD
#  define KG_SIN(x)  std::sin(x)
#  define KG_COS(x)  std::cos(x)
#  define KG_SQRT(x) std::sqrt(x)
#  define KG_ABS(x)  std::fabs(x)
#  define KG_MIN(a,b) ((a) < (b) ? (a) : (b))
#  define KG_MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

enum { KG_HIT_NONE = -1, KG_HIT_SKY = 0, KG_HIT_DISK = 1, KG_HIT_HORIZON = 2, KG_HIT_GRID = 3 };

template <typename R> struct KGState { R r, th, ph, t, pr, pth; };
template <typename R> struct KGConst { R a, E, L; };
template <typename R> struct KGDeriv { KGState<R> d; R H; };

template <typename R> struct KGParams {
    R rh;        // outer horizon radius 1 + sqrt(1 - a²)
    R rStop;     // capture radius (slightly above rh)
    R rDoom;     // innermost circular photon orbit radius: ingoing below it => captured
    R rEsc;      // escape radius
    R rIsco;     // disk inner edge
    R rDiskOut;  // disk outer edge
    R rGridOut;  // ray-traced equatorial grid outer edge
    R eps;       // relative step tolerance
    R epsAng;    // angular step tolerance (radians)
    int diskOn, gridOn, maxSteps;
};

template <typename R> struct KGHit {
    int type;
    R r, ph, g, dt, mu;   // equatorial hit: BL radius, azimuth, redshift factor g = E_obs/E_em, coordinate-time delay, emission cosine
    R dx, dy, dz;         // sky hit: unit direction on the celestial sphere (pseudo-Cartesian)
    R depth;              // flat-space distance from camera to hit (for compositing)
    int steps;
};

template <typename R>
inline KGDeriv<R> kg_deriv(KGState<R> y, KGConst<R> c) {
    const R a = c.a, E = c.E, L = c.L;
    const R s = KG_SIN(y.th), co = KG_COS(y.th);
    R s2 = s * s;
    if (s2 < R(1e-14)) s2 = R(1e-14);
    const R c2 = co * co;
    const R r = y.r, r2 = r * r, a2 = a * a;
    const R Sigma = r2 + a2 * c2;
    const R Delta = r2 - R(2) * r + a2;
    const R P = (r2 + a2) * E - a * L;
    const R Ls = L - a * E * s2;
    const R W = Ls * Ls / s2;
    const R F = Delta * y.pr * y.pr + y.pth * y.pth - P * P / Delta + W;
    const R H = F / (R(2) * Sigma);

    const R dDelta = R(2) * r - R(2);
    const R dP = R(2) * r * E;
    const R dF_dr = dDelta * y.pr * y.pr - (R(2) * P * dP * Delta - P * P * dDelta) / (Delta * Delta);
    const R dW_dth = R(2) * s * co * (a2 * E * E - L * L / (s2 * s2));

    KGDeriv<R> o;
    o.d.r   = Delta * y.pr;
    o.d.th  = y.pth;
    o.d.ph  = a * P / Delta + Ls / s2;
    o.d.t   = (r2 + a2) * P / Delta + a * Ls;
    o.d.pr  = R(-0.5) * dF_dr + R(2) * r * H;
    o.d.pth = R(-0.5) * dW_dth - R(2) * a2 * co * s * H;
    o.H = H;
    return o;
}

template <typename R>
inline KGState<R> kg_axpy(KGState<R> y, KGState<R> d, R h) {
    KGState<R> o;
    o.r = y.r + h * d.r;  o.th = y.th + h * d.th;  o.ph = y.ph + h * d.ph;
    o.t = y.t + h * d.t;  o.pr = y.pr + h * d.pr;  o.pth = y.pth + h * d.pth;
    return o;
}

// Classical RK4 step of size h (h may be negative), reusing a precomputed k1.
template <typename R>
inline KGState<R> kg_rk4(KGState<R> y, KGState<R> k1, KGConst<R> c, R h) {
    const KGState<R> k2 = kg_deriv(kg_axpy(y, k1, R(0.5) * h), c).d;
    const KGState<R> k3 = kg_deriv(kg_axpy(y, k2, R(0.5) * h), c).d;
    const KGState<R> k4 = kg_deriv(kg_axpy(y, k3, h), c).d;
    KGState<R> o;
    const R w = h / R(6);
    o.r   = y.r   + w * (k1.r   + R(2) * k2.r   + R(2) * k3.r   + k4.r);
    o.th  = y.th  + w * (k1.th  + R(2) * k2.th  + R(2) * k3.th  + k4.th);
    o.ph  = y.ph  + w * (k1.ph  + R(2) * k2.ph  + R(2) * k3.ph  + k4.ph);
    o.t   = y.t   + w * (k1.t   + R(2) * k2.t   + R(2) * k3.t   + k4.t);
    o.pr  = y.pr  + w * (k1.pr  + R(2) * k2.pr  + R(2) * k3.pr  + k4.pr);
    o.pth = y.pth + w * (k1.pth + R(2) * k2.pth + R(2) * k3.pth + k4.pth);
    return o;
}

// Adaptive step-size heuristic (magnitude only): limit the per-step change of
// every phase-space coordinate to a fraction of its natural scale.
template <typename R>
inline R kg_step_size(KGState<R> y, KGState<R> d, KGConst<R> c, KGParams<R> p) {
    const R tiny = R(1e-12);
    const R Escale = KG_ABS(c.E) + R(1e-3);
    const R hr  = p.eps * (KG_MAX(y.r - p.rh, R(0)) + R(0.05)) / (KG_ABS(d.r) + tiny);
    const R hth = p.epsAng / (KG_ABS(d.th) + tiny);
    const R hph = p.epsAng / (KG_ABS(d.ph) + tiny);
    const R hpr = p.eps * (KG_ABS(y.pr) + Escale) / (KG_ABS(d.pr) + tiny);
    const R hpt = p.eps * (KG_ABS(y.pth) + Escale * (R(1) + y.r)) / (KG_ABS(d.pth) + tiny);
    R h = KG_MIN(hr, KG_MIN(hth, KG_MIN(hph, KG_MIN(hpr, hpt))));
    if (c.L != R(0)) {
        // Rays with L ≠ 0 bounce off the centrifugal barrier before the axis; never let a
        // single step jump more than half-way to the axis or an RK4 stage may land on it.
        const R as = KG_ABS(KG_SIN(y.th));
        const R hpole = R(0.5) * as / (KG_ABS(d.th) + tiny);
        h = KG_MIN(h, hpole);
    }
    return h;
}

// Circular prograde (+φ) Keplerian orbit in the equatorial plane (Bardeen, Press & Teukolsky 1972).
template <typename R> inline R kg_omega_kepler(R r, R a) {
    const R sr = KG_SQRT(r);
    return R(1) / (r * sr + a);
}
template <typename R> inline R kg_ut_kepler(R r, R a) {
    const R sr = KG_SQRT(r);
    const R r32 = r * sr;
    const R den = KG_SQRT(r32 - R(3) * sr + R(2) * a);
    return (r32 + a) / (KG_SQRT(r32) * den);   // r^{3/4} = sqrt(r^{3/2})
}

// ZAMO lapse α and angular velocity ω at (r, θ).
template <typename R> inline void kg_zamo(R r, R th, R a, KG_THREAD R &alpha, KG_THREAD R &omega, KG_THREAD R &Sigma, KG_THREAD R &Delta, KG_THREAD R &A) {
    const R s = KG_SIN(th), co = KG_COS(th);
    const R r2 = r * r, a2 = a * a;
    Sigma = r2 + a2 * co * co;
    Delta = r2 - R(2) * r + a2;
    A = (r2 + a2) * (r2 + a2) - a2 * Delta * s * s;
    alpha = KG_SQRT(Sigma * Delta / A);
    omega = R(2) * a * r / A;
}

// Trace one photon backward from the camera. y0 holds the ARRIVING photon's
// phase-space state at the camera (E = -p_t > 0), t = 0. camX/Y/Z is the
// camera's pseudo-Cartesian position (only used for the compositing depth).
template <typename R>
inline KGHit<R> kg_trace(KGState<R> y, KGConst<R> c, KGParams<R> p, R camX, R camY, R camZ) {
    KGHit<R> hit;
    hit.type = KG_HIT_NONE; hit.r = hit.ph = hit.g = hit.dt = hit.mu = R(0);
    hit.dx = hit.dy = hit.dz = R(0); hit.depth = R(1e9); hit.steps = 0;
    const int wantPlane = p.diskOn | p.gridOn;
    int n = 0;
    for (; n < p.maxSteps; ++n) {
        const KGDeriv<R> D = kg_deriv(y, c);
        const R h = -kg_step_size(y, D.d, c, p);     // negative: backward in affine parameter
        const KGState<R> yn = kg_rk4(y, D.d, c, h);

        if (!(yn.r == yn.r) || !(yn.th == yn.th) || yn.r > R(1e30)) { hit.type = KG_HIT_HORIZON; break; }   // non-finite guard
        if (yn.r <= p.rStop || (yn.r < p.rDoom && yn.r < y.r)) { hit.type = KG_HIT_HORIZON; y = yn; break; }

        if (wantPlane) {
            const R c0 = KG_COS(y.th), c1 = KG_COS(yn.th);
            if (c0 * c1 < R(0)) {
                // Sub-step to the crossing, then one Newton correction on cos θ.
                const R f = c0 / (c0 - c1);
                KGState<R> yh = kg_rk4(y, D.d, c, h * f);
                {
                    const R ch = KG_COS(yh.th), sh = KG_SIN(yh.th);
                    const KGDeriv<R> Dh = kg_deriv(yh, c);
                    const R dcos = -sh * Dh.d.th;                   // d(cosθ)/dλ'
                    if (KG_ABS(dcos) > R(1e-12)) {
                        const R dl = -ch / dcos;
                        yh = kg_rk4(yh, Dh.d, c, dl);
                    }
                }
                const R rh = yh.r;
                int planeType = KG_HIT_NONE;
                if (p.diskOn && rh >= p.rIsco && rh <= p.rDiskOut) planeType = KG_HIT_DISK;
                else if (p.gridOn && rh > p.rh && rh <= p.rGridOut) planeType = KG_HIT_GRID;
                if (planeType != KG_HIT_NONE) {
                    hit.type = planeType;
                    hit.r = rh; hit.dt = -yh.t;
                    R gInv;
                    R s = KG_SIN(yh.th); if (KG_ABS(s) < R(1e-6)) s = R(1e-6);
                    hit.ph = yh.ph + (s > R(0) ? R(0) : R(3.14159265358979));   // θ outside [0,π] ⇒ φ + π
                    if (planeType == KG_HIT_DISK) {
                        const R Om = kg_omega_kepler(rh, c.a);
                        const R ut = kg_ut_kepler(rh, c.a);
                        gInv = ut * (c.E - Om * c.L);                 // locally measured energy in the fluid frame
                    } else {
                        R al, om, Sg, Dl, AA;
                        kg_zamo(rh, yh.th, c.a, al, om, Sg, Dl, AA);
                        gInv = (c.E - om * c.L) / al;                 // ZAMO frame energy
                    }
                    hit.g = R(1) / gInv;
                    const R Sig = rh * rh;                             // equatorial Σ
                    hit.mu = KG_ABS(yh.pth) / KG_SQRT(Sig) * hit.g;    // |p_(θ)| / E_loc
                    if (hit.mu > R(1)) hit.mu = R(1);
                    const R hx = rh * KG_COS(yh.ph) * (s > R(0) ? R(1) : R(-1));
                    const R hy = rh * KG_SIN(yh.ph) * (s > R(0) ? R(1) : R(-1));
                    const R ddx = hx - camX, ddy = hy - camY, ddz = -camZ;
                    hit.depth = KG_SQRT(ddx * ddx + ddy * ddy + ddz * ddz);
                    y = yh;
                    break;
                }
            }
        }

        if (yn.r > p.rEsc && yn.r > y.r) {
            hit.type = KG_HIT_SKY;
            // Forward-time photon velocity at the exit point, pseudo-Cartesian.
            const KGDeriv<R> Dn = kg_deriv(yn, c);
            const R s = KG_SIN(yn.th), co = KG_COS(yn.th);
            const R cp = KG_COS(yn.ph), sp = KG_SIN(yn.ph);
            const R vr = Dn.d.r, vt = Dn.d.th, vp = Dn.d.ph, rr = yn.r;
            R vx = vr * s * cp + rr * co * cp * vt - rr * s * sp * vp;
            R vy = vr * s * sp + rr * co * sp * vt + rr * s * cp * vp;
            R vz = vr * co - rr * s * vt;
            const R inv = R(1) / KG_SQRT(vx * vx + vy * vy + vz * vz + R(1e-30));
            hit.dx = -vx * inv; hit.dy = -vy * inv; hit.dz = -vz * inv;   // photon came FROM this direction
            hit.dt = -yn.t;
            y = yn;
            break;
        }
        y = yn;
    }
    if (hit.type == KG_HIT_NONE) hit.type = KG_HIT_HORIZON;   // trapped near the photon shell
    if (hit.type == KG_HIT_HORIZON) hit.depth = KG_SQRT(camX * camX + camY * camY + camZ * camZ);
    hit.steps = n;
    return hit;
}

// Build the arriving photon's state at a ZAMO camera from a unit viewing direction
// n = (n_r, n_θ, n_φ) in the ZAMO orthonormal frame (the photon moves along -n).
template <typename R>
inline void kg_camera_ray(R rc, R thc, R phc, R a, R nr, R nth, R nph, KG_THREAD KGState<R> &y, KG_THREAD KGConst<R> &c) {
    R al, om, Sg, Dl, AA;
    kg_zamo(rc, thc, a, al, om, Sg, Dl, AA);
    const R s = KG_SIN(thc);
    const R mr = -nr, mth = -nth, mph = -nph;         // photon spatial direction
    c.a = a;
    c.L = mph * KG_SQRT(AA / Sg) * s;
    if (KG_ABS(c.L) < R(1e-4)) c.L = R(0);
    c.E = al + om * c.L;
    y.r = rc; y.th = thc; y.ph = phc; y.t = R(0);
    y.pr = mr * KG_SQRT(Sg / Dl);
    y.pth = mth * KG_SQRT(Sg);
}

#endif // KERR_GEODESIC_H
