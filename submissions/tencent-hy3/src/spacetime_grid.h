#pragma once
// Spacetime curvature grid: a 2D sheet embedded in 3D that is pulled down
// into a funnel ("trapdoor") by the mass. The deflection at radius r
// follows the classic embedding diagram of the Schwarzschild spatial
// geometry:  z(r) = -2 sqrt(rs (r - rs))  (Flamm's paraboloid), clamped
// near the horizon so it plunges to -infinity (the trapdoor).
//
// Rendered as a wireframe mesh that the user can toggle with 'G'.

#include <glm/glm.hpp>
#include <vector>

namespace bh {

struct SpacetimeGrid {
    int segR = 60;     // radial segments
    int segT = 120;    // angular segments
    float maxR = 30.0f;
    std::vector<float> verts;   // xyz positions (line segments)
    std::vector<float> cols;    // rgb per vertex

    void build();
    // Returns vertex count for drawing as GL_LINES.
    size_t vertexCount() const { return verts.size() / 3; }
};

} // namespace bh
