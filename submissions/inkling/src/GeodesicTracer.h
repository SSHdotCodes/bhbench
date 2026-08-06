#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "BlackHolePhysics.h"

class GeodesicTracer {
public:
    GeodesicTracer();
    ~GeodesicTracer();
    void update(float time);
    void render(const glm::mat4& projection, const glm::mat4& view) const;
    void generatePaths(const glm::vec3& origin);
private:
    GLuint m_vao, m_vbo;
    std::vector<glm::vec3> m_pathPoints;
};
