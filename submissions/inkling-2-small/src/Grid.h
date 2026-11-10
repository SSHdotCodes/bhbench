// Grid.h — spacetime curvature visualisation:
//   * Flamm's paraboloid: exact embedding of the t = const, equatorial spatial
//     slice of Schwarzschild into 3-D Euclidean space,
//         z(r) = 2 * sqrt(2M (r - 2M))  =  sqrt(8M (r - 2M)) .
//     Vertical scale exaggerated x1.35 for legibility; the surface only exists
//     for r >= 2M — the "hole" in the sheet IS the event horizon.
//   * Horizon rendered as an opaque black occluder sphere (r = 2M).
//   * Optional marker rings: photon sphere (3M) and ISCO (6M).
#pragma once
#include <vector>
#include "Mat4.h"

namespace bhlib {

struct GridVertex {
    float x, y, z;
    float depth; // 0 rim .. 1 throat
};

struct GridGeometry {
    std::vector<GridVertex> lineVerts;  // drawn with GL_LINES
    std::vector<float>      surfaceVerts; // positions only, GL_TRIANGLES, faint fill
    std::vector<unsigned int> surfaceIndices;

    // r_out: outer radius of the embedding sheet (in M)
    // yScale: vertical exaggeration
    static float flammZ(float r, float yScale) {
        float v = 8.0f * 1.0f * (r - 2.0f); // M = 1
        return (v > 0.0f ? std::sqrt(v) : 0.0f) * yScale;
    }

    void build(float rOuter = 50.0f, float yScale = 1.35f) {
        lineVerts.clear();
        surfaceVerts.clear();
        surfaceIndices.clear();

        const float rIn   = 2.0015f; // hole strictly outside r = 2M
        const float zRim  = flammZ(rOuter, yScale);
        const int   nRings = 56;
        const int   ringSegs = 220;
        const float k = 2.35f; // logarithmic ring spacing: dense near the throat

        auto radiusOf = [&](float t) {
            return 2.0f + (std::exp(t * k) - 1.0f) / (std::exp(k) - 1.0f) * (rOuter - 2.0f);
        };
        auto depthOf = [&](float r) { // 1 at the throat -> 0 at the rim
            float zr = flammZ(r, yScale);
            float d = 1.0f - zr / zRim;
            return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
        };
        auto pointOf = [&](float r, float az) {
            // Sheet dips downward (y < 0); rim stays at y = 0.
            float y = (flammZ(r, yScale) - zRim);
            return GridVertex{std::cos(az) * r, y, std::sin(az) * r, depthOf(r)};
        };

        // --- concentric rings (line segments)
        for (int i = 0; i <= nRings; ++i) {
            float t = float(i) / float(nRings);
            float r = radiusOf(t);
            if (r < rIn) r = rIn;
            for (int j = 0; j < ringSegs; ++j) {
                float a0 = 2.0f * 3.14159265f * float(j) / float(ringSegs);
                float a1 = 2.0f * 3.14159265f * float(j + 1) / float(ringSegs);
                lineVerts.push_back(pointOf(r, a0));
                lineVerts.push_back(pointOf(r, a1));
            }
        }

        // --- radial spokes (line segments)
        const int nSpokes = 56;
        const int spokeSamples = 190;
        for (int s = 0; s < nSpokes; ++s) {
            float az = 2.0f * 3.14159265f * float(s) / float(nSpokes);
            for (int j = 0; j < spokeSamples; ++j) {
                float t0 = float(j) / float(spokeSamples);
                float t1 = float(j + 1) / float(spokeSamples);
                float r0 = radiusOf(t0), r1 = radiusOf(t1);
                if (r0 < rIn) r0 = rIn;
                if (r1 < rIn) r1 = rIn;
                lineVerts.push_back(pointOf(r0, az));
                lineVerts.push_back(pointOf(r1, az));
            }
        }

        // --- faint filled surface for depth cueing
        const int surfR = 46, surfA = 90;
        for (int i = 0; i <= surfR; ++i) {
            float t = float(i) / float(surfR);
            float r = radiusOf(t);
            if (r < rIn) r = rIn;
            for (int j = 0; j <= surfA; ++j) {
                float az = 2.0f * 3.14159265f * float(j) / float(surfA);
                GridVertex p = pointOf(r, az);
                surfaceVerts.push_back(p.x); surfaceVerts.push_back(p.y); surfaceVerts.push_back(p.z);
                surfaceVerts.push_back(p.depth);
            }
        }
        for (int i = 0; i < surfR; ++i)
            for (int j = 0; j < surfA; ++j) {
                unsigned a = unsigned(i * (surfA + 1) + j);
                unsigned b = a + 1;
                unsigned c = a + (surfA + 1);
                unsigned d = c + 1;
                surfaceIndices.push_back(a); surfaceIndices.push_back(c); surfaceIndices.push_back(b);
                surfaceIndices.push_back(b); surfaceIndices.push_back(c); surfaceIndices.push_back(d);
            }
    }
};

// UV sphere used as an opaque black occluder at r = 2M
struct SphereGeometry {
    std::vector<float> verts;   // pos + normal interleaved
    std::vector<unsigned int> indices;
    void build(float radius = 2.0f, int stacks = 40, int slices = 64) {
        verts.clear(); indices.clear();
        for (int i = 0; i <= stacks; ++i) {
            float v = float(i) / float(stacks);
            float phi = v * 3.14159265f; // 0..pi
            float y = std::cos(phi) * radius;
            float rr = std::sin(phi) * radius;
            for (int j = 0; j <= slices; ++j) {
                float u = float(j) / float(slices);
                float th = u * 2.0f * 3.14159265f;
                float x = rr * std::cos(th), z = rr * std::sin(th);
                verts.push_back(x); verts.push_back(y); verts.push_back(z);
                verts.push_back(x / radius); verts.push_back(y / radius); verts.push_back(z / radius);
            }
        }
        for (int i = 0; i < stacks; ++i)
            for (int j = 0; j < slices; ++j) {
                unsigned a = unsigned(i * (slices + 1) + j);
                unsigned b = a + 1;
                unsigned c = a + (slices + 1);
                unsigned d = c + 1;
                indices.push_back(a); indices.push_back(b); indices.push_back(c);
                indices.push_back(b); indices.push_back(d); indices.push_back(c);
            }
    }
};

// Flat circle in the y = 0 plane (for photon sphere / ISCO marker rings)
struct RingGeometry {
    std::vector<float> verts; // positions only
    void build(float radius, int segments = 256) {
        verts.clear();
        for (int i = 0; i <= segments; ++i) {
            float a = 2.0f * 3.14159265f * float(i) / float(segments);
            verts.push_back(std::cos(a) * radius);
            verts.push_back(0.0f);
            verts.push_back(std::sin(a) * radius);
        }
    }
};

} // namespace bhlib
