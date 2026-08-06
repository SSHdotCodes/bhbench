#pragma once

#include <glm/glm.hpp>
#include "shader.h"

// Renders the spacetime curvature grid (Flamm paraboloid embedding).
class GridRenderer {
public:
    GridRenderer();
    ~GridRenderer();

    void draw(const Shader& shader, const glm::mat4& view,
              const glm::mat4& proj, float rs, float time) const;

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertexCount_ = 0;

    void buildGrid(float rs);
};