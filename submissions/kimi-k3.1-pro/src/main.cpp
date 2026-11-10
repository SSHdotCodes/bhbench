#include "embedding.hpp"
#include "gl_utils.hpp"
#include "math.hpp"
#include "overlay.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef BLACK_HOLE_SHADER_DIR
#define BLACK_HOLE_SHADER_DIR "shaders"
#endif

namespace {
constexpr float PI = 3.14159265358979323846f;

struct State {
    float yaw = 0.0f;
    float pitch = 0.32f;
    float radius = 34.0f;
    float fovDegrees = 50.0f;
    float gridYaw = 0.65f;
    float gridPitch = 0.58f;
    float gridDistance = 66.0f;
    float exposure = 1.15f;
    float diskOpacity = 0.88f;
    float renderScale = 0.68f;
    double mouseX = 0.0;
    double mouseY = 0.0;
    bool leftDrag = false;
    bool rightDrag = false;
    bool hud = true;
    bool disk = true;
    bool stars = true;
    bool autoOrbit = false;
    bool adaptiveQuality = true;
    int viewMode = 0; // 0: lensing + inset, 1: lensing, 2: embedding only
    int maxSteps = 480;
    bool screenshotRequested = false;
};

Vec3 sphericalPosition(float radius, float yaw, float pitch) {
    return {radius * std::cos(pitch) * std::sin(yaw),
            radius * std::sin(pitch),
            radius * std::cos(pitch) * std::cos(yaw)};
}

std::string number(float value, int precision = 1) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

bool writeScreenshotPPM(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glFinish();
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        output.write(reinterpret_cast<const char*>(pixels.data() +
                     static_cast<size_t>(y) * width * 3),
                     static_cast<size_t>(width) * 3);
    }
    return static_cast<bool>(output);
}

State* state(GLFWwindow* window) {
    return static_cast<State*>(glfwGetWindowUserPointer(window));
}

void mouseButton(GLFWwindow* window, int button, int action, int) {
    State& s = *state(window);
    const bool pressed = action == GLFW_PRESS;
    if (button == GLFW_MOUSE_BUTTON_LEFT) s.leftDrag = pressed;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) s.rightDrag = pressed;
    glfwGetCursorPos(window, &s.mouseX, &s.mouseY);
}

void cursorPosition(GLFWwindow* window, double x, double y) {
    State& s = *state(window);
    const float dx = static_cast<float>(x - s.mouseX);
    const float dy = static_cast<float>(y - s.mouseY);
    s.mouseX = x;
    s.mouseY = y;
    if (s.leftDrag && s.viewMode != 2) {
        s.yaw -= dx * 0.005f;
        s.pitch = std::clamp(s.pitch + dy * 0.005f, -1.45f, 1.45f);
    }
    if (s.rightDrag || (s.leftDrag && s.viewMode == 2)) {
        s.gridYaw -= dx * 0.006f;
        s.gridPitch = std::clamp(s.gridPitch + dy * 0.006f, -1.35f, 1.35f);
    }
}

void scroll(GLFWwindow* window, double, double yOffset) {
    State& s = *state(window);
    if (s.viewMode == 2 || glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        s.gridDistance = std::clamp(s.gridDistance * static_cast<float>(std::exp(-0.08 * yOffset)), 42.0f, 140.0f);
    } else {
        s.radius = std::clamp(s.radius * static_cast<float>(std::exp(-0.055 * yOffset)), 12.0f, 80.0f);
    }
}

void key(GLFWwindow* window, int keyCode, int, int action, int) {
    if (action != GLFW_PRESS) return;
    State& s = *state(window);
    switch (keyCode) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        case GLFW_KEY_G: s.viewMode = (s.viewMode + 1) % 3; break;
        case GLFW_KEY_H: s.hud = !s.hud; break;
        case GLFW_KEY_D: s.disk = !s.disk; break;
        case GLFW_KEY_B: s.stars = !s.stars; break;
        case GLFW_KEY_SPACE: s.autoOrbit = !s.autoOrbit; break;
        case GLFW_KEY_Q: s.adaptiveQuality = !s.adaptiveQuality; break;
        case GLFW_KEY_R: {
            const bool hud = s.hud;
            s = State{};
            s.hud = hud;
            break;
        }
        case GLFW_KEY_LEFT_BRACKET: s.exposure = std::max(0.25f, s.exposure - 0.1f); break;
        case GLFW_KEY_RIGHT_BRACKET: s.exposure = std::min(4.0f, s.exposure + 0.1f); break;
        case GLFW_KEY_F12: s.screenshotRequested = true; break;
        default: break;
    }
}

class Renderer {
public:
    explicit Renderer(const std::string& shaderDirectory)
        : embedding_(shaderDirectory), overlay_(shaderDirectory) {
        rayProgram_ = loadProgram(shaderDirectory, "fullscreen.vert", "blackhole.frag");
        presentProgram_ = loadProgram(shaderDirectory, "fullscreen.vert", "present.frag");
        glGenVertexArrays(1, &fullscreenVAO_);
        glGenFramebuffers(1, &hdrFBO_);
        glGenTextures(1, &hdrTexture_);
    }

    ~Renderer() {
        glDeleteTextures(1, &hdrTexture_);
        glDeleteFramebuffers(1, &hdrFBO_);
        glDeleteVertexArrays(1, &fullscreenVAO_);
        glDeleteProgram(rayProgram_);
        glDeleteProgram(presentProgram_);
    }

    void resize(int framebufferWidth, int framebufferHeight, float scale) {
        const int width = std::max(1, static_cast<int>(framebufferWidth * scale));
        const int height = std::max(1, static_cast<int>(framebufferHeight * scale));
        if (width == targetWidth_ && height == targetHeight_) return;
        targetWidth_ = width;
        targetHeight_ = height;
        glBindTexture(GL_TEXTURE_2D, hdrTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, hdrTexture_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("HDR framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void render(const State& s, int framebufferWidth, int framebufferHeight,
                int windowWidth, int windowHeight, float fps) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        if (s.viewMode != 2) {
            renderLensing(s);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glUseProgram(presentProgram_);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hdrTexture_);
            glUniform1i(uniform(presentProgram_, "uImage"), 0);
            glUniform1f(uniform(presentProgram_, "uExposure"), s.exposure);
            glBindVertexArray(fullscreenVAO_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.003f, 0.006f, 0.013f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        if (s.viewMode == 0) {
            const float width = std::min(390.0f, windowWidth * 0.36f);
            const float height = 278.0f;
            const float x = windowWidth - width - 22.0f;
            const float bottom = 49.0f;
            renderGrid(s, x, bottom, width, height, windowWidth, windowHeight,
                       framebufferWidth, framebufferHeight);
        } else if (s.viewMode == 2) {
            renderGrid(s, 0.0f, 0.0f, static_cast<float>(windowWidth),
                       static_cast<float>(windowHeight), windowWidth, windowHeight,
                       framebufferWidth, framebufferHeight);
        }

        if (s.hud) {
            drawOverlay(s, windowWidth, windowHeight, fps);
        }
        glBindVertexArray(0);
    }

private:
    void renderLensing(const State& s) {
        const Vec3 camera = sphericalPosition(s.radius, s.yaw, s.pitch);
        const Vec3 forward = normalize(camera * -1.0f);
        const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const Vec3 up = normalize(cross(right, forward));
        const float observerLapse = 1.0f - 2.0f / s.radius;

        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
        glViewport(0, 0, targetWidth_, targetHeight_);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(rayProgram_);
        glUniform2f(uniform(rayProgram_, "uResolution"),
                    static_cast<float>(targetWidth_), static_cast<float>(targetHeight_));
        glUniform3f(uniform(rayProgram_, "uCameraPos"), camera.x, camera.y, camera.z);
        glUniform3f(uniform(rayProgram_, "uCameraRight"), right.x, right.y, right.z);
        glUniform3f(uniform(rayProgram_, "uCameraUp"), up.x, up.y, up.z);
        glUniform3f(uniform(rayProgram_, "uCameraForward"), forward.x, forward.y, forward.z);
        glUniform1f(uniform(rayProgram_, "uTanHalfFov"),
                    std::tan(s.fovDegrees * PI / 360.0f));
        glUniform1f(uniform(rayProgram_, "uObserverLapse"), observerLapse);
        glUniform1f(uniform(rayProgram_, "uDiskOpacity"), s.diskOpacity);
        glUniform1i(uniform(rayProgram_, "uShowDisk"), s.disk ? 1 : 0);
        glUniform1i(uniform(rayProgram_, "uShowStars"), s.stars ? 1 : 0);
        glUniform1i(uniform(rayProgram_, "uMaxSteps"), s.maxSteps);
        glBindVertexArray(fullscreenVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void renderGrid(const State& s, float x, float bottom, float width, float height,
                    int windowWidth, int windowHeight,
                    int framebufferWidth, int framebufferHeight) {
        const float sx = static_cast<float>(framebufferWidth) / windowWidth;
        const float sy = static_cast<float>(framebufferHeight) / windowHeight;
        const int vx = static_cast<int>(x * sx);
        const int vy = static_cast<int>(bottom * sy);
        const int vw = std::max(1, static_cast<int>(width * sx));
        const int vh = std::max(1, static_cast<int>(height * sy));
        glViewport(vx, vy, vw, vh);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vx, vy, vw, vh);
        glClearColor(0.003f, 0.007f, 0.014f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        const Vec3 target = {0.0f, -5.0f, 0.0f};
        const Vec3 eye = target + sphericalPosition(s.gridDistance, s.gridYaw, s.gridPitch);
        const Mat4 view = lookAt(eye, target, {0.0f, 1.0f, 0.0f});
        const Mat4 projection = perspective(45.0f * PI / 180.0f,
                                             width / height, 0.1f, 260.0f);
        embedding_.draw(projection * view);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
    }

    void insetBorder(float x, float y, float width, float height) {
        const Vec3 color = {0.16f, 0.29f, 0.34f};
        overlay_.rectangle(x - 1.0f, y - 1.0f, width + 2.0f, 1.0f, color, 0.8f);
        overlay_.rectangle(x - 1.0f, y + height, width + 2.0f, 1.0f, color, 0.8f);
        overlay_.rectangle(x - 1.0f, y, 1.0f, height, color, 0.8f);
        overlay_.rectangle(x + width, y, 1.0f, height, color, 0.8f);
    }

    void drawOverlay(const State& s, int width, int height, float fps) {
        overlay_.begin(static_cast<float>(width), static_cast<float>(height));
        const Vec3 white = {0.84f, 0.91f, 0.94f};
        const Vec3 muted = {0.42f, 0.60f, 0.65f};
        const Vec3 cyan = {0.24f, 0.80f, 0.90f};
        const Vec3 panel = {0.008f, 0.016f, 0.026f};

        if (s.viewMode != 2) {
            overlay_.rectangle(18.0f, 18.0f, 500.0f, 135.0f, panel, 0.79f);
            overlay_.rectangle(18.0f, 18.0f, 3.0f, 135.0f, cyan, 0.9f);
            overlay_.text(32.0f, 29.0f, "SCHWARZSCHILD BLACK HOLE", 1.75f, white);
            overlay_.text(32.0f, 52.0f, "GPU NULL-GEODESIC RAY TRACING / G=C=M=1", 1.1f, cyan);
            overlay_.text(32.0f, 72.0f,
                          "STATIC OBSERVER  R=" + number(s.radius) + "M  FOV=" +
                          number(s.fovDegrees, 0) + " DEG", 1.2f, white);
            overlay_.text(32.0f, 90.0f, "DISK 6M-16M / INNER EDGE = ISCO", 1.2f, white);
            overlay_.text(32.0f, 108.0f,
                          number(fps, 0) + " FPS  " + number(s.renderScale * 100.0f, 0) +
                          "% RES  " + std::to_string(s.maxSteps) + " MAX RK4 STEPS", 1.2f, muted);
            overlay_.text(32.0f, 126.0f,
                          std::string("D DISK:") + (s.disk ? "ON" : "OFF") +
                          "  B SKY:" + (s.stars ? "ON" : "OFF") +
                          "  Q ADAPT:" + (s.adaptiveQuality ? "ON" : "OFF"), 1.1f, muted);
        } else {
            overlay_.rectangle(22.0f, 22.0f, 535.0f, 164.0f, panel, 0.84f);
            overlay_.rectangle(22.0f, 22.0f, 3.0f, 164.0f, cyan, 0.9f);
            overlay_.text(38.0f, 35.0f, "FLAMM SPATIAL EMBEDDING", 1.9f, white);
            overlay_.text(38.0f, 64.0f, "CONSTANT-T / EQUATORIAL SCHWARZSCHILD SLICE", 1.15f, cyan);
            overlay_.text(38.0f, 88.0f, "DL^2 = DR^2/(1-2M/R) + R^2 DPHI^2", 1.25f, white);
            overlay_.text(38.0f, 110.0f, "Z(R) = 2 SQRT(2M(R-2M))", 1.25f, white);
            overlay_.text(38.0f, 137.0f, "SPATIAL GEOMETRY / NOT A NEWTONIAN POTENTIAL", 1.15f, muted);
            overlay_.text(38.0f, 158.0f, "RMB DRAG TO ORBIT / SHIFT+WHEEL TO ZOOM", 1.05f, muted);
        }

        if (s.viewMode == 0) {
            const float w = std::min(390.0f, width * 0.36f);
            const float h = 278.0f;
            const float x = width - w - 22.0f;
            const float y = height - 49.0f - h;
            insetBorder(x, y, w, h);
            overlay_.rectangle(x, y, w, 25.0f, panel, 0.88f);
            overlay_.text(x + 10.0f, y + 7.0f, "FLAMM SPATIAL EMBEDDING / T=CONST", 1.05f, cyan);
            overlay_.rectangle(x, y + h - 27.0f, w, 27.0f, panel, 0.88f);
            overlay_.text(x + 10.0f, y + h - 19.0f, "R: 2M", 1.05f, {0.95f, 0.20f, 0.10f});
            overlay_.text(x + 76.0f, y + h - 19.0f, "PHOTON: 3M", 1.05f, {1.0f, 0.58f, 0.12f});
            overlay_.text(x + 198.0f, y + h - 19.0f, "ISCO: 6M", 1.05f, {0.95f, 0.88f, 0.38f});
            overlay_.text(x + 10.0f, y + 31.0f, "SPATIAL SLICE / NOT A POTENTIAL WELL", 0.9f, muted, 0.85f);
        }

        const float controlsY = height - 36.0f;
        overlay_.rectangle(0.0f, controlsY, static_cast<float>(width), 36.0f, panel, 0.84f);
        overlay_.text(18.0f, controlsY + 7.0f,
                      "LMB ORBIT  WHEEL ZOOM  SPACE AUTO  G VIEW  RMB GRID  R RESET  F12 SAVE PPM",
                      1.05f, white);
        overlay_.text(18.0f, controlsY + 21.0f,
                      "D DISK  B SKY  H HUD  Q ADAPTIVE  [ ] EXPOSURE  ESC QUIT", 1.0f, muted);
        overlay_.draw();
    }

    EmbeddingDiagram embedding_;
    Overlay overlay_;
    GLuint rayProgram_ = 0;
    GLuint presentProgram_ = 0;
    GLuint fullscreenVAO_ = 0;
    GLuint hdrFBO_ = 0;
    GLuint hdrTexture_ = 0;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
};

void printHelp() {
    std::cout << "Usage: black-hole [options]\n"
              << "  --screenshot PATH  Save a PPM screenshot after --frames and exit\n"
              << "  --frames N         Frames to render before an automatic screenshot (default 45)\n"
              << "  --view N           0=lensing+grid, 1=lensing, 2=grid\n"
              << "  --scale X          Initial internal resolution scale (0.35 to 1.0)\n"
              << "  --no-adapt         Disable dynamic resolution\n"
              << "  --help             Show this help\n";
}

} // namespace

int main(int argc, char** argv) {
    State appState;
    std::string screenshotPath = "black-hole-screenshot.ppm";
    bool automaticScreenshot = false;
    int automaticFrames = 45;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help") {
            printHelp();
            return EXIT_SUCCESS;
        }
        if (argument == "--no-adapt") {
            appState.adaptiveQuality = false;
        } else if (argument == "--view" && i + 1 < argc) {
            appState.viewMode = std::clamp(std::atoi(argv[++i]), 0, 2);
        } else if (argument == "--scale" && i + 1 < argc) {
            appState.renderScale = std::clamp(static_cast<float>(std::atof(argv[++i])), 0.35f, 1.0f);
        } else if (argument == "--frames" && i + 1 < argc) {
            automaticFrames = std::max(1, std::atoi(argv[++i]));
        } else if (argument == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
            automaticScreenshot = true;
            appState.adaptiveQuality = false;
        } else {
            std::cerr << "Unknown or incomplete option: " << argument << '\n';
            printHelp();
            return EXIT_FAILURE;
        }
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1480, 920,
        "Schwarzschild | Relativistic ray tracing", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create an OpenGL 3.3 window\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window, &appState);
    glfwSetMouseButtonCallback(window, mouseButton);
    glfwSetCursorPosCallback(window, cursorPosition);
    glfwSetScrollCallback(window, scroll);
    glfwSetKeyCallback(window, key);

    try {
        Renderer renderer(BLACK_HOLE_SHADER_DIR);
        std::cout << "Schwarzschild renderer ready (G=c=M=1)\n"
                  << "  null geodesics: u'' + u = 3u^2\n"
                  << "  horizon 2M | photon sphere 3M | disk ISCO 6M\n"
                  << "  G cycles views; H toggles help; F12 saves a screenshot\n";

        double previousTime = glfwGetTime();
        double statisticTime = previousTime;
        double qualityTime = previousTime;
        int statisticFrames = 0;
        int qualityFrames = 0;
        int totalFrames = 0;
        float displayedFPS = 0.0f;

        while (!glfwWindowShouldClose(window)) {
            const double now = glfwGetTime();
            const float dt = std::min(0.1f, static_cast<float>(now - previousTime));
            previousTime = now;
            if (appState.autoOrbit && !appState.leftDrag) appState.yaw += dt * 0.10f;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                appState.radius = std::max(12.0f, appState.radius - dt * 10.0f);
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                appState.radius = std::min(80.0f, appState.radius + dt * 10.0f);
            }
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
                appState.fovDegrees = std::max(24.0f, appState.fovDegrees - dt * 20.0f);
            }
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
                appState.fovDegrees = std::min(90.0f, appState.fovDegrees + dt * 20.0f);
            }

            int framebufferWidth = 0, framebufferHeight = 0;
            int windowWidth = 0, windowHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            if (framebufferWidth == 0 || framebufferHeight == 0 ||
                windowWidth == 0 || windowHeight == 0) {
                glfwWaitEventsTimeout(0.1);
                continue;
            }

            if (appState.viewMode != 2) {
                renderer.resize(framebufferWidth, framebufferHeight, appState.renderScale);
            }
            renderer.render(appState, framebufferWidth, framebufferHeight,
                            windowWidth, windowHeight, displayedFPS);

            if (appState.screenshotRequested ||
                (automaticScreenshot && totalFrames + 1 >= automaticFrames)) {
                if (writeScreenshotPPM(screenshotPath, framebufferWidth, framebufferHeight)) {
                    std::cout << "Screenshot saved to " << screenshotPath << '\n';
                } else {
                    std::cerr << "Could not save screenshot to " << screenshotPath << '\n';
                }
                appState.screenshotRequested = false;
                if (automaticScreenshot) glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            glfwSwapBuffers(window);
            glfwPollEvents();
            ++totalFrames;
            ++statisticFrames;
            ++qualityFrames;

            if (now - statisticTime >= 0.5) {
                displayedFPS = static_cast<float>(statisticFrames / (now - statisticTime));
                statisticFrames = 0;
                statisticTime = now;
                std::ostringstream title;
                title << "Schwarzschild | " << static_cast<int>(displayedFPS + 0.5f)
                      << " fps | R=" << number(appState.radius) << "M | G=c=M=1";
                glfwSetWindowTitle(window, title.str().c_str());
            }
            if (now - qualityTime >= 2.0) {
                const float averageFPS = static_cast<float>(qualityFrames / (now - qualityTime));
                if (appState.adaptiveQuality && appState.viewMode != 2 && totalFrames > 30) {
                    if (averageFPS < 44.0f) {
                        appState.renderScale = std::max(0.35f, appState.renderScale - 0.07f);
                    } else if (averageFPS > 57.0f) {
                        appState.renderScale = std::min(1.0f, appState.renderScale + 0.035f);
                    }
                }
                qualityFrames = 0;
                qualityTime = now;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
