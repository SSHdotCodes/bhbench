#pragma once
// Scientific black hole simulator core.
//
// The physics here follows the Schwarzschild metric in units where
// G = c = 1. A black hole of mass M has Schwarzschild radius
//   rs = 2 M
// We work in these geometric units throughout. The observer is placed
// far from the hole and we integrate photon geodesics by numerically
// solving the orbit equation in the equatorial plane reduced form and,
// more generally, in 3D Cartesian coordinates using the standard
// second-order equation of motion for light in a Schwarzschild field:
//
//   d^2 x^i / d lambda^2 = - Gamma^i_{mu nu} (dx^mu/dlambda)(dx^nu/dlambda)
//
// with the non-zero Christoffel symbols of the Schwarzschild geometry.
// This gives correct gravitational lensing, photon-sphere capture and
// the shadow.

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace bh {

// Schwarzschild radius (geometric units, rs = 2M, M=1 => rs=2).
static constexpr float RS = 2.0f;

// Returns the gravitational acceleration on a null (photon) trajectory
// expressed in Cartesian coordinates, using the exact Schwarzschild
// Christoffel symbols. pos and vel are the current position and
// direction* (affine parameter derivative). h2 is the squared angular
// momentum L^2 = |pos x vel|^2, a conserved quantity used to simplify
// the radial terms.
glm::vec3 photonAccel(const glm::vec3& pos, const glm::vec3& vel, float h2);

// Integrate a single photon backwards from the camera.
// Returns the final colour accumulated along the ray.
// `diskHit` lets the caller know if the ray struck the accretion disk.
struct RayResult {
    glm::vec3 color;
    bool hitDisk;
    bool captured;   // fell past the horizon
    float diskT;     // parameter of disk crossing (for glow)
};

struct Camera {
    glm::vec3 pos;
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
    float fov;       // vertical fov in radians
};

// Accretion disk parameters (geometric units).
struct Disk {
    float inner;     // ISCO ~ 3 rs for Schwarzschild = 6M => 6.0 here (rs=2)
    float outer;
    float thickness; // visual half-thickness
};

// Trace one ray. `disk` describes the accretion disk. The returned color
// is the emission the camera receives.
RayResult traceRay(
    const glm::vec3& ro,        // ray origin (camera)
    const glm::vec3& rd,        // ray direction (unit)
    const Disk& disk,
    int maxSteps,
    float stepScale);

} // namespace bh
