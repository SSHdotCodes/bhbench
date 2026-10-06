#pragma once
#include "common.h"
#include "blackhole.h"
#include "raytracer.h"
#include "spacetime.h"
#include "accretion.h"
#include "starfield.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

class Renderer {
public:
    GLFWwindow* window = nullptr;
    int screenWidth = 1920;
    int screenHeight = 1080;

    GLuint gridVAO, gridVBO, gridIBO;
    GLuint diskVAO, diskVBO;
    GLuint starVAO, starVBO;
    GLuint horizonVAO, horizonVBO;

    BlackHole blackhole;
    RayTracer* rayTracer = nullptr;
    SpacetimeGrid spacetime;
    AccretionDisk accretion;
    Starfield starfield;

    Camera camera;
    double camDistance = 50.0;
    double camAngleX = 0.3;
    double camAngleY = 0.0;
    bool camRotating = false;
    bool showGrid = true;
    bool showDisk = true;
    bool showStars = true;
    bool showLensing = true;

    double time = 0.0;
    double lastFrame = 0.0;
    double frameDelta = 0.0;

    Renderer();
    ~Renderer();
    bool init();
    void run();
    void render();
    void updateCamera();

    void updateGridGL();
    void updateDiskGL();

    GLuint compileShader(const char* source, GLenum type);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);

    void renderFrame();
    void renderBackground();
    void renderLensingEffect();
    void renderAccretionDisk();
    void renderSpacetimeGrid();
    void renderHorizon();
    void renderStars();

    bool shouldClose() const;

    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* w, double xoff, double yoff);
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void resizeCallback(GLFWwindow* w, int width, int height);
};

extern Renderer* g_renderer;