#version 300 es
precision highp float;
precision highp int;
struct RTUniforms { vec4 cam; vec4 bh; vec4 view; vec4 jit; vec4 disk; vec4 opt; vec4 sky; vec4 misc; };
uniform RTUniforms U;
uniform sampler2D diskTab, bb;
in vec2 vUV; out vec4 fragColor;

struct Ray {
    float u, vu;    // u = 1/r and du/ds
    float mu, vmu;  // cos(theta) and dmu/ds
    float w, vw;    // w = sin^2(theta) tracked as its own variable (accurate at the poles where 1 - mu^2 is not)
    float phi;      // azimuth
    float that;     // regularised coordinate time  (t = that - total variation of r)
    float L, Q;     // constants of motion of the (forward) photon, E = 1
};


struct TraceCfg {
    float a;          // spin
    float r_plus;     // outer horizon
    float r_isco;     // inner edge of the disk
    float r_out;      // outer edge of the disk
    float r_far;      // escape radius (flat-space extrapolation beyond it)
    float c;          // step-size coefficient (smaller = more accurate)
    int max_steps;
    int disk;     // 1: intersect the accretion disk
    float disk_h;     // half-thickness of the disk photosphere as |cos theta| (aspect ratio H/r); >= 1e-3 enforced
    float halo;       // amplitude of the optically thin hot corona ("halo"); 0 disables
    float nu_cam;     // camera-measured photon energy per unit energy at infinity (for the corona's redshift)
};


struct TraceOut {
    int status;     // 0 = captured by horizon, 1 = escaped to infinity, 2 = hit the disk, 3 = step limit
    int steps;
    int crossings;  // equatorial-plane crossings before the terminal event (image order n)
    int surf;       // which disk surface was hit: 0 top face, 1 bottom face, 2 outer rim, 3 inner wall
    float r_hit, phi_hit, t_hit, vmu_hit;  // disk hit (t_hit = coordinate time offset, negative = in the past)
    float L;            // photon angular momentum (per unit energy)
    float dx, dy, dz;   // asymptotic direction of the ray (unit vector) if escaped
    float halo_e;       // corona: integral of  j g^3 Sigma dlambda  along the ray (observed intensity, arbitrary units)
    float halo_g;       // corona: emission-weighted mean frequency ratio g
};

// ---------------------------------------------------------------------------------------------
// Geodesic equations (second order form in u = 1/r)
// ---------------------------------------------------------------------------------------------

void rhs(float a, Ray  y, inout Ray d)
{
    float u = y.u, u2 = u * u, a2 = a * a;
    float s = a2 - a * y.L;
    float K = y.Q + (y.L - a) * (y.L - a);
    float mu = y.mu;
    float om = max(y.w, float(1e-14));            // sin^2(theta), accurate arbitrarily close to the polar axis
    d.u = y.vu;
    d.vu = (float(2) * s - K) * u + float(3) * K * u2 + float(2) * (s * s - a2 * K) * u2 * u;
    d.mu = y.vmu;
    d.vmu = -(y.Q + y.L * y.L - a2) * mu - float(2) * a2 * mu * mu * mu;
    // w = 1 - mu^2:  w'' = -2 M(w) + 2 (1 - w) M_w(w),   M(w) = -L^2 + (Q + L^2 + a^2) w - a^2 w^2
    {
        float q = y.Q + y.L * y.L + a2;
        float Mw = -y.L * y.L + q * y.w - a2 * y.w * y.w;
        d.w = y.vw;
        d.vw = float(2) * ((float(1) - y.w) * (q - float(2) * a2 * y.w) - Mw);
    }
    float D = float(1) - float(2) * u + a2 * u2;           // Delta / r^2
    float invD = float(1) / D;
    float Pr2 = float(1) + s * u2;                      // P / r^2
    d.phi = -(a * Pr2 * invD - a + y.L / om);         // backward parameter: sign flip
    float tdot = -((float(1) / u2 + a2) * Pr2 * invD + a * (y.L - a * om));
    float vr = -y.vu / u2;                          // dr/ds
    d.that = tdot + abs(vr);                         // regularised: removes the O(r^2) light-cone growth
    d.L = float(0);
    d.Q = float(0);
}


Ray axpy(Ray  y, Ray  k, float h)
{
    Ray o = y;
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


void rk4(float a, inout Ray y, float h)
{
    Ray k1, k2, k3, k4;
    rhs(a, y, k1);
    Ray yt = axpy(y, k1, float(0.5) * h);
    rhs(a, yt, k2);
    yt = axpy(y, k2, float(0.5) * h);
    rhs(a, yt, k3);
    yt = axpy(y, k3, h);
    rhs(a, yt, k4);
    float h6 = h / float(6);
    y.u += h6 * (k1.u + float(2) * (k2.u + k3.u) + k4.u);
    y.vu += h6 * (k1.vu + float(2) * (k2.vu + k3.vu) + k4.vu);
    y.mu += h6 * (k1.mu + float(2) * (k2.mu + k3.mu) + k4.mu);
    y.vmu += h6 * (k1.vmu + float(2) * (k2.vmu + k3.vmu) + k4.vmu);
    y.w += h6 * (k1.w + float(2) * (k2.w + k3.w) + k4.w);
    y.vw += h6 * (k1.vw + float(2) * (k2.vw + k3.vw) + k4.vw);
    y.phi += h6 * (k1.phi + float(2) * (k2.phi + k3.phi) + k4.phi);
    y.that += h6 * (k1.that + float(2) * (k2.that + k3.that) + k4.that);
}

// Radial and polar "potentials" (diagnostics / initial conditions).

float potential_F(float a, float u, float L, float Q)          // (du/dl)^2 = R/r^4
{
    float s = a * a - a * L;
    float K = Q + (L - a) * (L - a);
    return (float(1) + s * u * u) * (float(1) + s * u * u) - K * u * u * (float(1) - float(2) * u + a * a * u * u);
}

float potential_R(float a, float r, float L, float Q)
{
    float P = r * r + a * a - a * L;
    float K = Q + (L - a) * (L - a);
    return P * P - (r * r - float(2) * r + a * a) * K;
}

float potential_M(float a, float mu, float L, float Q)
{
    float m2 = mu * mu;
    return Q - (Q + L * L - a * a) * m2 - a * a * m2 * m2;
}

// Adaptive step.  Base: phase increment c of the polar/radial oscillation (w = its angular frequency in Mino
// time); limited so that u changes by at most 70% per step (keeps the regularised time accurate and never
// overshoots u = 0), by 0.08 in absolute terms (resolves the strong-field region), and near the polar axis.

float step_size(float c, Ray  y)
{
    float w = sqrt(max(y.Q + y.L * y.L, float(0))) + float(1);
    float h = c / w;
    float av = abs(y.vu) + float(1e-6);
    h = min(h, float(0.7) * y.u / av);
    h = min(h, float(0.08) / av);
    // resolve passages close to the polar axis, where phi swings by up to pi within a tiny lambda interval
    // (the constraint tightens as sin(theta)^3 below ~0.15 rad and relaxes quadratically above it)
    float sth = sqrt(max(y.w, float(1e-10)));
    float rel = max(float(1), (sth / float(0.15)) * (sth / float(0.15)));
    h = min(h, float(0.07) * sth * rel / (sqrt(max(y.Q + y.L * y.L, float(0))) + float(0.05)));
    return h;
}

// Constraint projection.  RK4 conserves the first integrals (du/dl)^2 = F(u), (dmu/dl)^2 = M(mu) only to O(h^4);
// near a turning point that error is comparable to the (tiny) value of the potential itself, and for rays that skim
// the polar axis (w = sin^2 theta ~ L^2/q ~ 1e-7) it destroys the azimuth.  After every step we therefore rescale the
// velocities back onto the constraint shell (signs are kept).  Turning points become exact; what remains is a small
// phase (timing) error.  Near the equator mu is the accurate variable, near the poles w = sin^2(theta) is;
// the two are re-synchronised in both directions.

float sgn_(float x) { return x < float(0) ? float(-1) : float(1); }


void project_state(float a, inout Ray y)
{
    float a2 = a * a;
    float F = potential_F(a, y.u, y.L, y.Q);
    y.vu = sgn_(y.vu) * sqrt(max(F, float(0)));
    if (abs(y.mu) < float(0.7)) {
        float M = potential_M(a, y.mu, y.L, y.Q);
        y.vmu = sgn_(y.vmu) * sqrt(max(M, float(0)));
        y.w = float(1) - y.mu * y.mu;
        y.vw = -float(2) * y.mu * y.vmu;
    } else {
        float q = y.Q + y.L * y.L + a2;
        y.w = min(max(y.w, float(1e-30)), float(1));
        float Mw = -y.L * y.L + q * y.w - a2 * y.w * y.w;
        y.vw = sgn_(y.vw) * sqrt(max(float(4) * (float(1) - y.w) * Mw, float(0)));
        float m = sgn_(y.mu) * sqrt(max(float(1) - y.w, float(0)));
        y.mu = m;
        y.vmu = (m != float(0)) ? -y.vw / (float(2) * m) : float(0);
    }
}

// One integrator step: RK4 followed by the constraint projection.

void step_ray(float a, inout Ray y, float h)
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

bool init_ray_zamo(float a, float r0, float theta0, float phi0, float nr, float nth, float nph,
                             inout Ray y, inout float nu_cam)
{
    float mu = cos(theta0);
    float s = max(sin(theta0), float(1e-4));
    float s2 = s * s;
    float r2 = r0 * r0, a2 = a * a;
    float Sigma = r2 + a2 * mu * mu;
    float Delta = r2 - float(2) * r0 + a2;
    float A = (r2 + a2) * (r2 + a2) - a2 * Delta * s2;
    float sg = sqrt(A * s2 / Sigma);       // sqrt(g_phiphi)
    float lapse = sqrt(Sigma * Delta / A);
    float omega = float(2) * a * r0 / A;
    float den = lapse - omega * sg * nph;
    if (!(den > float(1e-5))) return false;
    float Ez = float(1) / den;
    float pth = sqrt(Sigma) * Ez * nth;    // dtheta/ds
    float vr = sqrt(Sigma * Delta) * Ez * nr;
    y.u = float(1) / r0;
    y.vu = -y.u * y.u * vr;
    y.mu = mu;
    y.vmu = -s * pth;
    y.w = s2;
    y.vw = -float(2) * mu * y.vmu;
    y.phi = phi0;
    y.that = float(0);
    y.L = -sg * Ez * nph;
    // Rays in the camera's meridian plane have L = 0 exactly: they pass THROUGH the polar axis, where phi jumps by
    // pi (0/0 in dphi/dl = L/sin^2 theta).  The map is continuous in L, so nudge |L| to >= 5e-4 (negligible: a
    // 1e-5 rad camera-angle change) and the jump is then integrated correctly and matches both neighbours.
    if (abs(y.L) < float(5e-4)) y.L = (y.L < float(0)) ? float(-5e-4) : float(5e-4);
    y.Q = pth * pth + mu * mu * (y.L * y.L / s2 - a2);
    nu_cam = Ez;
    return true;
}

// Ray from explicit constants of motion (used by validation tests).  Starts moving inward.

bool init_ray_constants(float a, float r0, float mu0, float L, float Q, float vmu_sign, inout Ray y)
{
    float u0 = float(1) / r0;
    float F = potential_F(a, u0, L, Q);
    float M = potential_M(a, mu0, L, Q);
    if (F < float(0) || M < float(0)) return false;
    y.u = u0;
    y.vu = sqrt(F);
    y.mu = mu0;
    y.vmu = vmu_sign * sqrt(M);
    y.w = float(1) - mu0 * mu0;
    y.vw = -float(2) * mu0 * y.vmu;
    y.phi = float(0);
    y.that = float(0);
    y.L = L;
    y.Q = Q;
    return true;
}

// Asymptotic direction (unit vector, world frame with z = spin axis) from the ray state at large r.

void escape_direction(float a, Ray  y, inout float dx, inout float dy, inout float dz)
{
    Ray d;
    rhs(a, y, d);
    float r = float(1) / y.u;
    float st = sqrt(max(y.w, float(1e-14)));
    float ct = y.mu;
    float cp = cos(y.phi), sp = sin(y.phi);
    // components of the coordinate velocity in the local orthonormal frame, scaled by u^2 to stay O(1)
    float ar = -d.u;                               // (dr/ds) u^2 = -du/ds
    float at = (-d.mu / st) * y.u;                 // r dtheta/ds * u^2 = dtheta/ds * u
    float ap = st * d.phi * y.u;                   // r sin(theta) dphi/ds * u^2

    float n = sqrt(ar * ar + at * at + ap * ap);
    ar /= n; at /= n; ap /= n;
    dx = ar * st * cp + at * ct * cp - ap * sp;
    dy = ar * st * sp + at * ct * sp + ap * cp;
    dz = ar * ct - at * st;
}

// Cubic Hermite interpolation on [0,1] with slopes wrt u.

float hermite(float p0, float p1, float m0, float m1, float x)
{
    float x2 = x * x, x3 = x2 * x;
    return (float(2) * x3 - float(3) * x2 + float(1)) * p0 + (x3 - float(2) * x2 + x) * m0 +
           (float(-2) * x3 + float(3) * x2) * p1 + (x3 - x2) * m1;
}

float hermite_dx(float p0, float p1, float m0, float m1, float x)
{
    float x2 = x * x;
    return (float(6) * x2 - float(6) * x) * p0 + (float(3) * x2 - float(4) * x + float(1)) * m0 +
           (float(-6) * x2 + float(6) * x) * p1 + (float(3) * x2 - float(2) * x) * m1;
}

// ---------------------------------------------------------------------------------------------
// Kerr circular equatorial orbits (prograde), M = 1
// ---------------------------------------------------------------------------------------------

float horizon_radius(float a) { return float(1) + sqrt(max(float(1) - a * a, float(0))); }


float isco_radius(float a)
{
    float a2 = a * a;
    float z1 = float(1) + pow(float(1) - a2, float(1) / float(3)) * (pow(float(1) + a, float(1) / float(3)) + pow(max(float(1) - a, float(0)), float(1) / float(3)));
    float z2 = sqrt(float(3) * a2 + z1 * z1);
    return float(3) + z2 - sqrt((float(3) - z1) * (float(3) + z1 + float(2) * z2));
}

// Keplerian angular velocity  Omega = 1/(r^{3/2} + a)

float omega_kepler(float r, float a) { return float(1) / (r * sqrt(r) + a); }

// u^t of the circular orbit  u^t = (r^{3/2} + a) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} + 2a))

float ut_kepler(float r, float a)
{
    float sr = sqrt(r);
    float r15 = r * sr;
    return (r15 + a) / (sr * sqrt(sr) * sqrt(max(r15 - float(3) * sr + float(2) * a, float(1e-12))));
}

// Frequency ratio g = nu_obs/nu_emit for gas on a circular orbit seen by a photon (E=1, angular momentum L)
// arriving with camera-measured energy nu_cam.

float disk_redshift(float r, float a, float L, float nu_cam)
{
    return nu_cam / (ut_kepler(r, a) * (float(1) - omega_kepler(r, a) * L));
}

// Optically thin hot corona around the hole: a fat torus-like atmosphere, emissivity j ~ r^-5/2 exp(-mu^2/2 sigma^2),
// co-rotating on Keplerian orbits outside the ISCO and with the local frame-dragging (ZAMO) inside it.  The observed
// intensity from optically thin gas is  I = int j g^3 d(affine parameter)  (j/nu^2 and I/nu^3 are Lorentz invariants); the
// affine parameter is Sigma dlambda.  Returns j g^3 Sigma and stores g.

float halo_integrand(TraceCfg  cfg, float L, float u, float mu, inout float g_out)
{
    g_out = float(0);
    if (u < float(1) / float(40) || abs(mu) > float(0.85)) return float(0);
    float r = float(1) / u, a = cfg.a;
    float sig = float(0.30);
    float prof = exp(-mu * mu / (float(2) * sig * sig));
    float rf = r / float(26);
    float j = cfg.halo * prof * u * u * sqrt(u) * exp(-rf * rf);
    float Sigma = r * r + a * a * mu * mu;
    float nu_em;
    if (r >= cfg.r_isco) {
        nu_em = ut_kepler(r, a) * (float(1) - omega_kepler(r, a) * L);
    } else {
        float Delta = r * r - float(2) * r + a * a;
        float s2 = max(float(1) - mu * mu, float(1e-4));
        float A = (r * r + a * a) * (r * r + a * a) - a * a * Delta * s2;
        float lapse = sqrt(max(Sigma * Delta / A, float(1e-8)));
        nu_em = (float(1) - float(2) * a * r / A * L) / lapse;                       // ZAMO
    }
    if (!(nu_em > float(1e-3))) return float(0);
    float g = min(cfg.nu_cam / nu_em, float(6));
    g_out = g;
    return j * g * g * g * Sigma;
}

// ---------------------------------------------------------------------------------------------
// Trace one ray until it is captured, escapes, or hits the thin equatorial disk r in [r_isco, r_out].
// ---------------------------------------------------------------------------------------------

void trace_ray(TraceCfg  cfg, Ray y, inout TraceOut result)
{
    result.status = 3;
    result.steps = 0;
    result.crossings = 0;
    result.surf = 0;
    result.r_hit = result.phi_hit = result.t_hit = result.vmu_hit = float(0);
    result.L = y.L;
    result.dx = result.dy = result.dz = float(0);
    result.halo_e = result.halo_g = float(0);
    float halo_eg = float(0);
    float hw0 = float(0), hg0 = float(0);
    if (cfg.halo > float(0)) hw0 = halo_integrand(cfg, y.L, y.u, y.mu, hg0);
    float a = cfg.a;
    float u_cap = float(1) / (cfg.r_plus + float(0.01));
    float u_far = float(1) / cfg.r_far;
    float tv = float(0);  // accumulated |delta r|

    for (int i = 0; i < cfg.max_steps; ++i) {
        float h = step_size(cfg.c, y);
        Ray n = y;
        step_ray(a, n, h);
        result.steps = i + 1;

        if (n.u > u_cap) {
            result.status = 0;
            if (cfg.halo > float(0)) {   // finish the corona integral to the horizon (heavily redshifted: small)
                float hg1;
                float hw1 = halo_integrand(cfg, y.L, min(n.u, float(1) / cfg.r_plus), n.mu, hg1);
                result.halo_e += float(0.5) * h * (hw0 + hw1);
                halo_eg += float(0.5) * h * (hw0 * hg0 + hw1 * hg1);
                result.halo_g = (result.halo_e > float(0)) ? halo_eg / result.halo_e : float(1);
            }
            return;
        }
        float r_old = float(1) / y.u;

        if (cfg.disk != 0) {
            // The disk is an opaque wedge |cos(theta)| < h between r_isco and r_out: its photosphere is the cone pair
            // mu = +h (top face) and mu = -h (bottom face), closed by the outer rim and the inner wall.  A step can cross
            // several of these surfaces, so take the earliest valid one (Hermite root of each).
            float hh = max(cfg.disk_h, float(1e-3));
            float u_out = float(1) / cfg.r_out, u_in = float(1) / cfg.r_isco;
            bool ctop = (y.mu - hh) * (n.mu - hh) < float(0);
            bool cbot = (y.mu + hh) * (n.mu + hh) < float(0);
            bool crim = (y.u - u_out) * (n.u - u_out) < float(0);
            bool cin = (y.u - u_in) * (n.u - u_in) < float(0);
            if (ctop || cbot || crim || cin) {
                Ray k0, k1;
                rhs(a, y, k0);
                rhs(a, n, k1);
                float m0 = h * k0.mu, m1 = h * k1.mu;
                float xbest = float(2);
                int kind = -1;
                for (int e = 0; e < 4; ++e) {
                    if (e == 0 && !ctop) continue;
                    if (e == 1 && !cbot) continue;
                    if (e == 2 && !crim) continue;
                    if (e == 3 && !cin) continue;
                    float x;
                    if (e < 2) {
                        float sh = (e == 0) ? hh : -hh;
                        x = (y.mu - sh) / ((y.mu - sh) - (n.mu - sh));
                        for (int it = 0; it < 4; ++it) {
                            float f = hermite(y.mu - sh, n.mu - sh, m0, m1, x);
                            float fp = hermite_dx(y.mu - sh, n.mu - sh, m0, m1, x);
                            if (abs(fp) > float(1e-12)) x = x - f / fp;
                            x = min(max(x, float(0)), float(1));
                        }
                        float rr = float(1) / hermite(y.u, n.u, h * k0.u, h * k1.u, x);
                        if (rr < cfg.r_isco || rr > cfg.r_out) continue;
                    } else {
                        float ue = (e == 2) ? u_out : u_in;
                        x = (y.u - ue) / ((y.u - ue) - (n.u - ue));
                        for (int it = 0; it < 4; ++it) {
                            float f = hermite(y.u - ue, n.u - ue, h * k0.u, h * k1.u, x);
                            float fp = hermite_dx(y.u - ue, n.u - ue, h * k0.u, h * k1.u, x);
                            if (abs(fp) > float(1e-12)) x = x - f / fp;
                            x = min(max(x, float(0)), float(1));
                        }
                        if (abs(hermite(y.mu, n.mu, m0, m1, x)) >= hh) continue;
                    }
                    if (x < xbest) {
                        xbest = x;
                        kind = e;
                    }
                }
                if (kind >= 0) {
                    float ue = (kind == 2) ? u_out : (kind == 3) ? u_in : float(0);
                    result.status = 2;
                    result.surf = kind;
                    result.r_hit = (kind >= 2) ? float(1) / ue : float(1) / hermite(y.u, n.u, h * k0.u, h * k1.u, xbest);
                    result.phi_hit = hermite(y.phi, n.phi, h * k0.phi, h * k1.phi, xbest);
                    result.t_hit = hermite(y.that, n.that, h * k0.that, h * k1.that, xbest) - (tv + abs(result.r_hit - r_old));
                    result.vmu_hit = y.vmu + (n.vmu - y.vmu) * xbest;
                    if (cfg.halo > float(0)) {   // corona along the partial step up to the surface (Simpson on [0, xbest])
                        float hgm, hg1;
                        float xm = float(0.5) * xbest;
                        float wm = halo_integrand(cfg, y.L, hermite(y.u, n.u, h * y.vu, h * n.vu, xm), hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, xm), hgm);
                        float w1 = halo_integrand(cfg, y.L, hermite(y.u, n.u, h * y.vu, h * n.vu, xbest), hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, xbest), hg1);
                        result.halo_e += h * xbest * (hw0 + float(4) * wm + w1) / float(6);
                        halo_eg += h * xbest * (hw0 * hg0 + float(4) * wm * hgm + w1 * hg1) / float(6);
                        result.halo_g = (result.halo_e > float(0)) ? halo_eg / result.halo_e : float(1);
                    }
                    // count the equatorial crossing inside this step if it happened before the hit; rim / inner-wall
                    // hits on the lower half are the direct image of the rim (the crossing happened outside the disk)
                    if (y.mu * n.mu < float(0) && y.mu / (y.mu - n.mu) < xbest) result.crossings += 1;
                    if (kind >= 2 && hermite(y.mu, n.mu, m0, m1, xbest) < float(0) && result.crossings > 0) result.crossings -= 1;
                    return;
                }
            }
        }
        if (y.mu * n.mu < float(0)) result.crossings += 1;

        if (cfg.halo > float(0)) {   // Simpson rule over the step, midpoint from cubic Hermite interpolation of (u, mu)
            float hgm, hg1;
            float um = hermite(y.u, n.u, h * y.vu, h * n.vu, float(0.5));
            float mm = hermite(y.mu, n.mu, h * y.vmu, h * n.vmu, float(0.5));
            float hwm = halo_integrand(cfg, y.L, um, mm, hgm);
            float hw1 = halo_integrand(cfg, y.L, n.u, n.mu, hg1);
            result.halo_e += h * (hw0 + float(4) * hwm + hw1) / float(6);
            halo_eg += h * (hw0 * hg0 + float(4) * hwm * hgm + hw1 * hg1) / float(6);
            hw0 = hw1;
            hg0 = hg1;
            result.halo_g = (result.halo_e > float(0)) ? halo_eg / result.halo_e : float(1);
        }
        tv += abs(float(1) / n.u - r_old);
        y = n;
        if (y.u < u_far && y.vu < float(0)) {
            result.status = 1;
            escape_direction(a, y, result.dx, result.dy, result.dz);
            return;
        }
    }
}

// common.metal -- noise, blackbody colour, disk look-ups shared by the ray tracer and the funnel view.
// (kerr_shared.h and shared_types.h are prepended by the build.)

float PI_F = 3.14159265358979;

// ---------------------------------------------------------------------------------------------
// Hash / value noise
// ---------------------------------------------------------------------------------------------
uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
uint hash3u(uvec3 p) { return pcg(p.x + pcg(p.y + pcg(p.z))); }
float u01(uint h) { return float(h) * (1.0 / 4294967296.0); }

float vnoise(vec3 x)
{
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    ivec3 ii = ivec3(i);
    float n000 = u01(hash3u(uvec3(ii + ivec3(0, 0, 0))));
    float n100 = u01(hash3u(uvec3(ii + ivec3(1, 0, 0))));
    float n010 = u01(hash3u(uvec3(ii + ivec3(0, 1, 0))));
    float n110 = u01(hash3u(uvec3(ii + ivec3(1, 1, 0))));
    float n001 = u01(hash3u(uvec3(ii + ivec3(0, 0, 1))));
    float n101 = u01(hash3u(uvec3(ii + ivec3(1, 0, 1))));
    float n011 = u01(hash3u(uvec3(ii + ivec3(0, 1, 1))));
    float n111 = u01(hash3u(uvec3(ii + ivec3(1, 1, 1))));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm3(vec3 p, int octaves)
{
    float s = 0.0, amp = 0.5;
    for (int i = 0; i < octaves; ++i) {
        s += amp * vnoise(p);
        p = p * 2.03 + vec3(17.1, 3.7, 9.2);
        amp *= 0.5;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Blackbody look-up: table rows are (r, g, b) chroma with luminance 1, and log2 luminance relative to 6504 K.
// ---------------------------------------------------------------------------------------------
float BB_TMIN = 300.0;
float BB_TMAX = 400000.0;
int BB_N = 1024;

vec4 bb_row(sampler2D lut, float tempK)
{
    float x = log(max(tempK, BB_TMIN)) - log(BB_TMIN);
    x = x / log(BB_TMAX / BB_TMIN) * float(BB_N - 1);
    x = clamp(x, 0.0, float(BB_N) - 1.001);
    int i = int(x);
    float f = x - float(i);
    return mix(texelFetch(lut, ivec2(i,0),0), texelFetch(lut, ivec2(i + 1,0),0), f);
}
vec3 bb_radiance(sampler2D lut, float tempK)   // linear sRGB, 1.0 = visible luminance of a 6504 K body
{
    vec4 r = bb_row(lut, tempK);
    return r.rgb * exp2(r.a);
}
vec3 bb_chroma(sampler2D lut, float tempK) { return bb_row(lut, tempK).rgb; }

// ---------------------------------------------------------------------------------------------
// Novikov-Thorne temperature profile tempK(r)/T_peak, tabulated on [r_isco, r_out]
// ---------------------------------------------------------------------------------------------
int DISK_N = 1024;
float disk_temp_ratio(sampler2D tab, float r, float r_isco, float r_out)
{
    float x = (r - r_isco) / (r_out - r_isco) * float(DISK_N - 1);
    x = clamp(x, 0.0, float(DISK_N) - 1.001);
    int i = int(x);
    float f = x - float(i);
    return mix(texelFetch(tab, ivec2(i,0),0).r, texelFetch(tab, ivec2(i + 1,0),0).r, f);
}

// ---------------------------------------------------------------------------------------------
// Turbulent surface brightness of the disk (dimensionless, mean ~1).  The pattern is advected by the
// Keplerian shear Omega(r); two staggered epochs are cross-faded so the shear never winds up forever.
// ---------------------------------------------------------------------------------------------
float disk_epoch_pattern(float r, float phi, float t_age, float epoch, float a)
{
    float Om = omega_kepler(r, a);
    float ph = phi - Om * t_age;
    float ell = 0.55 + 0.09 * r;               // structure size grows with radius
    vec2 xy = r * vec2(cos(ph), sin(ph)) / ell;
    vec3 p = vec3(xy, epoch * 7.31);
    float n = fbm3(p, 4);                                 // 0..1 blobs, sheared into spirals by Omega(r)
    float rad = vnoise(vec3(r * 2.3 / ell * 1.6, epoch * 3.7, 11.0));   // fine radial banding
    return 0.25 + 1.55 * n * (0.8 + 0.4 * rad);
}
float disk_pattern(float r, float phi, float t, float a)
{
    float Tc = 46.0;
    float x = t / Tc;
    float eA = floor(x);
    float ageA = (x - eA) * Tc;
    float y = x + 0.5;
    float eB = floor(y);
    float ageB = (y - eB) * Tc;
    float wA = sin(PI_F * (x - eA));
    wA *= wA;
    float wB = 1.0 - wA;
    return wA * disk_epoch_pattern(r, phi, ageA, eA, a) + wB * disk_epoch_pattern(r, phi, ageB, eB + 101.0, a);
}

// ---------------------------------------------------------------------------------------------
// Tone mapping helpers
// ---------------------------------------------------------------------------------------------
vec3 aces_fitted(vec3 x)
{
    // Stephen Hill's fitted ACES: sRGB->AP1 input, RRT+ODT, AP1->sRGB output
    mat3 M_in = mat3(vec3(0.59719, 0.07600, 0.02840),
                                   vec3(0.35458, 0.90834, 0.13383),
                                   vec3(0.04823, 0.01566, 0.83777));
    mat3 M_out = mat3(vec3(1.60475, -0.10208, -0.00327),
                                    vec3(-0.53108, 1.10813, -0.07276),
                                    vec3(-0.07367, -0.00605, 1.07602));
    vec3 v = M_in * x;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(M_out * (a / b), 0.0, 1.0);
}
// raytrace.metal -- real-time Kerr ray tracer.  One = one camera ray, traced backward through the
// Kerr geometry with trace_ray (the very same code the CPU test-suite validates in double precision).

// ---------------------------------------------------------------------------------------------
// Background sky: procedural stars (blackbody coloured, gravitationally blue-shifted) + Milky Way
// ---------------------------------------------------------------------------------------------
vec3 star_layer(vec3 d, float cells, float density, float sigma, uint seed, float gshift,
                         sampler2D bb, float gain)
{
    vec3 ad = abs(d);
    int face;
    vec2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0.0 ? 0 : 1; uv = vec2(d.y, d.z) / ad.x; }
    else if (ad.y >= ad.z)            { face = d.y > 0.0 ? 2 : 3; uv = vec2(d.x, d.z) / ad.y; }
    else                               { face = d.z > 0.0 ? 4 : 5; uv = vec2(d.x, d.y) / ad.z; }
    uv = atan(uv) * (4.0 / PI_F);                       // equal-angle cell grid, uv in [-1, 1]
    vec2 q = (uv * 0.5 + 0.5) * cells;
    ivec2 c0 = ivec2(floor(q));
    float cell_rad = (PI_F * 0.5) / cells;        // angular size of a cell
    vec3 acc = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 c = c0 + ivec2(dx, dy);
            uint h = hash3u(uvec3(uint(c.x + 4096), uint(c.y + 4096), uint(face) * 131u + seed));
            if (u01(h) > density) continue;
            vec2 sp = vec2(u01(pcg(h ^ 0x1234567u)), u01(pcg(h ^ 0x9876543u)));
            vec2 dq = q - (vec2(c) + sp);
            float dist = length(dq) * cell_rad;
            if (dist > 4.0 * sigma) continue;
            float m = u01(pcg(h ^ 0xabcdef1u));
            float amp = gain * (0.03 + 5.0 * pow(m, 10.0));
            float temp = mix(2600.0, 26000.0, pow(u01(pcg(h ^ 0x5555555u)), 1.7)) * gshift;
            float prof = exp(-0.5 * dist * dist / (sigma * sigma));
            acc += bb_chroma(bb, temp) * amp * prof;
        }
    }
    return acc;
}

vec3 sky_color(vec3 d, float gshift, RTUniforms  U, sampler2D bb)
{
    float sigma = U.sky.y;
    vec3 col = vec3(0.0);
    // Milky Way: a tilted band with a bright bulge, dust lanes and colour variation
    if (U.sky.z > 0.0) {
        vec3 nG = normalize(vec3(0.34, -0.46, 0.82));
        vec3 e1 = normalize(cross(nG, vec3(0.0, 0.0, 1.0)));
        vec3 e2 = cross(nG, e1);
        float sb = dot(d, nG);
        float lon = atan(dot(d, e2), dot(d, e1));
        float band = exp(-sb * sb / (2.0 * 0.20 * 0.20));
        float core = exp(-lon * lon / 0.9) * exp(-sb * sb / (2.0 * 0.35 * 0.35));
        float n1 = fbm3(d * 3.1 + 5.0, 5);
        float n2 = fbm3(d * 9.0 + 1.0, 4);
        float dust = smoothstep(0.42, 0.70, fbm3(d * 5.3 + vec3(2.0, 8.0, 4.0), 5));
        float lum = band * (0.35 + 0.9 * n1) * (1.0 - 0.75 * dust * band) + 1.6 * core * (0.6 + 0.6 * n2);
        vec3 tint = mix(vec3(0.50, 0.62, 1.0), vec3(1.0, 0.74, 0.48), clamp(core * 2.0 + n1 - 0.3, 0.0, 1.0));
        col += tint * lum * 0.016 * U.sky.z;
        // unresolved faint stars riding the band
        col += vec3(0.9, 0.85, 0.8) * band * 0.0025 * U.sky.z * smoothstep(0.3, 0.9, n2);
    }
    if (U.disk.w > 0.5) {
        col += star_layer(d, 26.0, 0.55, sigma * 1.35, 11u, gshift, bb, 1.0);
        col += star_layer(d, 64.0, 0.50, sigma * 1.05, 23u, gshift, bb, 0.55);
        col += star_layer(d, 150.0, 0.42, sigma, 37u, gshift, bb, 0.30);
        col += star_layer(d, 360.0, 0.30, sigma, 41u, gshift, bb, 0.22);
    }
    // gravitational blue shift of the whole sky as seen from deep in the well (brightness ~ g^4)
    float g4 = gshift * gshift * gshift * gshift;
    col *= g4;
    // lensed celestial coordinate grid (lines every 15 degrees) -- shows the mapping of the sky
    if (U.opt.x > 0.5) {
        float th = acos(clamp(d.z, -1.0, 1.0));
        float ph = atan(d.y, d.x);
        float gstep = PI_F / 12.0;
        float lt = abs(fract(th / gstep + 0.5) - 0.5) * gstep;
        float lp = abs(fract(ph / gstep + 0.5) - 0.5) * gstep * max(sin(th), 0.05);
        float w = 0.0022;
        float line = max(exp(-lt * lt / (w * w)), exp(-lp * lp / (w * w)));
        col += line * vec3(0.05, 0.30, 0.60) * 0.9;
        // celestial equator accent
        float eq = exp(-(d.z * d.z) / (w * w * 1.5));
        col += eq * vec3(0.9, 0.5, 0.15) * 0.9;
    }
    return col;
}

// ---------------------------------------------------------------------------------------------
// The accretion disk surface
// ---------------------------------------------------------------------------------------------
vec3 disk_emission(TraceOut  tr, float nu_cam, RTUniforms  U,
                            sampler2D diskTab, sampler2D bb, inout float g_out)
{
    float a = U.bh.x;
    float r = tr.r_hit;
    float ut = ut_kepler(r, a);
    float Om = omega_kepler(r, a);
    float nu_em = ut * (1.0 - Om * tr.L);                 // photon energy in the gas frame (E_inf = 1)
    float g = nu_cam / nu_em;                               // total frequency ratio (Doppler x gravitational)
    g_out = g;
    float cosE = clamp(abs(tr.vmu_hit) / (r * nu_em), 0.0, 1.0);   // emission angle to the disk normal
    float limb = (1.0 + 2.06 * cosE) / (1.0 + 2.06 * 0.55);            // electron-scattering atmosphere
    if (tr.surf >= 2) limb = 1.0;                                          // rim / inner wall: face-on to the emitter's radial direction
    float t_em = U.view.z + tr.t_hit;                       // retarded time of emission
    float tex = 1.0;
    if (U.misc.x > 0.0) tex = mix(1.0, disk_pattern(r, tr.phi_hit, t_em, a), U.misc.x);
    float Tloc = U.disk.x * disk_temp_ratio(diskTab, r, U.bh.z, U.bh.w) * pow(max(tex, 0.02), 0.25);
    // outer edge fade (the disk simply thins result)
    float fade = mix(0.12, 1.0, smoothstep(U.bh.w, U.bh.w - 3.0, r));
    float Tobs = g * Tloc;
    vec3 rad = bb_radiance(bb, Tobs) * (limb * fade * U.misc.y);
    return rad;
}

// ---------------------------------------------------------------------------------------------
// Ray-trace kernel
// ---------------------------------------------------------------------------------------------

void main(){
    float W = U.view.x, H = U.view.y;

    float a = U.bh.x;
    vec2 ndc = vUV * 2.0 - 1.0;

    float th = U.cam.w;
    float aspect = float(W) / float(H);
    vec3 n = normalize(vec3(ndc.x * aspect * th, ndc.y * th, 1.0));   // (right, up, forward)

    // camera basis: forward = -e_r (toward the hole), up = -e_theta, right = +e_phi
    Ray ray;
    float nu_cam = 1.0;
    vec3 col = vec3(0.0);
    float moving = 1.0;   // alpha channel: 1 = content, 0 = animated (disk) -> temporal filter treats them differently
    bool ok = init_ray_zamo(a, U.cam.x, U.cam.y, U.cam.z, -n.z, -n.y, n.x, ray, nu_cam);

    if (ok) {
        TraceCfg cfg;
        cfg.a = a;
        cfg.r_plus = U.bh.y;
        cfg.r_isco = U.bh.z;
        cfg.r_out = U.bh.w;
        cfg.r_far = U.sky.w;
        cfg.c = U.opt.z;
        cfg.max_steps = int(U.opt.w);
        cfg.disk = (U.disk.y > 0.5) ? 1 : 0;
        cfg.disk_h = U.misc.w;
        cfg.halo = U.disk.z;
        cfg.nu_cam = nu_cam;
        TraceOut tr;
        trace_ray(cfg, ray, tr);

        int dbg = int(U.opt.y + 0.5);
        if (dbg == 3) {
            float s = float(tr.steps) / 96.0;
            col = vec3(clamp(s * 2.0, 0.0, 1.0), clamp(s * 2.0 - 0.5, 0.0, 1.0), clamp(s - 0.5, 0.0, 1.0));
        } else if (tr.status == 2) {
            float g;
            moving = 0.0;
            col = disk_emission(tr, nu_cam, U, diskTab, bb, g);
            if (dbg == 1) {   // image order: which pass of the equatorial plane
                vec3 pal[4] = vec3[4](vec3(1.0, 0.9, 0.7), vec3(0.2, 0.8, 1.0), vec3(1.0, 0.3, 0.8), vec3(0.4, 1.0, 0.4));
                col = pal[min(tr.crossings, 3)] * (0.25 + 0.75 * clamp(dot(col, vec3(0.3, 0.5, 0.2)), 0.0, 1.0));
            } else if (dbg == 2) {   // redshift map: blue = blueshift (g > 1), red = redshift (g < 1), grey = no shift
                float lg = clamp(log2(g) * 0.9, -1.0, 1.0);
                float t = pow(abs(lg), 0.6);
                vec3 target = lg > 0.0 ? vec3(0.05, 0.45, 1.0) : vec3(1.0, 0.10, 0.02);
                col = mix(vec3(0.35), target, t) * (0.55 + 0.45 * clamp(g, 0.0, 1.5) / 1.5);
            }
        } else if (tr.status == 1) {
            vec3 d = vec3(tr.dx, tr.dy, tr.dz);
            col = sky_color(d, nu_cam, U, bb) * U.sky.x;
            if (dbg == 1) col = vec3(0.03);
        } else if (tr.status == 0) {
            col = vec3(0.0);
        } else {
            col = vec3(0.0);   // step limit: rays trapped near the photon sphere -> dark (they converge to the critical curve)
        }
        // optically thin hot corona ("halo"): added in front of whatever the ray finally reached
        if (dbg == 0 && cfg.halo > 0.0 && tr.halo_e > 0.0)
            col += bb_radiance(bb, tr.halo_g * U.disk.x * 1.35) * (tr.halo_e * U.misc.y);
    }
    fragColor = vec4(col, moving);
}
