// Physics core: Schwarzschild null geodesics, Novikov-Thorne thin-disk model,
// gravitational redshift / Doppler factor, Flamm paraboloid embedding,
// blackbody colour science.  Shared by the GLSL shader (mirrored) and the CPU
// self-test / geometry generators, so the two must stay in sync.
//
// Units: geometric, M = G M_bh / c^2 = 1 (length unit).
//        rs = 2M = 2, photon sphere = 3M, ISCO (a = 0) = 6M.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bh {

constexpr double M_GEO   = 1.0;                 // GM/c^2
constexpr double RS      = 2.0;                 // Schwarzschild radius
constexpr double R_ISCO  = 6.0;                 // innermost stable circular orbit
constexpr double R_PHOTON = 3.0;                // photon sphere
constexpr double R_FPeak = (49.0 / 36.0) * R_ISCO;  // peak of NT flux
constexpr double B_CRIT  = 5.1961524227066318805;   // 3*sqrt(3) M
constexpr double SIGMA_SB = 5.670374e-5;        // Stefan-Boltzmann, cgs erg cm^-2 s^-1 K^-4

// ---------------------------------------------------------------------------
// Null geodesics in Schwarzschild spacetime.
//
// With u = 1/r measured in the photon's orbital plane and phi the in-plane
// polar angle, every photon obeys exactly:
//        d^2u/dphi^2 + u = 3 M u^2                (Einstein's equation)
// and carries the constant  1/b^2 = (du/dphi)^2 + u^2 (1 - 2M u),
// where b = L/E is the impact parameter.
// ---------------------------------------------------------------------------
struct Photon {
    double u = 0.0;   // 1/r
    double w = 0.0;   // du/dphi
};

inline Photon deriv(const Photon& y) { return {y.w, 3.0 * M_GEO * y.u * y.u - y.u}; }

inline Photon rk4Step(Photon y, double h) {
    Photon k1 = deriv(y);
    Photon k2 = deriv({y.u + 0.5 * h * k1.u, y.w + 0.5 * h * k1.w});
    Photon k3 = deriv({y.u + 0.5 * h * k2.u, y.w + 0.5 * h * k2.w});
    Photon k4 = deriv({y.u + h * k3.u, y.w + h * k3.w});
    return {y.u + (h / 6.0) * (k1.u + 2 * k2.u + 2 * k3.u + k4.u),
            y.w + (h / 6.0) * (k1.w + 2 * k2.w + 2 * k3.w + k4.w)};
}

// Adaptive step heuristic (identical to the one in raytrace.frag): the
// dominant error is the RK4 phase error ~ h^5/120, so the step is capped in
// the far field and tightened where u (i.e. 1/r) grows near the hole.
inline double stepH(double u, double stepMax) {
    return stepMax / (1.0 + 6.0 * u + 25.0 * u * u);
}

inline double invB2(const Photon& y) {
    return y.w * y.w + y.u * y.u * (1.0 - 2.0 * M_GEO * y.u);
}

enum class Term { Captured, Escaped, MaxSteps };
struct TraceOut {
    Term   term = Term::MaxSteps;
    double phi = 0.0;
    Photon y{};
    int    steps = 0;
};

// Integrate until the photon crosses the horizon, escapes to r >= rFar while
// moving outwards, or exhausts the step budget.
TraceOut tracePhoton(Photon y, double stepMax, double rFar, int maxSteps);

// ---------------------------------------------------------------------------
// Gravitational + Doppler shift for gas on a circular Keplerian orbit.
//   g = nu_obs / nu_emit = sqrt(1 - rs/r) / (1 - Omega * L_z/E),  Omega = sqrt(M/r^3)
// with L_z/E = b * (n_hat . z_hat), n_hat the normal of the photon's plane.
// ---------------------------------------------------------------------------
inline double keplerOmega(double r) { return std::sqrt(M_GEO / (r * r * r)); }
double gFactor(double r, double bLz);

// ---------------------------------------------------------------------------
// Novikov-Thorne (Page 1974) radiative flux for a = 0, in cgs, r in M units:
//   F(r) = 3 G M Mdot / (8 pi r^3) * (1 - sqrt(r_in/r))
// ---------------------------------------------------------------------------
inline double ntFluxShape(double r) {
    if (r <= R_ISCO) return 0.0;
    return (1.0 - std::sqrt(R_ISCO / r)) / (r * r * r);
}

struct DiskParams {
    double massMsun = 10.0;
    double mdotMsunPerYr = 1e-7;
    double F0 = 0.0;         // erg cm^-2 s^-1 multiplying ntFluxShape(r)
    double FPeak = 0.0;      // peak flux over the disk
    double TPhysPeak = 0.0;  // effective temperature at the peak, kelvin
    double tempScale = 1.0;  // display temperature mapping (T_disp = g*T/tempScale)
};
DiskParams makeDiskParams(double massMsun, double mdotMsunPerYr, bool physicalColors);

// ---------------------------------------------------------------------------
// Flamm paraboloid: isometric embedding of the t = const Schwarzschild slice.
//   z(r) = 2 sqrt(rs (r - rs)),   1 + z'^2 = 1 / (1 - rs/r)   (exact isometry)
//   Gaussian curvature K = -rs / (2 r^3)
// ---------------------------------------------------------------------------
inline double flammZ(double r) { return 2.0 * std::sqrt(RS * (r - RS)); }
inline double flammSlope(double r) { return std::sqrt(RS / (r - RS)); }
inline double gaussianCurvature(double r) { return -RS / (2.0 * r * r * r); }

// ---------------------------------------------------------------------------
// Blackbody colour: Planck spectrum x CIE 1931 fit x XYZ->linear sRGB,
// normalised so max(rgb) = 1 (intensity is applied separately).
// ---------------------------------------------------------------------------
std::vector<float> buildBlackbodyLUT(int n);  // n * 4 floats, RGBA, log-spaced T

// ---------------------------------------------------------------------------
// Equatorial null geodesics for the "geodesic gallery" view (mode 3).
// ---------------------------------------------------------------------------
struct Polyline {
    std::vector<std::array<double, 2>> pts;
    bool captured = false;
    double b = 0.0;
};
Polyline traceEquatorialRay(double b, double rStart, double phiMax, int maxSteps);

// ---------------------------------------------------------------------------
// Radial free fall from rest at infinity (for the spacetime-grid demo).
//   proper time:        (dr/dtau)^2 = rs/r
//   Schwarzschild time: dr/dt = -(1 - rs/r) sqrt(rs/r)   -> freezes at r = rs
// ---------------------------------------------------------------------------
double infallProperRadius(double tau, double r0);
void infallCoordinateTimeTable(double r0, double dt, int n, std::vector<double>& outR);

}  // namespace bh
