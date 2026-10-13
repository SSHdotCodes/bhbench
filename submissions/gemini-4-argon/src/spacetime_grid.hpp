#pragma once

#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "physics.hpp"

namespace bh {

struct GridVertex {
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 meta; // x = r/M (or arc length), y = type
};

struct SpacetimeMeshData {
    std::vector<GridVertex> surfaceTris; // Translucent Flamm funnel surface
    std::vector<GridVertex> gridLines;   // Coordinate lines, critical rings, and 3D photon geodesics
};

inline glm::vec4 curvatureColorRamp(float r, float M, float rPlus) {
    // Color transitions from cool deep-space cyan far out to intense violet/crimson at the horizon trapdoor
    float t = std::clamp((4.5f * M) / std::max(r, rPlus * 0.7f), 0.0f, 1.0f);
    glm::vec3 farCol(0.10f, 0.62f, 0.95f);
    glm::vec3 midCol(0.35f, 0.28f, 0.95f);
    glm::vec3 throatCol(1.00f, 0.18f, 0.42f);
    glm::vec3 rgb = (t < 0.55f)
        ? glm::mix(farCol, midCol, t / 0.55f)
        : glm::mix(midCol, throatCol, (t - 0.55f) / 0.45f);
    float alpha = glm::mix(0.28f, 0.88f, std::pow(t, 0.65f));
    return glm::vec4(rgb, alpha);
}

inline void buildSpacetimeTrapdoorMesh(
    SpacetimeMeshData& out,
    const BlackHoleParams& params,
    float timeSec
) {
    out.surfaceTris.clear();
    out.gridLines.clear();

    const float M = params.mass;
    const float aStar = params.spin;
    const float rPlus = kerrEventHorizon(M, aStar);
    const float rErgo = kerrEquatorialErgosphere(M);
    const float rPhPro = kerrPhotonSpherePrograde(M, aStar);
    const float rISCO = kerrISCO(M, aStar);
    const float rMax = params.gridExtentM * M;
    const float depth = params.trapdoorDepth;
    const float yBaseOffset = -1.35f * M; // Place asymptotic flat sheet slightly below equatorial disk

    auto evalFunnelPoint = [&](float r, float phi) -> glm::vec3 {
        float y = flammParaboloidHeight(r, M, aStar, depth, rMax) + yBaseOffset;
        // Inside the trapdoor throat (r < rPlus), pinch radius slightly to form a sleek relativistic vortex tube
        float rVisual = (r >= rPlus) ? r : rPlus * (0.38f + 0.62f * (r / rPlus));
        return glm::vec3(rVisual * std::cos(phi), y, rVisual * std::sin(phi));
    };

    // ------------------------------------------------------------------------
    // 1. Polar Flamm's Paraboloid Surface & Concentric Funnel Rings + Spokes
    // ------------------------------------------------------------------------
    const int numRadial = 56;
    const int numAzimuth = 96;
    const float rMin = 0.25f * rPlus;

    auto radiusAtIdx = [&](int i) -> float {
        float u = static_cast<float>(i) / static_cast<float>(numRadial);
        // Concentrate resolution near the horizon throat and photon sphere
        return rMin + (rMax - rMin) * std::pow(u, 1.85f);
    };

    // Semi-transparent shaded Flamm funnel surface triangles
    for (int i = 0; i < numRadial; ++i) {
        float r0 = radiusAtIdx(i);
        float r1 = radiusAtIdx(i + 1);
        glm::vec4 c0 = curvatureColorRamp(r0, M, rPlus);
        glm::vec4 c1 = curvatureColorRamp(r1, M, rPlus);
        c0.a *= 0.14f;
        c1.a *= 0.14f;

        for (int j = 0; j < numAzimuth; ++j) {
            float phi0 = (static_cast<float>(j) / numAzimuth) * glm::two_pi<float>();
            float phi1 = (static_cast<float>(j + 1) / numAzimuth) * glm::two_pi<float>();

            glm::vec3 p00 = evalFunnelPoint(r0, phi0);
            glm::vec3 p10 = evalFunnelPoint(r1, phi0);
            glm::vec3 p11 = evalFunnelPoint(r1, phi1);
            glm::vec3 p01 = evalFunnelPoint(r0, phi1);

            out.surfaceTris.push_back({p00, c0, {r0 / M, 0.0f}});
            out.surfaceTris.push_back({p10, c1, {r1 / M, 0.0f}});
            out.surfaceTris.push_back({p11, c1, {r1 / M, 0.0f}});

            out.surfaceTris.push_back({p00, c0, {r0 / M, 0.0f}});
            out.surfaceTris.push_back({p11, c1, {r1 / M, 0.0f}});
            out.surfaceTris.push_back({p01, c0, {r0 / M, 0.0f}});
        }
    }

    // Concentric radial rings down the Flamm paraboloid funnel
    for (int i = 0; i <= numRadial; i += 2) {
        float r = radiusAtIdx(i);
        glm::vec4 col = curvatureColorRamp(r, M, rPlus);
        for (int j = 0; j < numAzimuth; ++j) {
            float phi0 = (static_cast<float>(j) / numAzimuth) * glm::two_pi<float>();
            float phi1 = (static_cast<float>(j + 1) / numAzimuth) * glm::two_pi<float>();
            out.gridLines.push_back({evalFunnelPoint(r, phi0), col, {r / M, 0.0f}});
            out.gridLines.push_back({evalFunnelPoint(r, phi1), col, {r / M, 0.0f}});
        }
    }

    // Frame-dragged radial coordinate spokes plunging into the trapdoor
    const int numSpokes = 28;
    for (int s = 0; s < numSpokes; ++s) {
        float basePhi = (static_cast<float>(s) / numSpokes) * glm::two_pi<float>();
        for (int i = 0; i < numRadial; ++i) {
            float r0 = radiusAtIdx(i);
            float r1 = radiusAtIdx(i + 1);
            float twist0 = kerrFrameDraggingTwist(r0, M, aStar, timeSec * 0.25f);
            float twist1 = kerrFrameDraggingTwist(r1, M, aStar, timeSec * 0.25f);
            glm::vec4 c0 = curvatureColorRamp(r0, M, rPlus);
            glm::vec4 c1 = curvatureColorRamp(r1, M, rPlus);
            c0.a *= 1.15f;
            c1.a *= 1.15f;

            out.gridLines.push_back({evalFunnelPoint(r0, basePhi + twist0), c0, {r0 / M, 0.0f}});
            out.gridLines.push_back({evalFunnelPoint(r1, basePhi + twist1), c1, {r1 / M, 0.0f}});
        }
    }

    // ------------------------------------------------------------------------
    // 2. Cartesian Coordinate Grid Sheet Warped into the Central Gravity Well
    //    (Classic general-relativity rubber-sheet / trapdoor embedding look)
    // ------------------------------------------------------------------------
    const int numCartLines = 22;
    const int cartSegs = 90;
    const float halfSpan = rMax * 0.88f;
    for (int i = 0; i <= numCartLines; ++i) {
        float coord = -halfSpan + (2.0f * halfSpan * i) / numCartLines;
        // Skip lines that don't pass near or outside the throat
        for (int axis = 0; axis < 2; ++axis) {
            for (int k = 0; k < cartSegs; ++k) {
                float t0 = -halfSpan + (2.0f * halfSpan * k) / cartSegs;
                float t1 = -halfSpan + (2.0f * halfSpan * (k + 1)) / cartSegs;

                glm::vec2 xz0 = (axis == 0) ? glm::vec2(t0, coord) : glm::vec2(coord, t0);
                glm::vec2 xz1 = (axis == 0) ? glm::vec2(t1, coord) : glm::vec2(coord, t1);

                float r0 = glm::length(xz0);
                float r1 = glm::length(xz1);
                if (r0 < rPlus * 1.05f || r1 < rPlus * 1.05f || r0 > rMax || r1 > rMax) continue;

                // Apply subtle Kerr frame-dragging rotation to the Cartesian grid near the black hole
                float phi0 = std::atan2(xz0.y, xz0.x) + 0.45f * kerrFrameDraggingTwist(r0, M, aStar, 0.0f);
                float phi1 = std::atan2(xz1.y, xz1.x) + 0.45f * kerrFrameDraggingTwist(r1, M, aStar, 0.0f);

                glm::vec3 p0 = evalFunnelPoint(r0, phi0);
                glm::vec3 p1 = evalFunnelPoint(r1, phi1);

                glm::vec4 c0 = curvatureColorRamp(r0, M, rPlus);
                glm::vec4 c1 = curvatureColorRamp(r1, M, rPlus);
                float edgeFade0 = 1.0f - glm::smoothstep(rMax * 0.75f, rMax, r0);
                float edgeFade1 = 1.0f - glm::smoothstep(rMax * 0.75f, rMax, r1);
                c0.a *= 0.48f * edgeFade0;
                c1.a *= 0.48f * edgeFade1;

                out.gridLines.push_back({p0, c0, {r0 / M, 0.0f}});
                out.gridLines.push_back({p1, c1, {r1 / M, 0.0f}});
            }
        }
    }

    // ------------------------------------------------------------------------
    // 3. Highlighted Relativistic Landmark Rings on the Funnel:
    //    - Event Horizon r_+ (Crimson)
    //    - Ergosphere r_ergo (Gold)
    //    - Prograde Photon Sphere r_ph (Electric Cyan)
    //    - ISCO r_ISCO (Emerald Green)
    // ------------------------------------------------------------------------
    auto addHighlightRing = [&](float radius, const glm::vec4& color, float typeId) {
        const int segs = 128;
        for (int j = 0; j < segs; ++j) {
            float phi0 = (static_cast<float>(j) / segs) * glm::two_pi<float>();
            float phi1 = (static_cast<float>(j + 1) / segs) * glm::two_pi<float>();
            glm::vec3 p0 = evalFunnelPoint(radius, phi0) + glm::vec3(0.0f, 0.03f * M, 0.0f);
            glm::vec3 p1 = evalFunnelPoint(radius, phi1) + glm::vec3(0.0f, 0.03f * M, 0.0f);
            out.gridLines.push_back({p0, color, {radius / M, typeId}});
            out.gridLines.push_back({p1, color, {radius / M, typeId}});
        }
    };

    // Event Horizon Ring (r_+) - Trapdoor Rim
    addHighlightRing(rPlus * 1.02f, glm::vec4(1.00f, 0.18f, 0.32f, 0.98f), 1.0f);
    // Ergosphere Static Limit Ring (2M)
    if (std::abs(aStar) > 0.05f) {
        addHighlightRing(rErgo, glm::vec4(1.00f, 0.68f, 0.15f, 0.88f), 4.0f);
    }
    // Photon Sphere Ring (r_ph)
    addHighlightRing(rPhPro, glm::vec4(0.15f, 0.92f, 1.00f, 0.95f), 2.0f);
    // ISCO Ring (r_ISCO)
    addHighlightRing(rISCO, glm::vec4(0.20f, 1.00f, 0.55f, 0.90f), 3.0f);

    // ------------------------------------------------------------------------
    // 4. Real-Time 3D Null Geodesic Photon Beams (Demonstrating Lensing,
    //    Photon Ring Capture, and Plunging into the Spacetime Trapdoor!)
    // ------------------------------------------------------------------------
    if (params.showGeodesicBeams) {
        float bPro = kerrCriticalImpactParam(M, aStar, true);
        float bRetro = kerrCriticalImpactParam(M, aStar, false);

        // Create a curated fan of impact parameters spanning weak lensing, critical photon-sphere orbits, and horizon capture
        std::vector<float> impactParams = {
            -10.5f, -8.0f, -6.2f,
            -(bPro + 0.55f), -(bPro + 0.12f), -(bPro + 0.025f), // Near prograde photon sphere!
            -(bPro - 0.35f), -1.4f,                             // Captured into the trapdoor!
            1.6f, (bRetro - 0.45f),                             // Captured into the trapdoor!
            (bRetro + 0.035f), (bRetro + 0.18f), (bRetro + 0.65f), // Near retrograde photon sphere!
            8.5f, 11.0f
        };

        for (float b : impactParams) {
            glm::vec3 startPos(b * M, 0.0f, 25.0f * M);
            glm::vec3 startDir(0.0f, 0.0f, -1.0f);
            GeodesicBeamPath beam = traceGeodesicBeam(startPos, startDir, M, aStar, 340, 27.0f);

            if (beam.points.size() < 2) continue;

            float arcLen = 0.0f;
            for (size_t k = 0; k + 1 < beam.points.size(); ++k) {
                glm::vec3 p0 = beam.points[k];
                glm::vec3 p1 = beam.points[k + 1];
                float r0 = glm::length(glm::vec2(p0.x, p0.z));
                float r1 = glm::length(glm::vec2(p1.x, p1.z));

                // Project the 3D geodesic onto the Flamm's paraboloid spacetime sheet so rays visibly
                // ride along the curved spacetime surface and plunge into the trapdoor throat!
                float y0 = flammParaboloidHeight(r0, M, aStar, depth, rMax) + yBaseOffset + 0.08f * M;
                float y1 = flammParaboloidHeight(r1, M, aStar, depth, rMax) + yBaseOffset + 0.08f * M;
                p0.y = y0;
                p1.y = y1;

                float segLen = glm::length(p1 - p0) / M;
                float a0 = arcLen;
                float a1 = arcLen + segLen;
                arcLen = a1;

                glm::vec4 beamCol;
                if (beam.outcome == 1) {
                    // Captured ray plunging into the trapdoor throat: hot gold -> crimson
                    float plunge = std::clamp((5.0f * M) / std::max(r0, rPlus), 0.0f, 1.0f);
                    beamCol = glm::mix(glm::vec4(1.0f, 0.78f, 0.25f, 0.85f),
                                       glm::vec4(1.0f, 0.15f, 0.35f, 0.98f), plunge);
                } else if (beam.outcome == 2) {
                    // Critical photon-sphere orbiting ray: brilliant electric cyan-white
                    beamCol = glm::vec4(0.35f, 0.98f, 1.0f, 0.95f);
                } else {
                    // Gravitationally deflected (lensed) ray: warm amber-white
                    beamCol = glm::vec4(0.95f, 0.88f, 0.52f, 0.72f);
                }

                out.gridLines.push_back({p0, beamCol, {a0, 5.0f}});
                out.gridLines.push_back({p1, beamCol, {a1, 5.0f}});
            }
        }
    }
}

} // namespace bh
