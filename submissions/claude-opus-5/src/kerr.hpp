// kerr.hpp — Kerr spacetime physics in geometrized units G = c = M = 1.
//
// Everything here is the CPU-side (double precision) reference implementation.
// The GPU ray tracer in shaders/blackhole.frag mirrors these formulas in float.
// Keeping both lets `blackhole --verify` check the analytic results against the
// same numerical machinery the renderer uses.
//
// Coordinates: Boyer-Lindquist (t, r, theta, phi).
//
//   Sigma = r^2 + a^2 cos^2(theta)
//   Delta = r^2 - 2 r + a^2
//   A     = (r^2+a^2)^2 - a^2 Delta sin^2(theta)
//
// Covariant metric (BL):
//   g_tt   = -(1 - 2r/Sigma)
//   g_tphi = -2 a r sin^2(th) / Sigma
//   g_rr   = Sigma/Delta
//   g_thth = Sigma
//   g_phph = (r^2 + a^2 + 2 a^2 r sin^2(th)/Sigma) sin^2(th)
//
// Inverse metric:
//   g^tt   = -A/(Sigma Delta)
//   g^tphi = -2 a r/(Sigma Delta)
//   g^rr   = Delta/Sigma
//   g^thth = 1/Sigma
//   g^phph = (Delta - a^2 sin^2 th)/(Sigma Delta sin^2 th)

#pragma once
#include <cmath>
#include <algorithm>
#include <array>

namespace kerr {

constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- horizons --

inline double horizonOuter(double a) { return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a)); }
inline double horizonInner(double a) { return 1.0 - std::sqrt(std::max(0.0, 1.0 - a * a)); }

// Static limit surface (outer boundary of the ergosphere).
inline double ergosphere(double a, double theta) {
    double c = std::cos(theta);
    return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a * c * c));
}

// -------------------------------------------------------- circular orbits --

// Innermost stable circular orbit. Bardeen, Press & Teukolsky (1972) eq. 2.21.
// prograde = orbit co-rotating with the hole.
inline double iscoRadius(double a, bool prograde = true) {
    double a2 = a * a;
    double Z1 = 1.0 + std::cbrt(1.0 - a2) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double Z2 = std::sqrt(3.0 * a2 + Z1 * Z1);
    double s  = prograde ? -1.0 : 1.0;
    return 3.0 + Z2 + s * std::sqrt((3.0 - Z1) * (3.0 + Z1 + 2.0 * Z2));
}

// Radius of the equatorial circular photon orbit. BPT (1972) eq. 2.18.
inline double photonRadius(double a, bool prograde = true) {
    double s = prograde ? -1.0 : 1.0;
    return 2.0 * (1.0 + std::cos((2.0 / 3.0) * std::acos(s * a)));
}

// Marginally bound circular orbit (capture radius for a particle from rest at infinity).
inline double mbRadius(double a, bool prograde = true) {
    double s = prograde ? -1.0 : 1.0;
    return 2.0 + s * a + 2.0 * std::sqrt(std::max(0.0, 1.0 + s * a));
}

// Keplerian angular velocity of a circular equatorial orbit, d(phi)/dt.
inline double keplerOmega(double r, double a, bool prograde = true) {
    double s = prograde ? 1.0 : -1.0;
    return s / (std::pow(r, 1.5) + s * a);
}

// Specific energy and angular momentum of a circular equatorial orbit. BPT eq. 2.12-2.13.
inline void circularEL(double r, double a, double& E, double& L, bool prograde = true) {
    double s   = prograde ? 1.0 : -1.0;
    double v   = std::sqrt(r);
    double den = std::sqrt(std::max(1e-12, 1.0 - 3.0 / r + s * 2.0 * a / (r * v)));
    E = (1.0 - 2.0 / r + s * a / (r * v)) / den;
    L = s * v * (1.0 - s * 2.0 * a / (r * v) + a * a / (r * r)) / den;
}

// ------------------------------------------------- Novikov-Thorne emission --

// Roots of x^3 - 3x + 2a = 0 used by the Page & Thorne (1974) flux integral.
struct NTRoots {
    double x0, x1, x2, x3;
};

inline NTRoots ntRoots(double a) {
    NTRoots R;
    double ac = std::acos(std::clamp(a, -1.0, 1.0));
    R.x1 = 2.0 * std::cos((ac - PI) / 3.0);
    R.x2 = 2.0 * std::cos((ac + PI) / 3.0);
    R.x3 = -2.0 * std::cos(ac / 3.0);
    R.x0 = std::sqrt(iscoRadius(a, true));
    return R;
}

// Radiative flux from one face of a Novikov-Thorne thin disk, in units where
// M = c = G = 1 and Mdot = 1.  Page & Thorne (1974), eq. 15n.
//
//   F(r) = (3 Mdot / 8 pi) * B(x) / ( r^2 (x^3 - 3x + 2a) ),     x = sqrt(r)
//
// The far field limit is F -> 3 Mdot / (8 pi r^3), matching the Newtonian thin
// disk, and F -> 0 at the ISCO (zero-torque inner boundary condition).
inline double ntFlux(double r, double a) {
    double rin = iscoRadius(a, true);
    if (r <= rin) return 0.0;
    NTRoots R = ntRoots(a);
    double x = std::sqrt(r);
    double x0 = R.x0, x1 = R.x1, x2 = R.x2, x3 = R.x3;

    // Guard the near-degenerate root structure as a -> 1 (x1 -> x2 -> 1).
    auto safe = [](double v) { return (std::abs(v) < 1e-9) ? (v < 0 ? -1e-9 : 1e-9) : v; };
    double c1 = 3.0 * (x1 - a) * (x1 - a) / safe(x1 * (x1 - x2) * (x1 - x3));
    double c2 = 3.0 * (x2 - a) * (x2 - a) / safe(x2 * (x2 - x1) * (x2 - x3));
    double c3 = 3.0 * (x3 - a) * (x3 - a) / safe(x3 * (x3 - x1) * (x3 - x2));

    double bracket = x - x0 - 1.5 * a * std::log(x / x0)
                   - c1 * std::log(std::abs((x - x1) / safe(x0 - x1)))
                   - c2 * std::log(std::abs((x - x2) / safe(x0 - x2)))
                   - c3 * std::log(std::abs((x - x3) / safe(x0 - x3)));

    double den = x * x * x - 3.0 * x + 2.0 * a;
    if (std::abs(den) < 1e-12) return 0.0;

    return std::max(0.0, (3.0 / (8.0 * PI)) * bracket / (r * r * den));
}

// Radiative efficiency: fraction of rest mass converted to radiation for matter
// spiralling in through a Novikov-Thorne disk.  eta = 1 - E_isco.
inline double efficiency(double a) {
    double E, L;
    circularEL(iscoRadius(a, true), a, E, L, true);
    return 1.0 - E;
}

// ------------------------------------------------------- geodesic machinery --

// Ray state.  E and L (= p_t and p_phi, up to sign) are constants of motion and
// live outside the integrated state vector.
struct State {
    double t, r, th, ph;   // position
    double pr, pth;        // covariant momenta
};

struct Constants {
    double a;   // spin
    double E;   // -p_t
    double L;   // p_phi
};

inline State operator+(const State& x, const State& y) {
    return {x.t + y.t, x.r + y.r, x.th + y.th, x.ph + y.ph, x.pr + y.pr, x.pth + y.pth};
}
inline State operator*(double s, const State& x) {
    return {s * x.t, s * x.r, s * x.th, s * x.ph, s * x.pr, s * x.pth};
}

// The Hamiltonian for null geodesics, written so that both derivatives stay cheap:
//
//   H = F / (2 Sigma),
//   F = Delta p_r^2 + p_th^2 - P^2/Delta + (L - a E sin^2 th)^2 / sin^2 th
//   P = (r^2 + a^2) E - a L
//
// H = 0 on any null geodesic, which we use as a running accuracy monitor.
inline double hamiltonian(const State& y, const Constants& c) {
    double a2 = c.a * c.a;
    double s = std::sin(y.th), cs = std::cos(y.th);
    double s2 = std::max(1e-12, s * s);
    double Sig = y.r * y.r + a2 * cs * cs;
    double Del = y.r * y.r - 2.0 * y.r + a2;
    double P = (y.r * y.r + a2) * c.E - c.a * c.L;
    double q = c.L - c.a * c.E * s2;
    double F = Del * y.pr * y.pr + y.pth * y.pth - P * P / Del + q * q / s2;
    return F / (2.0 * Sig);
}

// dy/dlambda for the Hamiltonian above.
inline State geodesicRHS(const State& y, const Constants& c) {
    double a = c.a, E = c.E, L = c.L;
    double a2 = a * a;
    double s = std::sin(y.th), cs = std::cos(y.th);
    double s2 = std::max(1e-12, s * s);
    double r = y.r, r2 = r * r;

    double Sig = r2 + a2 * cs * cs;
    double Del = r2 - 2.0 * r + a2;
    double invSig = 1.0 / Sig;
    double invDel = 1.0 / Del;

    double P = (r2 + a2) * E - a * L;
    double q = L - a * E * s2;

    double F = Del * y.pr * y.pr + y.pth * y.pth - P * P * invDel + q * q / s2;

    double dDel = 2.0 * r - 2.0;
    double dFdr = dDel * y.pr * y.pr - 4.0 * r * E * P * invDel + P * P * dDel * invDel * invDel;
    double dFdth = 2.0 * s * cs * (a2 * E * E - L * L / (s2 * s2));

    // dH/dx = (1/2Sigma) [ dF/dx - (F/Sigma) dSigma/dx ]
    double dHdr  = 0.5 * invSig * (dFdr  - F * invSig * (2.0 * r));
    double dHdth = 0.5 * invSig * (dFdth - F * invSig * (-2.0 * a2 * cs * s));

    State d;
    d.t   = invSig * ((r2 + a2) * P * invDel + a * q);
    d.r   = Del * invSig * y.pr;
    d.th  = invSig * y.pth;
    d.ph  = invSig * (a * P * invDel + L / s2 - a * E);
    d.pr  = -dHdr;
    d.pth = -dHdth;
    return d;
}

// Cash-Karp embedded Runge-Kutta 4(5).  Returns the 5th order solution in `out`
// and the magnitude of the embedded error estimate.
inline double rkck(const State& y, const Constants& c, double h, State& out) {
    static const double b21 = 0.2;
    static const double b31 = 3.0 / 40.0,    b32 = 9.0 / 40.0;
    static const double b41 = 0.3,           b42 = -0.9,        b43 = 1.2;
    static const double b51 = -11.0 / 54.0,  b52 = 2.5,         b53 = -70.0 / 27.0,  b54 = 35.0 / 27.0;
    static const double b61 = 1631.0 / 55296.0, b62 = 175.0 / 512.0, b63 = 575.0 / 13824.0,
                        b64 = 44275.0 / 110592.0, b65 = 253.0 / 4096.0;
    static const double c1 = 37.0 / 378.0, c3 = 250.0 / 621.0, c4 = 125.0 / 594.0, c6 = 512.0 / 1771.0;
    static const double d1 = 2825.0 / 27648.0, d3 = 18575.0 / 48384.0, d4 = 13525.0 / 55296.0,
                        d5 = 277.0 / 14336.0, d6 = 0.25;

    State k1 = geodesicRHS(y, c);
    State k2 = geodesicRHS(y + (h * b21) * k1, c);
    State k3 = geodesicRHS(y + (h * b31) * k1 + (h * b32) * k2, c);
    State k4 = geodesicRHS(y + (h * b41) * k1 + (h * b42) * k2 + (h * b43) * k3, c);
    State k5 = geodesicRHS(y + (h * b51) * k1 + (h * b52) * k2 + (h * b53) * k3 + (h * b54) * k4, c);
    State k6 = geodesicRHS(y + (h * b61) * k1 + (h * b62) * k2 + (h * b63) * k3 + (h * b64) * k4 + (h * b65) * k5, c);

    out = y + (h * c1) * k1 + (h * c3) * k3 + (h * c4) * k4 + (h * c6) * k6;
    State low = y + (h * d1) * k1 + (h * d3) * k3 + (h * d4) * k4 + (h * d5) * k5 + (h * d6) * k6;

    // Mixed absolute/relative error, weighted per component.  Weighting every
    // component by |r| (which can be 1e9 in the far field) would let the angular
    // error grow without bound, so theta and phi are held to an absolute
    // tolerance while r and the momenta get a relative one.
    double e = 0.0;
    e = std::max(e, std::abs(out.r   - low.r)   / (1.0 + std::abs(y.r)));
    e = std::max(e, std::abs(out.th  - low.th));
    e = std::max(e, std::abs(out.ph  - low.ph));
    e = std::max(e, std::abs(out.pr  - low.pr)  / (1.0 + std::abs(y.pr)));
    e = std::max(e, std::abs(out.pth - low.pth) / (1.0 + std::abs(y.pth)));
    return e;
}

// ------------------------------------------------------- observer tetrads --

// Zero-angular-momentum observer (ZAMO) quantities at (r, theta).
struct ZAMO {
    double alpha;   // lapse
    double omega;   // frame dragging angular velocity dphi/dt
    double varpi;   // cylindrical radius of the phi leg
    double sqSD;    // sqrt(Sigma/Delta)
    double sqS;     // sqrt(Sigma)
};

inline ZAMO zamo(double r, double th, double a) {
    double a2 = a * a, s = std::sin(th), c = std::cos(th);
    double s2 = s * s;
    double Sig = r * r + a2 * c * c;
    double Del = r * r - 2.0 * r + a2;
    double A = (r * r + a2) * (r * r + a2) - a2 * Del * s2;
    ZAMO z;
    z.alpha = std::sqrt(std::max(1e-12, Sig * Del / A));
    z.omega = 2.0 * a * r / A;
    z.varpi = std::sqrt(A / Sig) * s;
    z.sqSD  = std::sqrt(Sig / Del);
    z.sqS   = std::sqrt(Sig);
    return z;
}

// Build the covariant photon momentum for a light ray that ARRIVES at a ZAMO
// from local viewing direction `n` (components along the orthonormal legs
// e_(r), e_(theta), e_(phi)).  The photon itself travels along -n, so its
// local components are k^(a) = (1, -n).  Normalised to unit locally measured
// energy, i.e. k.u_obs = -1.
//
// Integrating this state with a NEGATIVE step traces the ray backwards in time,
// which is what the renderer does.
inline void raymomentum(double r, double th, double a, const double n[3],
                        Constants& c, double& pr, double& pth) {
    ZAMO z = zamo(r, th, a);
    c.a = a;
    c.E = z.alpha - z.omega * z.varpi * n[2];   // E = -k_t
    c.L = -z.varpi * n[2];                      // L =  k_phi
    pr  = -n[0] * z.sqSD;
    pth = -n[1] * z.sqS;
}

// ------------------------------------------------------------- kinematics --

// Contravariant 4-velocity of the disk material at equatorial radius r.
// Outside the ISCO: Keplerian circular orbit.
// Inside: geodesic plunge that conserves the ISCO's E and L.
struct FourVel { double ut, ur, uph; };

inline FourVel diskFourVelocity(double r, double a) {
    double rin = iscoRadius(a, true);
    double a2 = a * a;
    double Del = r * r - 2.0 * r + a2;
    double Sig = r * r;                              // equatorial: cos(th) = 0
    double A = (r * r + a2) * (r * r + a2) - a2 * Del;

    double gtt = -A / (Sig * Del);
    double gtp = -2.0 * a * r / (Sig * Del);
    double gpp = (Del - a2) / (Sig * Del);
    double grr = Del / Sig;

    FourVel u;
    if (r >= rin) {
        double Om = keplerOmega(r, a, true);
        double G_tt = -(1.0 - 2.0 / r);
        double G_tp = -2.0 * a / r;
        double G_pp = r * r + a2 + 2.0 * a2 / r;
        double nrm = -(G_tt + 2.0 * Om * G_tp + Om * Om * G_pp);
        u.ut = 1.0 / std::sqrt(std::max(1e-9, nrm));
        u.ur = 0.0;
        u.uph = Om * u.ut;
    } else {
        double E, L;
        circularEL(rin, a, E, L, true);
        u.ut  = -gtt * E + gtp * L;
        u.uph = -gtp * E + gpp * L;
        double ur2 = -(1.0 + (gtt * E * E - 2.0 * gtp * E * L + gpp * L * L)) / std::max(1e-12, grr);
        double ur_cov = -std::sqrt(std::max(0.0, ur2));   // infalling
        u.ur = grr * ur_cov;
    }
    return u;
}

}  // namespace kerr
