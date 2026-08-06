#include "GeodesicTracer.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

GeodesicTracer::GeodesicTracer() : m_vao(0), m_vbo(0) {
    m_pathPoints.reserve(500);
}

GeodesicTracer::~GeodesicTracer() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
}

void GeodesicTracer::update(float time) {
    // Regenerate paths periodically
    static float lastGen = 0.0f;
    if (time - lastGen > 0.2f) {
        lastGen = time;
        generatePaths(glm::vec3(15.0f, 5.0f, 10.0f));
    }
}

void GeodesicTracer::generatePaths(const glm::vec3& origin) {
    m_pathPoints.clear();
    const glm::vec3 bhCenter(0.0f, 0.0f, 0.0f);
    
    // Trace multiple rays around the black hole
    for (int i = 0; i < 20; ++i) {
        float angle = (float)i / 20.0f * 2.0f * 3.14159f;
        glm::vec3 dir = glm::normalize(glm::vec3(
            std::cos(angle),
            0.1f,
            std::sin(angle)
        ));
        
        BlackHolePhysics::PhotonPath path = BlackHolePhysics::tracePhoton(origin, dir, bhCenter);
        for (const auto& p : path.points) {
            m_pathPoints.push_back(p);
        }
        m_pathPoints.push_back(glm::vec3(0.0f)); // separator
    }
    
    if (m_vao == 0) {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
    }
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_pathPoints.size() * sizeof(glm::vec3), m_pathPoints.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void GeodesicTracer::render(const glm::mat4& projection, const glm::mat4& view) const {
    if (m_vao == 0 || m_pathPoints.empty()) return;
    
    // Simple line rendering using shader program inline (basic fixed pipeline approximation via manual shader)
    // For simplicity, we'll rely on the main renderer for full effects
}
