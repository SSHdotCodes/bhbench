#pragma once

#include <cmath>

// Schwarzschild geometry in geometrized units G = c = M = 1.
// This CPU reference mirrors the null-geodesic equation used by the GPU.
namespace bh {

constexpr double kHorizon = 2.0;
constexpr double kPhotonSphere = 3.0;
constexpr double kISCO = 6.0;
constexpr double kCriticalImpact = 3.0 * 1.7320508075688772935;

struct OrbitState {
    double u;  // 1 / r
    double up; // du / dphi
};

// u'' + u = 3 M u^2, with M = 1.
inline double orbitAcceleration(double u) {
    return u * (3.0 * u - 1.0);
}

inline OrbitState derivative(const OrbitState& s) {
    return {s.up, orbitAcceleration(s.u)};
}

inline OrbitState operator+(const OrbitState& a, const OrbitState& b) {
    return {a.u + b.u, a.up + b.up};
}

inline OrbitState operator*(const OrbitState& a, double x) {
    return {a.u * x, a.up * x};
}

inline OrbitState rk4(const OrbitState& s, double h) {
    const OrbitState k1 = derivative(s);
    const OrbitState k2 = derivative(s + k1 * (0.5 * h));
    const OrbitState k3 = derivative(s + k2 * (0.5 * h));
    const OrbitState k4 = derivative(s + k3 * h);
    return s + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (h / 6.0);
}

inline double lapse(double r) {
    return 1.0 - 2.0 / r;
}

// Impact parameter measured by a static observer at radius r. nRadial and
// nTangential are components of a unit direction in the static orthonormal frame.
inline double impactParameter(double r, double nTangential) {
    return r * nTangential / (lapse(r) > 0.0 ? std::sqrt(lapse(r)) : 0.0);
}

inline OrbitState initialOrbit(double r, double nRadial, double nTangential) {
    const double b = impactParameter(r, nTangential);
    return {1.0 / r, -nRadial / b};
}

// Circular geodesic speed measured in the local static orthonormal frame.
inline double circularSpeed(double r) {
    return 1.0 / std::sqrt(r - 2.0);
}

inline double circularGamma(double r) {
    return std::sqrt((r - 2.0) / (r - 3.0));
}

// Zero-torque thin-disk radial factor, proportional to emitted sigma T^4.
inline double diskFluxFactor(double r, double rInner = kISCO) {
    return (rInner * rInner * rInner) / (r * r * r) *
           (1.0 - std::sqrt(rInner / r));
}

} // namespace bh
