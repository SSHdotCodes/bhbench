#include "camera.h"
#include "gl_loader.h"
#include "grid_renderer.h"
#include "shader.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr float kRs = 2.0f;  // Schwarzschild radius (2M, geometric units G=c=1)

Camera gCamera;
bool gShowGrid = true;
bool gMouseDown = false;
double gLastMouseX = 0.0;
double gLastMouseY = 0.0;
float gTime = 0.0f;

std::string shaderDir() {
    namespace fs = std::filesystem;
    const fs::path exeRelative = fs::current_path() / "shaders";
    if (fs::exists(exeRelative)) return exeRelative.string();

    const fs::path buildRelative = fs::current_path() / ".." / "shaders";
    if (fs::exists(buildRelative)) return fs::canonical(buildRelative).string();

    throw std::runtime_error("Could not locate shaders/ directory");
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gMouseDown = (action == GLFW_PRESS);
        if (gMouseDown) {
            glfwGetCursorPos(window, &gLastMouseX, &gLastMouseY);
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double x, double y) {
    if (!gMouseDown) return;
    const float dx = static_cast<float>(x - gLastMouseX);
    const float dy = static_cast<float>(y - gLastMouseY);
    gCamera.rotate(-dx * 0.005f, dy * 0.005f);
    gLastMouseX = x;
    gLastMouseY = y;
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    gCamera.zoom(static_cast<float>(-yoffset * 1.5));
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_G) gShowGrid = !gShowGrid;
    if (key == GLFW_KEY_R) {
        gCamera = Camera{};
        gCamera.orbitTheta = 0.55f;
        gCamera.orbitRadius = 30.0f;
    }
}

struct Quad {
    unsigned int vao = 0, vbo = 0;

    void init() {
        const float verts[] = {
            -1.f, -1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 1.f
        };
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void destroy() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};

}  // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight,
        "Black Hole Simulation — Schwarzschild Ray Tracing", nullptr, nullptr);
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

    const std::string shaders = shaderDir();
    Shader rayShader(shaders + "/quad.vert", shaders + "/raytrace.frag");
    Shader gridShader(shaders + "/grid.vert", shaders + "/grid.frag");

    Quad quad;
    quad.init();
    GridRenderer grid;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);

    auto lastFrame = std::chrono::steady_clock::now();
    int frameCount = 0;
    float fps = 0.0f;

    std::cout << "Controls:\n"
              << "  Drag mouse — orbit camera\n"
              << "  Scroll     — zoom\n"
              << "  G          — toggle spacetime grid\n"
              << "  R          — reset camera\n"
              << "  Escape     — quit\n";

    while (!glfwWindowShouldClose(window)) {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        gTime += dt;

        ++frameCount;
        static float fpsTimer = 0.0f;
        fpsTimer += dt;
        if (fpsTimer >= 1.0f) {
            fps = static_cast<float>(frameCount) / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
            std::string title = "Black Hole Sim | " + std::to_string(static_cast<int>(fps)) + " FPS"
                              + (gShowGrid ? " | Grid ON" : " | Grid OFF");
            glfwSetWindowTitle(window, title.c_str());
        }

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        // ── Pass 1: GPU ray tracing (gravitational lensing + accretion disk) ──
        rayShader.use();
        const glm::vec3 camPos = gCamera.position();
        const glm::vec3 target = gCamera.target();
        const glm::vec3 forward = glm::normalize(target - camPos);
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        const glm::vec3 up = glm::cross(right, forward);
        const float fovTan = std::tan(glm::radians(gCamera.fov * 0.5f));

        rayShader.setVec2("uResolution", static_cast<float>(fbW), static_cast<float>(fbH));
        rayShader.setFloat("uTime", gTime);
        rayShader.setFloat("uRs", kRs);
        rayShader.setVec3("uCamPos", camPos.x, camPos.y, camPos.z);
        rayShader.setVec3("uCamRight", right.x, right.y, right.z);
        rayShader.setVec3("uCamUp", up.x, up.y, up.z);
        rayShader.setVec3("uCamForward", forward.x, forward.y, forward.z);
        rayShader.setFloat("uFovTan", fovTan);
        rayShader.setInt("uSteps", 96);
        rayShader.setFloat("uStepSize", 0.15f);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        quad.draw();

        // ── Pass 2: Spacetime curvature grid overlay (Flamm paraboloid) ──
        if (gShowGrid) {
            glDisable(GL_DEPTH_TEST);
            const glm::mat4 view = gCamera.viewMatrix();
            const glm::mat4 proj = gCamera.projectionMatrix(
                static_cast<float>(fbW) / static_cast<float>(fbH));
            grid.draw(gridShader, view, proj, kRs, gTime);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    quad.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}