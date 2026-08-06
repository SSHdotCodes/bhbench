#include "SpacetimeGrid.h"
#include "ShaderProgram.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

SpacetimeGrid::SpacetimeGrid(float size, int divisions)
    : m_size(size), m_divisions(divisions), m_vao(0), m_vbo(0), m_ebo(0) {
    std::vector<float> verts;
    std::vector<unsigned int> indices;
    
    float half = size / 2.0f;
    float step = size / divisions;
    
    for (int i = 0; i <= divisions; ++i) {
        float x = -half + i * step;
        for (int j = 0; j <= divisions; ++j) {
            float z = -half + j * step;
            verts.push_back(x);
            verts.push_back(0.0f); // y: base plane
            verts.push_back(z);
            verts.push_back(0.0f); // normal x
            verts.push_back(1.0f); // normal y (up)
            verts.push_back(0.0f); // normal z
        }
    }
    
    for (int i = 0; i < divisions; ++i) {
        for (int j = 0; j < divisions; ++j) {
            int row1 = i * (divisions + 1);
            int row2 = (i + 1) * (divisions + 1);
            int col1 = j;
            int col2 = j + 1;
            
            indices.push_back(row1 + col1);
            indices.push_back(row1 + col2);
            indices.push_back(row2 + col1);
            
            indices.push_back(row1 + col2);
            indices.push_back(row2 + col2);
            indices.push_back(row2 + col1);
        }
    }
    
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
}

SpacetimeGrid::~SpacetimeGrid() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}

void SpacetimeGrid::render(const glm::mat4& projection, const glm::mat4& view, float time) const {
    static ShaderProgram shader("/Users/sshpro/black-hole-cpp-inkling/shaders/spacetime_grid.vert", "/Users/sshpro/black-hole-cpp-inkling/shaders/spacetime_grid.frag");
    shader.use();
    shader.setMat4("projection", glm::value_ptr(projection));
    shader.setMat4("view", glm::value_ptr(view));
    shader.setFloat("uTime", time);
    shader.setVec3("uBlackHolePos", 0.0f, 0.0f, 0.0f);
    shader.setFloat("uSchwarzschildRadius", 2.0f);
    
    glm::mat4 model = glm::mat4(1.0f);
    shader.setMat4("model", glm::value_ptr(model));
    
    glBindVertexArray(m_vao);
    // Draw as wireframe-like using line loops (approximation with triangles but thin lines via shader)
    glDrawElements(GL_TRIANGLES, (m_divisions * m_divisions * 6), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
