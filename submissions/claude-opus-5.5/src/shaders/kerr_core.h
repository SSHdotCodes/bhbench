// kerr_core.h — Kerr null-geodesic integrator, shared VERBATIM by the Metal shaders (float) and the C++
// validation suite (float and double).  NO include guard on purpose: the C++ side includes it once per
// precision inside different namespaces (see kerr_cpu.h).
//
// The includer must provide:  real, vec3, BH_THREAD (address-space qualifier for references; "thread" in
// Metal, empty in C++) and the usual math functions (sqrt, abs, min, max, sin, cos, acos, pow, dot,
// normalize) for those types.
//
// ------------------------------------------------------------------------------------------------------
// Physics & numerics
// ------------------------------------------------------------------------------------------------------
// Units G = c = M = 1, Boyer–Lindquist coordinates (t, r, θ, φ), signed spin a (|a| < 1).
//   Σ = r² + a²cos²θ,  Δ = r² − 2Mr + a²,  A = (r²+a²)² − a²Δ sin²θ.
// A photon has conserved energy E = −p_t, axial angular momentum L = p_φ and Carter constant Q.
// In Mino time λ (dλ = dσ/Σ, σ affine) Carter's equations separate completely:
//   (dr/dλ)² = R(r) = [E(r²+a²) − aL]² − Δ[(L − aE)² + Q]
//   (dθ/dλ)² = Θ(θ) = Q + a²E²cos²θ − L²cot²θ
//    dφ/dλ   = a[E(r²+a²) − aL]/Δ − aE + L/sin²θ
//    dt/dλ   = (r²+a²)[E(r²+a²) − aL]/Δ − a(aE sin²θ − L)
// We never integrate these first-order forms directly (they need ± bookkeeping at turning points and are
// singular at the poles).  Instead:
//
//  * Radial motion in ρ = 1/r.  (dρ/dλ)² = P(ρ) ≡ ρ⁴R(1/ρ) is a QUARTIC POLYNOMIAL:
//        P(ρ) = (E + Bρ²)² − Kρ²(1 − 2Mρ + a²ρ²),   B = a(aE − L),  K = (L − aE)² + Q
//    so ρ'' = P'(ρ)/2 exactly — smooth through radial turning points, regular at the horizon, and spatial
//    infinity is the finite point ρ = 0 (reached at finite λ), so the asymptotic sky direction is exact.
//
//  * Polar + θ-part of the azimuth as a particle on the unit sphere.  With n = (sinθ cosψ, sinθ sinψ, cosθ)
//    and ψ' = L/sin²θ, Carter's Θ equation is equivalent to motion on S² in the potential −½a²E²n_z²:
//        n'' = a²E² n_z ẑ − (|n'|² + a²E² n_z²) n,       |n'|² − a²E² n_z² = Q + L²  (conserved)
//    This has no coordinate singularity at the poles (rays passing over the spin axis are exact).
//
//  * Frame dragging: the r-dependent part of φ' is integrated separately,
//        φ_r' = aρ(2ME − aLρ)/(1 − 2Mρ + a²ρ²),     and φ = ψ + φ_r  (a rotation of n about the spin axis).
//
// Rays are traced BACKWARDS from the camera.  The second-order system is invariant under λ → −λ; the
// initial first derivatives and the first-order rates (φ_r', t') flip sign.  Integration is classic RK4 with
// an adaptive step bounded in angle swept, Δρ and Δφ_r, with a manifold projection that re-imposes |n| = 1
// and the conserved angular speed after every step.  Crossings of the equatorial plane / embedding surface /
// infinity are located to machine precision by regula falsi on RK4 sub-steps.
//
// Capture criterion: every spherical photon orbit has r ≥ r_ph⁺ (the prograde equatorial photon orbit), so a
// backward ray moving inward at r < r_ph⁺ has no radial turning point left before the horizon: it is
// captured.  This stops captured rays long before the horizon where φ_r' diverges (BL coordinates).
// ------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------
// Scalar Kerr quantities (M = 1)
// ---------------------------------------------------------------------------------------------
inline real kerr_horizon(real a) { return real(1) + sqrt(max(real(0), real(1) - a * a)); }

// Radius of the circular equatorial photon orbit; prograde = +1 (co-rotating with the hole) or −1.
inline real kerr_photon_orbit(real a, real prograde) {
    return real(2) * (real(1) + cos(real(2) / real(3) * acos(-prograde * abs(a))));
}

// Innermost stable circular orbit of a disk rotating in +φ; signed a (a < 0 ⇒ retrograde disk).
inline real kerr_isco(real a) {
    real a2 = a * a;
    real third = real(1) / real(3);
    real z1 = real(1) + pow(max(real(0), real(1) - a2), third) *
                        (pow(max(real(0), real(1) + a), third) + pow(max(real(0), real(1) - a), third));
    real z2 = sqrt(real(3) * a2 + z1 * z1);
    real sgn = a > real(0) ? real(1) : (a < real(0) ? real(-1) : real(0));
    return real(3) + z2 - sgn * sqrt(max(real(0), (real(3) - z1) * (real(3) + z1 + real(2) * z2)));
}

// Circular equatorial geodesic orbiting in +φ: angular velocity dφ/dt and u^t.
inline real kerr_orbit_omega(real a, real r) { return real(1) / (r * sqrt(r) + a); }
inline real kerr_orbit_ut(real a, real r) {
    real x = sqrt(r), r32 = r * x;
    return (r32 + a) / sqrt(max(r32 * (r32 - real(3) * x + real(2) * a), real(1e-20)));
}

// ---------------------------------------------------------------------------------------------
// Local frame of the camera: ZAMO (locally non-rotating observer) plus an optional boost
// ---------------------------------------------------------------------------------------------
struct CamFrame {
    vec3 right, up, fwd;   // camera axes, components on the ZAMO triad (e_r, e_θ, e_φ)
    vec3 beta;             // camera 3-velocity relative to the ZAMO (same triad), |beta| < 1
    real gamma;            // 1/sqrt(1 − beta²)
    real r, sinT, cosT, sinP, cosP;
    real Sigma, Delta, Akerr, alpha, omega, varpi;   // α: lapse, ω: frame-dragging rate, ϖ = sqrt(g_φφ)
};

inline void kerr_frame_metric(real a, real m, real r, real sinT, real cosT, BH_THREAD CamFrame& c) {
    real a2 = a * a, r2 = r * r;
    c.r = r;
    c.sinT = sinT;
    c.cosT = cosT;
    c.Sigma = r2 + a2 * cosT * cosT;
    c.Delta = r2 - real(2) * m * r + a2;
    c.Akerr = (r2 + a2) * (r2 + a2) - a2 * c.Delta * sinT * sinT;
    c.alpha = sqrt(max(c.Sigma * c.Delta / c.Akerr, real(0)));
    c.omega = real(2) * m * a * r / c.Akerr;
    c.varpi = sqrt(c.Akerr / c.Sigma) * sinT;
}

// ---------------------------------------------------------------------------------------------
// Ray state
// ---------------------------------------------------------------------------------------------
struct KerrRay {       // constants of motion of the physical (future-directed) photon; camera-frame energy = 1
    real a, m;         // spin and mass used for the ray paths (m = 0: flat space)
    real E, L, Q;
    real cA, cB, cK;   // P(ρ) = (cA + cB ρ²)² − cK ρ² (1 − 2mρ + a²ρ²)
    real a2E2;         // a²E²: strength of the angular potential
    real vn2;          // Q + L²: conserved |n'|² − a²E² n_z²
};

struct RayState {
    real rho, drho;    // 1/r and dρ/dλ (backward Mino time)
    vec3 n, dn;        // angular position on the unit sphere (azimuth ψ, without frame dragging) and n'
    real phir;         // accumulated frame-dragging azimuth φ_r
    real tdel;         // accumulated coordinate-time delay t_camera − t  (≥ 0)
};

struct RayDeriv {
    real drho, ddrho;
    vec3 dn, ddn;
    real dphir, dtdel;
};

// Initialise a backward ray for the pixel direction `dir` (camera frame: x right, y up, z forward).
inline void kerr_init_ray(BH_THREAD const CamFrame& c, real a, real m, vec3 dir,
                          BH_THREAD KerrRay& k, BH_THREAD RayState& s) {
    vec3 d = normalize(c.right * dir.x + c.up * dir.y + c.fwd * dir.z);
    // The photon that reaches the camera travels along −d with unit energy in the camera frame.
    vec3 pv = -d;
    real p0 = real(1);
    real b2 = dot(c.beta, c.beta);
    if (b2 > real(1e-12)) {   // Lorentz boost camera frame -> ZAMO frame (aberration + Doppler)
        real bp = dot(c.beta, pv);
        real p0z = c.gamma * (p0 + bp);
        pv = pv + c.beta * ((c.gamma - real(1)) * bp / b2 + c.gamma * p0);
        p0 = p0z;
    }
    real pr = pv.x, pth = pv.y, pph = pv.z;   // ZAMO tetrad components p^(r), p^(θ), p^(φ); p^(t) = p0

    k.a = a;
    k.m = m;
    k.L = c.varpi * pph;
    k.E = c.alpha * p0 + c.omega * k.L;
    k.Q = c.Sigma * pth * pth + c.cosT * c.cosT * ((c.Akerr / c.Sigma) * pph * pph - a * a * k.E * k.E);
    k.cA = k.E;
    k.cB = a * (a * k.E - k.L);
    real lae = k.L - a * k.E;
    k.cK = lae * lae + k.Q;
    k.a2E2 = a * a * k.E * k.E;
    k.vn2 = k.Q + k.L * k.L;

    // Backward Mino-time derivatives: r'_b = −sqrt(ΣΔ) p^(r),  θ'_b = −sqrt(Σ) p^(θ),  sinθ ψ'_b = −sqrt(A/Σ) p^(φ)
    s.rho = real(1) / c.r;
    s.drho = s.rho * s.rho * sqrt(c.Sigma * c.Delta) * pr;
    s.n = vec3(c.sinT * c.cosP, c.sinT * c.sinP, c.cosT);
    vec3 eth = vec3(c.cosT * c.cosP, c.cosT * c.sinP, -c.sinT);
    vec3 eph = vec3(-c.sinP, c.cosP, real(0));
    s.dn = eth * (-sqrt(c.Sigma) * pth) + eph * (-sqrt(c.Akerr / c.Sigma) * pph);
    s.phir = real(0);
    s.tdel = real(0);
}

inline RayDeriv kerr_deriv(BH_THREAD const KerrRay& k, BH_THREAD const RayState& s, bool wantTime) {
    RayDeriv d;
    real rho = s.rho, rho2 = rho * rho, a2 = k.a * k.a;
    real ab = k.cA + k.cB * rho2;
    d.drho = s.drho;
    d.ddrho = real(2) * k.cB * rho * ab - k.cK * rho * (real(1) - real(3) * k.m * rho + real(2) * a2 * rho2);
    real nz = s.n.z;
    real v2 = dot(s.dn, s.dn);
    d.dn = s.dn;
    d.ddn = vec3(real(0), real(0), k.a2E2 * nz) - s.n * (v2 + k.a2E2 * nz * nz);
    real D = real(1) - real(2) * k.m * rho + a2 * rho2;   // = ρ²Δ
    real invD = real(1) / D;
    d.dphir = -k.a * rho * (real(2) * k.m * k.E - k.a * k.L * rho) * invD;
    if (wantTime && rho > real(1e-3)) {
        d.dtdel = (real(1) + a2 * rho2) * ab * invD / rho2 - k.a * (k.a * k.E * (real(1) - nz * nz) - k.L);
    } else {
        d.dtdel = real(0);
    }
    return d;
}

inline RayState kerr_advance(BH_THREAD const RayState& s, BH_THREAD const RayDeriv& d, real h) {
    RayState o;
    o.rho = s.rho + h * d.drho;
    o.drho = s.drho + h * d.ddrho;
    o.n = s.n + d.dn * h;
    o.dn = s.dn + d.ddn * h;
    o.phir = s.phir + h * d.dphir;
    o.tdel = s.tdel + h * d.dtdel;
    return o;
}

inline RayState kerr_rk4(BH_THREAD const KerrRay& k, BH_THREAD const RayState& s, real h, bool wantTime) {
    RayDeriv k1 = kerr_deriv(k, s, wantTime);
    RayState t = kerr_advance(s, k1, h * real(0.5));
    RayDeriv k2 = kerr_deriv(k, t, wantTime);
    t = kerr_advance(s, k2, h * real(0.5));
    RayDeriv k3 = kerr_deriv(k, t, wantTime);
    t = kerr_advance(s, k3, h);
    RayDeriv k4 = kerr_deriv(k, t, wantTime);
    real h6 = h / real(6);
    RayState o;
    o.rho = s.rho + h6 * (k1.drho + real(2) * (k2.drho + k3.drho) + k4.drho);
    o.drho = s.drho + h6 * (k1.ddrho + real(2) * (k2.ddrho + k3.ddrho) + k4.ddrho);
    o.n = s.n + (k1.dn + (k2.dn + k3.dn) * real(2) + k4.dn) * h6;
    o.dn = s.dn + (k1.ddn + (k2.ddn + k3.ddn) * real(2) + k4.ddn) * h6;
    o.phir = s.phir + h6 * (k1.dphir + real(2) * (k2.dphir + k3.dphir) + k4.dphir);
    o.tdel = s.tdel + h6 * (k1.dtdel + real(2) * (k2.dtdel + k3.dtdel) + k4.dtdel);
    // Manifold projection: |n| = 1, n' tangent, and the conserved angular speed |n'|² = Q + L² + a²E²n_z².
    o.n = normalize(o.n);
    o.dn = o.dn - o.n * dot(o.dn, o.n);
    real target = k.vn2 + k.a2E2 * o.n.z * o.n.z;
    real cur = dot(o.dn, o.dn);
    if (target > real(0) && cur > real(0)) o.dn = o.dn * sqrt(target / cur);
    return o;
}

inline vec3 kerr_rotz(vec3 n, real ang) {
    real c = cos(ang), s = sin(ang);
    return vec3(c * n.x - s * n.y, s * n.x + c * n.y, n.z);
}

// ---------------------------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------------------------
struct TraceCfg {
    real rhoHorizon;    // stop (captured) when ρ ≥ this
    real rhoCapture;    // stop (captured) when moving inward with ρ > this (1/r_ph⁺); huge = off
    real rhoEscape;     // escaped when ρ ≤ this (0 ⇒ exactly at infinity)
    real invStepAng, invStepRho, invStepPhi;   // step control: 1/(max Δangle), 1/(max Δρ), 1/(max Δφ_r)
    real funnelRho;     // test embedding-surface crossings only while ρ > funnelRho (0 ⇒ no surface)
    int maxSteps;
    int wantTime;       // integrate the light-travel delay (needed for the animated disk)
};

struct TraceResult {
    int status;         // BH_TRACE_*
    int steps;
    int crossings;      // equatorial-plane crossings (image order + 1 at a disk hit)
    vec3 dir;           // escaped: asymptotic direction in BL Cartesian axes
    real phir, tdel, rho;
};

inline real kerr_step_size(BH_THREAD const KerrRay& k, BH_THREAD const RayState& s, BH_THREAD const TraceCfg& cfg) {
    real vn = sqrt(dot(s.dn, s.dn));
    real vr = abs(s.drho);
    real rho = s.rho;
    real D = real(1) - real(2) * k.m * rho + k.a * k.a * rho * rho;
    real vphi = abs(k.a * rho * (real(2) * k.m * k.E - k.a * k.L * rho)) / max(D, real(1e-6));
    real rate = vn * cfg.invStepAng + vr * cfg.invStepRho + vphi * cfg.invStepPhi;
    return real(1) / max(rate, real(1e-6));
}

// Event functions whose sign change marks a crossing: 0 = equatorial plane, 1 = escape, 2 = embedding surface.
template <typename V>
inline real kerr_event_value(int which, BH_THREAD const RayState& s, BH_THREAD V& vis, real rhoEsc) {
    if (which == 0) return s.n.z;
    if (which == 1) return s.rho - rhoEsc;
    real r = real(1) / max(s.rho, real(1e-6));
    real nz = s.n.z;
    return nz * r - vis.funnelHeight(sqrt(max(real(0), real(1) - nz * nz)) * r);
}

// Locate the crossing inside the step [s, s+h] (f0 = f(s), f1 = f(s+h), opposite signs) with the Illinois
// variant of regula falsi; every evaluation is a fresh RK4 sub-step from s, so the result has RK4 accuracy.
template <typename V>
inline RayState kerr_refine(int which, BH_THREAD const KerrRay& k, BH_THREAD const RayState& s, real f0, real f1,
                            real h, bool wantTime, real rhoEsc, BH_THREAD V& vis, BH_THREAD real& hHit) {
    real h0 = real(0), h1 = h;
    RayState c = s;
    int side = 0;
    for (int it = 0; it < 4; ++it) {
        real hm = (h0 * f1 - h1 * f0) / (f1 - f0);
        c = kerr_rk4(k, s, hm, wantTime);
        real fm = kerr_event_value(which, c, vis, rhoEsc);
        hHit = hm;
        if (fm * f1 > real(0)) {
            h1 = hm; f1 = fm;
            if (side == -1) f0 *= real(0.5);
            side = -1;
        } else {
            h0 = hm; f0 = fm;
            if (side == 1) f1 *= real(0.5);
            side = 1;
        }
    }
    return c;
}

// Trace one backward ray.  The visitor receives every event in order along the ray and may stop the ray:
//   real funnelHeight(real R)                                      embedding surface height Z(R)
//   bool onPlane(k, state, crossingIndex, pathLength, goingDown)   equatorial plane crossing
//   bool onFunnel(k, state, prevState, nextState, pathLength)      embedding surface crossing
template <typename V>
inline TraceResult kerr_trace(BH_THREAD const KerrRay& k, RayState s, BH_THREAD const TraceCfg& cfg, BH_THREAD V& vis) {
    TraceResult tr;
    tr.status = BH_TRACE_MAXSTEPS;
    tr.steps = 0;
    tr.crossings = 0;
    tr.dir = s.n;
    tr.phir = real(0);
    tr.tdel = real(0);
    tr.rho = s.rho;
    bool wantT = cfg.wantTime != 0;
    bool funnel = cfg.funnelRho > real(0);
    real path = real(0);   // approximate Euclidean path length (for anti-aliasing footprints)
    real fPrev = funnel ? kerr_event_value(2, s, vis, cfg.rhoEscape) : real(0);
    bool fValid = funnel;

    for (int i = 0; i < cfg.maxSteps; ++i) {
        tr.steps = i + 1;
        if (s.rho >= cfg.rhoHorizon || (s.drho > real(0) && s.rho > cfg.rhoCapture)) {
            tr.status = BH_TRACE_CAPTURED;
            tr.rho = s.rho;
            break;
        }
        real h = kerr_step_size(k, s, cfg);
        RayState ns = kerr_rk4(k, s, h, wantT);

        // --- events inside this step
        real hP = real(-1), hF = real(-1);
        RayState cP = ns, cF = ns;
        if (s.n.z * ns.n.z < real(0)) {
            cP = kerr_refine(0, k, s, s.n.z, ns.n.z, h, wantT, cfg.rhoEscape, vis, hP);
            if (cP.rho <= cfg.rhoEscape) hP = real(-1);   // "crossing" beyond infinity: not physical
        }
        real fNew = fPrev;
        bool nearNow = funnel && (s.rho > cfg.funnelRho || ns.rho > cfg.funnelRho);   // only near the surface
        if (nearNow) {
            if (!fValid) fPrev = kerr_event_value(2, s, vis, cfg.rhoEscape);
            fNew = kerr_event_value(2, ns, vis, cfg.rhoEscape);
            if (fPrev * fNew < real(0)) {
                cF = kerr_refine(2, k, s, fPrev, fNew, h, wantT, cfg.rhoEscape, vis, hF);
                if (cF.rho <= cfg.rhoEscape) hF = real(-1);
            }
        }
        int first = -1, second = -1;
        if (hP >= real(0) && hF >= real(0)) {
            first = hP <= hF ? 0 : 2;
            second = hP <= hF ? 2 : 0;
        } else if (hP >= real(0)) {
            first = 0;
        } else if (hF >= real(0)) {
            first = 2;
        }
        bool stop = false;
        for (int e = 0; e < 2; ++e) {
            int ev = e == 0 ? first : second;
            if (ev < 0) break;
            if (ev == 0) {
                tr.crossings += 1;
                stop = vis.onPlane(k, cP, tr.crossings, path, s.n.z > real(0));
                if (stop) { tr.rho = cP.rho; tr.tdel = cP.tdel; tr.phir = cP.phir; }
            } else {
                stop = vis.onFunnel(k, cF, s, ns, path);
                if (stop) { tr.rho = cF.rho; tr.tdel = cF.tdel; tr.phir = cF.phir; }
            }
            if (stop) break;
        }
        if (stop) {
            tr.status = BH_TRACE_ABSORBED;
            break;
        }

        // --- escape to infinity (or to the requested finite radius)
        if (ns.rho <= cfg.rhoEscape) {
            real hE = h;
            RayState c = kerr_refine(1, k, s, s.rho - cfg.rhoEscape, ns.rho - cfg.rhoEscape, h, wantT,
                                     cfg.rhoEscape, vis, hE);
            tr.status = BH_TRACE_ESCAPED;
            tr.dir = kerr_rotz(c.n, c.phir);
            tr.phir = c.phir;
            tr.tdel = c.tdel;
            tr.rho = c.rho;
            break;
        }
        real rm = real(2) / (s.rho + ns.rho);
        path += k.E * rm * rm * h;   // dσ = Σ dλ ≈ r² dλ, |dx/dσ| ≈ E
        s = ns;
        fPrev = fNew;
        fValid = nearNow;
    }
    return tr;
}

// ---------------------------------------------------------------------------------------------
// Analytic shadow boundary (Bardeen 1973): the capture region in the (ξ = L/E, η = Q/E²) plane is bounded by
// the constants of the unstable spherical photon orbits
//     ξ(r) = −(r³ − 3r² + a²r + a²) / (a(r − 1)),    η(r) = −r³(r³ − 6r² + 9r − 4a²) / (a²(r − 1)²)
// for r between the prograde and retrograde equatorial photon orbits.  Returns < 0 inside (captured).
// ---------------------------------------------------------------------------------------------
inline real kerr_xi_crit(real a, real r) { return -(r * r * r - real(3) * r * r + a * a * r + a * a) / (a * (r - real(1))); }
inline real kerr_eta_crit(real a, real r) {
    real rm1 = r - real(1);
    return -r * r * r * (r * r * r - real(6) * r * r + real(9) * r - real(4) * a * a) / (a * a * rm1 * rm1);
}
inline real kerr_shadow_fn(real a, real xi, real eta) {
    if (a < real(0)) { a = -a; xi = -xi; }
    if (a < real(1e-3)) return xi * xi + eta - real(27);
    real r0 = kerr_photon_orbit(a, real(1)), r1 = kerr_photon_orbit(a, real(-1));
    real x0 = kerr_xi_crit(a, r0), x1 = kerr_xi_crit(a, r1);   // x0 > x1
    if (xi >= x0) return max(eta, real(0)) + (xi - x0) * real(4);
    if (xi <= x1) return max(eta, real(0)) + (x1 - xi) * real(4);
    real lo = r0, hi = r1;
    for (int i = 0; i < 40; ++i) {
        real mid = real(0.5) * (lo + hi);
        if (kerr_xi_crit(a, mid) > xi) lo = mid; else hi = mid;
    }
    return eta - kerr_eta_crit(a, real(0.5) * (lo + hi));
}
