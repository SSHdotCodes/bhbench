#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Shader.h"
#include "Camera.h"
#include "GridMesh.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

enum class Mode { Raytrace, Grid };

struct AppState {
    // Each view gets its own camera so switching modes always lands on a
    // well-framed shot; orbit/zoom controls act on whichever is active.
    Camera rtCamera;
    Camera gridCamera;
    Mode mode = Mode::Raytrace;

    Camera& activeCamera() { return mode == Mode::Raytrace ? rtCamera : gridCamera; }

    float rs = 1.0f;
    float diskInner = 3.0f;  // ISCO = 3*rs for a Schwarzschild black hole
    float diskOuter = 12.0f;

    int maxSteps = 520;
    float stepScale = 0.045f;
    int showLensing = 1;
    float diskBrightness = 1.1f;

    float renderScale = 1.0f;
    bool paused = false;
    float simTime = 0.0f;

    bool dragging = false;
    double lastMouseX = 0.0, lastMouseY = 0.0;

    int winW = 1280, winH = 800;
};

AppState g_app;

// The two views want different default framing: the raytracer looks
// best from a fairly low, near-edge-on angle (shows the lensed disk
// arcing dramatically over the shadow), while the curvature grid reads
// best from higher up, showing the whole funnel at once.
Camera makeDefaultGridCamera() {
    Camera c;
    c.distance = 24.0f;
    c.pitch = 0.95f;
    c.yaw = 0.6f;
    c.maxDistance = 300.0f;
    return c;
}

void printControls() {
    std::cout <<
        "\n=== Black Hole Simulator: Controls ===\n"
        "  Left-drag      Orbit camera\n"
        "  Scroll         Zoom\n"
        "  TAB / M        Toggle Raytrace <-> Spacetime Grid view\n"
        "  L              Toggle gravitational lensing on/off (A/B compare)\n"
        "  SPACE          Pause / resume disk motion\n"
        "  =/-            Increase / decrease render resolution\n"
        "  ]/[            Increase / decrease ray-march quality (steps)\n"
        "  R              Reset camera\n"
        "  ESC            Quit\n"
        "=======================================\n\n";
}

void framebufferSizeCallback(GLFWwindow*, int w, int h) {
    g_app.winW = w;
    g_app.winH = h;
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    g_app.activeCamera().zoom(std::pow(0.9f, static_cast<float>(yoffset)));
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_app.dragging = true;
            glfwGetCursorPos(window, &g_app.lastMouseX, &g_app.lastMouseY);
        } else if (action == GLFW_RELEASE) {
            g_app.dragging = false;
        }
    }
}

void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (g_app.dragging) {
        double dx = x - g_app.lastMouseX;
        double dy = y - g_app.lastMouseY;
        g_app.lastMouseX = x;
        g_app.lastMouseY = y;
        g_app.activeCamera().orbit(static_cast<float>(-dx) * 0.005f, static_cast<float>(-dy) * 0.005f);
    }
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_TAB:
        case GLFW_KEY_M:
            g_app.mode = (g_app.mode == Mode::Raytrace) ? Mode::Grid : Mode::Raytrace;
            std::cout << "Mode: " << (g_app.mode == Mode::Raytrace ? "Raytrace" : "Spacetime Grid") << "\n";
            break;
        case GLFW_KEY_L:
            g_app.showLensing = 1 - g_app.showLensing;
            std::cout << "Lensing: " << (g_app.showLensing ? "ON" : "OFF") << "\n";
            break;
        case GLFW_KEY_SPACE:
            g_app.paused = !g_app.paused;
            break;
        case GLFW_KEY_EQUAL:
            g_app.renderScale = std::clamp(g_app.renderScale + 0.1f, 0.2f, 1.0f);
            std::cout << "Render scale: " << g_app.renderScale << "\n";
            break;
        case GLFW_KEY_MINUS:
            g_app.renderScale = std::clamp(g_app.renderScale - 0.1f, 0.2f, 1.0f);
            std::cout << "Render scale: " << g_app.renderScale << "\n";
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            g_app.maxSteps = std::clamp(g_app.maxSteps + 40, 60, 900);
            std::cout << "Max steps: " << g_app.maxSteps << "\n";
            break;
        case GLFW_KEY_LEFT_BRACKET:
            g_app.maxSteps = std::clamp(g_app.maxSteps - 40, 60, 900);
            std::cout << "Max steps: " << g_app.maxSteps << "\n";
            break;
        case GLFW_KEY_R:
            if (g_app.mode == Mode::Raytrace) g_app.rtCamera = Camera();
            else g_app.gridCamera = makeDefaultGridCamera();
            break;
        default:
            break;
    }
}

unsigned int makeFullscreenQuadVAO(unsigned int& outVBO) {
    float verts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
    };
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    outVBO = vbo;
    return vao;
}

// Offscreen target the scene is rendered into; blitted (and optionally
// up/down-scaled) to the window each frame. Decouples ray-march cost from
// the window's (possibly Retina-doubled) pixel size, giving a simple real-
// time performance knob.
struct FBO {
    unsigned int fbo = 0, colorTex = 0, depthRBO = 0;
    int w = 0, h = 0;

    void destroy() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (colorTex) glDeleteTextures(1, &colorTex);
        if (depthRBO) glDeleteRenderbuffers(1, &depthRBO);
        fbo = colorTex = depthRBO = 0;
    }

    void create(int width, int height) {
        destroy();
        w = std::max(1, width);
        h = std::max(1, height);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

        glGenRenderbuffers(1, &depthRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Warning: offscreen framebuffer is incomplete\n";
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

} // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(g_app.winW, g_app.winH,
        "Black Hole Simulator - Schwarzschild Ray Tracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetKeyCallback(window, keyCallback);

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    g_app.winW = fbW;
    g_app.winH = fbH;
    g_app.gridCamera = makeDefaultGridCamera();

    printControls();
    std::cout << "GL_VERSION:  " << glGetString(GL_VERSION) << "\n";
    std::cout << "GL_RENDERER: " << glGetString(GL_RENDERER) << "\n\n";

    // All GL-resource-owning objects live in this scope so they are torn
    // down (via destructor/explicit delete) while the GL context is still
    // current. glfwDestroyWindow/glfwTerminate below would otherwise free
    // the context first, leaving any later-destructed GL handle to delete
    // into a context that no longer exists (undefined behavior on exit).
    {
        std::unique_ptr<Shader> raytraceShader;
        std::unique_ptr<Shader> gridShader;
        try {
            raytraceShader = std::make_unique<Shader>(
                std::string(SHADER_DIR) + "fullscreen.vert",
                std::string(SHADER_DIR) + "blackhole.frag");
            gridShader = std::make_unique<Shader>(
                std::string(SHADER_DIR) + "grid.vert",
                std::string(SHADER_DIR) + "grid.frag");
        } catch (const std::exception& e) {
            std::cerr << "Shader error: " << e.what() << "\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        unsigned int quadVBO = 0;
        unsigned int quadVAO = makeFullscreenQuadVAO(quadVBO);

        GridMesh grid;
        grid.init(g_app.rs, g_app.diskOuter * 1.6f, 90, 140);

        FBO fbo;
        fbo.create(static_cast<int>(fbW * g_app.renderScale), static_cast<int>(fbH * g_app.renderScale));
        float lastRenderScale = g_app.renderScale;
        int lastFbW = fbW, lastFbH = fbH;

        double lastFrameTime = glfwGetTime();
        double fpsAccum = 0.0;
        int fpsFrames = 0;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            double now = glfwGetTime();
            float dt = static_cast<float>(now - lastFrameTime);
            lastFrameTime = now;
            if (!g_app.paused) g_app.simTime += dt;

            fpsAccum += dt;
            fpsFrames++;
            if (fpsAccum >= 1.0) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "Black Hole Simulator  |  %.1f fps  |  %s",
                    fpsFrames / fpsAccum,
                    g_app.mode == Mode::Raytrace ? "Raytrace" : "Spacetime Grid");
                glfwSetWindowTitle(window, buf);
                fpsAccum = 0.0;
                fpsFrames = 0;
            }

            if (g_app.winW != lastFbW || g_app.winH != lastFbH || g_app.renderScale != lastRenderScale) {
                lastFbW = g_app.winW;
                lastFbH = g_app.winH;
                lastRenderScale = g_app.renderScale;
                fbo.create(static_cast<int>(lastFbW * g_app.renderScale), static_cast<int>(lastFbH * g_app.renderScale));
            }

            glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
            glViewport(0, 0, fbo.w, fbo.h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

            float aspect = static_cast<float>(fbo.w) / static_cast<float>(std::max(1, fbo.h));

            if (g_app.mode == Mode::Raytrace) {
                glDisable(GL_DEPTH_TEST);
                glClear(GL_COLOR_BUFFER_BIT);

                raytraceShader->use();
                raytraceShader->setVec3("uCamPos", g_app.rtCamera.position());
                raytraceShader->setVec3("uCamForward", g_app.rtCamera.forward());
                raytraceShader->setVec3("uCamRight", g_app.rtCamera.right());
                raytraceShader->setVec3("uCamUp", g_app.rtCamera.up());
                raytraceShader->setFloat("uTanHalfFov", std::tan(glm::radians(g_app.rtCamera.fovDeg) * 0.5f));
                raytraceShader->setFloat("uAspect", aspect);
                raytraceShader->setFloat("uRs", g_app.rs);
                raytraceShader->setFloat("uDiskInner", g_app.diskInner);
                raytraceShader->setFloat("uDiskOuter", g_app.diskOuter);
                raytraceShader->setFloat("uTime", g_app.simTime);
                raytraceShader->setFloat("uDiskBrightness", g_app.diskBrightness);
                raytraceShader->setInt("uMaxSteps", g_app.maxSteps);
                raytraceShader->setFloat("uStepScale", g_app.stepScale);
                raytraceShader->setInt("uShowLensing", g_app.showLensing);

                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
            } else {
                glEnable(GL_DEPTH_TEST);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                gridShader->use();
                gridShader->setMat4("uView", g_app.gridCamera.view());
                gridShader->setMat4("uProj", g_app.gridCamera.proj(aspect, 0.05f, 500.0f));
                gridShader->setFloat("uRs", g_app.rs);
                gridShader->setFloat("uDiskInner", g_app.diskInner);
                gridShader->setFloat("uDiskOuter", g_app.diskOuter);
                grid.render();
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glViewport(0, 0, g_app.winW, g_app.winH);
            glBlitFramebuffer(0, 0, fbo.w, fbo.h, 0, 0, g_app.winW, g_app.winH,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);

            glfwSwapBuffers(window);
        }

        fbo.destroy();
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
        // raytraceShader, gridShader and grid are destroyed here, at the
        // end of this scope, while the GL context is still current.
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
