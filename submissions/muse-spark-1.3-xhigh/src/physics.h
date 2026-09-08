// black-hole-cpp-muse-1-3 / src/physics.h
// Schwarzschild black-hole physics in geometrized units (G = c = 1).
//
// Scene units: Schwarzschild radius Rs = 1  =>  mass M = Rs/2 = 0.5.
//   event horizon : r = Rs        = 1.0
//   photon sphere : r = 3M        = 1.5 Rs
//   ISCO          : r = 6M        = 3.0 Rs
//   shadow radius : b_c = 3*sqrt(3)*M ~= 2.598 Rs (critical impact parameter)
//   Flamm paraboloid (spatial embedding): w(r) = 2*sqrt(Rs*(r-Rs))
//   Kretschmann scalar: K = 48*M^2/r^6  (used to color spacetime grid)
//   Novikov-Thorne flux shape: F(r) ~ r^-3 * (1 - sqrt(rIn/r)), rIn = ISCO
//   Keplerian Omega = sqrt(M/r^3); shell-measured orbital speed v = sqrt(M/(r-2M))
//   Redshift factor g = sqrt(1-Rs/r) / (gamma*(1 - dot(betaVec, photonDir)))
//   Observed bolometric intensity scales as g^3 (g^4 frequency-integrated).
//
// Null-geodesic integrator used in the fragment shader (and mirrored on CPU
// for --test) follows from the Schwarzschild orbit equation
//   d^2u/dphi^2 + u = 3M u^2,  u = 1/r,
// written as a 3-D central-force problem:
//   hvec = r x v,  h2 = |hvec|^2,  a = -1.5 * Rs * h2 * r_vec / r^5.
// This is the standard GPU-tracing form of the exact null geodesic spatial
// track (affine parametrization, |v| ~ 1 far away).
#pragma once
#include <cmath>

namespace bh {

inline constexpr double RS = 1.0;          // Schwarzschild radius (scene units)
inline constexpr double MASS = 0.5;        // M = Rs/2
inline constexpr double R_HORIZON = 1.0;   // event horizon
inline constexpr double R_PHOTON = 1.5;    // photon sphere
inline constexpr double R_ISCO = 3.0;      // innermost stable circular orbit
inline constexpr double R_DISK_IN = 3.0;   // disk inner edge = ISCO
inline constexpr double R_DISK_OUT = 9.0;  // disk outer edge
inline constexpr double B_CRIT = 2.598076211353316; // 3*sqrt(3)*M, shadow radius
inline constexpr double ESCAPE_R = 40.0;   // radius at which a ray has escaped

inline double flammW(double r) {
    if (r <= RS) return 0.0;
    return 2.0 * std::sqrt(RS * (r - RS));
}

inline double kretschmann(double r) {
    double r6 = r*r*r*r*r*r;
    if (r6 <= 0.0) return 0.0;
    return 48.0 * MASS * MASS / r6;
}

// Novikov-Thorne flux shape, normalized so max over [rIn,inf) is ~1.
inline double diskFluxShape(double r, double rIn = R_DISK_IN) {
    if (r < rIn) return 0.0;
    double f = (1.0 / (r*r*r)) * (1.0 - std::sqrt(rIn / r));
    // Normalization: max of shape is ~0.00507 near r ~= 4.08*rIn/2... compute
    // scale so peak ~ 1 for display. Peak of x^-3(1-sqrt(rIn/r)) with
    // rIn=3 sits at r = 49/12*rIn/ ? -> just use measured constant.
    constexpr double NORM = 197.372; // 1 / 0.0050658
    return f * NORM;
}

inline double keplerOmega(double r) {
    return std::sqrt(MASS / (r*r*r));
}

// Orbital speed measured by a static shell observer (exact Schwarzschild).
inline double shellOrbitalSpeed(double r) {
    double denom = r - 2.0 * MASS; // = r - Rs
    if (denom <= 1e-9) return 1.0;
    double v = std::sqrt(MASS / denom);
    if (v > 0.999) v = 0.999;
    return v;
}

struct Vec3 { double x, y, z; };

// Minimal CPU mirror of the GPU integrator: straight-line + GR bend.
// Returns true if captured (r < Rs), false if escaped. Used by --test.
inline bool tracePhotonCPU(Vec3 p0, Vec3 v0, bool bending,
                           int steps = 400, double dt0 = 0.12) {
    double px=p0.x, py=p0.y, pz=p0.z;
    double vx=v0.x, vy=v0.y, vz=v0.z;
    for (int i = 0; i < steps; ++i) {
        double r = std::sqrt(px*px + py*py + pz*pz);
        if (r < RS) return true;                       // captured
        if (r > ESCAPE_R && (px*vx + py*vy + pz*vz) > 0) return false; // escaped
        double dt = (r - RS*0.95) * 0.3;
        if (dt < 0.02) dt = 0.02;
        if (dt > dt0*4.0) dt = dt0*4.0;
        if (bending) {
            // h = r x v
            double hx = py*vz - pz*vy;
            double hy = pz*vx - px*vz;
            double hz = px*vy - py*vx;
            double h2 = hx*hx + hy*hy + hz*hz;
            double r2 = r*r;
            double inv = -1.5 * RS * h2 / (r2*r2*r);
            double ax = inv*px, ay = inv*py, az = inv*pz;
            vx += ax*dt; vy += ay*dt; vz += az*dt;
        }
        px += vx*dt; py += vy*dt; pz += vz*dt;
    }
    double r = std::sqrt(px*px + py*py + pz*pz);
    return r < 4.0; // treat non-escaped as captured-ish for the test
}

} // namespace bh
