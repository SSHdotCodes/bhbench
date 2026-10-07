// Kerr black hole in Kerr-Schild Cartesian coordinates, units G = c = M = 1, signature (-,+,+,+).
//
//   g_{mu nu} = eta_{mu nu} + H l_mu l_nu            g^{mu nu} = eta^{mu nu} - H l^mu l^nu
//   H   = 2 r^3 / (r^4 + a^2 z^2)
//   l_mu = (1, (r x + a y)/(r^2 + a^2), (r y - a x)/(r^2 + a^2), z/r)
//   r is the Kerr-Schild radius: r^4 - (x^2 + y^2 + z^2 - a^2) r^2 - a^2 z^2 = 0
//
// The spacetime is regular on the horizon and at the axis, so photons are integrated in these
// Cartesian coordinates with RK4 and no coordinate singularities. The Killing constants are
//   E = -p_t (normalised to 1) and L = y p_x - x p_y  (the photon's z-angular momentum).
// The equatorial plane z = 0 is the Boyer-Lindquist equator theta = pi/2 with r^2 = x^2 + y^2 - a^2.
//
// This header is compiled twice: as C++ (double or float, for the tests and the CPU renderer) and
// as Metal Shading Language (float, for the GPU). Keep it to the common subset of both.

#ifndef BH_KERR_CORE_H
#define BH_KERR_CORE_H

#ifndef __METAL_VERSION__
#include <cmath>
using std::sqrt;
using std::sin;
using std::cos;
using std::atan2;
using std::exp;
using std::log;
using std::pow;
using std::fabs;
using std::acos;
#endif

#define OUT_ESCAPED 0
#define OUT_CAPTURED 1
#define OUT_DISK 2
#define OUT_UNRESOLVED 3

template <typename T> struct V3 {
    T x, y, z;
};

template <typename T> inline V3<T> v3(T x, T y, T z) {
    V3<T> v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
template <typename T> inline V3<T> operator+(V3<T> a, V3<T> b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
template <typename T> inline V3<T> operator-(V3<T> a, V3<T> b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
template <typename T> inline V3<T> operator*(T s, V3<T> a) { return v3(s * a.x, s * a.y, s * a.z); }
template <typename T> inline T dot3(V3<T> a, V3<T> b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <typename T> inline T len3(V3<T> a) { return sqrt(dot3(a, a)); }
template <typename T> inline V3<T> norm3(V3<T> a) {
    T s = T(1) / len3(a);
    return s * a;
}

// Phase-space point of a photon: position x and covariant spatial momentum p_i (p_t = +1 implicitly).
template <typename T> struct Phase {
    V3<T> x;
    V3<T> p;
};
template <typename T> inline Phase<T> operator+(Phase<T> a, Phase<T> b) {
    Phase<T> r;
    r.x = a.x + b.x;
    r.p = a.p + b.p;
    return r;
}
template <typename T> inline Phase<T> operator*(T s, Phase<T> a) {
    Phase<T> r;
    r.x = s * a.x;
    r.p = s * a.p;
    return r;
}

// Metric pieces and their first derivatives at one Cartesian point.
template <typename T> struct KSData {
    T r;         // Kerr-Schild radius
    T H;         // scalar function H
    T l[3];      // covariant spatial part of l_mu (l_t = 1)
    T dr[3];     // d r / d x^i
    T dH[3];     // d H / d x^i (total derivative, includes the r dependence)
    T dl[3][3];  // dl[i][j] = d l_j / d x^i
};

// Evaluates the Kerr-Schild radius, H, l_mu and all first derivatives analytically.
// Divisions are shared through four reciprocals (r, D, Q, S) because this runs four times per RK4 step.
template <typename T> inline KSData<T> ks_eval(T a, V3<T> x) {
    KSData<T> k;
    T a2 = a * a;
    T z2 = x.z * x.z;
    T rho2 = x.x * x.x + x.y * x.y;
    T A = rho2 + z2 - a2;                                  // R^2 - a^2
    T r2 = T(0.5) * (A + sqrt(A * A + T(4) * a2 * z2));
    T r = sqrt(r2);
    T ir = T(1) / r;
    T ir2 = ir * ir;
    T r4 = r2 * r2;
    T D = r2 + a2;                                         // r^2 + a^2
    T iD = T(1) / D;
    T Q = r4 + a2 * z2;                                    // r^4 + a^2 z^2
    T iQ = T(1) / Q;
    T S = r * rho2 * iD * iD + z2 * ir * ir2;              // -(1/2) dF/dr for F = rho^2/D + z^2/r^2 - 1
    T iS = T(1) / S;

    k.r = r;
    k.H = T(2) * r * r2 * iQ;
    k.dr[0] = x.x * iD * iS;
    k.dr[1] = x.y * iD * iS;
    k.dr[2] = x.z * ir2 * iS;

    T iQ2 = iQ * iQ;
    T dHdr = T(2) * r2 * (T(3) * a2 * z2 - r4) * iQ2;
    T dHdz = -T(4) * a2 * x.z * r * r2 * iQ2;
    for (int i = 0; i < 3; ++i) {
        k.dH[i] = dHdr * k.dr[i] + (i == 2 ? dHdz : T(0));
    }

    T nx = r * x.x + a * x.y;                              // D * l_x
    T ny = r * x.y - a * x.x;                              // D * l_y
    k.l[0] = nx * iD;
    k.l[1] = ny * iD;
    k.l[2] = x.z * ir;

    for (int i = 0; i < 3; ++i) {
        T dD = T(2) * r * k.dr[i];                         // d D / d x^i
        T dnx = x.x * k.dr[i] + (i == 0 ? r : T(0)) + (i == 1 ? a : T(0));
        T dny = x.y * k.dr[i] + (i == 1 ? r : T(0)) - (i == 0 ? a : T(0));
        // d(num/D)/dx = (d num - num dD / D) / D
        k.dl[i][0] = (dnx - nx * dD * iD) * iD;
        k.dl[i][1] = (dny - ny * dD * iD) * iD;
        k.dl[i][2] = (i == 2 ? ir : T(0)) - x.z * k.dr[i] * ir2;
    }
    return k;
}

// Kerr-Schild radius only (cheap; used for horizon and disk tests).
template <typename T> inline T ks_radius(T a, V3<T> x) {
    T a2 = a * a;
    T A = x.x * x.x + x.y * x.y + x.z * x.z - a2;
    T r2 = T(0.5) * (A + sqrt(A * A + T(4) * a2 * x.z * x.z));
    return sqrt(r2);
}

// Hamiltonian flow of h = (1/2) g^{mu nu} p_mu p_nu with p_t = 1:
//   dx^i/dlambda = p_i - H l_i q,          q = l^mu p_mu = -1 + l_j p_j
//   dp_i/dlambda = (1/2) [ dH/dx^i q^2 + 2 H q p_j dl_j/dx^i ]
template <typename T> inline Phase<T> ray_rhs(T a, Phase<T> s) {
    KSData<T> k = ks_eval(a, s.x);
    T q = T(-1) + k.l[0] * s.p.x + k.l[1] * s.p.y + k.l[2] * s.p.z;
    Phase<T> d;
    d.x = v3(s.p.x - k.H * k.l[0] * q, s.p.y - k.H * k.l[1] * q, s.p.z - k.H * k.l[2] * q);
    T pdl[3];
    for (int i = 0; i < 3; ++i) {
        pdl[i] = s.p.x * k.dl[i][0] + s.p.y * k.dl[i][1] + s.p.z * k.dl[i][2];
    }
    d.p = v3(T(0.5) * (k.dH[0] * q * q + T(2) * k.H * q * pdl[0]),
             T(0.5) * (k.dH[1] * q * q + T(2) * k.H * q * pdl[1]),
             T(0.5) * (k.dH[2] * q * q + T(2) * k.H * q * pdl[2]));
    return d;
}

// Classical fourth-order Runge-Kutta step of length h in the affine parameter, given k1 = ray_rhs(s).
template <typename T> inline Phase<T> rk4_step_k1(T a, Phase<T> s, Phase<T> k1, T h) {
    Phase<T> k2 = ray_rhs(a, s + (T(0.5) * h) * k1);
    Phase<T> k3 = ray_rhs(a, s + (T(0.5) * h) * k2);
    Phase<T> k4 = ray_rhs(a, s + h * k3);
    return s + (h / T(6)) * (k1 + T(2) * k2 + T(2) * k3 + k4);
}

template <typename T> inline Phase<T> rk4_step(T a, Phase<T> s, T h) {
    return rk4_step_k1(a, s, ray_rhs(a, s), h);
}

// Adaptive affine step. Two limits, both scaled by eta:
//   position: the step moves the photon about eta * |x| in space (coordinate speed |dx/dlambda| grows near the hole);
//   momentum: the step changes the covariant momentum by about eta * |p| (dp/dlambda grows like |p|^2 H
//             for infalling photons, which is where an unscaled step loses the null condition).
template <typename T> inline T step_length(T eta, T R, Phase<T> s, Phase<T> k1) {
    T speed = len3(k1.x);
    T hx = R / (speed > T(0.1) ? speed : T(0.1));
    T pmag = len3(s.p);
    T dpmag = len3(k1.p);
    T hp = (pmag > T(0.1) ? pmag : T(0.1)) / (dpmag > T(1e-9) ? dpmag : T(1e-9));
    T h = hx < hp ? hx : hp;
    return eta * h;
}

// Null residual g^{mu nu} p_mu p_nu. Must stay at zero along an exact photon path.
template <typename T> inline T null_residual(T a, Phase<T> s) {
    KSData<T> k = ks_eval(a, s.x);
    T q = T(-1) + k.l[0] * s.p.x + k.l[1] * s.p.y + k.l[2] * s.p.z;
    return T(-1) + dot3(s.p, s.p) - k.H * q * q;
}

// Conserved z-angular momentum per unit energy of the photon (E = 1), physical sign.
template <typename T> inline T photon_L(Phase<T> s) { return s.x.y * s.p.x - s.x.x * s.p.y; }

// Covector lowering: c_mu = g_{mu nu} c^nu, given the contravariant 4-vector (c0, c_spatial).
template <typename T> inline void lower4(const KSData<T>& k, T c0, V3<T> cs, T out[4]) {
    T lam = c0 + k.l[0] * cs.x + k.l[1] * cs.y + k.l[2] * cs.z;   // l_mu c^mu
    out[0] = -c0 + k.H * lam;
    out[1] = cs.x + k.H * k.l[0] * lam;
    out[2] = cs.y + k.H * k.l[1] * lam;
    out[3] = cs.z + k.H * k.l[2] * lam;
}

// Metric inner product of two contravariant 4-vectors.
template <typename T> inline T g_dot(const KSData<T>& k, T a0, V3<T> as, T b0, V3<T> bs) {
    T bl[4];
    lower4(k, b0, bs, bl);
    return a0 * bl[0] + as.x * bl[1] + as.y * bl[2] + as.z * bl[3];
}

// Static observer at a fixed point with an orthonormal triad (right, up, forward).
template <typename T> struct Observer {
    V3<T> pos;
    T u[4];       // covariant 4-velocity of the static observer
    T e[3][4];    // covariant orthonormal triad: e[0] right, e[1] up, e[2] forward
    T ut;         // u^t = 1/sqrt(1 - H); gravitational redshift factor of the observer
};

// Builds the static observer's orthonormal frame: the flat basis vectors are projected orthogonal
// to u and then Gram-Schmidt orthonormalised with the full metric.
template <typename T> inline Observer<T> make_observer(T a, V3<T> pos, V3<T> right, V3<T> up, V3<T> fwd) {
    Observer<T> o;
    KSData<T> k = ks_eval(a, pos);
    T ut = T(1) / sqrt(T(1) - k.H);
    o.pos = pos;
    o.ut = ut;
    lower4(k, ut, v3(T(0), T(0), T(0)), o.u);

    // Each flat basis vector v = (0, b) is projected orthogonal to u: w = v + (u.v) u.
    V3<T> basis[3] = {right, up, fwd};
    T w0[3];
    for (int j = 0; j < 3; ++j) {
        T uv = o.u[1] * basis[j].x + o.u[2] * basis[j].y + o.u[3] * basis[j].z;
        w0[j] = uv * ut;
    }

    // Gram-Schmidt with the full metric, storing contravariant components.
    V3<T> ec[3];
    T e0[3];
    for (int j = 0; j < 3; ++j) {
        T c0 = w0[j];
        V3<T> cs = basis[j];
        for (int m = 0; m < j; ++m) {
            T pr = g_dot(k, c0, cs, e0[m], ec[m]);
            c0 = c0 - pr * e0[m];
            cs = cs - pr * ec[m];
        }
        T nrm = sqrt(g_dot(k, c0, cs, c0, cs));
        e0[j] = c0 / nrm;
        ec[j] = (T(1) / nrm) * cs;
    }
    for (int j = 0; j < 3; ++j) {
        lower4(k, e0[j], ec[j], o.e[j]);
    }
    return o;
}

// Initial photon phase-space point for the pixel at local screen coordinates (sx, sy):
// the photon arrives from direction (sx, sy, 1) of the observer, so it is traced out along n.
// The traced curve is the time reverse of the physical light ray, which is why p_t is +1 after
// normalisation and the momentum points into the scene.
template <typename T> inline Phase<T> pixel_ray(const Observer<T>& o, T sx, T sy) {
    T inv = T(1) / sqrt(sx * sx + sy * sy + T(1));
    T nR = sx * inv;
    T nU = sy * inv;
    T nF = inv;
    T p[4];
    for (int mu = 0; mu < 4; ++mu) {
        p[mu] = -o.u[mu] + nR * o.e[0][mu] + nU * o.e[1][mu] + nF * o.e[2][mu];
    }
    Phase<T> s;
    s.x = o.pos;
    s.p = v3(p[1] / p[0], p[2] / p[0], p[3] / p[0]);
    return s;
}

// Prograde equatorial circular photon orbit (Bardeen 1972): r = 2 [1 + cos((2/3) arccos(-a))].
template <typename T> inline T photon_orbit_prograde_radius(T a) {
    return T(2) * (T(1) + cos(T(2.0 / 3.0) * acos(-a)));
}

// Radius inside which a photon is declared captured. Escaping photons cannot turn around inside the
// smallest unstable circular photon orbit, so r < r_capture implies capture. Stopping here avoids the
// momentum growth near the horizon, which is physical in these coordinates but costly to resolve.
template <typename T> inline T capture_radius(T a) {
    T rp = T(1) + sqrt(T(1) - a * a);
    return rp + T(0.5) * (photon_orbit_prograde_radius(a) - rp);
}

template <typename T> struct TraceParams {
    T a;
    T r_plus;
    T r_capture;
    T r_isco;
    T r_out;
    T eta;
    T r_escape;
    T cam_ut;
    int max_steps;
};

template <typename T> struct Hit {
    int outcome;
    V3<T> dir;   // escaped: asymptotic direction the traced photon travels in (the sky direction)
    T r;         // disk: emission radius
    T g;         // disk: redshift factor E_obs / E_emit
    T L;         // disk: photon L/E at emission
    int steps;
};

// Interpolated normalised flux F(r)/F_max from a table on the uniform grid [r0, r1].
template <typename T, typename P> inline T disk_flux_norm(P lut, int n, T r, T r0, T r1) {
    T u = (r - r0) / (r1 - r0) * T(n - 1);
    if (u <= T(0)) {
        return lut[0];
    }
    if (u >= T(n - 1)) {
        return lut[n - 1];
    }
    int i = int(u);
    T f = u - T(i);
    return (T(1) - f) * T(lut[i]) + f * T(lut[i + 1]);
}

// Traces one photon back from the observer. Returns where it ends: escaped to infinity, captured by
// the horizon, or its first crossing of the equatorial disk at r in [r_isco, r_out].
template <typename T> inline Hit<T> trace_photon(const TraceParams<T>& tp, Phase<T> s) {
    Hit<T> out;
    out.outcome = OUT_UNRESOLVED;
    out.dir = v3(T(0), T(0), T(0));
    out.r = T(0);
    out.g = T(0);
    out.L = T(0);
    out.steps = 0;
    T a2 = tp.a * tp.a;
    for (int n = 0; n < tp.max_steps; ++n) {
        out.steps = n;
        T R = len3(s.x);
        if (ks_radius(tp.a, s.x) < tp.r_capture) {
            out.outcome = OUT_CAPTURED;
            return out;
        }
        if (R > tp.r_escape && dot3(s.x, s.p) > T(0)) {
            out.outcome = OUT_ESCAPED;
            out.dir = norm3(s.p);
            return out;
        }
        Phase<T> k1 = ray_rhs(tp.a, s);
        T h = step_length(tp.eta, R, s, k1);
        Phase<T> next = rk4_step_k1(tp.a, s, k1, h);
        bool up = s.x.z > T(0);
        if (up != (next.x.z > T(0))) {
            // The photon crossed the equatorial plane inside this step: bisect on the sub-step length.
            T lo = T(0);
            T hi = T(1);
            for (int it = 0; it < 16; ++it) {
                T mid = T(0.5) * (lo + hi);
                Phase<T> m = rk4_step(tp.a, s, mid * h);
                if ((m.x.z > T(0)) == up) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            Phase<T> hitp = rk4_step(tp.a, s, hi * h);
            T rho2 = hitp.x.x * hitp.x.x + hitp.x.y * hitp.x.y;
            T r2h = rho2 - a2;
            if (r2h > T(0)) {
                T rh = sqrt(r2h);
                if (rh >= tp.r_isco && rh <= tp.r_out) {
                    // Circular prograde equatorial orbit at rh: Omega, u^t from Bardeen-Press-Teukolsky.
                    T r32 = rh * sqrt(rh);
                    T Om = T(1) / (r32 + tp.a);
                    T ut = (r32 + tp.a) / (sqrt(rh) * sqrt(sqrt(rh)) * sqrt(r32 - T(3) * sqrt(rh) + T(2) * tp.a));
                    T L = photon_L(hitp);
                    out.outcome = OUT_DISK;
                    out.r = rh;
                    out.L = L;
                    out.g = tp.cam_ut / (ut * (T(1) - Om * L));
                    out.steps = n;
                    return out;
                }
            }
        }
        s = next;
    }
    return out;
}

#endif
