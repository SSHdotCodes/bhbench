#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

class AccretionDisk {
public:
    AccretionDisk(float innerRadius = 6.0f, float outerRadius = 12.0f);
    ~AccretionDisk();
    void render(const glm::mat4& projection, const glm::mat4& view, float time, const glm::vec3& cameraPos) const;
private:
    GLuint m_vao, m_vbo;
    int m_vertexCount;
};
