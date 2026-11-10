#include "embedding.hpp"

#include "gl_utils.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr float PI = 3.14159265358979323846f;

float embeddingHeight(float radius) {
    // z(r) = 2 sqrt(2 M (r - 2M)), shifted so the outer rim is at y=0.
    const float outerZ = 2.0f * std::sqrt(2.0f * (EmbeddingDiagram::outerRadius - 2.0f));
    return 2.0f * std::sqrt(2.0f * (radius - 2.0f)) - outerZ;
}

EmbeddingDiagram::Vertex vertex(float radius, float angle, const Vec3& color,
                                float lift = 0.0f) {
    return {{radius * std::cos(angle), embeddingHeight(radius) + lift,
             radius * std::sin(angle)}, color};
}

void addRing(std::vector<EmbeddingDiagram::Vertex>& lines, float radius,
             const Vec3& color, int segments = 256) {
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * PI * static_cast<float>(i) / segments;
        const float b = 2.0f * PI * static_cast<float>(i + 1) / segments;
        lines.push_back(vertex(radius, a, color, 0.025f));
        lines.push_back(vertex(radius, b, color, 0.025f));
    }
}

void configureVertexArray(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EmbeddingDiagram::Vertex),
                          reinterpret_cast<void*>(offsetof(EmbeddingDiagram::Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EmbeddingDiagram::Vertex),
                          reinterpret_cast<void*>(offsetof(EmbeddingDiagram::Vertex, color)));
}
} // namespace

EmbeddingDiagram::EmbeddingDiagram(const std::string& shaderDirectory) {
    program_ = loadProgram(shaderDirectory, "embedding.vert", "embedding.frag");

    constexpr int radialSegments = 112;
    constexpr int angularSegments = 192;
    std::vector<Vertex> surface;
    std::vector<std::uint32_t> indices;
    for (int j = 0; j <= radialSegments; ++j) {
        const float t = static_cast<float>(j) / radialSegments;
        const float radius = 2.0f + (outerRadius - 2.0f) * std::pow(t, 1.45f);
        const float shade = 0.25f + 0.75f * t;
        const Vec3 color = {0.010f + 0.012f * shade,
                            0.028f + 0.050f * shade,
                            0.045f + 0.075f * shade};
        for (int i = 0; i <= angularSegments; ++i) {
            const float angle = 2.0f * PI * static_cast<float>(i) / angularSegments;
            surface.push_back(vertex(radius, angle, color));
        }
    }
    for (int j = 0; j < radialSegments; ++j) {
        for (int i = 0; i < angularSegments; ++i) {
            const std::uint32_t a = j * (angularSegments + 1) + i;
            const std::uint32_t b = a + angularSegments + 1;
            indices.insert(indices.end(), {a, b, a + 1, b, b + 1, a + 1});
        }
    }

    std::vector<Vertex> lines;
    const Vec3 gridColor = {0.10f, 0.34f, 0.42f};
    for (int i = 0; i < 48; ++i) {
        const float angle = 2.0f * PI * static_cast<float>(i) / 48;
        for (int j = 0; j < radialSegments; ++j) {
            const float t0 = static_cast<float>(j) / radialSegments;
            const float t1 = static_cast<float>(j + 1) / radialSegments;
            const float r0 = 2.0f + (outerRadius - 2.0f) * std::pow(t0, 1.45f);
            const float r1 = 2.0f + (outerRadius - 2.0f) * std::pow(t1, 1.45f);
            lines.push_back(vertex(r0, angle, gridColor, 0.025f));
            lines.push_back(vertex(r1, angle, gridColor, 0.025f));
        }
    }
    for (int j = 1; j <= 42; ++j) {
        const float t = static_cast<float>(j) / 42;
        const float radius = 2.0f + (outerRadius - 2.0f) * std::pow(t, 1.45f);
        addRing(lines, radius, gridColor, 160);
    }
    addRing(lines, 2.0f, {0.95f, 0.16f, 0.07f});   // horizon
    addRing(lines, 3.0f, {1.00f, 0.55f, 0.08f});   // photon sphere
    addRing(lines, 6.0f, {0.95f, 0.88f, 0.35f});   // ISCO

    surfaceIndexCount_ = static_cast<GLsizei>(indices.size());
    lineVertexCount_ = static_cast<GLsizei>(lines.size());

    glGenVertexArrays(1, &surfaceVAO_);
    glGenBuffers(1, &surfaceVBO_);
    glGenBuffers(1, &surfaceEBO_);
    configureVertexArray(surfaceVAO_, surfaceVBO_);
    glBufferData(GL_ARRAY_BUFFER, surface.size() * sizeof(Vertex), surface.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surfaceEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(std::uint32_t),
                 indices.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &linesVAO_);
    glGenBuffers(1, &linesVBO_);
    configureVertexArray(linesVAO_, linesVBO_);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(Vertex), lines.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

EmbeddingDiagram::~EmbeddingDiagram() {
    glDeleteBuffers(1, &linesVBO_);
    glDeleteBuffers(1, &surfaceEBO_);
    glDeleteBuffers(1, &surfaceVBO_);
    glDeleteVertexArrays(1, &linesVAO_);
    glDeleteVertexArrays(1, &surfaceVAO_);
    glDeleteProgram(program_);
}

void EmbeddingDiagram::draw(const Mat4& mvp) const {
    glUseProgram(program_);
    glUniformMatrix4fv(uniform(program_, "uMVP"), 1, GL_FALSE, mvp.v);
    glUniform1f(uniform(program_, "uAlpha"), 1.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBindVertexArray(surfaceVAO_);
    glDrawElements(GL_TRIANGLES, surfaceIndexCount_, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);

    glBindVertexArray(linesVAO_);
    glDrawArrays(GL_LINES, 0, lineVertexCount_);
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}
