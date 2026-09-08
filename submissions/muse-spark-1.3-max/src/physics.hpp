// black-hole-cpp-musemax — physics core (dependency-free).
//
// Geometric units G = c = 1 with black hole mass M = 1, so lengths/times
// are in units of M. Schwarzschild radius rs = 2M = 2, photon sphere 3M,
// critical impact parameter b_crit = 3*sqrt(3) M.
//
// The GPU fragment shader implements the same null-geodesic integrator
// below (orbital-plane reduction of Schwarzschild geodesics, integrated
// with velocity Verlet). This CPU mirror is used for:
//   - the bent light-ray visualization in the spacetime-grid view,
//   - automated scientific-accuracy self-tests (--selftest).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bh {

constexpr double PI = 3.14159265358979323846;
constexpr double MASS = 1.0;          // black hole mass M
constexpr double RS = 2.0;            // Schwarzschild radius 2M
constexpr double R_PHOTON = 3.0;      // photon sphere radius 3M
constexpr double B_CRIT = 5.196152422706632;  // 3*sqrt(3): shadow impact parameter
constexpr double ESCAPE_R = 50.0;     // radius at which rays are "at infinity"
constexpr double DISK_OUTER = 14.0;   // accretion disk outer edge (M)
constexpr double DISK_INNER_MIN = 2.5;// floor: disk must stay outside horizon

// Bardeen, Press & Teukolsky (1972) Kerr ISCO radius, a in (-1, 1).
// a = 0 -> 6M, a -> +1 -> 1M (prograde), a -> -1 -> 9M (retrograde).
inline double isco_kerr(double a) {
    if (a > 0.9999) a = 0.9999;
    if (a < -0.9999) a = -0.9999;
    const double a2 = a * a;
    const double z1 =
        1.0 + std::cbrt(1.0 - a2) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    const double z2 = std::sqrt(3.0 * a2 + z1 * z1);
    const double s = (a >= 0.0) ? 1.0 : -1.0;
    return 3.0 + z2 - s * std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
}

// Disk inner edge actually used by the renderer: Kerr ISCO, but never
// inside the Schwarzschild horizon used by the ray tracer.
inline double disk_inner(double spin) {
    const double isco = isco_kerr(spin);
    return isco > DISK_INNER_MIN ? isco : DISK_INNER_MIN;
}

// Schwarzschild lapse function 1 - rs/r.
inline double lapse(double r) { return 1.0 - RS / r; }

// Null radial geodesic acceleration d^2 r / dλ^2 in the orbital plane,
// derived from the effective potential V = (L^2/r^2)(1 - rs/r):
//   d^2r/dλ^2 = (L^2/r^3)(1 - 3M/r).
inline double null_accel(double r, double L) {
    const double r3 = r * r * r;
    return (L * L / r3) * (1.0 - 3.0 * MASS / r);
}

// Flamm's paraboloid: embedding of the Schwarzschild equatorial plane,
// w(r) = 2*sqrt(rs*(r - rs)). This is the "trapdoor" funnel surface.
inline double flamm_w(double r, double rs = RS) {
    return 2.0 * std::sqrt(rs * (r - rs));
}

// Page-Thorne / Shakura-Sunyaev flux shape F(r) ~ r^-3 (1 - sqrt(r_isco/r)).
// Disk temperature T ~ F^1/4.
inline double disk_flux(double r, double r_isco) {
    if (r <= r_isco) return 0.0;
    return (1.0 / (r * r * r)) * (1.0 - std::sqrt(r_isco / r));
}

// Radius of peak flux for the above profile: (49/36) r_isco.
inline double disk_flux_peak_r(double r_isco) { return r_isco * 49.0 / 36.0; }

// Kerr equatorial (prograde) coordinate angular velocity Ω = 1/(r^3/2 + a).
inline double omega_kerr(double r, double a) {
    return 1.0 / (std::pow(r, 1.5) + a * MASS);
}

// Locally measured orbital speed of a circular Schwarzschild geodesic,
// as seen by a static shell observer: v = sqrt(M/(r - 2M)).
// Exact for spin 0; used as the Doppler-velocity approximation otherwise.
inline double orbital_speed(double r) {
    if (r <= RS) return 0.95;
    const double v = std::sqrt(MASS / (r - RS));
    return v > 0.95 ? 0.95 : v;
}

// Combined gravitational + Doppler redshift factor g = E_obs / E_emit,
// from k·u products evaluated in a static-observer orthonormal tetrad:
//   k·u_obs  = -E / sqrt(1 - rs/r_obs)
//   k·u_emit = γ (-E/sqrt(1 - rs/r) + v (k_phys · φhat_disk))
// k_dot_phi is the PHYSICAL photon's spatial momentum (emitter -> observer)
// along the disk's azimuthal direction. Backward-traced rays run the
// opposite way, so callers must negate the traced direction. Matches the
// fragment shader exactly.
inline double g_factor(double E, double r, double r_obs, double v,
                       double k_dot_phi) {
    const double gamma = 1.0 / std::sqrt(1.0 - v * v);
    const double num = -E / std::sqrt(lapse(r_obs));
    const double den = gamma * (-E / std::sqrt(lapse(r)) + v * k_dot_phi);
    return num / den;
}

// Shadow angular radius for a static observer at r_obs:
// sin α = (b_crit / r_obs) sqrt(1 - rs/r_obs).
inline double shadow_angle(double r_obs) {
    const double s = (B_CRIT / r_obs) * std::sqrt(lapse(r_obs));
    return std::asin(s > 1.0 ? 1.0 : s);
}

struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double len() const { return std::sqrt(dot(*this)); }
    Vec3 norm() const { const double l = len(); return {x / l, y / l, z / l}; }
};

// Null geodesic state reduced to its orbital plane (spherical symmetry of
// Schwarzschild keeps every photon in one plane). Affine-parameter scaling
// k = 1: unit spatial speed at the camera.
struct PlaneRay {
    double r;    // radius
    double phi;  // angle in plane from start direction
    double pr;   // dr/dλ
    double L;    // conserved angular momentum
    double E;    // conserved energy

    // dr0_phys: radial component of the unit spatial ray direction
    //           (physical/orthonormal components at r0).
    // dt0_mag:  magnitude of the tangential component.
    static PlaneRay init(double r0, double dr0_phys, double dt0_mag) {
        PlaneRay s;
        const double sq = std::sqrt(lapse(r0));
        s.r = r0;
        s.phi = 0.0;
        s.pr = dr0_phys * sq;
        s.L = r0 * dt0_mag;
        const double f = lapse(r0);
        s.E = std::sqrt(s.pr * s.pr + f * s.L * s.L / (r0 * r0));
        return s;
    }

    double impact_param() const { return L / E; }

    // One velocity-Verlet step. Returns false once captured (r < rs).
    bool step(double h) {
        const double a0 = null_accel(r, L);
        const double rN = r + pr * h + 0.5 * a0 * h * h;
        if (rN < RS) return false;
        const double a1 = null_accel(rN, L);
        const double rmid = 0.5 * (r + rN);
        phi += L * h / (rmid * rmid);
        pr += 0.5 * (a0 + a1) * h;
        r = rN;
        return true;
    }
};

inline double geo_step_size(double r) {
    double h = 0.06 * r;
    if (h < 0.02) h = 0.02;
    if (h > 0.5) h = 0.5;
    return h;
}

struct TraceResult {
    bool captured = true;
    double total_phi = 0.0;  // swept angle (scattering orbits)
    std::vector<Vec3> path;  // world-space samples (optional)
};

// Trace a photon from world position p0 along unit direction d0.
// Plane basis: e1 = radial, e2 = tangential (both world-space).
// escape_r must exceed |p0|; rays crossing it count as escaped.
inline TraceResult trace_world(Vec3 p0, Vec3 d0, bool record_path,
                               int max_steps = 20000,
                               double escape_r = ESCAPE_R) {
    TraceResult out;
    const double r0 = p0.len();
    Vec3 e1 = p0 * (1.0 / r0);
    const double dr0 = d0.dot(e1);
    Vec3 tv = d0 - e1 * dr0;
    double dt0 = tv.len();
    Vec3 e2;
    if (dt0 > 1e-9) {
        e2 = tv * (1.0 / dt0);
    } else {
        // Degenerate: ray aimed exactly at the center. Pick any normal.
        Vec3 a = (std::fabs(e1.y) < 0.99) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 c = {e1.y * a.z - e1.z * a.y, e1.z * a.x - e1.x * a.z,
                  e1.x * a.y - e1.y * a.x};
        e2 = c.norm();
        dt0 = 0.0;
    }
    PlaneRay s = PlaneRay::init(r0, dr0, dt0);
    if (record_path) {
        out.path.reserve(1024);
        out.path.push_back(p0);
    }
    for (int i = 0; i < max_steps; ++i) {
        if (!s.step(geo_step_size(s.r))) {
            out.captured = true;
            out.total_phi = s.phi;
            return out;
        }
        if (s.r > escape_r) {
            out.captured = false;
            out.total_phi = s.phi;
            return out;
        }
        if (record_path && (i % 4 == 0)) {
            const double c = std::cos(s.phi), sn = std::sin(s.phi);
            out.path.push_back((e1 * c + e2 * sn) * s.r);
        }
    }
    out.captured = (s.r <= escape_r);
    out.total_phi = s.phi;
    return out;
}

// Trace a scattering ray with coordinate offset b fired from distance r0
// along +X from (-X, 0, b), X = sqrt(r0^2 - b^2).
//
// Finite-distance note: with unit physical speed at r0, the conserved
// quantities are exactly L = b and E = sqrt(1 - rs/r0), so the orbit is
// the same as an infinity photon with impact parameter L/E = b/sqrt(f).
// Capture threshold in b is therefore b_crit*sqrt(f(r0)), and weak
// deflection is 4M/(L/E). The self-tests compare against these exact
// finite-distance predictions.
inline TraceResult trace_impact(double b, double r0, bool record_path = false,
                                double escape_r = -1.0) {
    const double X = std::sqrt(r0 * r0 - b * b);
    if (escape_r < 0) escape_r = std::max(ESCAPE_R, r0 * 1.2);
    return trace_world(Vec3(-X, 0, b), Vec3(1, 0, 0), record_path, 20000,
                       escape_r);
}

// Self-tests (implemented in physics.cpp). Returns number of failures.
int run_self_tests();

}  // namespace bh
