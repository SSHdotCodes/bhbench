#include "shaders.hpp"

#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct QualityPreset {
    const char* name;
    float renderScale;
    int maximumDimension;
    float geodesicStepScale;
    int geodesicSteps;
    int embeddingSteps;
};

constexpr std::array<QualityPreset, 4> kQualityPresets{{
    {"Performance", 0.46F, 960, 0.050F, 300, 180},
    {"Balanced",    0.62F, 1280, 0.034F, 520, 240},
    {"High",        0.78F, 1700, 0.023F, 820, 290},
    {"Reference",   1.00F, 2400, 0.015F, 1400, 320},
}};

struct AppState {
    GLFWwindow* window = nullptr;
    float yaw = 0.18F;
    float pitch = 0.32F;
    float distance = 32.0F;
    float exposure = 1.22F;
    double simulationTime = 0.0;
    bool paused = false;
    bool autoOrbit = true;
    bool showDisk = true;
    bool showHalo = true;
    bool showGrid = true;
    bool lensing = true;
    bool bloom = true;
    bool dragging = false;
    bool vsync = true;
    int viewMode = 2;
    int quality = 1;
    double previousCursorX = 0.0;
    double previousCursorY = 0.0;
    bool fullscreen = false;
    int windowedX = 100;
    int windowedY = 100;
    int windowedWidth = 1400;
    int windowedHeight = 900;
};

void glfwErrorCallback(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader compilation failed:\n" + log);
}

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) return program;

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Program linking failed:\n" + log);
}

GLint uniform(GLuint program, const char* name) {
    const GLint location = glGetUniformLocation(program, name);
    if (location < 0) {
        throw std::runtime_error(std::string("Required shader uniform missing: ") + name);
    }
    return location;
}

struct SceneUniforms {
    GLint resolution;
    GLint time;
    GLint yaw;
    GLint pitch;
    GLint distance;
    GLint stepScale;
    GLint maxSteps;
    GLint embeddingSteps;
    GLint viewMode;
    GLint showDisk;
    GLint showHalo;
    GLint showGrid;
    GLint lensing;

    explicit SceneUniforms(GLuint program)
        : resolution(uniform(program, "uResolution")),
          time(uniform(program, "uTime")),
          yaw(uniform(program, "uYaw")),
          pitch(uniform(program, "uPitch")),
          distance(uniform(program, "uDistance")),
          stepScale(uniform(program, "uStepScale")),
          maxSteps(uniform(program, "uMaxSteps")),
          embeddingSteps(uniform(program, "uEmbeddingSteps")),
          viewMode(uniform(program, "uViewMode")),
          showDisk(uniform(program, "uShowDisk")),
          showHalo(uniform(program, "uShowHalo")),
          showGrid(uniform(program, "uShowGrid")),
          lensing(uniform(program, "uLensing")) {}
};

struct PresentUniforms {
    GLint scene;
    GLint textureSize;
    GLint exposure;
    GLint bloom;

    explicit PresentUniforms(GLuint program)
        : scene(uniform(program, "uScene")),
          textureSize(uniform(program, "uTextureSize")),
          exposure(uniform(program, "uExposure")),
          bloom(uniform(program, "uBloom")) {}
};

struct RenderTarget {
    GLuint framebuffer = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;

    void initialize() {
        glGenFramebuffers(1, &framebuffer);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void resize(int newWidth, int newHeight) {
        newWidth = std::max(newWidth, 1);
        newHeight = std::max(newHeight, 1);
        if (newWidth == width && newHeight == height) return;
        width = newWidth;
        height = newHeight;

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture, 0);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("HDR render framebuffer is incomplete");
        }
    }

    void destroy() {
        if (texture != 0) glDeleteTextures(1, &texture);
        if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
        texture = 0;
        framebuffer = 0;
    }
};

const char* viewName(int viewMode) {
    switch (viewMode) {
        case 0: return "OBSERVER";
        case 1: return "CURVATURE";
        default: return "SPLIT";
    }
}

void resetState(AppState& state) {
    state.yaw = 0.18F;
    state.pitch = 0.32F;
    state.distance = 32.0F;
    state.exposure = 1.22F;
    state.simulationTime = 0.0;
    state.paused = false;
    state.autoOrbit = true;
    state.showDisk = true;
    state.showHalo = true;
    state.showGrid = true;
    state.lensing = true;
    state.bloom = true;
    state.viewMode = 2;
    state.quality = 1;
}

void toggleFullscreen(AppState& state) {
    if (!state.fullscreen) {
        glfwGetWindowPos(state.window, &state.windowedX, &state.windowedY);
        glfwGetWindowSize(state.window, &state.windowedWidth, &state.windowedHeight);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode != nullptr) {
            glfwSetWindowMonitor(state.window, monitor, 0, 0,
                                 mode->width, mode->height, mode->refreshRate);
            state.fullscreen = true;
        }
    } else {
        glfwSetWindowMonitor(state.window, nullptr,
                             state.windowedX, state.windowedY,
                             state.windowedWidth, state.windowedHeight,
                             GLFW_DONT_CARE);
        state.fullscreen = false;
    }
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_1:
            state->viewMode = 0;
            break;
        case GLFW_KEY_2:
            state->viewMode = 1;
            break;
        case GLFW_KEY_3:
            state->viewMode = 2;
            break;
        case GLFW_KEY_SPACE:
            state->paused = !state->paused;
            break;
        case GLFW_KEY_A:
            state->autoOrbit = !state->autoOrbit;
            break;
        case GLFW_KEY_D:
            state->showDisk = !state->showDisk;
            break;
        case GLFW_KEY_H:
            state->showHalo = !state->showHalo;
            break;
        case GLFW_KEY_G:
            state->showGrid = !state->showGrid;
            break;
        case GLFW_KEY_L:
            state->lensing = !state->lensing;
            break;
        case GLFW_KEY_B:
            state->bloom = !state->bloom;
            break;
        case GLFW_KEY_Q:
            state->quality = (state->quality + 1)
                           % static_cast<int>(kQualityPresets.size());
            break;
        case GLFW_KEY_LEFT_BRACKET:
            state->exposure = std::max(0.25F, state->exposure / 1.15F);
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            state->exposure = std::min(4.0F, state->exposure * 1.15F);
            break;
        case GLFW_KEY_V:
            state->vsync = !state->vsync;
            glfwSwapInterval(state->vsync ? 1 : 0);
            break;
        case GLFW_KEY_F:
            toggleFullscreen(*state);
            break;
        case GLFW_KEY_R:
            resetState(*state);
            break;
        default:
            break;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;
    state->dragging = action == GLFW_PRESS;
    if (state->dragging) {
        glfwGetCursorPos(window, &state->previousCursorX, &state->previousCursorY);
        state->autoOrbit = false;
    }
}

void cursorPositionCallback(GLFWwindow* window, double x, double y) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || !state->dragging) return;
    const double dx = x - state->previousCursorX;
    const double dy = y - state->previousCursorY;
    state->previousCursorX = x;
    state->previousCursorY = y;
    state->yaw -= static_cast<float>(dx * 0.0045);
    state->pitch = std::clamp(state->pitch - static_cast<float>(dy * 0.0038),
                              0.055F, 1.02F);
}

void scrollCallback(GLFWwindow* window, double, double yOffset) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;
    state->distance *= static_cast<float>(std::exp(-yOffset * 0.085));
    state->distance = std::clamp(state->distance, 12.0F, 60.0F);
    state->autoOrbit = false;
}

void updateWindowTitle(AppState& state, double framesPerSecond,
                       int renderWidth, int renderHeight) {
    const QualityPreset& preset = kQualityPresets[static_cast<std::size_t>(state.quality)];
    std::ostringstream title;
    title << "Schwarzschild Lab | " << viewName(state.viewMode)
          << " | " << std::fixed << std::setprecision(0) << framesPerSecond << " FPS"
          << " | r_o=" << std::setprecision(1) << state.distance << "M"
          << " | " << preset.name << ' ' << renderWidth << 'x' << renderHeight
          << " | cyan r=2 boundary, gold r=3, green r=6"
          << " | 1/2/3 views, drag, scroll, Space, Q";
    glfwSetWindowTitle(state.window, title.str().c_str());
}

void printControls() {
    std::cout
        << "\nSchwarzschild Lab - exact null geodesics in a fixed Schwarzschild metric\n"
        << "Geometric units: G = c = M = 1; horizon r=2, photon sphere r=3, ISCO r=6.\n\n"
        << "Controls\n"
        << "  1 / 2 / 3 : observer / curvature embedding / split view\n"
        << "  Left drag  : orbit observer\n"
        << "  Scroll     : observer radius\n"
        << "  Space      : pause disk emissivity animation\n"
        << "  A          : auto orbit\n"
        << "  D / H / G  : disk / halo / embedding grid\n"
        << "  L          : exact lensing vs straight-ray comparison\n"
        << "  B          : display bloom\n"
        << "  Q          : performance / balanced / high / reference quality\n"
        << "  [ / ]      : exposure down / up\n"
        << "  V / F      : vsync / fullscreen\n"
        << "  R / Esc    : reset / quit\n\n";
}

} // namespace

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Could not initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    AppState state;
    GLuint sceneProgram = 0;
    GLuint presentProgram = 0;
    GLuint vertexArray = 0;
    RenderTarget renderTarget;

    try {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0);
#ifdef __APPLE__
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

        state.window = glfwCreateWindow(state.windowedWidth, state.windowedHeight,
                                        "Schwarzschild Lab", nullptr, nullptr);
        if (state.window == nullptr) {
            throw std::runtime_error("Could not create an OpenGL 4.1 window");
        }
        glfwMakeContextCurrent(state.window);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(state.window, &state);
        glfwSetKeyCallback(state.window, keyCallback);
        glfwSetMouseButtonCallback(state.window, mouseButtonCallback);
        glfwSetCursorPosCallback(state.window, cursorPositionCallback);
        glfwSetScrollCallback(state.window, scrollCallback);

        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        std::cout << "GPU: " << (renderer != nullptr ? renderer : "unknown") << '\n';
        std::cout << "OpenGL: " << (version != nullptr ? version : "unknown") << '\n';

        sceneProgram = createProgram(shaders::kFullscreenVertex,
                                     shaders::kBlackHoleFragment);
        presentProgram = createProgram(shaders::kFullscreenVertex,
                                       shaders::kPresentFragment);
        const SceneUniforms sceneUniforms(sceneProgram);
        const PresentUniforms presentUniforms(presentProgram);

        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        renderTarget.initialize();
        printControls();

        double previousFrameTime = glfwGetTime();
        double titleTimer = previousFrameTime;
        int titleFrameCount = 0;

        while (glfwWindowShouldClose(state.window) == GLFW_FALSE) {
            glfwPollEvents();
            const double now = glfwGetTime();
            const double deltaTime = std::min(now - previousFrameTime, 0.1);
            previousFrameTime = now;

            if (state.autoOrbit) {
                state.yaw += static_cast<float>(deltaTime * 0.045);
            }
            if (!state.paused) {
                state.simulationTime += deltaTime * 4.0;
            }

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(state.window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                glfwWaitEventsTimeout(0.05);
                continue;
            }

            const QualityPreset& preset =
                kQualityPresets[static_cast<std::size_t>(state.quality)];
            float scale = preset.renderScale;
            const int framebufferMaximum = std::max(framebufferWidth, framebufferHeight);
            if (static_cast<float>(framebufferMaximum) * scale
                > static_cast<float>(preset.maximumDimension)) {
                scale = static_cast<float>(preset.maximumDimension)
                      / static_cast<float>(framebufferMaximum);
            }
            const int renderWidth = std::max(
                1, static_cast<int>(std::lround(static_cast<double>(framebufferWidth)
                                                * static_cast<double>(scale))));
            const int renderHeight = std::max(
                1, static_cast<int>(std::lround(static_cast<double>(framebufferHeight)
                                                * static_cast<double>(scale))));
            renderTarget.resize(renderWidth, renderHeight);

            glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
            glViewport(0, 0, renderTarget.width, renderTarget.height);
            glUseProgram(sceneProgram);
            glUniform2f(sceneUniforms.resolution,
                        static_cast<float>(renderTarget.width),
                        static_cast<float>(renderTarget.height));
            glUniform1f(sceneUniforms.time, static_cast<float>(state.simulationTime));
            glUniform1f(sceneUniforms.yaw, state.yaw);
            glUniform1f(sceneUniforms.pitch, state.pitch);
            glUniform1f(sceneUniforms.distance, state.distance);
            glUniform1f(sceneUniforms.stepScale, preset.geodesicStepScale);
            glUniform1i(sceneUniforms.maxSteps, preset.geodesicSteps);
            glUniform1i(sceneUniforms.embeddingSteps, preset.embeddingSteps);
            glUniform1i(sceneUniforms.viewMode, state.viewMode);
            glUniform1i(sceneUniforms.showDisk, state.showDisk ? 1 : 0);
            glUniform1i(sceneUniforms.showHalo, state.showHalo ? 1 : 0);
            glUniform1i(sceneUniforms.showGrid, state.showGrid ? 1 : 0);
            glUniform1i(sceneUniforms.lensing, state.lensing ? 1 : 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glUseProgram(presentProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderTarget.texture);
            glUniform1i(presentUniforms.scene, 0);
            glUniform2f(presentUniforms.textureSize,
                        static_cast<float>(renderTarget.width),
                        static_cast<float>(renderTarget.height));
            glUniform1f(presentUniforms.exposure, state.exposure);
            glUniform1i(presentUniforms.bloom, state.bloom ? 1 : 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glfwSwapBuffers(state.window);

            ++titleFrameCount;
            const double titleInterval = now - titleTimer;
            if (titleInterval >= 0.5) {
                const double fps = static_cast<double>(titleFrameCount) / titleInterval;
                updateWindowTitle(state, fps, renderTarget.width, renderTarget.height);
                titleTimer = now;
                titleFrameCount = 0;
            }
        }

        renderTarget.destroy();
        glDeleteVertexArrays(1, &vertexArray);
        glDeleteProgram(sceneProgram);
        glDeleteProgram(presentProgram);
        glfwDestroyWindow(state.window);
        glfwTerminate();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        if (state.window != nullptr) {
            glfwMakeContextCurrent(state.window);
            renderTarget.destroy();
            if (vertexArray != 0) glDeleteVertexArrays(1, &vertexArray);
            if (sceneProgram != 0) glDeleteProgram(sceneProgram);
            if (presentProgram != 0) glDeleteProgram(presentProgram);
            glfwDestroyWindow(state.window);
        }
        glfwTerminate();
        return EXIT_FAILURE;
    }
}
