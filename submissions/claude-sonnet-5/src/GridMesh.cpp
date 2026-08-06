#include "GridMesh.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <vector>
#include <cmath>
#include <algorithm>

GridMesh::~GridMesh() {
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
}

void GridMesh::init(float innerRadius, float outerRadius, int rings, int spokes) {
    rings = std::max(rings, 2);
    spokes = std::max(spokes, 3);

    std::vector<float> verts; // (x, z) pairs; radius is recovered as length(x,z) in the shader
    verts.reserve(static_cast<size_t>(rings) * spokes * 2);

    for (int ring = 0; ring < rings; ++ring) {
        float t = static_cast<float>(ring) / static_cast<float>(rings - 1);
        float r = innerRadius + t * (outerRadius - innerRadius);
        for (int s = 0; s < spokes; ++s) {
            float theta = (2.0f * 3.14159265359f * s) / static_cast<float>(spokes);
            verts.push_back(r * std::cos(theta));
            verts.push_back(r * std::sin(theta));
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<size_t>(rings) * spokes * 4);

    // Concentric ring lines.
    for (int ring = 0; ring < rings; ++ring) {
        int base = ring * spokes;
        for (int s = 0; s < spokes; ++s) {
            indices.push_back(base + s);
            indices.push_back(base + (s + 1) % spokes);
        }
    }
    // Radial spoke lines.
    for (int ring = 0; ring + 1 < rings; ++ring) {
        int base0 = ring * spokes;
        int base1 = (ring + 1) * spokes;
        for (int s = 0; s < spokes; ++s) {
            indices.push_back(base0 + s);
            indices.push_back(base1 + s);
        }
    }

    indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void GridMesh::render() const {
    glBindVertexArray(vao);
    glDrawElements(GL_LINES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
