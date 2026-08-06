#include "AccretionDisk.h"
#include "ShaderProgram.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

AccretionDisk::AccretionDisk(float innerRadius, float outerRadius)
    : m_vao(0), m_vbo(0), m_vertexCount(0) {
    const int segments = 200;
    std::vector<float> verts;
    
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * 2.0f * 3.14159265f;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        
        // Outer ring
        verts.push_back(outerRadius * cosT);
        verts.push_back(0.0f);
        verts.push_back(outerRadius * sinT);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
        
        // Inner ring
        verts.push_back(innerRadius * cosT);
        verts.push_back(0.0f);
        verts.push_back(innerRadius * sinT);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
    }
    
    m_vertexCount = verts.size() / 6;
    
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
}

AccretionDisk::~AccretionDisk() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
}

void AccretionDisk::render(const glm::mat4& projection, const glm::mat4& view, float time, const glm::vec3& cameraPos) const {
    static ShaderProgram shader("/Users/sshpro/black-hole-cpp-inkling/shaders/accretion_disk.vert", "/Users/sshpro/black-hole-cpp-inkling/shaders/accretion_disk.frag");
    shader.use();
    shader.setMat4("projection", glm::value_ptr(projection));
    shader.setMat4("view", glm::value_ptr(view));
    shader.setFloat("uTime", time);
    shader.setVec3("uBlackHolePos", 0.0f, 0.0f, 0.0f);
    shader.setFloat("uSchwarzschildRadius", 2.0f);
    shader.setVec3("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, time * 0.3f, glm::vec3(0.0f, 1.0f, 0.0f));
    shader.setMat4("model", glm::value_ptr(model));
    
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, m_vertexCount);
    glBindVertexArray(0);
}
