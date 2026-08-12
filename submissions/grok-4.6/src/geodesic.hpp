#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

// Geometric units: G = c = 1. Schwarzschild radius rs = 2M.
// Null geodesics from the Hamiltonian
//   H = 1/2 [ -E^2 / α + α p_r^2 + p_θ^2 / r^2 + L_z^2 / (r^2 sin^2 θ) ]
// with α = 1 - 2M/r. Used on the CPU to draw a handful of sample light
// trajectories so the spacetime fabric can show captured vs scattered rays.

namespace bh {

struct GeoState {
    double r = 0, th = 0, ph = 0, pr = 0, pth = 0;
};

inline void geoDeriv(const GeoState& s, double E, double Lz, double M, GeoState& d) {
    const double rs = 2.0 * M;
    const double r = std::max(s.r, rs * 1.001);
    const double alpha = 1.0 - rs / r;
    const double sth = std::sin(s.th);
    const double cth = std::cos(s.th);
    const double sinSafe = std::copysign(std::max(std::abs(sth), 1e-5), sth == 0.0 ? 1.0 : sth);
    const double sin2 = sinSafe * sinSafe;

    d.r = alpha * s.pr;
    d.th = s.pth / (r * r);
    d.ph = Lz / (r * r * sin2);
    d.pr = -(M / (r * r)) * (E * E / (alpha * alpha) + s.pr * s.pr)
         + (s.pth * s.pth + Lz * Lz / sin2) / (r * r * r);
    d.pth = (Lz * Lz * cth) / (r * r * sin2 * sinSafe);
}

inline GeoState geoAdd(const GeoState& a, const GeoState& b, double h) {
    return {a.r + h * b.r, a.th + h * b.th, a.ph + h * b.ph,
            a.pr + h * b.pr, a.pth + h * b.pth};
}

inline void rk4(GeoState& s, double E, double Lz, double M, double h) {
    GeoState k1, k2, k3, k4;
    geoDeriv(s, E, Lz, M, k1);
    GeoState s2 = geoAdd(s, k1, h * 0.5);
    geoDeriv(s2, E, Lz, M, k2);
    GeoState s3 = geoAdd(s, k2, h * 0.5);
    geoDeriv(s3, E, Lz, M, k3);
    GeoState s4 = geoAdd(s, k3, h);
    geoDeriv(s4, E, Lz, M, k4);
    s.r += h * (k1.r + 2 * k2.r + 2 * k3.r + k4.r) / 6.0;
    s.th += h * (k1.th + 2 * k2.th + 2 * k3.th + k4.th) / 6.0;
    s.ph += h * (k1.ph + 2 * k2.ph + 2 * k3.ph + k4.ph) / 6.0;
    s.pr += h * (k1.pr + 2 * k2.pr + 2 * k3.pr + k4.pr) / 6.0;
    s.pth += h * (k1.pth + 2 * k2.pth + 2 * k3.pth + k4.pth) / 6.0;
}

inline glm::vec3 sphToCart(double r, double th, double ph) {
    const double sth = std::sin(th);
    return glm::vec3(static_cast<float>(r * sth * std::cos(ph)),
                     static_cast<float>(r * std::cos(th)),
                     static_cast<float>(r * sth * std::sin(ph)));
}

// Flamm embedding of the equatorial slice, zeroed at rMatch so the well
// sits in a flat pedagogical exterior (the true embedding keeps growing
// like sqrt(r) and never flattens).
inline float flammY(float r, float M, float rMatch) {
    const float rs = 2.0f * M;
    if (r >= rMatch) {
        return 0.0f;
    }
    const auto z = [&](float rr) -> float {
        rr = std::max(rr, rs * 1.001f);
        return 2.0f * std::sqrt(rs * (rr - rs));
    };
    return z(r) - z(rMatch);
}

// Equatorial incoming ray with impact parameter b = L/E, started far on -x.
inline std::vector<glm::vec3> integrateEquatorialRay(double M, double b, int maxSteps = 2800) {
    const double rs = 2.0 * M;
    const double r0 = 48.0 * M;
    const double E = 1.0;
    const double Lz = b * E;

    GeoState s;
    s.r = r0;
    s.th = 1.5707963267948966;
    s.ph = 3.141592653589793;
    const double alpha = 1.0 - rs / s.r;
    // Ingoing, with a small azimuthal kick from the impact parameter.
    // Local radial / azimuthal direction of a static observer.
    const double nphi = (s.r > 0.0) ? (b / s.r) : 0.0;
    const double nr = -std::sqrt(std::max(0.0, 1.0 - nphi * nphi));
    s.pr = nr / std::sqrt(std::max(alpha, 1e-8));
    s.pth = 0.0;

    std::vector<glm::vec3> pts;
    pts.reserve(static_cast<size_t>(maxSteps / 2));
    const float rOuter = static_cast<float>(16.0 * M);

    for (int i = 0; i < maxSteps; ++i) {
        if (s.r < rs * 1.02 || s.r > 70.0 * M) {
            break;
        }
        if (!std::isfinite(s.r) || !std::isfinite(s.ph)) {
            break;
        }
        glm::vec3 p = sphToCart(s.r, s.th, s.ph);
        p.y = flammY(static_cast<float>(s.r), static_cast<float>(M), rOuter) + 0.04f;
        pts.push_back(p);

        const double h = std::clamp(0.08 * s.r / (1.0 + 6.0 * M / s.r), 0.012, 0.35);
        rk4(s, E, Lz, M, h);
    }
    return pts;
}

} // namespace bh
