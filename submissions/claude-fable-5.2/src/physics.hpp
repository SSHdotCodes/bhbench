// =============================================================================
//  physics.hpp — double-precision Kerr quantities used on the CPU side:
//  characteristic radii, circular orbits, the Novikov-Thorne / Page-Thorne
//  disk flux, the observer tetrad, and the isometric embedding ("Flamm
//  funnel") of the equatorial plane.   Units G = c = M = 1.
//
//  Spin convention: a in (-1, 1).  The hole's angular momentum is a * z^.
//  The accretion disk always orbits in the +phi direction, so a > 0 is a
//  prograde disk and a < 0 a retrograde one.
// =============================================================================
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace kerr {

constexpr double kPi = 3.14159265358979323846;

inline double horizon(double a) { return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a)); }
inline double innerHorizon(double a) { return 1.0 - std::sqrt(std::max(0.0, 1.0 - a * a)); }

// Innermost stable circular orbit for a +phi orbit (Bardeen, Press & Teukolsky 1972).
inline double isco(double a) {
    double z1 = 1.0 + std::cbrt(1.0 - a * a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    double s  = (a >= 0.0) ? 1.0 : -1.0;
    return 3.0 + z2 - s * std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
}

// Equatorial circular photon orbit, co-rotating (+phi) or counter-rotating (-phi).
inline double photonOrbit(double a, bool plusPhi) {
    return 2.0 * (1.0 + std::cos((2.0 / 3.0) * std::acos(plusPhi ? -a : a)));
}

// Critical impact parameter L_z/E of that orbit (signed: + for +phi).
inline double criticalImpact(double a, bool plusPhi) {
    double r = photonOrbit(a, plusPhi);
    return plusPhi ? (3.0 * std::sqrt(r) - a) : -(3.0 * std::sqrt(r) + a);
}

// Circular equatorial geodesics in the +phi direction.
inline double omegaK(double r, double a) { return 1.0 / (r * std::sqrt(r) + a); }
inline double circDenominator(double r, double a) {
    double s = std::sqrt(r);
    return std::pow(r, 0.75) * std::sqrt(r * s - 3.0 * s + 2.0 * a);
}
inline double circE(double r, double a) {
    double s = std::sqrt(r);
    return (r * s - 2.0 * s + a) / circDenominator(r, a);
}
inline double circL(double r, double a) {
    double s = std::sqrt(r);
    return (r * r - 2.0 * a * s + a * a) / circDenominator(r, a);
}
// u^t of the circular orbit.
inline double circUt(double r, double a) {
    double s = std::sqrt(r);
    return (r * s + a) / (std::pow(r, 0.75) * std::sqrt(r * s - 3.0 * s + 2.0 * a));
}

// -----------------------------------------------------------------------------
// Time-averaged energy flux from one face of a geometrically thin, optically
// thick disk with zero torque at the ISCO (Novikov & Thorne 1973; Page &
// Thorne 1974), for unit accretion rate:
//
//   F(r) = 1/(4 pi r) * (-dOmega/dr) / (E - Omega L)^2 * Int_{r_ms}^{r} (E - Omega L) dL
//
// Evaluated here by direct quadrature of that defining integral.
// -----------------------------------------------------------------------------
struct DiskFlux {
    double a = 0, rIn = 6, rMax = 100;
    std::vector<double> r, F;      // samples, r = rIn + (rMax - rIn) u^2
    double Fmax = 0, rPeak = 0;

    void build(double spin, double rOuter, int n = 6000) {
        a = spin; rIn = isco(a); rMax = rOuter;
        r.resize(n + 1); F.resize(n + 1);
        double integral = 0.0;
        double Lp = circL(rIn, a), wp = circE(rIn, a) - omegaK(rIn, a) * Lp;
        r[0] = rIn; F[0] = 0.0; Fmax = 0; rPeak = rIn;
        for (int i = 1; i <= n; ++i) {
            double u  = double(i) / n;
            double ri = rIn + (rMax - rIn) * u * u;
            double Om = omegaK(ri, a), L = circL(ri, a), w = circE(ri, a) - Om * L;
            integral += 0.5 * (w + wp) * (L - Lp);
            double dOm = -1.5 * std::sqrt(ri) * Om * Om;
            r[i] = ri;
            F[i] = (-dOm) / (w * w) * integral / (4.0 * kPi * ri);
            if (F[i] > Fmax) { Fmax = F[i]; rPeak = ri; }
            Lp = L; wp = w;
        }
    }
    double flux(double rq) const {
        if (rq <= rIn || rq >= rMax) return 0.0;
        double u = std::sqrt((rq - rIn) / (rMax - rIn)) * (r.size() - 1);
        size_t i = std::min(size_t(u), r.size() - 2);
        double t = u - double(i);
        return F[i] * (1.0 - t) + F[i + 1] * t;
    }
};

// Page & Thorne (1974) closed form, eq. (15n).  Independent cross-check.
inline double fluxPageThorne(double r, double a) {
    double x = std::sqrt(r), x0 = std::sqrt(isco(a));
    double th = std::acos(a) / 3.0;
    double x1 = 2.0 * std::cos(th - kPi / 3.0);
    double x2 = 2.0 * std::cos(th + kPi / 3.0);
    double x3 = -2.0 * std::cos(th);
    double br = x - x0 - 1.5 * a * std::log(x / x0)
        - 3.0 * (x1 - a) * (x1 - a) / (x1 * (x1 - x2) * (x1 - x3)) * std::log((x - x1) / (x0 - x1))
        - 3.0 * (x2 - a) * (x2 - a) / (x2 * (x2 - x1) * (x2 - x3)) * std::log((x - x2) / (x0 - x2))
        - 3.0 * (x3 - a) * (x3 - a) / (x3 * (x3 - x1) * (x3 - x2)) * std::log((x - x3) / (x0 - x3));
    double f = 1.5 / (x * x * (x * x * x - 3.0 * x + 2.0 * a)) * br;
    return f / (4.0 * kPi * r);
}

// -----------------------------------------------------------------------------
// Kerr-Schild metric g_mu_nu at a Cartesian point, index order (t, x, y, z).
// -----------------------------------------------------------------------------
inline void ksMetric(const double X[3], double a, double g[4][4]) {
    double a2 = a * a;
    double w  = X[0] * X[0] + X[1] * X[1] + X[2] * X[2] - a2;
    double r2 = 0.5 * (w + std::sqrt(w * w + 4.0 * a2 * X[2] * X[2]));
    double r  = std::sqrt(r2);
    double f  = 2.0 * r2 * r / (r2 * r2 + a2 * X[2] * X[2]);
    double l[4] = {1.0, (r * X[0] + a * X[1]) / (r2 + a2), (r * X[1] - a * X[0]) / (r2 + a2), X[2] / r};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            g[i][j] = ((i == j) ? (i == 0 ? -1.0 : 1.0) : 0.0) + f * l[i] * l[j];
}

inline double ksRadius(const double X[3], double a) {
    double a2 = a * a;
    double w  = X[0] * X[0] + X[1] * X[1] + X[2] * X[2] - a2;
    return std::sqrt(0.5 * (w + std::sqrt(w * w + 4.0 * a2 * X[2] * X[2])));
}

// -----------------------------------------------------------------------------
// Orthonormal tetrad of a static observer (u parallel to d/dt) whose spatial
// legs are obtained by Gram-Schmidt from the coordinate directions
// forward, right, up.  e[A][mu], A = 0 (time), 1 right, 2 up, 3 forward;
// mu = (t, x, y, z).  Returns false inside the ergoregion, where no static
// observer exists.
// -----------------------------------------------------------------------------
struct Tetrad { double e[4][4]; };

inline bool staticTetrad(const double X[3], double a, const double fwd[3], const double right[3],
                         const double up[3], Tetrad& T) {
    double g[4][4];
    ksMetric(X, a, g);
    if (g[0][0] >= -1e-9) return false;
    auto dot = [&](const double* u, const double* v) {
        double s = 0;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) s += g[i][j] * u[i] * v[j];
        return s;
    };
    double* e0 = T.e[0];
    e0[0] = 1.0 / std::sqrt(-g[0][0]); e0[1] = e0[2] = e0[3] = 0.0;

    const double* seeds[3] = {fwd, right, up};
    int slot[3] = {3, 1, 2};
    for (int k = 0; k < 3; ++k) {
        double v[4] = {0.0, seeds[k][0], seeds[k][1], seeds[k][2]};
        double c = dot(v, e0);                       // g(u,u) = -1  ->  add c*u
        for (int i = 0; i < 4; ++i) v[i] += c * e0[i];
        for (int j = 0; j < k; ++j) {
            const double* ej = T.e[slot[j]];
            double cj = dot(v, ej);
            for (int i = 0; i < 4; ++i) v[i] -= cj * ej[i];
        }
        double n = std::sqrt(dot(v, v));
        for (int i = 0; i < 4; ++i) T.e[slot[k]][i] = v[i] / n;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Isometric embedding of the equatorial plane (Boyer-Lindquist t = const,
// theta = pi/2) as a surface of revolution in Euclidean 3-space.
//
//   induced metric:  dl^2 = (r^2/Delta) dr^2 + Rc(r)^2 dphi^2
//   Rc(r)^2 = r^2 + a^2 + 2 a^2 / r            (circumferential radius)
//   (dz/dr)^2 = r^2/Delta - (dRc/dr)^2
//
// For a = 0 this is Flamm's paraboloid z = sqrt(8 (r - 2)).  The substitution
// r = r+ + s^2 removes the integrable 1/sqrt singularity at the horizon.
// Also tabulated:
//   ell(r)  proper (ruler) distance from the horizon, d ell = r dr / sqrt(Delta);
//   tau(r)  proper time a zero-angular-momentum observer falling from rest at
//           infinity ("rain") needs to reach the horizon,
//           (dr/dtau)^2 = 2 (r^2 + a^2) / r^3;
//   lapse   of the zero-angular-momentum observers, sqrt(Delta r^2 / A): the
//           rate of their clocks relative to clocks at infinity (0 at the horizon).
// -----------------------------------------------------------------------------
struct EmbeddingSample { double r, Rc, z, tau, ell, lapse; };

inline std::vector<EmbeddingSample> embedding(double a, double rOuter, int n) {
    std::vector<EmbeddingSample> out(n + 1);
    double rp = horizon(a), rm = innerHorizon(a);
    double sMax = std::sqrt(rOuter - rp);
    auto Rc = [&](double r) { return std::sqrt(r * r + a * a + 2.0 * a * a / r); };
    auto dzds = [&](double s) {
        double r  = rp + s * s;
        double dR = (r - a * a / (r * r)) / Rc(r);
        double v  = 4.0 * r * r / (s * s + rp - rm) - 4.0 * s * s * dR * dR;
        return std::sqrt(std::max(v, 0.0));
    };
    auto dtauds = [&](double s) {
        double r = rp + s * s;
        return 2.0 * s / std::sqrt(2.0 * (r * r + a * a) / (r * r * r));
    };
    auto dellds = [&](double s) {
        double r = rp + s * s;
        return 2.0 * r / std::sqrt(s * s + rp - rm);
    };
    auto lapse = [&](double r) {
        double D = r * r - 2.0 * r + a * a;
        double A = (r * r + a * a) * (r * r + a * a) - a * a * D;
        return std::sqrt(std::max(D, 0.0) * r * r / A);
    };
    const int sub = 16;                           // Simpson sub-intervals per sample
    double z = 0.0, tau = 0.0, ell = 0.0;
    out[0] = {rp, Rc(rp), 0.0, 0.0, 0.0, 0.0};
    for (int i = 1; i <= n; ++i) {
        double s0 = sMax * double(i - 1) / n, s1 = sMax * double(i) / n;
        double h = (s1 - s0) / sub;
        double sz = dzds(s0) + dzds(s1), st = dtauds(s0) + dtauds(s1), sl = dellds(s0) + dellds(s1);
        for (int k = 1; k < sub; ++k) {
            double wgt = (k & 1) ? 4.0 : 2.0;
            sz += wgt * dzds(s0 + k * h);
            st += wgt * dtauds(s0 + k * h);
            sl += wgt * dellds(s0 + k * h);
        }
        z += sz * h / 3.0;
        tau += st * h / 3.0;
        ell += sl * h / 3.0;
        double r = rp + s1 * s1;
        out[i] = {r, Rc(r), z, tau, ell, lapse(r)};
    }
    return out;
}

}  // namespace kerr
