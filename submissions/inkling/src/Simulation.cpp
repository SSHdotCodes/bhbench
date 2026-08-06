#include "Simulation.h"
#include "Window.h"
#include "Renderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GL/glew.h>

Simulation::Simulation()
    : m_time(0.0f), m_cameraAngle(0.0f),
      m_cameraPos(12.0f, 3.0f, 12.0f) {
    m_window = std::make_unique<Window>(1280, 800, "Black Hole Simulation - Scientific Visualization");
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        throw std::runtime_error("Failed to initialize GLEW");
    }
    m_renderer = std::make_unique<Renderer>();
    m_renderer->init();
}

Simulation::~Simulation() = default;

void Simulation::run() {
    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        
        m_time += 0.016f; // ~60 fps
        m_cameraAngle += 0.005f;
        
        // Orbiting camera around black hole
        m_cameraPos = glm::vec3(
            14.0f * std::cos(m_cameraAngle),
            3.0f + 2.0f * std::sin(m_cameraAngle * 2.0f),
            14.0f * std::sin(m_cameraAngle)
        );
        
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::mat4 view = glm::lookAt(m_cameraPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
        
        m_renderer->renderFrame(m_time, m_cameraPos, view);
        
        m_window->swapBuffers();
    }
}
