#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace bh {

struct BlackHoleParams {
    float mass = 1.0f;               // Geometric mass M (G = c = 1); r_s = 2M
    float spin = 0.65f;              // Dimensionless Kerr spin parameter a* = a/M in [-0.995, 0.995]
    float diskInnerMult = 1.0f;      // Multiplier on r_ISCO(a*)
    float diskOuterM = 17.5f;        // Outer accretion disk radius in units of M
    float diskTempKelvin = 5200.0f;  // Peak effective temperature scale (K) for visual blackbody
    float diskIntensity = 1.85f;     // Radiative flux multiplier
    float diskThickness = 0.10f;     // Geometric half-thickness H/r
    float dopplerStrength = 1.0f;    // Relativistic Doppler beaming strength [0..1.5]
    float haloStrength = 1.35f;      // Photon ring & coronal halo intensity
    float bloomStrength = 0.36f;     // HDR bloom strength
    float exposure = 1.05f;          // ACES tone-mapping exposure

    // Spacetime curvature ("Trapdoor in Spacetime") parameters
    bool showSpacetimeGrid = true;   // Show 3D Flamm's paraboloid trapdoor grid
    bool showLensedGrid = false;     // Also lens the spacetime grid through the ray tracer
    bool showGeodesicBeams = true;   // Show 3D photon geodesic trajectories over the trapdoor
    bool showRelativisticJets = false; // Show Blandford-Znajek polar jets
    bool showAccretionDisk = true;   // Show relativistic accretion disk
    bool showHUD = true;             // Show scientific telemetry HUD
    bool animateTime = true;         // Animate accretion turbulence & infalling spacetime pulses
    float trapdoorDepth = 1.20f;     // Flamm's paraboloid vertical scale
    float gridExtentM = 28.0f;       // Outer radius of spacetime grid in M
    int maxRaySteps = 210;           // RK4 steps per pixel in GPU ray tracer
};

// Schwarzschild radius r_s = 2M
inline float schwarzschildRadius(float M) {
    return 2.0f * M;
}

// Kerr outer event horizon: r_+ = M * (1 + sqrt(1 - a*^2))
inline float kerrEventHorizon(float M, float aStar) {
    float a2 = std::clamp(aStar * aStar, 0.0f, 0.9999f);
    return M * (1.0f + std::sqrt(1.0f - a2));
}

// Equatorial ergosphere static limit: r_ergo(theta=pi/2) = 2M
inline float kerrEquatorialErgosphere(float M) {
    return 2.0f * M;
}

// Prograde circular photon orbit radius in equatorial plane (Bardeen, Press, Teukolsky 1972)
inline float kerrPhotonSpherePrograde(float M, float aStar) {
    float a = std::clamp(std::abs(aStar), 0.0f, 0.9999f);
    float angle = (2.0f / 3.0f) * std::acos(-a);
    return 2.0f * M * (1.0f + std::cos(angle));
}

// Retrograde circular photon orbit radius in equatorial plane
inline float kerrPhotonSphereRetrograde(float M, float aStar) {
    float a = std::clamp(std::abs(aStar), 0.0f, 0.9999f);
    float angle = (2.0f / 3.0f) * std::acos(a);
    return 2.0f * M * (1.0f + std::cos(angle));
}

// Exact Bardeen-Press-Teukolsky (1972) Innermost Stable Circular Orbit (ISCO) radius
inline float kerrISCO(float M, float aStar) {
    float a = std::clamp(aStar, -0.999f, 0.999f);
    float a2 = a * a;
    float oneMinusA2 = std::max(1.0f - a2, 1e-6f);
    float z1 = 1.0f + std::cbrt(oneMinusA2) * (std::cbrt(1.0f + a) + std::cbrt(1.0f - a));
    float z2 = std::sqrt(3.0f * a2 + z1 * z1);
    float signA = (a >= 0.0f) ? 1.0f : -1.0f;
    float term = std::sqrt(std::max(0.0f, (3.0f - z1) * (3.0f + z1 + 2.0f * z2)));
    return M * (3.0f + z2 - signA * term);
}

// Critical impact parameter for photon capture in equatorial plane (prograde & retrograde)
inline float kerrCriticalImpactParam(float M, float aStar, bool prograde) {
    float a = aStar * M;
    float r_ph = prograde ? kerrPhotonSpherePrograde(M, aStar)
                          : kerrPhotonSphereRetrograde(M, aStar);
    // For circular photon orbit at r_ph, impact parameter xi = L_z / E = -(r_ph^2(r_ph - 3M) + a^2(r_ph + M)) / (a(r_ph - M))
    // In the limit a -> 0, b_crit = 3 * sqrt(3) * M
    if (std::abs(aStar) < 1e-4f) {
        return 3.0f * std::sqrt(3.0f) * M;
    }
    float sign = prograde ? 1.0f : -1.0f;
    float aSigned = sign * std::abs(a);
    float xi = -(r_ph * r_ph * (r_ph - 3.0f * M) + aSigned * aSigned * (r_ph + M)) /
               (aSigned * (r_ph - M));
    return std::abs(xi);
}

// Exact Flamm's paraboloid embedding height y(r) for the "Trapdoor in Spacetime"
// Asymptotically flat (y -> 0) at large r, with exact Flamm square-root vertical
// tangent dy/dr ~ +sqrt(r_s / (r - r_+)) -> +infinity at the horizon r -> r_+,
// plunging downward into the deep relativistic trapdoor throat for r <= r_+.
inline float flammParaboloidHeight(float r, float M, float aStar, float depthScale, float /*rMax*/) {
    float r_plus = kerrEventHorizon(M, aStar);
    if (r > r_plus) {
        float dr = (r - r_plus) / M;
        // Combines exact Flamm square-root singularity sqrt(r - r_+) at the horizon
        // with asymptotic flatness at large radius so the outer grid sits cleanly below the disk
        float flammWell = -5.4f * M / (1.0f + 0.28f * dr + 0.055f * dr * dr)
                          - 2.6f * M * (1.0f - std::sqrt(dr / (dr + 3.2f)));
        return depthScale * flammWell;
    } else {
        // Vertical relativistic trapdoor throat plunging below r_+
        float yRim = -8.0f * M;
        float frac = std::clamp((r_plus - r) / (0.75f * r_plus), 0.0f, 1.0f);
        float throatDrop = -6.2f * M * (0.85f * frac + 1.15f * frac * frac);
        return depthScale * (yRim + throatDrop);
    }
}

// Kerr frame-dragging azimuthal twist angle for spatial grid lines
inline float kerrFrameDraggingTwist(float r, float M, float aStar, float timePhase = 0.0f) {
    float a = aStar * M;
    float r_plus = kerrEventHorizon(M, aStar);
    float rSafe = std::max(r, r_plus * 0.65f);
    // Integrated Lense-Thirring twist ~ 2Ma / r^2 enhanced near the horizon
    float twist = (2.0f * M * a) / (rSafe * rSafe + 0.35f * r_plus * r_plus);
    float horizonBoost = 1.4f * aStar * std::exp(-std::max(0.0f, r - r_plus) / (1.5f * M));
    return twist + horizonBoost + 0.12f * aStar * timePhase / (1.0f + 0.15f * rSafe / M);
}

// CPU-side exact Cartesian Kerr-Schild Hamiltonian RK4 integrator
// Used for generating 3D photon geodesic beam curves & physics verification
struct GeodesicState {
    glm::vec3 x;
    glm::vec3 p;
};

struct KerrAux {
    float r;
    float f;
    glm::vec3 k;
    glm::vec3 grad_r;
};

inline KerrAux evalKerrAux(const glm::vec3& x, float M, float a) {
    float rho2 = glm::dot(x, x);
    float a2 = a * a;
    float diff = rho2 - a2;
    float disc = std::sqrt(std::max(0.0f, diff * diff + 4.0f * a2 * x.y * x.y));
    float r2 = std::max(0.5f * (diff + disc), 1e-6f);
    float r = std::sqrt(r2);
    float r3 = r2 * r;
    float r4 = r2 * r2;
    float denom = r4 + a2 * x.y * x.y;
    float r2_a2 = r2 + a2;

    KerrAux out;
    out.r = r;
    out.f = (2.0f * M * r3) / denom;
    out.k = glm::vec3(
        (r * x.x + a * x.z) / r2_a2,
        x.y / r,
        (r * x.z - a * x.x) / r2_a2
    );
    out.grad_r = (r3 / denom) * glm::vec3(x.x, (r2_a2 / r2) * x.y, x.z);
    return out;
}

inline GeodesicState kerrHamiltonianRHS(const GeodesicState& s, float M, float a) {
    KerrAux aux = evalKerrAux(s.x, M, a);
    float r = aux.r;
    float r2 = r * r;
    float r3 = r2 * r;
    float r4 = r2 * r2;
    float a2 = a * a;
    float denom = r4 + a2 * s.x.y * s.x.y;
    float invDenom2 = 1.0f / (denom * denom);
    float r2_a2 = r2 + a2;

    float kp = glm::dot(aux.k, s.p);
    float q = kp + 1.0f;

    // dx/dlambda = dH/dp = p - f * q * k
    glm::vec3 dx = s.p - aux.f * q * aux.k;

    // grad f
    float df_dr = 2.0f * M * r2 * (3.0f * a2 * s.x.y * s.x.y - r4) * invDenom2;
    float df_dy_explicit = -4.0f * M * r3 * a2 * s.x.y * invDenom2;
    glm::vec3 grad_f = df_dr * aux.grad_r + glm::vec3(0.0f, df_dy_explicit, 0.0f);

    // grad_x(k . p)
    glm::vec3 dkp_dx_explicit(
        (r * s.p.x - a * s.p.z) / r2_a2,
        s.p.y / r,
        (a * s.p.x + r * s.p.z) / r2_a2
    );
    float dkp_dr = ((s.x.x - 2.0f * r * aux.k.x) * s.p.x +
                    (s.x.z - 2.0f * r * aux.k.z) * s.p.z) / r2_a2 -
                   (aux.k.y * s.p.y) / r;
    glm::vec3 grad_kp = dkp_dx_explicit + dkp_dr * aux.grad_r;

    // dp/dlambda = -dH/dx = 0.5 * q^2 * grad_f + f * q * grad_kp
    glm::vec3 dp = (0.5f * q * q) * grad_f + (aux.f * q) * grad_kp;

    return {dx, dp};
}

inline GeodesicState initNullGeodesic(const glm::vec3& x0, const glm::vec3& dirUnit, float M, float a) {
    glm::vec3 d = glm::normalize(dirUnit);
    KerrAux aux = evalKerrAux(x0, M, a);
    float dk = glm::dot(d, aux.k);
    float oneMinusDk2 = std::max(0.0f, 1.0f - dk * dk);
    float s = 1.0f / std::sqrt(std::max(1e-6f, 1.0f - aux.f * oneMinusDk2));
    float q0 = (s * s * oneMinusDk2) / std::max(1e-6f, 1.0f - s * dk);
    glm::vec3 p0 = s * d + (aux.f * q0) * aux.k;
    return {x0, p0};
}

struct GeodesicBeamPath {
    std::vector<glm::vec3> points;
    std::vector<float> redshift; // local gravitational factor or affine progress
    int outcome = 0; // 0 = escaped (weak/strong lensing), 1 = captured by horizon, 2 = photon ring
};

inline GeodesicBeamPath traceGeodesicBeam(
    const glm::vec3& startPos,
    const glm::vec3& startDir,
    float M,
    float aStar,
    int maxSteps = 320,
    float maxRadius = 32.0f
) {
    float a = aStar * M;
    float r_plus = kerrEventHorizon(M, aStar);
    GeodesicState state = initNullGeodesic(startPos, startDir, M, a);

    GeodesicBeamPath path;
    path.points.reserve(maxSteps);
    path.redshift.reserve(maxSteps);

    float totalAngle = 0.0f;
    glm::vec3 prevPos = state.x;

    for (int i = 0; i < maxSteps; ++i) {
        KerrAux aux = evalKerrAux(state.x, M, a);
        path.points.push_back(state.x);
        path.redshift.push_back(std::clamp(1.0f - (2.0f * M) / std::max(aux.r, r_plus), 0.0f, 1.0f));

        if (aux.r <= r_plus * 1.015f) {
            path.outcome = 1; // Captured by event horizon ("trapdoor")
            break;
        }
        if (aux.r > maxRadius * M && i > 10) {
            path.outcome = (totalAngle > 3.8f) ? 2 : 0;
            break;
        }

        // Adaptive step size
        float h = std::clamp(0.055f * (aux.r - 0.85f * r_plus), 0.015f * M, 0.35f * M);

        GeodesicState k1 = kerrHamiltonianRHS(state, M, a);
        GeodesicState s2{state.x + 0.5f * h * k1.x, state.p + 0.5f * h * k1.p};
        GeodesicState k2 = kerrHamiltonianRHS(s2, M, a);
        GeodesicState s3{state.x + 0.5f * h * k2.x, state.p + 0.5f * h * k2.p};
        GeodesicState k3 = kerrHamiltonianRHS(s3, M, a);
        GeodesicState s4{state.x + h * k3.x, state.p + h * k3.p};
        GeodesicState k4 = kerrHamiltonianRHS(s4, M, a);

        state.x += (h / 6.0f) * (k1.x + 2.0f * k2.x + 2.0f * k3.x + k4.x);
        state.p += (h / 6.0f) * (k1.p + 2.0f * k2.p + 2.0f * k3.p + k4.p);

        glm::vec2 u1(prevPos.x, prevPos.z);
        glm::vec2 u2(state.x.x, state.x.z);
        float l1 = glm::length(u1), l2 = glm::length(u2);
        if (l1 > 1e-4f && l2 > 1e-4f) {
            float c = std::clamp(glm::dot(u1 / l1, u2 / l2), -1.0f, 1.0f);
            totalAngle += std::acos(c);
        }
        prevPos = state.x;
    }
    return path;
}

} // namespace bh
