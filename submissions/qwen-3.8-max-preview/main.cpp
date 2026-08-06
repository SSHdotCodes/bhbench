#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const int WIDTH = 1280;
static const int HEIGHT = 800;

static float camTheta = 0.4f;
static float camPhi = 1.2f;
static float camDist = 18.0f;
static bool dragging = false;
static double lastX = 0, lastY = 0;
static bool showGrid = true;
static float diskInner = 3.0f;
static float diskOuter = 12.0f;

static std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return s;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, 2048, nullptr, log);
        std::cerr << "Link error:\n" << log << "\n";
    }
    return p;
}

static GLuint loadProgram(const char* vertPath, const char* fragPath) {
    std::string vsSrc = readFile(vertPath);
    std::string fsSrc = readFile(fragPath);
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc.c_str());
    return linkProgram(vs, fs);
}

struct GridMesh {
    GLuint vao, vbo;
    int vertexCount;
};

static GridMesh createFlammGrid(float rs, float rMax, int radialLines, int ringCount) {
    std::vector<float> verts;
    float rMin = rs + 0.01f;

    for (int i = 0; i < radialLines; i++) {
        float angle = 2.0f * M_PI * i / radialLines;
        float ca = cosf(angle), sa = sinf(angle);
        for (int j = 0; j < ringCount; j++) {
            float t0 = (float)j / ringCount;
            float t1 = (float)(j + 1) / ringCount;
            float r0 = rMin + t0 * (rMax - rMin);
            float r1 = rMin + t1 * (rMax - rMin);
            float z0 = 2.0f * sqrtf(rs * (r0 - rs));
            float z1 = 2.0f * sqrtf(rs * (r1 - rs));
            verts.insert(verts.end(), {r0 * ca, -z0, r0 * sa});
            verts.insert(verts.end(), {r1 * ca, -z1, r1 * sa});
        }
    }

    for (int j = 0; j <= ringCount; j++) {
        float t = (float)j / ringCount;
        float r = rMin + t * (rMax - rMin);
        float z = 2.0f * sqrtf(rs * (r - rs));
        int segments = 128;
        for (int i = 0; i < segments; i++) {
            float a0 = 2.0f * M_PI * i / segments;
            float a1 = 2.0f * M_PI * (i + 1) / segments;
            verts.insert(verts.end(), {r * cosf(a0), -z, r * sinf(a0)});
            verts.insert(verts.end(), {r * cosf(a1), -z, r * sinf(a1)});
        }
    }

    GridMesh mesh;
    mesh.vertexCount = (int)(verts.size() / 3);
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    return mesh;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole Simulation - Schwarzschild Geodesic Ray Tracer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window creation failed\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init failed\n";
        return 1;
    }

    GLuint rayProg = loadProgram("shaders/raytrace.vert", "shaders/raytrace.frag");
    GLuint gridProg = loadProgram("shaders/grid.vert", "shaders/grid.frag");

    float quadVerts[] = {-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0};
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    float rs = 2.0f;
    GridMesh grid = createFlammGrid(rs, 20.0f, 36, 60);

    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int action, int) {
        if (btn == GLFW_MOUSE_BUTTON_LEFT) {
            dragging = (action == GLFW_PRESS);
            glfwGetCursorPos(w, &lastX, &lastY);
        }
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        if (!dragging) return;
        float dx = (float)(x - lastX), dy = (float)(y - lastY);
        camTheta -= dx * 0.005f;
        camPhi -= dy * 0.005f;
        if (camPhi < 0.1f) camPhi = 0.1f;
        if (camPhi > M_PI - 0.1f) camPhi = (float)M_PI - 0.1f;
        lastX = x; lastY = y;
    });

    glfwSetScrollCallback(window, [](GLFWwindow*, double, double yoff) {
        camDist -= (float)yoff * 1.5f;
        if (camDist < 4.0f) camDist = 4.0f;
        if (camDist > 60.0f) camDist = 60.0f;
    });

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
            showGrid = !showGrid;
            while (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) glfwPollEvents();
        }

        float time = (float)glfwGetTime();

        glm::vec3 camPos(
            camDist * sinf(camPhi) * cosf(camTheta),
            camDist * cosf(camPhi),
            camDist * sinf(camPhi) * sinf(camTheta)
        );
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)WIDTH / HEIGHT, 0.1f, 200.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        glUseProgram(rayProg);
        glUniform3fv(glGetUniformLocation(rayProg, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniformMatrix4fv(glGetUniformLocation(rayProg, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(rayProg, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform2f(glGetUniformLocation(rayProg, "uResolution"), WIDTH, HEIGHT);
        glUniform1f(glGetUniformLocation(rayProg, "uTime"), time);
        glUniform1f(glGetUniformLocation(rayProg, "uRs"), rs);
        glUniform1f(glGetUniformLocation(rayProg, "uDiskInner"), diskInner);
        glUniform1f(glGetUniformLocation(rayProg, "uDiskOuter"), diskOuter);
        glUniform1i(glGetUniformLocation(rayProg, "uShowGrid"), showGrid ? 1 : 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (showGrid) {
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(gridProg);
            glUniformMatrix4fv(glGetUniformLocation(gridProg, "uView"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(gridProg, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
            glUniform1f(glGetUniformLocation(gridProg, "uRs"), rs);
            glUniform1f(glGetUniformLocation(gridProg, "uTime"), time);
            glBindVertexArray(grid.vao);
            glDrawArrays(GL_LINES, 0, grid.vertexCount);
            glDisable(GL_BLEND);
        }

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
