#pragma once

#include "geodesic.hpp"
#include "gl_compat.hpp"

#include <cmath>
#include <vector>
#include <glm/glm.hpp>

// Spatial equatorial slice of Schwarzschild embeds in R^3 as Flamm's
// paraboloid:  z^2 = 4 rs (r - rs).  Placing the outer radius at y = 0
// and the horizon at the bottom of the throat is the textbook "trapdoor
// in spacetime".  Circles, spokes, critical rings, and a few integrated
// null geodesics are drawn on that surface.

struct LineMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;

    void destroy() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
        count = 0;
    }
};

struct LineVert {
    glm::vec3 pos;
    glm::vec3 color;
};

inline void pushLine(std::vector<LineVert>& v, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& ca, const glm::vec3& cb) {
    v.push_back({a, ca});
    v.push_back({b, cb});
}

inline LineMesh uploadLines(const std::vector<LineVert>& verts) {
    LineMesh m;
    m.count = static_cast<GLsizei>(verts.size());
    if (m.count == 0) {
        return m;
    }
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(LineVert)),
                 verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert),
                          reinterpret_cast<void*>(sizeof(glm::vec3)));
    glBindVertexArray(0);
    return m;
}

inline glm::vec3 fabricPoint(float r, float phi, float M, float rOuter) {
    return glm::vec3(r * std::cos(phi), bh::flammY(r, M, rOuter), r * std::sin(phi));
}

inline LineMesh buildSpacetimeFabric(float M) {
    const float rs = 2.0f * M;
    const float rMatch = 16.0f * M;
    const float rFar = 46.0f * M;
    const float rMin = rs * 1.035f;

    const glm::vec3 colFabric(0.20f, 0.78f, 1.00f);
    const glm::vec3 colFar(0.08f, 0.22f, 0.38f);
    const glm::vec3 colHorizon(1.00f, 0.18f, 0.07f);
    const glm::vec3 colPhoton(1.00f, 0.82f, 0.22f);
    const glm::vec3 colIsco(0.35f, 1.00f, 0.55f);
    const glm::vec3 colRayCap(1.00f, 0.38f, 0.10f);
    const glm::vec3 colRayEsc(1.00f, 0.72f, 0.28f);
    const glm::vec3 colRayPh(0.95f, 0.95f, 1.00f);

    std::vector<LineVert> v;
    v.reserve(28000);

    auto ringColor = [&](float r) {
        if (r >= rMatch) {
            const float t = std::clamp((r - rMatch) / (rFar - rMatch), 0.0f, 1.0f);
            return glm::mix(colFabric * 0.75f, colFar, t);
        }
        const float t = std::clamp((r - rMin) / (rMatch - rMin), 0.0f, 1.0f);
        return glm::mix(colFabric, colFabric * 0.55f, t);
    };

    // Concentric coordinate circles: Flamm well, then a flat exterior sheet.
    const int nPhi = 160;
    std::vector<float> radii;
    for (int i = 0; i <= 18; ++i) {
        const float t = static_cast<float>(i) / 18.0f;
        radii.push_back(rMin + (rMatch - rMin) * t * t);
    }
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        radii.push_back(rMatch + (rFar - rMatch) * t);
    }
    radii.push_back(rs * 1.04f);
    radii.push_back(3.0f * M);
    radii.push_back(6.0f * M);

    for (float r : radii) {
        glm::vec3 special = ringColor(r);
        if (std::abs(r - 3.0f * M) < 0.08f * M) {
            special = colPhoton;
        } else if (std::abs(r - 6.0f * M) < 0.08f * M) {
            special = colIsco;
        } else if (r < rs * 1.08f) {
            special = colHorizon;
        }
        const int segs = r > rMatch ? 128 : nPhi;
        for (int i = 0; i < segs; ++i) {
            const float p0 = 6.2831853f * static_cast<float>(i) / segs;
            const float p1 = 6.2831853f * static_cast<float>(i + 1) / segs;
            pushLine(v, fabricPoint(r, p0, M, rMatch), fabricPoint(r, p1, M, rMatch),
                     special, special);
        }
    }

    const int nSpokes = 24;
    const int nRad = 56;
    for (int s = 0; s < nSpokes; ++s) {
        const float phi = 6.2831853f * static_cast<float>(s) / nSpokes;
        for (int i = 0; i < nRad; ++i) {
            const float t0 = static_cast<float>(i) / nRad;
            const float t1 = static_cast<float>(i + 1) / nRad;
            // Denser samples inside the well.
            const float u0 = t0 < 0.45f ? (t0 / 0.45f) * (t0 / 0.45f) * 0.45f : t0;
            const float u1 = t1 < 0.45f ? (t1 / 0.45f) * (t1 / 0.45f) * 0.45f : t1;
            const float r0 = rMin + (rFar - rMin) * u0;
            const float r1 = rMin + (rFar - rMin) * u1;
            pushLine(v, fabricPoint(r0, phi, M, rMatch), fabricPoint(r1, phi, M, rMatch),
                     ringColor(r0), ringColor(r1));
        }
    }

    // Vertical drop lines from the flat sheet down the throat.
    for (int s = 0; s < 12; ++s) {
        const float phi = 6.2831853f * static_cast<float>(s) / 12.0f;
        glm::vec3 lip = fabricPoint(rMatch, phi, M, rMatch);
        glm::vec3 mid = fabricPoint(8.0f * M, phi, M, rMatch);
        glm::vec3 deep = fabricPoint(rMin + 0.15f * M, phi, M, rMatch);
        const glm::vec3 dim(0.12f, 0.32f, 0.48f);
        pushLine(v, glm::vec3(lip.x, 0.0f, lip.z), mid, dim, ringColor(8.0f * M) * 0.6f);
        pushLine(v, mid, deep, ringColor(8.0f * M) * 0.6f, colHorizon * 0.7f);
    }

    // Sample null geodesics on the fabric.
    // Critical impact parameter for capture of a photon from infinity:
    //   b_crit = 3√3 M ≈ 5.19615 M.
    const double Mc = static_cast<double>(M);
    const double bCrit = 3.0 * std::sqrt(3.0) * Mc;
    const double bs[] = {
        0.35 * bCrit, 0.70 * bCrit, 0.92 * bCrit, 0.995 * bCrit,
        1.000 * bCrit, 1.02 * bCrit, 1.15 * bCrit, 1.45 * bCrit,
        1.90 * bCrit, 2.60 * bCrit, 3.40 * bCrit
    };

    for (double b : bs) {
        const auto pts = bh::integrateEquatorialRay(Mc, b);
        if (pts.size() < 2) {
            continue;
        }
        const bool captured = glm::length(glm::vec2(pts.back().x, pts.back().z)) < 4.0f * M;
        const bool photon = std::abs(b - bCrit) < 0.03 * bCrit;
        glm::vec3 c0 = photon ? colRayPh : (captured ? colRayCap : colRayEsc);
        for (size_t i = 1; i < pts.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(pts.size());
            glm::vec3 c1 = glm::mix(c0, c0 * 0.25f, t);
            pushLine(v, pts[i - 1], pts[i], glm::mix(c0, c0 * 0.25f, t - 1.0f / pts.size()), c1);
        }
    }

    return uploadLines(v);
}
