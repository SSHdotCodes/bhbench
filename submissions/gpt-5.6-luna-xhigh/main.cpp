#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct AppState {
    float cameraDistance = 26.0f;
    float inclination = 0.72f;
    float yaw = 0.0f;
    float exposure = 1.35f;
    bool showGrid = true;
    bool showDisk = true;
    bool showHalo = true;
    bool paused = false;
};

std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Unable to open shader file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint compileShader(GLenum type, const std::string& source, const char* label) {
    GLuint shader = glCreateShader(type);
    const char* sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << label << " shader compilation failed:\n" << log << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint makeProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, "Vertex");
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "Fragment");
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Program link failed:\n" << log << '\n';
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }

    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) {
        return;
    }

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_G:
            if (action == GLFW_PRESS) state->showGrid = !state->showGrid;
            break;
        case GLFW_KEY_D:
            if (action == GLFW_PRESS) state->showDisk = !state->showDisk;
            break;
        case GLFW_KEY_H:
            if (action == GLFW_PRESS) state->showHalo = !state->showHalo;
            break;
        case GLFW_KEY_SPACE:
            if (action == GLFW_PRESS) state->paused = !state->paused;
            break;
        case GLFW_KEY_R:
            if (action == GLFW_PRESS) *state = AppState{};
            break;
        case GLFW_KEY_UP:
            state->inclination = std::clamp(state->inclination - 0.035f, 0.05f, 1.52f);
            break;
        case GLFW_KEY_DOWN:
            state->inclination = std::clamp(state->inclination + 0.035f, 0.05f, 1.52f);
            break;
        case GLFW_KEY_LEFT:
            state->yaw -= 0.06f;
            break;
        case GLFW_KEY_RIGHT:
            state->yaw += 0.06f;
            break;
        case GLFW_KEY_W:
            state->cameraDistance = std::clamp(state->cameraDistance - 1.5f, 10.0f, 55.0f);
            break;
        case GLFW_KEY_S:
            state->cameraDistance = std::clamp(state->cameraDistance + 1.5f, 10.0f, 55.0f);
            break;
        case GLFW_KEY_EQUAL:
        case GLFW_KEY_KP_ADD:
            state->exposure = std::clamp(state->exposure + 0.08f, 0.25f, 3.0f);
            break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            state->exposure = std::clamp(state->exposure - 0.08f, 0.25f, 3.0f);
            break;
        default:
            break;
    }
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void printControls() {
    std::cout
        << "Black hole ray tracer\n"
        << "  Arrow keys: orbit camera\n"
        << "  W/S: zoom in/out\n"
        << "  G: spacetime grid   D: accretion disk   H: halo/photon ring\n"
        << "  +/-: exposure       Space: pause animation   R: reset   Esc: quit\n";
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed.\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Schwarzschild black hole — GPU ray tracer", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Unable to create an OpenGL window.\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Core OpenGL requires a VAO to be bound even when the vertex shader uses
    // gl_VertexID and has no vertex attributes.
    GLuint vertexArray = 0;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);

    AppState state;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    GLuint program = 0;
    try {
        program = makeProgram(readTextFile("black_hole.vert"), readTextFile("black_hole.frag"));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'
                  << "Run the executable with black_hole.vert and black_hole.frag in the working directory.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (program == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    GLint resolutionLocation = glGetUniformLocation(program, "uResolution");
    GLint timeLocation = glGetUniformLocation(program, "uTime");
    GLint cameraDistanceLocation = glGetUniformLocation(program, "uCameraDistance");
    GLint inclinationLocation = glGetUniformLocation(program, "uInclination");
    GLint yawLocation = glGetUniformLocation(program, "uYaw");
    GLint massLocation = glGetUniformLocation(program, "uMass");
    GLint showGridLocation = glGetUniformLocation(program, "uShowGrid");
    GLint showDiskLocation = glGetUniformLocation(program, "uShowDisk");
    GLint showHaloLocation = glGetUniformLocation(program, "uShowHalo");
    GLint exposureLocation = glGetUniformLocation(program, "uExposure");

    printControls();

    unsigned int frameCount = 0;
    double titleClock = glfwGetTime();
    double simulationTime = 0.0;
    double previousClock = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const double delta = std::min(now - previousClock, 0.1);
        previousClock = now;
        if (!state.paused) {
            simulationTime += delta;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glViewport(0, 0, framebufferWidth, framebufferHeight);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glUniform2f(resolutionLocation, static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));
        glUniform1f(timeLocation, static_cast<float>(simulationTime));
        glUniform1f(cameraDistanceLocation, state.cameraDistance);
        glUniform1f(inclinationLocation, state.inclination);
        glUniform1f(yawLocation, state.yaw);
        glUniform1f(massLocation, 1.0f);
        glUniform1i(showGridLocation, state.showGrid ? 1 : 0);
        glUniform1i(showDiskLocation, state.showDisk ? 1 : 0);
        glUniform1i(showHaloLocation, state.showHalo ? 1 : 0);
        glUniform1f(exposureLocation, state.exposure);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUseProgram(0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        ++frameCount;
        if (now - titleClock > 1.0) {
            const double fps = static_cast<double>(frameCount) / (now - titleClock);
            std::ostringstream title;
            title << "Schwarzschild black hole — GPU ray tracer — " << static_cast<int>(fps) << " FPS";
            glfwSetWindowTitle(window, title.str().c_str());
            titleClock = now;
            frameCount = 0;
        }
    }

    glDeleteProgram(program);
    glDeleteVertexArrays(1, &vertexArray);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
