// Native mesh function copied verbatim by tools/export-grid.py.
#include "../../../submissions/gemini-4-hydrogen/src/core/Math.hpp"
#include "../../../submissions/gemini-4-hydrogen/src/core/BlackHolePhysics.hpp"
#include "../../../submissions/gemini-4-hydrogen/src/shaders/ShaderTypes.h"
#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
using namespace bh;
std::vector<GridVertex> buildSpacetimeGridMesh(double spinA, float rOuter, int numRings, int numSegs, std::vector<uint32_t>& outIndices) {
    double rp = physics::eventHorizonRadius(spinA);
    constexpr int kThroatRings = 12;
    int totalRings = numRings + kThroatRings;

    std::vector<double> ringR(totalRings);
    std::vector<double> ringZ(totalRings);

    double zHorizon = physics::kerrEquatorialEmbeddingZ(rp, spinA, rOuter);
    double throatDepth = 4.2;

    // Causal trapdoor throat interior extension below r_+
    for (int i = 0; i < kThroatRings; ++i) {
        double frac = static_cast<double>(i) / static_cast<double>(kThroatRings); // 0 .. <1
        double t = 1.0 - frac; // 1 at bottom, -> 0 at horizon
        double r = rp * (0.72 + 0.28 * frac);
        ringR[i] = r;
        ringZ[i] = zHorizon - throatDepth * std::pow(t, 1.35);
    }

    // Exact Kerr equatorial spatial embedding (reduces to Flamm's paraboloid at a=0)
    // Scaled gently (0.55x) so the full trapdoor throat stays framed within camera FOV
    for (int i = 0; i < numRings; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(numRings - 1);
        double r = rp + (static_cast<double>(rOuter) - rp) * (t * t);
        ringR[kThroatRings + i] = r;
        ringZ[kThroatRings + i] = 0.52 * physics::kerrEquatorialEmbeddingZ(r, spinA, rOuter);
    }
    zHorizon = ringZ[kThroatRings];
    throatDepth = 2.8;
    for (int i = 0; i < kThroatRings; ++i) {
        double frac = static_cast<double>(i) / static_cast<double>(kThroatRings);
        double t = 1.0 - frac;
        double r = rp * (0.72 + 0.28 * frac);
        ringR[i] = r;
        ringZ[i] = zHorizon - throatDepth * std::pow(t, 1.35);
    }

    std::vector<GridVertex> vertices;
    vertices.reserve(static_cast<size_t>(totalRings) * static_cast<size_t>(numSegs + 1));

    for (int i = 0; i < totalRings; ++i) {
        double r = ringR[i];
        double z = ringZ[i];

        // Finite-difference slope dz/dr for smooth surface normal
        int iPrev = std::max(0, i - 1);
        int iNext = std::min(totalRings - 1, i + 1);
        double dr = std::max(1e-4, ringR[iNext] - ringR[iPrev]);
        double dz = ringZ[iNext] - ringZ[iPrev];
        double slope = dz / dr;

        // Kerr frame-dragging twist angle
        float twist = static_cast<float>(2.4 * spinA * std::pow(rp / std::max(r, rp * 0.72), 2.2));

        for (int j = 0; j <= numSegs; ++j) {
            double phi = (2.0 * kPi * static_cast<double>(j)) / static_cast<double>(numSegs);
            float cosP = static_cast<float>(std::cos(phi));
            float sinP = static_cast<float>(std::sin(phi));

            Vec3 normal{static_cast<float>(-slope * cosP), static_cast<float>(-slope * sinP), 1.0f};
            normal = normal.normalized();

            GridVertex v{};
            v.posAndInfo = simd_make_float4(static_cast<float>(r) * cosP,
                                            static_cast<float>(r) * sinP,
                                            static_cast<float>(z),
                                            static_cast<float>(r));
            v.normalAndTwist = simd_make_float4(normal.x, normal.y, normal.z, twist);
            vertices.push_back(v);
        }
    }

    outIndices.clear();
    outIndices.reserve(static_cast<size_t>(totalRings - 1) * static_cast<size_t>(numSegs) * 6);
    int stride = numSegs + 1;
    for (int i = 0; i < totalRings - 1; ++i) {
        for (int j = 0; j < numSegs; ++j) {
            uint32_t a = static_cast<uint32_t>(i * stride + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * stride + j);
            uint32_t d = c + 1;
            outIndices.push_back(a);
            outIndices.push_back(c);
            outIndices.push_back(b);
            outIndices.push_back(b);
            outIndices.push_back(c);
            outIndices.push_back(d);
        }
    }

    return vertices;
}


int main(int argc,char**argv){
 std::vector<uint32_t> indices;
 auto vertices=buildSpacetimeGridMesh(0.90,28.0f,128,160,indices);
 std::ofstream(std::string(argv[1])+"/grid.f32",std::ios::binary).write((char*)vertices.data(),vertices.size()*sizeof(GridVertex));
 std::ofstream(std::string(argv[1])+"/grid.u32",std::ios::binary).write((char*)indices.data(),indices.size()*sizeof(uint32_t));
 std::cout<<vertices.size()<<" vertices, "<<indices.size()<<" indices, "<<sizeof(GridVertex)<<" bytes/vertex\n";
}
