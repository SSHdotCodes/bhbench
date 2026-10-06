//
// Black Hole Simulation - Main Entry
// ===================================
// OpenGL core profile with GLFW window management.
// Renders a general-relativistic black hole using GPU ray tracing
// of Schwarzschild geodesics.
//

#define GL_SILENCE_DEPRECATION

#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <iostream>
#include <cmath>
#include <string>
#include <fstream>
#include <sstream>

struct vec3 {
    float x, y, z;
    vec3() : x(0), y(0), z(0) {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// ── Global state for GLFW callbacks ─────────────────────────────────
struct AppState {
    float yaw = -90.0f;
    float pitch = 15.0f;
    float distance = 18.0f;
    int showGrid = 1;
    float gridDensity = 1.0f;
    float gridBend = 1.0f;
    float exposure = 1.5f;
    bool autoRotate = true;
    float autoRotateSpeed = 8.0f;
    bool paused = false;
    float simTime = 0.0f;
    bool mouseDown = false;
    double lastMouseX = 0;
    double lastMouseY = 0;
    GLFWwindow* window = nullptr;
};

static AppState gState;

// ── Shader compilation helpers ──────────────────────────────────────
static std::string readShaderFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static GLuint compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "Shader compilation failed:\n" << info << std::endl;
    }
    return shader;
}

static GLuint createProgram(const std::string& vertSrc, const std::string& fragSrc) {
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar info[512];
        glGetProgramInfoLog(program, 512, nullptr, info);
        std::cerr << "Program link failed:\n" << info << std::endl;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

// ── Static callback wrappers ────────────────────────────────────────
static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gState.mouseDown = (action == GLFW_PRESS);
    }
}

static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
    if (gState.mouseDown && !gState.autoRotate) {
        double dx = xpos - gState.lastMouseX;
        double dy = ypos - gState.lastMouseY;
        gState.yaw += dx * 0.3f;
        gState.pitch -= dy * 0.3f;
        gState.pitch = fmaxf(-85.0f, fminf(85.0f, gState.pitch));
    }
    gState.lastMouseX = xpos;
    gState.lastMouseY = ypos;
}

static void scrollCallback(GLFWwindow* w, double xoff, double yoff) {
    gState.distance += yoff * 2.0f;
    gState.distance = fmaxf(5.0f, fminf(50.0f, gState.distance));
}

static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_R: gState.autoRotate = !gState.autoRotate; break;
            case GLFW_KEY_G: gState.showGrid = 1 - gState.showGrid; break;
            case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GL_TRUE); break;
            case GLFW_KEY_SPACE: gState.paused = !gState.paused; break;
            case GLFW_KEY_E: gState.exposure = fmaxf(0.1f, gState.exposure - 0.2f); break;
            case GLFW_KEY_D: gState.exposure = fminf(5.0f, gState.exposure + 0.2f); break;
            case GLFW_KEY_W: gState.gridBend = fminf(3.0f, gState.gridBend + 0.1f); break;
            case GLFW_KEY_S: gState.gridBend = fmaxf(0.1f, gState.gridBend - 0.1f); break;
            case GLFW_KEY_Q: gState.gridDensity = fmaxf(0.3f, gState.gridDensity - 0.1f); break;
            case GLFW_KEY_A: gState.gridDensity = fminf(3.0f, gState.gridDensity + 0.1f); break;
        }
    }
}

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
}

// ── Main ────────────────────────────────────────────────────────────
int main(void) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 800,
        "Black Hole Simulation - Schwarzschild Geodesic Ray Tracing",
        nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    gState.window = window;
    glfwMakeContextCurrent(window);

    // Set up static callbacks
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // Find shaders
    std::string baseDir;
    const char* possiblePaths[] = {"shaders/", "../shaders/", "../../shaders/", ""};
    for (auto p : possiblePaths) {
        std::string test = std::string(p) + "blackhole.frag";
        std::ifstream t(test);
        if (t.good()) {
            baseDir = p;
            break;
        }
    }

    std::string vertSrc = readShaderFile(baseDir + "blackhole.vert");
    std::string fragSrc = readShaderFile(baseDir + "blackhole.frag");

    if (vertSrc.empty() || fragSrc.empty()) {
        std::cerr << "Could not load shaders. Ensure shaders/ directory is accessible." << std::endl;
        glfwTerminate();
        return -1;
    }

    GLuint program = createProgram(vertSrc, fragSrc);

    // Full-screen quad
    static const float quadVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLint locCamPos = glGetUniformLocation(program, "camPos");
    GLint locCamTarget = glGetUniformLocation(program, "camTarget");
    GLint locAspectRatio = glGetUniformLocation(program, "aspectRatio");
    GLint locTime = glGetUniformLocation(program, "time");
    GLint locShowGrid = glGetUniformLocation(program, "showGrid");
    GLint locGridDensity = glGetUniformLocation(program, "gridDensity");
    GLint locGridBend = glGetUniformLocation(program, "gridBend");
    GLint locBgColor = glGetUniformLocation(program, "bgColor");
    GLint locExposure = glGetUniformLocation(program, "exposure");

    std::cout << "\n=== Black Hole Simulation Controls ===" << std::endl;
    std::cout << "  Mouse drag  - orbit camera" << std::endl;
    std::cout << "  Scroll      - zoom in/out" << std::endl;
    std::cout << "  R           - toggle auto-rotate" << std::endl;
    std::cout << "  G           - toggle spacetime grid" << std::endl;
    std::cout << "  Space       - pause/resume time" << std::endl;
    std::cout << "  E / D       - decrease/increase exposure" << std::endl;
    std::cout << "  W / S       - increase/decrease grid bend" << std::endl;
    std::cout << "  Q / A       - decrease/increase grid density" << std::endl;
    std::cout << "  ESC         - exit" << std::endl;
    std::cout << "======================================\n" << std::endl;

    // Main loop
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = currentTime - lastTime;
        lastTime = currentTime;

        if (!gState.paused) {
            gState.simTime += dt;
        }

        if (gState.autoRotate) {
            gState.yaw += gState.autoRotateSpeed * dt;
        }

        // Camera position
        float r = gState.distance;
        float yawRad = gState.yaw * float(M_PI) / 180.0f;
        float pitchRad = gState.pitch * float(M_PI) / 180.0f;
        float cx = r * cos(pitchRad) * sin(yawRad);
        float cy = r * sin(pitchRad);
        float cz = r * cos(pitchRad) * cos(yawRad);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = (float)width / (float)height;

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glUniform3f(locCamPos, cx, cy, cz);
        glUniform3f(locCamTarget, 0.0f, 0.0f, 0.0f);
        glUniform1f(locAspectRatio, aspect);
        glUniform1f(locTime, gState.simTime);
        glUniform1i(locShowGrid, gState.showGrid);
        glUniform1f(locGridDensity, gState.gridDensity);
        glUniform1f(locGridBend, gState.gridBend);
        glUniform3f(locBgColor, 0.01f, 0.01f, 0.02f);
        glUniform1f(locExposure, gState.exposure);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);

    glfwTerminate();
    return 0;
}
