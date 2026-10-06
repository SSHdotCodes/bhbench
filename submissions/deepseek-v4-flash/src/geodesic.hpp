#pragma once
// Schwarzschild geodesics in geometrized units (G = c = 1, M = 1, rs = 2M = 2).
//
// All integration uses the Hamiltonian formulation of null/timelike geodesics:
//
//   H = (1/2) g^uv p_u p_v
//
// with the Schwarzschild metric in Cartesian form:
//   g_00  = -A,          A = 1 - rs/r
//   g_ij  = delta_ij + (rs/(r-rs)) (x_i x_j)/r^2
//   g^00  = -1/A
//   g^ij  = delta_ij - (rs/r) (x_i x_j)/r^2
//
// p_0 = -E is conserved (static metric). The canonical equations reduce to:
//   dx^i/dlambda = p^i - (rs/r) (p.x) x^i / r^2
//   dp_i/dlambda = -E^2 rs x_i/(2 r^3 A^2)
//                + rs (p.x) p_i / r^3
//                - (3/2) rs (p.x)^2 x_i / r^5
//
// and the constraint g^uv p_u p_v = -q (q = 0 null, q = 1 timelike) is enforced
// by rescaling |p| every step (exact for this quadratic Hamiltonian, prevents
// numerical drift away from the light cone).

#include <array>
#include <cmath>
#include <vector>

namespace geo {

constexpr double RS = 2.0;      // Schwarzschild radius for M = 1
constexpr double ISCO = 6.0;    // innermost stable circular orbit
constexpr double PHOTON_SPHERE = 3.0;
constexpr double ESC_R = 55.0;  // escape radius (sky at infinity)
constexpr double DISK_IN = 6.0;
constexpr double DISK_OUT = 22.0;

inline double lapse(double r) { return 1.0 - RS / r; }

// ---------------------------------------------------------------------------
// RHS of the geodesic equations.
// ---------------------------------------------------------------------------
inline void deriv(const double x[3], const double p[3], double E,
                  double dx[3], double dp[3]) {
    const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    const double r2 = r * r, r3 = r2 * r, r5 = r3 * r2;
    const double px = p[0] * x[0] + p[1] * x[1] + p[2] * x[2];
    const double a = lapse(r);
    const double k = RS * px / r3;                   // rs (p.x) / r^3
    const double l = 1.5 * RS * px * px / r5;        // 1.5 rs (p.x)^2 / r^5
    const double m0 = 0.5 * E * E * RS / (r3 * a * a); // E^2 rs/(2 r^3 A^2)
    const double cx = RS * px / r3;                  // rs (p.x)/r^3 (radial) 
    for (int i = 0; i < 3; ++i) {
        dx[i] = p[i] - cx * x[i];
        dp[i] = -m0 * x[i] + k * p[i] - l * x[i];
    }
}

// ---------------------------------------------------------------------------
// Project momentum onto the mass shell (light cone for q = 0).
// ---------------------------------------------------------------------------
inline void project(double p[3], const double x[3], double E, double q) {
    const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    const double a = lapse(r);
    const double pxn = p[0] * x[0] + p[1] * x[1] + p[2] * x[2]; // p.x
    const double target = E * E / a - q + RS * pxn * pxn / (r * r * r);
    const double pp = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    const double s = (target > 0.0 && pp > 0.0) ? std::sqrt(target / pp) : 0.0;
    p[0] *= s; p[1] *= s; p[2] *= s;
}

// ---------------------------------------------------------------------------
// Adaptive RK4 step. h scales with r so that the strong-field region is
// sampled finely (photon ring) while the far field integrates cheaply.
// ---------------------------------------------------------------------------
inline void rk4step(double x[3], double p[3], double E, double q, double h) {
    double k1x[3], k1p[3], k2x[3], k2p[3], k3x[3], k3p[3], k4x[3], k4p[3];
    double tx[3], tp[3];
    deriv(x, p, E, k1x, k1p);
    for (int i = 0; i < 3; ++i) { tx[i] = x[i] + 0.5 * h * k1x[i]; tp[i] = p[i] + 0.5 * h * k1p[i]; }
    project(tp, tx, E, q);
    deriv(tx, tp, E, k2x, k2p);
    for (int i = 0; i < 3; ++i) { tx[i] = x[i] + 0.5 * h * k2x[i]; tp[i] = p[i] + 0.5 * h * k2p[i]; }
    project(tp, tx, E, q);
    deriv(tx, tp, E, k3x, k3p);
    for (int i = 0; i < 3; ++i) { tx[i] = x[i] + h * k3x[i]; tp[i] = p[i] + h * k3p[i]; }
    project(tp, tx, E, q);
    deriv(tx, tp, E, k4x, k4p);
    for (int i = 0; i < 3; ++i) {
        x[i] += h * (k1x[i] + 2.0 * k2x[i] + 2.0 * k3x[i] + k4x[i]) / 6.0;
        p[i] += h * (k1p[i] + 2.0 * k2p[i] + 2.0 * k3p[i] + k4p[i]) / 6.0;
    }
    project(p, x, E, q);
}

inline double stepSize(double r, double base, double hmin, double hmax) {
    double h = base * std::pow(r, 1.3);
    if (h < hmin) h = hmin;
    if (h > hmax) h = hmax;
    return h;
}

// ---------------------------------------------------------------------------
// Full ray trace. Returns termination code.
//   0  escaped to infinity (r > ESC_R)
//   1  crossed the accretion disk (rc in (DISK_IN, DISK_OUT), z = 0 plane)
//   2  swallowed by the horizon
//   3  step budget exhausted
// ---------------------------------------------------------------------------
struct RayResult {
    int code = 0;
    double x[3] = {0, 0, 0};
    double p[3] = {0, 0, 0};
    double rc = 0.0;   // disk crossing radius
    double b = 0.0;    // signed angular momentum per unit energy at disk hit
    int steps = 0;
};

inline RayResult traceRay(const double x0[3], const double p0[3], double E,
                          double hBase, double hmin, double hmax, int maxSteps) {
    RayResult out;
    double x[3] = {x0[0], x0[1], x0[2]};
    double p[3] = {p0[0], p0[1], p0[2]};
    project(p, x, E, 0.0);
    double zPrev = x[2];
    double xPrev[3] = {x[0], x[1], x[2]};
    for (int i = 0; i < maxSteps; ++i) {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        if (r <= RS * 1.0005) { out.code = 2; break; }
        if (r > ESC_R) { out.code = 0; break; }
        const double h = stepSize(r, hBase, hmin, hmax);
        xPrev[0] = x[0]; xPrev[1] = x[1]; xPrev[2] = x[2];
        rk4step(x, p, E, 0.0, h);
        const double zn = x[2];
        if (zPrev * zn < 0.0) {
            const double t = zPrev / (zPrev - zn);
            const double xc0 = xPrev[0] + t * (x[0] - xPrev[0]);
            const double xc1 = xPrev[1] + t * (x[1] - xPrev[1]);
            const double rc = std::sqrt(xc0 * xc0 + xc1 * xc1);
            if (rc > DISK_IN && rc < DISK_OUT) {
                out.code = 1;
                out.rc = rc;
                out.b = x[0] * p[1] - x[1] * p[0]; // L_z (signed)
                out.x[0] = xc0; out.x[1] = xc1; out.x[2] = 0.0;
                out.p[0] = p[0]; out.p[1] = p[1]; out.p[2] = p[2];
                break;
            }
        }
        zPrev = zn;
        out.steps = i;
    }
    if (out.code != 1 && out.code != 2) {
        out.x[0] = x[0]; out.x[1] = x[1]; out.x[2] = x[2];
        out.p[0] = p[0]; out.p[1] = p[1]; out.p[2] = p[2];
        if (out.code != 0) out.code = 3;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Accretion disk physics (Novikov-Thorne thin disk).
// ---------------------------------------------------------------------------
inline double diskTemperature(double r) {
    // T(r) = T_peak * (r/rp)^-3/4 * [(1 - sqrt(6/r)) / (1 - sqrt(6/rp))]^1/4
    // with rp = 49/6 ~ 8.167 the radius of peak temperature.
    const double rp = 49.0 / 6.0;
    const double r6 = r > 6.02 ? r : 6.02;
    const double f1 = std::pow(rp / r6, 0.75);
    const double f2 = std::pow((1.0 - std::sqrt(6.0 / r6)) / (1.0 - std::sqrt(6.0 / rp)), 0.25);
    return f1 * f2; // normalized to 1 at peak
}

// Doppler + gravitational redshift factor for a Keplerian emitter at radius r.
inline double dopplerFactor(double r, double b) {
    const double om = std::pow(r, -1.5);                 // Keplerian Omega
    const double th = 1.0 / std::sqrt(std::max(1.0 - 3.0 / r, 1e-4));
    return 1.0 / (th * std::max(1.0 - b * om, 1e-4));
}

} // namespace geo
