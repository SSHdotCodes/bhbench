#include "Renderer.h"
#include "SpacetimeGrid.h"
#include "AccretionDisk.h"
#include "GeodesicTracer.h"
#include "ShaderProgram.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

Renderer::Renderer() : m_quadVAO(0), m_quadVBO(0), m_starFieldTexture(0) {}

void Renderer::init() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    m_grid = std::make_unique<SpacetimeGrid>(25.0f, 50);
    m_disk = std::make_unique<AccretionDisk>(5.0f, 14.0f);
    m_tracer = std::make_unique<GeodesicTracer>();
    
    initQuad();
    initStarField();
    m_rayShader = std::make_unique<ShaderProgram>("/Users/sshpro/black-hole-cpp-inkling/shaders/ray_tracing.vert", "/Users/sshpro/black-hole-cpp-inkling/shaders/ray_tracing.frag");
}

void Renderer::initQuad() {
    float quadVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::initStarField() {
    // Procedural starfield texture: small white dots on black
    const int size = 512;
    unsigned char data[size][size][3];
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float val = (float)(std::rand() % 100) / 100.0f;
            if (val < 0.02f) val = 0.9f + (float)(std::rand() % 100) / 1000.0f;
            else val = 0.0f;
            data[y][x][0] = (unsigned char)(val * 255.0f);
            data[y][x][1] = (unsigned char)(val * 255.0f);
            data[y][x][2] = (unsigned char)(val * 255.0f);
        }
    }
    glGenTextures(1, &m_starFieldTexture);
    glBindTexture(GL_TEXTURE_2D, m_starFieldTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &m_quadVAO);
    glDeleteBuffers(1, &m_quadVBO);
    glDeleteTextures(1, &m_starFieldTexture);
}

void Renderer::renderFrame(float time, const glm::vec3& cameraPos, const glm::mat4& viewMatrix) {
    int width = 1280, height = 800; // Will be updated by window
    
    // Clear
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)width / (float)height, 0.1f, 200.0f);
    
    // First pass: ray-traced background with gravitational lensing
    renderRayTracingPass(cameraPos, time, width, height);
    
    // Second pass: 3D scene elements (spacetime grid, accretion disk)
    glEnable(GL_DEPTH_TEST);
    
    // Spacetime curvature grid
    m_grid->render(projection, viewMatrix, time);
    
    // Accretion disk
    m_disk->render(projection, viewMatrix, time, cameraPos);
}

void Renderer::renderRayTracingPass(const glm::vec3& cameraPos, float time, int width, int height) {
    glDisable(GL_DEPTH_TEST);
    
    m_rayShader->use();
    
    // Create inverse view matrix for ray direction computation
    // We approximate using camera position for simplicity
    m_rayShader->setVec3("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);
    m_rayShader->setFloat("uTime", time);
    m_rayShader->setFloat("uSchwarzschildRadius", 2.0f);
    m_rayShader->setFloat("uEventHorizonRadius", 2.0f);
    
    // Pass environment texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_starFieldTexture);
    m_rayShader->setInt("uStarField", 0);
    
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    
    glEnable(GL_DEPTH_TEST);
}
