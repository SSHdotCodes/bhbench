// Schwarzschild null geodesics and Novikov–Thorne thin-disk flux.
// Geometric units: G = c = 1, black-hole mass M = 1, so r_s = 2.
//
// Cartesian Schwarzschild metric (r = |x|):
//   g_tt = -(1 - r_s/r)
//   g_ij = δ_ij + [r_s / (r - r_s)] n_i n_j
//   g^tt = -1 / (1 - r_s/r)
//   g^ij = δ^ij - (r_s / r^3) x^i x^j
//
// Rays are integrated with the Hamiltonian form
//   dx^i/dλ = g^ij q_j
//   dq_i/dλ = -1/2 ∂_i g^{αβ} q_α q_β
// where q_μ is past-directed, so the spatial curve runs from the camera
// out into the scene. q_t is conserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace bh {

inline constexpr double M = 1.0;
inline constexpr double RS = 2.0 * M;
inline constexpr double PHOTON_SPHERE = 3.0 * M;
inline constexpr double ISCO = 6.0 * M;
inline constexpr double B_CRIT = 5.196152422706632; // 3 * sqrt(3) * M

struct State {
    double x, y, z;
    double px, py, pz;
    double pt;
};

struct Deriv {
    double x, y, z;
    double px, py, pz;
};

inline double radius(const State& s) {
    return std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
}

// Returns false if the state is inside the horizon guard (derivatives singular).
inline bool geodesic_deriv(const State& s, double rs, Deriv& d) {
    const double r = radius(s);
    if (r < rs + 1e-4) return false;
    const double rm = r - rs;
    const double xp = s.x * s.px + s.y * s.py + s.z * s.pz;
    const double inv_r = 1.0 / r;
    const double inv_r3 = inv_r * inv_r * inv_r;
    const double inv_r5 = inv_r3 * inv_r * inv_r;
    const double rs_xp_r3 = rs * inv_r3 * xp;

    d.x = s.px - rs_xp_r3 * s.x;
    d.y = s.py - rs_xp_r3 * s.y;
    d.z = s.pz - rs_xp_r3 * s.z;

    const double pt2 = s.pt * s.pt;
    const double radial_coeff =
        -(0.5 * rs * pt2) / (r * rm * rm) - (1.5 * rs * inv_r5 * xp * xp);
    const double mom_coeff = rs_xp_r3; // rs * (x·p) / r^3

    d.px = radial_coeff * s.x + mom_coeff * s.px;
    d.py = radial_coeff * s.y + mom_coeff * s.py;
    d.pz = radial_coeff * s.z + mom_coeff * s.pz;
    return true;
}

inline State state_add(const State& s, const Deriv& d, double h) {
    State o = s;
    o.x += h * d.x;
    o.y += h * d.y;
    o.z += h * d.z;
    o.px += h * d.px;
    o.py += h * d.py;
    o.pz += h * d.pz;
    return o;
}

inline Deriv deriv_add(const Deriv& a, const Deriv& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.px + b.px, a.py + b.py, a.pz + b.pz};
}

inline Deriv deriv_scale(const Deriv& a, double h) {
    return {a.x * h, a.y * h, a.z * h, a.px * h, a.py * h, a.pz * h};
}

// One RK4 step. Returns false if a stage entered the horizon guard.
inline bool rk4(State& s, double h, double rs) {
    Deriv k1, k2, k3, k4;
    if (!geodesic_deriv(s, rs, k1)) return false;
    State s2 = state_add(s, k1, 0.5 * h);
    if (!geodesic_deriv(s2, rs, k2)) return false;
    State s3 = state_add(s, k2, 0.5 * h);
    if (!geodesic_deriv(s3, rs, k3)) return false;
    State s4 = state_add(s, k3, h);
    if (!geodesic_deriv(s4, rs, k4)) return false;

    Deriv acc = deriv_add(k1, deriv_scale(k2, 2.0));
    acc = deriv_add(acc, deriv_scale(k3, 2.0));
    acc = deriv_add(acc, k4);
    s = state_add(s, acc, h / 6.0);
    return radius(s) > rs + 1e-3;
}

// Affine step that moves the ray by about `coord_step` in coordinate distance,
// and refuses to jump a large fraction of the distance to the horizon.
inline double adaptive_step(const State& s, double rs, double coord_step) {
    Deriv d;
    if (!geodesic_deriv(s, rs, d)) return 1e-4;
    const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const double r = radius(s);
    double target = coord_step;
    if (r < 12.0) target = std::min(target, 0.045 * r);
    if (r < 6.0) target = std::min(target, 0.02 * r);
    const double guard = 0.30 * std::max(r - rs, 1e-3);
    target = std::min(target, guard);
    const double cyl = std::sqrt(s.x * s.x + s.y * s.y);
    if (std::fabs(s.z) < 2.5 && cyl < 30.0) target = std::min(target, 0.12);
    double h = target / std::max(speed, 1e-8);
    if (h > 1.5) h = 1.5;
    if (h < 1e-4) h = 1e-4;
    return h;
}

// Null constraint g^{μν} q_μ q_ν. Should stay near 0.
inline double null_constraint(const State& s, double rs) {
    const double r = radius(s);
    const double alpha = 1.0 - rs / r;
    const double xp = s.x * s.px + s.y * s.py + s.z * s.pz;
    const double spatial =
        (s.px * s.px + s.py * s.py + s.pz * s.pz) - (rs / (r * r * r)) * xp * xp;
    return -s.pt * s.pt / alpha + spatial;
}

// Static-observer orthonormal frame at `pos`, looking at the origin.
// basis_* are contravariant coordinate components of the local triad.
struct CameraFrame {
    double pos[3];
    double right[3];
    double up[3];
    double forward[3]; // already scaled by sqrt(alpha): a unit radial vector
    double sqrt_alpha;
    double alpha;
    double r;
};

inline CameraFrame make_camera(double r, double theta, double phi) {
    CameraFrame c{};
    c.r = r;
    c.pos[0] = r * std::sin(theta) * std::cos(phi);
    c.pos[1] = r * std::sin(theta) * std::sin(phi);
    c.pos[2] = r * std::cos(theta);
    c.alpha = 1.0 - RS / r;
    c.sqrt_alpha = std::sqrt(c.alpha);
    const double nx = c.pos[0] / r;
    const double ny = c.pos[1] / r;
    const double nz = c.pos[2] / r;
    // Euclidean forward (toward the hole) and an orthonormal tangential pair.
    const double fx = -nx, fy = -ny, fz = -nz;
    const double ux0 = (std::fabs(nz) < 0.92) ? 0.0 : 0.0;
    const double uy0 = (std::fabs(nz) < 0.92) ? 0.0 : 1.0;
    const double uz0 = (std::fabs(nz) < 0.92) ? 1.0 : 0.0;
    // right = normalize(forward × world_up)
    double rx = fy * uz0 - fz * uy0;
    double ry = fz * ux0 - fx * uz0;
    double rz = fx * uy0 - fy * ux0;
    double rn = std::sqrt(rx * rx + ry * ry + rz * rz);
    rx /= rn;
    ry /= rn;
    rz /= rn;
    // up = right × forward?  We want up = right × forward so that
    // right = forward × up holds... 
    // lookAt convention: up_cam = cross(right, forward)? No.
    // Standard: right = normalize(cross(forward, worldUp))
    //           camUp = cross(right, forward)
    // cross(right, forward):
    const double upx = ry * fz - rz * fy;
    const double upy = rz * fx - rx * fz;
    const double upz = rx * fy - ry * fx;
    c.right[0] = rx;
    c.right[1] = ry;
    c.right[2] = rz;
    c.up[0] = upx;
    c.up[1] = upy;
    c.up[2] = upz;
    // Contravariant unit vector pointing at the hole.
    c.forward[0] = c.sqrt_alpha * fx;
    c.forward[1] = c.sqrt_alpha * fy;
    c.forward[2] = c.sqrt_alpha * fz;
    return c;
}

// Local direction (right, up, forward) -> initial past-directed covector.
// local must be a Euclidean unit vector in the observer's frame.
inline State ray_state(const CameraFrame& cam, double lr, double lu, double lf) {
    const double dx = lr * cam.right[0] + lu * cam.up[0] + lf * cam.forward[0];
    const double dy = lr * cam.right[1] + lu * cam.up[1] + lf * cam.forward[1];
    const double dz = lr * cam.right[2] + lu * cam.up[2] + lf * cam.forward[2];
    const double r = cam.r;
    const double gamma = RS / (r - RS);
    const double nx = cam.pos[0] / r;
    const double ny = cam.pos[1] / r;
    const double nz = cam.pos[2] / r;
    const double nd = nx * dx + ny * dy + nz * dz;
    State s;
    s.x = cam.pos[0];
    s.y = cam.pos[1];
    s.z = cam.pos[2];
    s.px = dx + gamma * nx * nd;
    s.py = dy + gamma * ny * nd;
    s.pz = dz + gamma * nz * nd;
    s.pt = cam.sqrt_alpha; // ω_loc = 1
    return s;
}

// Equatorial ray at (r0, 0, 0) with local angle ψ from the inward radial
// direction, toward +y. Impact parameter b = r sinψ / sqrt(α).
inline State equatorial_ray(double r0, double b) {
    const double alpha = 1.0 - RS / r0;
    const double sqrt_a = std::sqrt(alpha);
    double sinpsi = b * sqrt_a / r0;
    if (sinpsi > 1.0) sinpsi = 1.0;
    const double psi = std::asin(sinpsi);
    State s;
    s.x = r0;
    s.y = 0.0;
    s.z = 0.0;
    s.pt = sqrt_a;
    s.px = -std::cos(psi) / sqrt_a;
    s.py = std::sin(psi);
    s.pz = 0.0;
    return s;
}

enum class Fate { Escaped, Captured, Timeout };

struct TraceResult {
    Fate fate;
    double min_r;
    double deflected; // angle between initial and final coordinate velocity
    int steps;
    double constraint;
    State final_state;
    double vx0, vy0, vz0;
    double vx1, vy1, vz1;
};

inline TraceResult trace_ray(State s, double coord_step, int max_steps, double escape_r) {
    TraceResult tr{};
    tr.fate = Fate::Timeout;
    tr.min_r = radius(s);
    tr.steps = 0;
    tr.constraint = 0.0;
    Deriv d0;
    geodesic_deriv(s, RS, d0);
    const double sp0 = std::sqrt(d0.x * d0.x + d0.y * d0.y + d0.z * d0.z);
    tr.vx0 = d0.x / sp0;
    tr.vy0 = d0.y / sp0;
    tr.vz0 = d0.z / sp0;
    bool saw_peri = false;
    for (int i = 0; i < max_steps; ++i) {
        const double r = radius(s);
        tr.min_r = std::min(tr.min_r, r);
        tr.constraint = std::max(tr.constraint, std::fabs(null_constraint(s, RS)));
        if (r < RS + 0.06) {
            tr.fate = Fate::Captured;
            tr.steps = i;
            tr.final_state = s;
            return tr;
        }
        const double h = adaptive_step(s, RS, coord_step);
        State prev = s;
        if (!rk4(s, h, RS)) {
            tr.fate = Fate::Captured;
            tr.steps = i + 1;
            tr.final_state = prev;
            return tr;
        }
        const double r1 = radius(s);
        tr.min_r = std::min(tr.min_r, r1);
        if (r1 < 0.85 * radius(prev) || r1 < tr.min_r + 1.0) saw_peri = true;
        if (r1 < 0.7 * std::sqrt(prev.x * prev.x + prev.y * prev.y + prev.z * prev.z))
            saw_peri = true;
        Deriv d1;
        if (!geodesic_deriv(s, RS, d1)) {
            tr.fate = Fate::Captured;
            tr.steps = i + 1;
            return tr;
        }
        const double outward = s.x * d1.x + s.y * d1.y + s.z * d1.z;
        if (r1 > escape_r && outward > 0.0 && tr.min_r < 0.9 * escape_r) {
            const double sp = std::sqrt(d1.x * d1.x + d1.y * d1.y + d1.z * d1.z);
            tr.vx1 = d1.x / sp;
            tr.vy1 = d1.y / sp;
            tr.vz1 = d1.z / sp;
            const double cth = std::clamp(tr.vx0 * tr.vx1 + tr.vy0 * tr.vy1 + tr.vz0 * tr.vz1, -1.0, 1.0);
            tr.deflected = std::acos(cth);
            tr.fate = Fate::Escaped;
            tr.steps = i + 1;
            tr.final_state = s;
            (void)saw_peri;
            return tr;
        }
        tr.steps = i + 1;
    }
    tr.final_state = s;
    return tr;
}

// Circular-orbit constants (M = 1). Valid for r > 3.
inline double orbit_E(double r) {
    return (1.0 - 2.0 / r) / std::sqrt(1.0 - 3.0 / r);
}
inline double orbit_L(double r) {
    return std::sqrt(r) / std::sqrt(1.0 - 3.0 / r);
}
inline double orbit_Omega(double r) { return std::pow(r, -1.5); }

// dL/dr for equatorial circular geodesics.
inline double orbit_dLdr(double r) {
    const double a = 1.0 - 3.0 / r;
    const double L = std::sqrt(r) / std::sqrt(a);
    const double dln = 0.5 / r * ((1.0 - 6.0 / r) / a);
    return L * dln;
}

// Integrand (E - Ω L) dL/dr = sqrt(1 - 3/r) * dL/dr.
inline double flux_integrand(double r) {
    if (r <= ISCO) return 0.0;
    const double eol = std::sqrt(1.0 - 3.0 / r); // E - Ω L
    return eol * orbit_dLdr(r);
}

// One-sided Novikov–Thorne flux for Ṁ = 1, M = 1:
//   F(r) = 1/(4π r) * (-Ω_,r) / (E-ΩL)^2 * ∫_ISCO^r (E-ΩL) L_,r dr'
struct FluxTable {
    static constexpr int N = 512;
    double r_in = ISCO;
    double r_out = 80.0;
    double r[N]{};
    double F[N]{};      // raw flux, Ṁ = 1
    double Fnorm[N]{};  // F / Fmax
    double Fmax = 0.0;
    double r_peak = ISCO;
};

inline FluxTable build_flux_table() {
    FluxTable t;
    // Running trapezoid on a fine grid, sampled into the table.
    const int fine = 20000;
    const double r0 = ISCO;
    const double r1 = t.r_out;
    double integ = 0.0;
    double prev_r = r0;
    double prev_y = 0.0; // integrand vanishes at ISCO
    int sample = 0;
    t.r[0] = r0;
    t.F[0] = 0.0;
    sample = 1;
    for (int i = 1; i <= fine; ++i) {
        const double r = r0 + (r1 - r0) * (static_cast<double>(i) / fine);
        const double y = flux_integrand(r);
        integ += 0.5 * (prev_y + y) * (r - prev_r);
        prev_r = r;
        prev_y = y;
        // Record ~N samples.
        const int target = static_cast<int>(std::llround(static_cast<double>(i) / fine * (t.N - 1)));
        if (target >= sample && sample < t.N) {
            const double eol2 = 1.0 - 3.0 / r;
            const double dOmega = 1.5 * std::pow(r, -2.5); // -Ω_,r
            const double f = (dOmega / eol2) * integ;
            const double F = f / (4.0 * M_PI * r);
            t.r[sample] = r;
            t.F[sample] = F;
            if (F > t.Fmax) {
                t.Fmax = F;
                t.r_peak = r;
            }
            ++sample;
        }
    }
    while (sample < t.N) {
        t.r[sample] = t.r[sample - 1];
        t.F[sample] = t.F[sample - 1];
        ++sample;
    }
    for (int i = 0; i < t.N; ++i) t.Fnorm[i] = (t.Fmax > 0.0) ? t.F[i] / t.Fmax : 0.0;
    return t;
}

inline double sample_Fnorm(const FluxTable& t, double r) {
    if (r <= t.r_in || r >= t.r_out) return 0.0;
    const double u = (r - t.r_in) / (t.r_out - t.r_in);
    const double f = u * (t.N - 1);
    int i = static_cast<int>(f);
    if (i < 0) i = 0;
    if (i >= t.N - 1) i = t.N - 2;
    const double a = f - i;
    return t.Fnorm[i] * (1.0 - a) + t.Fnorm[i + 1] * a;
}

// Largest root of r^3 - b^2 r + 2 b^2 = 0 above the photon sphere (turning point).
inline double turning_point(double b) {
    double r = b; // weak-field guess
    for (int i = 0; i < 40; ++i) {
        const double f = r * r * r - b * b * r + 2.0 * b * b;
        const double df = 3.0 * r * r - b * b;
        r -= f / df;
    }
    return r;
}

} // namespace bh
