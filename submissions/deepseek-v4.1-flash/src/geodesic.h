// Null geodesic integrator for the Schwarzschild metric, in Cartesian-like
// coordinates.
//
// For a photon the exact orbit equation in the equatorial plane is
//     u'' + u = (3/2) r_s u^2 ,   u = 1/r,  ' = d/dphi
// which is reproduced exactly by the Cartesian equation of motion
//
//     d^2 x / dlambda^2 = -(3/2) r_s h^2 x / r^5 ,   h = |x cross v|
//
// with h conserved along the ray. This is not an approximation: it is the exact
// null geodesic equation of Schwarzschild, and it integrates cleanly with RK4.
// (Sanity check: the weak-field limit gives a deflection of 2 r_s / b = 4M/b.)
#pragma once

#include <vector>

#include "math3d.h"
#include "physics.h"

namespace geo {

struct Conserved {
    double h = 0;   // |L| / E  (impact parameter magnitude)
    double E = 0;   // conserved energy
    double bz = 0;  // L_z / E  (signed, about the disk axis)
};

Conserved conservedQuantities(m3::Vec3 pos, m3::Vec3 vel);

// Converts a unit direction measured in the orthonormal frame of a *static*
// observer at `pos` into a coordinate 3-velocity dx/dlambda.
m3::Vec3 localToCoordinateVelocity(m3::Vec3 pos, m3::Vec3 localDir);

struct Point {
    m3::Vec3 pos;
    m3::Vec3 vel;
};

struct Result {
    std::vector<Point> path;
    bool captured = false;   // crossed the horizon
    bool escaped = false;    // reached escapeR moving outward
    m3::Vec3 finalDir{0, 0, 1};
    Conserved q;
    int steps = 0;
    double deflection = 0.0;  // radians between initial and final direction
};

// Adaptive step length, shared with the GLSL integrator so CPU and GPU agree.
inline double stepSize(double r, double h, double scale = 1.0) {
    double dl = 0.05 * r * r / (h + 1.0);
    dl = std::min(dl, 0.15 * r);
    dl = std::max(dl, 0.002);
    return dl * scale;
}

// Integrates from a coordinate state. `stepScale` < 1 increases accuracy.
Result trace(m3::Vec3 pos, m3::Vec3 vel, int maxSteps = 20000,
             double escapeR = 1e4, double stepScale = 1.0, bool recordPath = true);

// Massive test particle. The exact Schwarzschild orbit equation is
//     u'' + u = M/h^2 + 3M u^2
// which corresponds to the Cartesian equation of motion
//     d^2 x / dlambda^2 = -M x / r^3 - 3 M h^2 x / r^5 .
// The first term is Newtonian gravity (GM = 1); the second carries the
// relativistic perihelion advance and the existence of the ISCO.
Result traceMassive(m3::Vec3 pos, m3::Vec3 vel, int maxSteps = 20000,
                    double escapeR = 1e4, double stepScale = 1.0,
                    bool recordPath = true);

// Convenience: start from a static observer looking along `localDir`.
Result traceFromObserver(m3::Vec3 pos, m3::Vec3 localDir, int maxSteps = 20000,
                         double escapeR = 1e4, double stepScale = 1.0,
                         bool recordPath = true);

}  // namespace geo
