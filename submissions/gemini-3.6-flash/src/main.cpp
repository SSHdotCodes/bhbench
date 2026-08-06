#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <vector>

// Window dimensions
int g_width = 1280;
int g_height = 720;

// Camera state
float g_camRadius = 18.0f;
float g_camYaw = 0.2f;      // radians
float g_camPitch = 0.15f;    // radians
glm::vec3 g_camTarget(0.0f, 0.0f, 0.0f);

bool g_isMouseDown = false;
double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;

// Simulation parameters
float g_rs = 1.0f;               // Schwarzschild radius
bool g_showGrid = true;          // Spacetime curvature grid
bool g_showDisk = true;          // Accretion disk
bool g_dopplerEffect = true;     // Relativistic Doppler & redshift
bool g_showStars = true;         // Background stars
bool g_trapdoorMode = false;     // Flamm paraboloid trapdoor grid view
bool g_paused = false;
float g_diskBrightness = 1.0f;
float g_simTime = 0.0f;
int g_maxSteps = 250;            // Geodesic integration steps

// Shader compilation helper
GLuint compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        std::cerr << "Shader Compilation Error:\n" << infoLog << std::endl;
    }
    return shader;
}

GLuint createProgram(const std::string& vertSrc, const std::string& fragSrc) {
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, 1024, NULL, infoLog);
        std::cerr << "Program Link Error:\n" << infoLog << std::endl;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

std::string readFile(const std::string& filePath) {
    std::vector<std::string> candidates = {
        filePath,
        "../" + filePath,
        "/Users/sshpro/black-hole-cpp-gemini36flash/" + filePath
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    std::cerr << "Failed to open file in candidate paths: " << filePath << std::endl;
    return "";
}

// Callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    g_width = width;
    g_height = height;
    glViewport(0, 0, width, height);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_isMouseDown = true;
            glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
        } else if (action == GLFW_RELEASE) {
            g_isMouseDown = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_isMouseDown) {
        float dx = static_cast<float>(xpos - g_lastMouseX);
        float dy = static_cast<float>(ypos - g_lastMouseY);

        g_camYaw += dx * 0.005f;
        g_camPitch += dy * 0.005f;

        // Clamp pitch to avoid gimbal lock flip
        const float pitchLimit = 1.55f;
        if (g_camPitch > pitchLimit) g_camPitch = pitchLimit;
        if (g_camPitch < -pitchLimit) g_camPitch = -pitchLimit;

        g_lastMouseX = xpos;
        g_lastMouseY = ypos;
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    g_camRadius -= static_cast<float>(yoffset) * 1.0f;
    if (g_camRadius < 3.0f) g_camRadius = 3.0f;
    if (g_camRadius > 80.0f) g_camRadius = 80.0f;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
        if (key == GLFW_KEY_G) g_showGrid = !g_showGrid;
        if (key == GLFW_KEY_D) g_showDisk = !g_showDisk;
        if (key == GLFW_KEY_R) g_dopplerEffect = !g_dopplerEffect;
        if (key == GLFW_KEY_B) g_showStars = !g_showStars;
        if (key == GLFW_KEY_C) g_trapdoorMode = !g_trapdoorMode;
        if (key == GLFW_KEY_SPACE) g_paused = !g_paused;

        if (key == GLFW_KEY_UP) g_diskBrightness *= 1.25f;
        if (key == GLFW_KEY_DOWN) g_diskBrightness *= 0.80f;

        if (key == GLFW_KEY_RIGHT) g_rs += 0.2f;
        if (key == GLFW_KEY_LEFT) {
            g_rs -= 0.2f;
            if (g_rs < 0.2f) g_rs = 0.2f;
        }
        if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) g_maxSteps += 50;
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            g_maxSteps -= 50;
            if (g_maxSteps < 50) g_maxSteps = 50;
        }
    }
}

void printHelp() {
    std::cout << "=========================================================\n";
    std::cout << "     SCIENTIFIC BLACK HOLE REALTIME 3D SIMULATOR\n";
    std::cout << "=========================================================\n";
    std::cout << "Controls:\n";
    std::cout << "  Mouse Drag    : Orbit camera around black hole\n";
    std::cout << "  Mouse Scroll  : Zoom camera in / out\n";
    std::cout << "  G             : Toggle Spacetime Curvature Grid\n";
    std::cout << "  D             : Toggle Relativistic Accretion Disk\n";
    std::cout << "  R             : Toggle Relativistic Doppler & Redshift\n";
    std::cout << "  B             : Toggle Background Starfield Lensing\n";
    std::cout << "  C             : Toggle Flamm's Paraboloid Trapdoor Visualization\n";
    std::cout << "  SPACE         : Pause / Resume Disk Simulation\n";
    std::cout << "  UP / DOWN     : Increase / Decrease Accretion Disk Brightness\n";
    std::cout << "  RIGHT / LEFT  : Increase / Decrease Black Hole Mass (r_s)\n";
    std::cout << "  + / -         : Increase / Decrease Ray Tracing Step Count\n";
    std::cout << "  ESC           : Exit\n";
    std::cout << "=========================================================\n\n";
}

int main() {
    printHelp();

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(g_width, g_height, "Realtime Scientifically Accurate Black Hole Simulator", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW Window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    // Load shaders
    std::string vertCode = readFile("shaders/blackhole.vert");
    std::string fragCode = readFile("shaders/blackhole.frag");

    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "Error loading shader files!" << std::endl;
        return -1;
    }

    GLuint shaderProgram = createProgram(vertCode, fragCode);

    // Fullscreen Quad VAO
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Uniform locations
    GLint locResolution = glGetUniformLocation(shaderProgram, "u_resolution");
    GLint locCamPos = glGetUniformLocation(shaderProgram, "u_camPos");
    GLint locCamForward = glGetUniformLocation(shaderProgram, "u_camForward");
    GLint locCamRight = glGetUniformLocation(shaderProgram, "u_camRight");
    GLint locCamUp = glGetUniformLocation(shaderProgram, "u_camUp");
    GLint locRs = glGetUniformLocation(shaderProgram, "u_rs");
    GLint locTime = glGetUniformLocation(shaderProgram, "u_time");
    GLint locShowGrid = glGetUniformLocation(shaderProgram, "u_showGrid");
    GLint locShowDisk = glGetUniformLocation(shaderProgram, "u_showDisk");
    GLint locDopplerEffect = glGetUniformLocation(shaderProgram, "u_dopplerEffect");
    GLint locShowStars = glGetUniformLocation(shaderProgram, "u_showStars");
    GLint locTrapdoorMode = glGetUniformLocation(shaderProgram, "u_trapdoorMode");
    GLint locDiskBrightness = glGetUniformLocation(shaderProgram, "u_diskBrightness");
    GLint locMaxSteps = glGetUniformLocation(shaderProgram, "u_maxSteps");

    double lastTime = glfwGetTime();
    int frameCount = 0;

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        if (!g_paused) {
            g_simTime += deltaTime;
        }

        frameCount++;
        static float fpsTimer = 0.0f;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            char title[256];
            snprintf(title, sizeof(title),
                "Black Hole Simulator [FPS: %d | Mass r_s: %.2f | Grid: %s | Disk: %s | Doppler: %s | Trapdoor: %s | Steps: %d]",
                frameCount, g_rs, g_showGrid ? "ON" : "OFF", g_showDisk ? "ON" : "OFF",
                g_dopplerEffect ? "ON" : "OFF", g_trapdoorMode ? "ON" : "OFF", g_maxSteps);
            glfwSetWindowTitle(window, title);
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // Calculate camera position and frame basis
        float cx = g_camRadius * cos(g_camPitch) * sin(g_camYaw);
        float cy = g_camRadius * sin(g_camPitch);
        float cz = g_camRadius * cos(g_camPitch) * cos(g_camYaw);
        glm::vec3 camPos(cx, cy, cz);

        glm::vec3 camForward = glm::normalize(g_camTarget - camPos);
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        if (fabs(glm::dot(camForward, worldUp)) > 0.99f) {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        glm::vec3 camRight = glm::normalize(glm::cross(camForward, worldUp));
        glm::vec3 camUp = glm::normalize(glm::cross(camRight, camForward));

        // Render pass
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glUniform2f(locResolution, static_cast<float>(g_width), static_cast<float>(g_height));
        glUniform3f(locCamPos, camPos.x, camPos.y, camPos.z);
        glUniform3f(locCamForward, camForward.x, camForward.y, camForward.z);
        glUniform3f(locCamRight, camRight.x, camRight.y, camRight.z);
        glUniform3f(locCamUp, camUp.x, camUp.y, camUp.z);
        glUniform1f(locRs, g_rs);
        glUniform1f(locTime, g_simTime);
        glUniform1i(locShowGrid, g_showGrid ? 1 : 0);
        glUniform1i(locShowDisk, g_showDisk ? 1 : 0);
        glUniform1i(locDopplerEffect, g_dopplerEffect ? 1 : 0);
        glUniform1i(locShowStars, g_showStars ? 1 : 0);
        glUniform1i(locTrapdoorMode, g_trapdoorMode ? 1 : 0);
        glUniform1f(locDiskBrightness, g_diskBrightness);
        glUniform1i(locMaxSteps, g_maxSteps);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
