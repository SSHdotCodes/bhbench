#pragma once
#include <glm/glm.hpp>
#include <memory>

class Window;
class Renderer;

class Simulation {
public:
    Simulation();
    ~Simulation();
    void run();
    float getTime() const { return m_time; }
private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    float m_time;
    glm::vec3 m_cameraPos;
    float m_cameraAngle;
};
