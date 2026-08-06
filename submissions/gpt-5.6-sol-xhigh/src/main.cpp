#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 800;
constexpr double kPi = 3.14159265358979323846;

struct AppState {
    float yaw = 0.35F;
    float pitch = 24.0F * static_cast<float>(kPi / 180.0);
    float distance = 25.0F;
    bool dragging = false;
    bool paused = false;
    bool showDisk = true;
    bool showHalo = true;
    bool showGrid = true;
    int quality = 1;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
};

std::string readTextFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open shader: " + path);
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

GLuint compileShader(GLenum type, const std::string& source, const char* label) {
    const GLuint shader = glCreateShader(type);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string(label) + " compilation failed:\n" + log);
    }
    return shader;
}

GLuint createProgram(const std::string& fragmentSource) {
    static constexpr const char* vertexSource = R"GLSL(
#version 410 core
out vec2 v_uv;

void main() {
    vec2 position = vec2(
        (gl_VertexID == 1) ? 3.0 : -1.0,
        (gl_VertexID == 2) ? 3.0 : -1.0
    );
    v_uv = 0.5 * (position + 1.0);
    gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, "Vertex shader");
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "Fragment shader");
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Shader link failed:\n" + log);
    }
    return program;
}

void resetCamera(AppState& state) {
    state.yaw = 0.35F;
    state.pitch = 24.0F * static_cast<float>(kPi / 180.0);
    state.distance = 25.0F;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto& state = *static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    state.dragging = action == GLFW_PRESS;
    glfwGetCursorPos(window, &state.lastMouseX, &state.lastMouseY);
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& state = *static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state.dragging) {
        const double dx = x - state.lastMouseX;
        const double dy = y - state.lastMouseY;
        state.yaw -= static_cast<float>(dx * 0.0045);
        state.pitch = std::clamp(state.pitch + static_cast<float>(dy * 0.0035), -0.08F, 1.25F);
    }
    state.lastMouseX = x;
    state.lastMouseY = y;
}

void scrollCallback(GLFWwindow* window, double, double yOffset) {
    auto& state = *static_cast<AppState*>(glfwGetWindowUserPointer(window));
    state.distance = std::clamp(
        state.distance * std::exp(static_cast<float>(-yOffset * 0.09)), 10.0F, 45.0F);
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }
    auto& state = *static_cast<AppState*>(glfwGetWindowUserPointer(window));
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_SPACE:
            state.paused = !state.paused;
            break;
        case GLFW_KEY_D:
            state.showDisk = !state.showDisk;
            break;
        case GLFW_KEY_H:
            state.showHalo = !state.showHalo;
            break;
        case GLFW_KEY_G:
            state.showGrid = !state.showGrid;
            break;
        case GLFW_KEY_Q:
            state.quality = (state.quality + 1) % 3;
            break;
        case GLFW_KEY_R:
            resetCamera(state);
            break;
        default:
            break;
    }
}

void setUniform1i(GLuint program, const char* name, int value) {
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) {
        glUniform1i(location, value);
    }
}

void setUniform1f(GLuint program, const char* name, float value) {
    const GLint location = glGetUniformLocation(program, name);
    if (location >= 0) {
        glUniform1f(location, value);
    }
}

}  // namespace

int main() {
    try {
        if (glfwInit() == GLFW_FALSE) {
            throw std::runtime_error("GLFW initialization failed");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0);
#ifdef __APPLE__
        // A 1x framebuffer keeps the expensive geodesic integration real-time on Retina Macs.
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

        GLFWwindow* window = glfwCreateWindow(
            kInitialWidth, kInitialHeight, "Schwarzschild Black Hole — loading", nullptr, nullptr);
        if (window == nullptr) {
            glfwTerminate();
            throw std::runtime_error("Unable to create an OpenGL 4.1 window");
        }

        AppState state;
        glfwSetWindowUserPointer(window, &state);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        const std::string fragmentSource = readTextFile(BH_SHADER_PATH);
        const GLuint program = createProgram(fragmentSource);
        GLuint vertexArray = 0;
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        glDisable(GL_DEPTH_TEST);

        std::cout
            << "Real-time Schwarzschild ray tracer\n"
            << "  Left drag: orbit camera    Scroll: zoom    R: reset\n"
            << "  D: disk    H: halo    G: Flamm grid inset    Q: quality\n"
            << "  Space: pause    Esc: quit\n\n"
            << "Units: r_s = 2GM/c^2 = 1; photon sphere = 1.5 r_s; ISCO = 3 r_s.\n";

        const auto start = std::chrono::steady_clock::now();
        auto previousFrame = start;
        auto titleUpdate = start;
        double simulationTime = 0.0;
        int framesSinceTitle = 0;

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();
            const auto now = std::chrono::steady_clock::now();
            const double delta = std::chrono::duration<double>(now - previousFrame).count();
            previousFrame = now;
            if (!state.paused) {
                simulationTime += std::min(delta, 0.1);
            }

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            if (width == 0 || height == 0) {
                glfwWaitEvents();
                continue;
            }

            glViewport(0, 0, width, height);
            glUseProgram(program);
            const GLint resolution = glGetUniformLocation(program, "u_resolution");
            glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
            setUniform1f(program, "u_time", static_cast<float>(simulationTime));
            setUniform1f(program, "u_yaw", state.yaw);
            setUniform1f(program, "u_pitch", state.pitch);
            setUniform1f(program, "u_cameraDistance", state.distance);
            setUniform1i(program, "u_showDisk", state.showDisk ? 1 : 0);
            setUniform1i(program, "u_showHalo", state.showHalo ? 1 : 0);
            setUniform1i(program, "u_showGrid", state.showGrid ? 1 : 0);
            setUniform1i(program, "u_quality", state.quality);

            glDrawArrays(GL_TRIANGLES, 0, 3);
            glfwSwapBuffers(window);

            ++framesSinceTitle;
            const double titleSeconds = std::chrono::duration<double>(now - titleUpdate).count();
            if (titleSeconds >= 0.5) {
                const double fps = static_cast<double>(framesSinceTitle) / titleSeconds;
                static constexpr const char* qualityNames[] = {"fast", "balanced", "high"};
                std::ostringstream title;
                title << "Schwarzschild Ray Tracer | " << std::fixed << std::setprecision(1) << fps
                      << " FPS | " << qualityNames[state.quality]
                      << " | D disk: " << (state.showDisk ? "on" : "off")
                      << " | H halo: " << (state.showHalo ? "on" : "off")
                      << " | G Flamm grid: " << (state.showGrid ? "on" : "off");
                glfwSetWindowTitle(window, title.str().c_str());
                framesSinceTitle = 0;
                titleUpdate = now;
            }
        }

        glDeleteVertexArrays(1, &vertexArray);
        glDeleteProgram(program);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        glfwTerminate();
        return EXIT_FAILURE;
    }
}
