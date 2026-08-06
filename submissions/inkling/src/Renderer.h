#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>

class Window;
class ShaderProgram;
class SpacetimeGrid;
class AccretionDisk;
class GeodesicTracer;

class Renderer {
public:
    Renderer();
    ~Renderer();
    void init();
    void renderFrame(float time, const glm::vec3& cameraPos, const glm::mat4& viewMatrix);
    void renderRayTracingPass(const glm::vec3& cameraPos, float time, int width, int height);
private:
    GLuint m_quadVAO, m_quadVBO;
    std::unique_ptr<SpacetimeGrid> m_grid;
    std::unique_ptr<AccretionDisk> m_disk;
    std::unique_ptr<GeodesicTracer> m_tracer;
    std::unique_ptr<ShaderProgram> m_rayShader;
    GLuint m_starFieldTexture;
    
    void initQuad();
    void initStarField();
};
