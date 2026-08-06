#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <cmath>

namespace BlackHolePhysics {

// Schwarzschild metric parameters (GM/c² = M in geometric units)
// For visualization: use M = 1, so Rs = 2
constexpr float SCHWARZSCHILD_RADIUS = 2.0f;  // 2GM/c²
constexpr float EVENT_HORIZON_RADIUS = SCHWARZSCHILD_RADIUS;  // In geometric units, r_s = 2M
constexpr float ISCO_RADIUS = 6.0f; // Innermost Stable Circular Orbit = 6M for Schwarzschild

// Metric components in Schwarzschild coordinates
// g_tt = -(1 - Rs/r), g_rr = (1 - Rs/r)^(-1), g_θθ = r², g_φφ = r² sin²θ
struct MetricTensor {
    float g_tt;
    float g_rr;
    float g_thth;
    float g_pp;
};

inline MetricTensor schwarzschildMetric(float r, float theta) {
    float factor = 1.0f - SCHWARZSCHILD_RADIUS / r;
    return {
        -(factor),           // g_tt
        1.0f / factor,       // g_rr
        r * r,               // g_θθ
        r * r * sin(theta) * sin(theta)  // g_φφ
    };
}

// Null geodesic deflection angle (Einstein formula): α = 4M/b = 2Rs/b
inline float deflectionAngle(float impactParameter) {
    return 2.0f * SCHWARZSCHILD_RADIUS / std::max(impactParameter, 0.001f);
}

// Gravitational redshift: z = 1/sqrt(1 - Rs/r) - 1, so observed freq = emitted / (1+z)
inline float redshiftFactor(float r) {
    return std::sqrt(1.0f - SCHWARZSCHILD_RADIUS / std::max(r, SCHWARZSCHILD_RADIUS + 0.001f));
}

// Temperature profile for thin accretion disk (Shakura-Sunyaev): T ∝ r^(-3/4)
inline float diskTemperature(float r) {
    return std::pow(std::max(r / (3.0f * SCHWARZSCHILD_RADIUS), 0.1f), -0.75f);
}

// Photon trajectory integration (4th-order Runge-Kutta for null geodesics in Schwarzschild metric)
// Simplified for real-time: use approximate deflection formula
struct PhotonPath {
    std::vector<glm::vec3> points;
    std::vector<float> curvature;
};

PhotonPath tracePhoton(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& bhCenter);

} // namespace BlackHolePhysics
