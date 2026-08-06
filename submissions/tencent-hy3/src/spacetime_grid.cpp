#include "spacetime_grid.h"
#include "blackhole.h"
#include <cmath>

namespace bh {

static float embeddingZ(float r) {
    // Flamm's paraboloid (spatial embedding of Schwarzschild):
    //   z(r) = -2 * sqrt( rs * (r - rs) )
    // diverges at r -> rs, so we clamp to model the "trapdoor".
    const float rs = RS;
    if (r <= rs * 1.02f) return -18.0f; // plunging funnel
    const float z = -2.0f * std::sqrt(rs * (r - rs));
    return glm::max(z, -18.0f);
}

void SpacetimeGrid::build() {
    verts.clear();
    cols.clear();

    auto pushLine = [&](const glm::vec3& a, const glm::vec3& b) {
        // colour by depth: deep = red/orange (danger), shallow = blue.
        const float da = glm::clamp((-a.z) / 18.0f, 0.0f, 1.0f);
        const float db = glm::clamp((-b.z) / 18.0f, 0.0f, 1.0f);
        glm::vec3 ca = glm::mix(glm::vec3(0.2f, 0.5f, 1.0f),
                                glm::vec3(1.0f, 0.3f, 0.1f), da);
        glm::vec3 cb = glm::mix(glm::vec3(0.2f, 0.5f, 1.0f),
                                glm::vec3(1.0f, 0.3f, 0.1f), db);
        verts.insert(verts.end(), {a.x, a.y, a.z});
        verts.insert(verts.end(), {b.x, b.y, b.z});
        cols.insert(cols.end(), {ca.r, ca.g, ca.b});
        cols.insert(cols.end(), {cb.r, cb.g, cb.b});
    };

    for (int i = 0; i < segR; ++i) {
        const float r0 = RS * 1.02f + (maxR - RS * 1.02f) * (float)i / segR;
        const float r1 = RS * 1.02f + (maxR - RS * 1.02f) * (float)(i + 1) / segR;
        for (int j = 0; j < segT; ++j) {
            const float t0 = 2.0f * 3.14159265f * (float)j / segT;
            const float t1 = 2.0f * 3.14159265f * (float)(j + 1) / segT;
            glm::vec3 a(r0 * std::cos(t0), r0 * std::sin(t0), embeddingZ(r0));
            glm::vec3 b(r1 * std::cos(t0), r1 * std::sin(t0), embeddingZ(r1));
            glm::vec3 c(r0 * std::cos(t1), r0 * std::sin(t1), embeddingZ(r0));
            pushLine(a, b); // radial
            pushLine(a, c); // angular
        }
    }
}

} // namespace bh
