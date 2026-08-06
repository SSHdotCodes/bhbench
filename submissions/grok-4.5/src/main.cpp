// Real-time Schwarzschild black-hole simulation
// - GPU null-geodesic ray tracing (gravitational lensing)
// - Thin accretion disk + photon-ring halos
// - Flamm-paraboloid spacetime grid ("trapdoor in spacetime")
//
// Geometric units G = c = 1. OpenGL 4.1 core (macOS-compatible).

#include "camera.hpp"
#include "mesh.hpp"
#include "physics.hpp"
#include "shader.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

namespace {

struct AppState {
    Camera camera;
    int width = 1280;
    int height = 720;
    bool mousePressed = false;
    double lastX = 0.0;
    double lastY = 0.0;
    bool showGrid = true;
    bool showDisk = true;
    bool showSurface = true;
    int quality = 1; // 0 fast, 1 normal, 2 high
    float mass = bh::defaultMass;
    float timeScale = 1.0f;
    bool paused = false;
    std::string shaderDir;
};

AppState g;

std::string findShaderDir() {
    const char* candidates[] = {
        "shaders",
        "./shaders",
        "../shaders",
        "../../shaders",
        "black-hole-cpp-grok45/shaders",
    };
    for (const char* c : candidates) {
        std::string path = std::string(c) + "/blackhole.vert";
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            fclose(f);
            return c;
        }
    }
    return "shaders";
}

void framebufferSizeCallback(GLFWwindow*, int w, int h) {
    g.width = std::max(w, 1);
    g.height = std::max(h, 1);
    glViewport(0, 0, g.width, g.height);
    g.camera.setAspect(static_cast<float>(g.width) / static_cast<float>(g.height));
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g.mousePressed = true;
            glfwGetCursorPos(window, &g.lastX, &g.lastY);
        } else if (action == GLFW_RELEASE) {
            g.mousePressed = false;
        }
    }
}

void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (!g.mousePressed) return;
    float dx = static_cast<float>(x - g.lastX);
    float dy = static_cast<float>(y - g.lastY);
    g.lastX = x;
    g.lastY = y;
    g.camera.orbit(dx * 0.005f, -dy * 0.005f);
}

void scrollCallback(GLFWwindow*, double, double yoff) {
    g.camera.zoom(static_cast<float>(yoff * 0.25));
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_G:
        g.showGrid = !g.showGrid;
        break;
    case GLFW_KEY_F:
        g.showSurface = !g.showSurface;
        break;
    case GLFW_KEY_D:
        g.showDisk = !g.showDisk;
        break;
    case GLFW_KEY_SPACE:
        g.paused = !g.paused;
        break;
    case GLFW_KEY_1:
        g.quality = 0;
        break;
    case GLFW_KEY_2:
        g.quality = 1;
        break;
    case GLFW_KEY_3:
        g.quality = 2;
        break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
        g.mass = std::min(g.mass * 1.1f, 5.0f);
        break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        g.mass = std::max(g.mass / 1.1f, 0.3f);
        break;
    case GLFW_KEY_R:
        g.camera = Camera();
        g.camera.setAspect(static_cast<float>(g.width) / static_cast<float>(g.height));
        g.mass = bh::defaultMass;
        break;
    default:
        break;
    }
}

unsigned int createEmptyVAO() {
    unsigned int vao = 0;
    glGenVertexArrays(1, &vao);
    return vao;
}

void printBanner() {
    std::cout << R"(
======================================================================
  Schwarzschild Black Hole — Real-time GR Ray Tracer
  Geodesic lensing · Accretion disk · Flamm spacetime grid
----------------------------------------------------------------------
  Mouse drag   orbit camera     Scroll     zoom
  G            toggle grid      F          Flamm surface
  D            toggle disk      Space      pause animation
  1/2/3        quality low/med/high
  +/-          mass             R          reset
  Esc          quit
  Units: G=c=1 · rs=2M · photon sphere=3M · ISCO=6M
======================================================================
)" << std::flush;
}

} // namespace

int main() {
    printBanner();

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(g.width, g.height,
                                          "Schwarzschild Black Hole — Geodesic Ray Tracer",
                                          nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    g.width = fbw;
    g.height = fbh;
    glViewport(0, 0, g.width, g.height);
    g.camera.setAspect(static_cast<float>(g.width) / static_cast<float>(g.height));

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";

    g.shaderDir = findShaderDir();
    std::cout << "Shaders: " << g.shaderDir << "\n" << std::flush;

    Shader bhShader;
    Shader gridShader;
    if (!bhShader.loadFromFiles(g.shaderDir + "/blackhole.vert",
                                g.shaderDir + "/blackhole.frag")) {
        std::cerr << "Failed to load blackhole shaders\n";
        return 1;
    }
    if (!gridShader.loadFromFiles(g.shaderDir + "/grid.vert",
                                  g.shaderDir + "/grid.frag")) {
        std::cerr << "Failed to load grid shaders\n";
        return 1;
    }

    unsigned int fsVAO = createEmptyVAO();

    float rs = bh::schwarzschildRadius(g.mass);
    Mesh flammSurface = Mesh::createFlammSurface(rs, rs * 1.05f, 35.0f, 64, 96);
    Mesh flammLines = Mesh::createFlammGridLines(rs, rs * 1.05f, 35.0f, 24, 64);
    Mesh cage = Mesh::createCurvatureCage(rs, 30.0f, 20);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifdef GL_MULTISAMPLE
    glEnable(GL_MULTISAMPLE);
#endif

    auto lastFrame = std::chrono::steady_clock::now();
    float simTime = 0.0f;
    float fpsSmooth = 60.0f;
    int frameCount = 0;
    float massCached = g.mass;

    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        dt = std::min(dt, 0.05f);
        if (!g.paused) simTime += dt * g.timeScale;

        frameCount++;
        fpsSmooth = fpsSmooth * 0.95f + (1.0f / std::max(dt, 1e-4f)) * 0.05f;
        if (frameCount % 30 == 0) {
            std::string title = "Schwarzschild BH | FPS " +
                                std::to_string(static_cast<int>(fpsSmooth + 0.5f)) +
                                " | M=" + std::to_string(g.mass).substr(0, 4) +
                                " rs=" + std::to_string(bh::schwarzschildRadius(g.mass)).substr(0, 4) +
                                " | Q" + std::to_string(g.quality) +
                                (g.showDisk ? " disk" : "") +
                                (g.showGrid ? " grid" : "");
            glfwSetWindowTitle(window, title.c_str());
        }

        if (std::abs(g.mass - massCached) > 1e-4f) {
            massCached = g.mass;
            rs = bh::schwarzschildRadius(g.mass);
            flammSurface = Mesh::createFlammSurface(rs, rs * 1.05f, 35.0f, 64, 96);
            flammLines = Mesh::createFlammGridLines(rs, rs * 1.05f, 35.0f, 24, 64);
            cage = Mesh::createCurvatureCage(rs, 30.0f, 20);
        }

        glViewport(0, 0, g.width, g.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Pass 1: full-screen geodesic ray trace
        glDisable(GL_DEPTH_TEST);
        bhShader.use();

        glm::vec3 camPos = g.camera.position();
        glm::vec3 right, up, forward;
        g.camera.basis(right, up, forward);
        float tanHalf = std::tan(g.camera.fovY() * 0.5f);
        float aspect = static_cast<float>(g.width) / static_cast<float>(g.height);

        bhShader.setVec3("uCamPos", glm::value_ptr(camPos));
        bhShader.setVec3("uCamRight", glm::value_ptr(right));
        bhShader.setVec3("uCamUp", glm::value_ptr(up));
        bhShader.setVec3("uCamForward", glm::value_ptr(forward));
        bhShader.setFloat("uTanHalfFov", tanHalf);
        bhShader.setFloat("uAspect", aspect);
        bhShader.setFloat("uM", g.mass);
        bhShader.setFloat("uTime", simTime);
        bhShader.setInt("uMaxSteps", g.quality == 0 ? 180 : (g.quality == 1 ? 320 : 500));
        bhShader.setFloat("uDiskInner", bh::isco(g.mass));
        bhShader.setFloat("uDiskOuter", 18.0f * g.mass);
        bhShader.setInt("uShowDisk", g.showDisk ? 1 : 0);
        bhShader.setInt("uQuality", g.quality);

        glBindVertexArray(fsVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Pass 2: spacetime grid (Flamm trapdoor embedding)
        if (g.showGrid) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);

            glm::mat4 view = g.camera.view();
            glm::mat4 proj = g.camera.projection();
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
            glm::mat4 mvp = proj * view * model;

            gridShader.use();
            gridShader.setMat4("uMVP", glm::value_ptr(mvp));
            gridShader.setMat4("uModel", glm::value_ptr(model));
            gridShader.setMat4("uView", glm::value_ptr(view));
            gridShader.setVec3("uCamPos", glm::value_ptr(camPos));
            gridShader.setFloat("uRs", bh::schwarzschildRadius(g.mass));
            gridShader.setFloat("uTime", simTime);

            if (g.showSurface) {
                gridShader.setInt("uMode", 0);
                flammSurface.draw();
            }

            glLineWidth(1.2f);
            gridShader.setInt("uMode", 1);
            flammLines.draw();

            gridShader.setInt("uMode", 2);
            cage.draw();

            glDepthMask(GL_TRUE);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &fsVAO);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
