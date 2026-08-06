#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

class SpacetimeGrid {
public:
    SpacetimeGrid(float size = 20.0f, int divisions = 40);
    ~SpacetimeGrid();
    void render(const glm::mat4& projection, const glm::mat4& view, float time) const;
private:
    GLuint m_vao, m_vbo, m_ebo;
    float m_size;
    int m_divisions;
};
