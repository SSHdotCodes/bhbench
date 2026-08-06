#include "grid_renderer.h"

#include "gl_loader.h"
#include <vector>
#include <cmath>

namespace {
// Flamm paraboloid embedding: z = 2*sqrt(r_s*(r - r_s)) for r > r_s.
// This is the classic "trampoline" visualization of Schwarzschild spatial geometry.
float embeddingHeight(float r, float rs) {
    if (r <= rs * 1.001f) {
        return -4.0f * rs;
    }
    return 2.0f * std::sqrt(rs * (r - rs));
}
}  // namespace

GridRenderer::GridRenderer() {
    buildGrid(2.0f);
}

GridRenderer::~GridRenderer() {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void GridRenderer::buildGrid(float rs) {
    std::vector<float> vertices;
    const int radialLines = 24;
    const int ringSegments = 64;
    const float rMin = rs * 1.02f;
    const float rMax = 50.0f;

    // Radial spokes converging toward the event horizon ("trapdoor").
    for (int i = 0; i < radialLines; ++i) {
        const float phi = (2.0f * static_cast<float>(M_PI) * i) / radialLines;
        const float cosP = std::cos(phi);
        const float sinP = std::sin(phi);

        for (int j = 0; j < ringSegments; ++j) {
            const float t0 = static_cast<float>(j) / ringSegments;
            const float t1 = static_cast<float>(j + 1) / ringSegments;
            const float r0 = rMin + (rMax - rMin) * t0 * t0;
            const float r1 = rMin + (rMax - rMin) * t1 * t1;

            const float x0 = r0 * cosP, z0 = r0 * sinP;
            const float x1 = r1 * cosP, z1 = r1 * sinP;

            vertices.insert(vertices.end(), {
                x0, embeddingHeight(r0, rs), z0,
                x1, embeddingHeight(r1, rs), z1
            });
        }
    }

    // Concentric rings showing increasing curvature near horizon.
    const int numRings = 18;
    for (int ring = 1; ring <= numRings; ++ring) {
        const float t = static_cast<float>(ring) / numRings;
        const float r = rMin + (rMax - rMin) * t * t;
        const float y = embeddingHeight(r, rs);

        for (int seg = 0; seg < ringSegments; ++seg) {
            const float phi0 = (2.0f * static_cast<float>(M_PI) * seg) / ringSegments;
            const float phi1 = (2.0f * static_cast<float>(M_PI) * (seg + 1)) / ringSegments;

            vertices.insert(vertices.end(), {
                r * std::cos(phi0), y, r * std::sin(phi0),
                r * std::cos(phi1), y, r * std::sin(phi1)
            });
        }
    }

    vertexCount_ = static_cast<int>(vertices.size() / 3);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void GridRenderer::draw(const Shader& shader, const glm::mat4& view,
                        const glm::mat4& proj, float rs, float time) const {
    shader.use();
    shader.setMat4("uView", &view[0][0]);
    shader.setMat4("uProj", &proj[0][0]);
    shader.setFloat("uRs", rs);
    shader.setFloat("uTime", time);

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}