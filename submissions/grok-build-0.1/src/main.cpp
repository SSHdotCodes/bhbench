#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>

#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

static const int WINDOW_WIDTH = 1280;
static const int WINDOW_HEIGHT = 720;

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 3.5f, 22.0f);
    float yaw = -1.57f;   // look toward -z initially? adjust
    float pitch = 0.15f;
    float distance = 22.0f; // orbit distance
    float azimuth = 0.8f;   // around z (phi)
    float polar = 1.35f;    // from +z

    glm::vec3 target = glm::vec3(0.0f);

    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;

    void updateBasis() {
        // Spherical orbit camera
        float x = distance * sin(polar) * cos(azimuth);
        float y = distance * sin(polar) * sin(azimuth);
        float z = distance * cos(polar);
        position = glm::vec3(x, y, z);

        forward = glm::normalize(target - position);
        right = glm::normalize(glm::cross(forward, glm::vec3(0,0,1)));
        if (glm::length(right) < 0.001f) right = glm::vec3(1,0,0);
        up = glm::normalize(glm::cross(right, forward));
    }

    void processMouseDrag(float dx, float dy, bool shift) {
        float sens = 0.0045f;
        if (shift) {
            // dolly or move target a bit
            distance += dy * 0.08f;
            distance = std::max(3.5f, std::min(distance, 120.0f));
        } else {
            azimuth += dx * sens * 1.6f;
            polar += dy * sens * 1.2f;
            polar = std::max(0.05f, std::min(polar, 3.13f));
        }
        updateBasis();
    }

    void processScroll(float yoff) {
        distance -= yoff * 1.1f;
        distance = std::max(3.0f, std::min(distance, 140.0f));
        updateBasis();
    }

    void processKey(int key, float dt) {
        float move = 6.0f * dt;
        if (key == GLFW_KEY_W) distance -= move;
        if (key == GLFW_KEY_S) distance += move;
        if (key == GLFW_KEY_A) azimuth -= move * 0.6f;
        if (key == GLFW_KEY_D) azimuth += move * 0.6f;
        if (key == GLFW_KEY_Q) polar -= move * 0.3f;
        if (key == GLFW_KEY_E) polar += move * 0.3f;
        distance = std::max(3.0f, std::min(distance, 140.0f));
        polar = std::max(0.05f, std::min(polar, 3.13f));
        updateBasis();
    }
};

struct Shader {
    GLuint program = 0;

    bool load(const std::string& vertPath, const std::string& fragPath) {
        std::string vertSrc = loadFile(vertPath);
        std::string fragSrc = loadFile(fragPath);
        if (vertSrc.empty() || fragSrc.empty()) {
            std::cerr << "Failed to load shader files: " << vertPath << " " << fragPath << std::endl;
            return false;
        }
        GLuint vs = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (!vs || !fs) return false;

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[1024];
            glGetProgramInfoLog(program, 1024, nullptr, log);
            std::cerr << "Shader link error:\n" << log << std::endl;
            return false;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        return true;
    }

    void use() const { glUseProgram(program); }

    void setFloat(const std::string& name, float v) const {
        glUniform1f(glGetUniformLocation(program, name.c_str()), v);
    }
    void setInt(const std::string& name, int v) const {
        glUniform1i(glGetUniformLocation(program, name.c_str()), v);
    }
    void setVec3(const std::string& name, const glm::vec3& v) const {
        glUniform3fv(glGetUniformLocation(program, name.c_str()), 1, glm::value_ptr(v));
    }
    void setMat4(const std::string& name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(program, name.c_str()), 1, GL_FALSE, glm::value_ptr(m));
    }

private:
    static std::string loadFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            // Try relative to executable or shaders subdir
            std::ifstream f2("shaders/" + path.substr(path.find_last_of("/\\") + 1));
            if (!f2.is_open()) return "";
            std::stringstream ss; ss << f2.rdbuf(); return ss.str();
        }
        std::stringstream ss; ss << file.rdbuf(); return ss.str();
    }

    static GLuint compile(GLenum type, const std::string& src) {
        GLuint shader = glCreateShader(type);
        const char* csrc = src.c_str();
        glShaderSource(shader, 1, &csrc, nullptr);
        glCompileShader(shader);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[2048];
            glGetShaderInfoLog(shader, 2048, nullptr, log);
            std::cerr << "Shader compile error:\n" << log << std::endl;
            return 0;
        }
        return shader;
    }
};

Camera camera;
bool mousePressed = false;
double lastMouseX = 0.0, lastMouseY = 0.0;
bool shiftHeld = false;

float rs = 2.0f;
float diskInner = 6.0f;
float diskOuter = 22.0f;
float exposure = 1.35f;
float diskBrightness = 1.8f;
int maxSteps = 92;
float stepSize = 0.085f;
float gridWarp = 1.0f;
float gridAlpha = 0.85f;

bool paused = false;
float simTime = 0.0f;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, true);
        if (key == GLFW_KEY_R) {
            camera.distance = 22.0f;
            camera.azimuth = 0.8f;
            camera.polar = 1.35f;
            camera.updateBasis();
            diskBrightness = 1.8f;
            exposure = 1.35f;
            maxSteps = 92;
            stepSize = 0.085f;
            gridWarp = 1.0f;
        }
        if (key == GLFW_KEY_P) paused = !paused;
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            diskBrightness = std::max(0.2f, diskBrightness - 0.15f);
        }
        if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
            diskBrightness = std::min(5.0f, diskBrightness + 0.15f);
        }
        if (key == GLFW_KEY_LEFT_BRACKET) stepSize = std::max(0.01f, stepSize - 0.005f);
        if (key == GLFW_KEY_RIGHT_BRACKET) stepSize = std::min(0.4f, stepSize + 0.005f);
        if (key == GLFW_KEY_1) maxSteps = std::max(20, maxSteps - 8);
        if (key == GLFW_KEY_2) maxSteps = std::min(240, maxSteps + 8);
        if (key == GLFW_KEY_3) exposure = std::max(0.4f, exposure - 0.1f);
        if (key == GLFW_KEY_4) exposure = std::min(4.0f, exposure + 0.1f);
        if (key == GLFW_KEY_5) gridWarp = std::max(0.0f, gridWarp - 0.1f);
        if (key == GLFW_KEY_6) gridWarp = std::min(2.5f, gridWarp + 0.1f);
        if (key == GLFW_KEY_7) gridAlpha = std::max(0.1f, gridAlpha - 0.08f);
        if (key == GLFW_KEY_8) gridAlpha = std::min(1.0f, gridAlpha + 0.08f);
        if (key == GLFW_KEY_LEFT)  diskInner = std::max(3.5f, diskInner - 0.3f);
        if (key == GLFW_KEY_RIGHT) diskInner = std::min(12.0f, diskInner + 0.3f);
        if (key == GLFW_KEY_UP)   diskOuter = std::min(60.0f, diskOuter + 1.0f);
        if (key == GLFW_KEY_DOWN) diskOuter = std::max(diskInner + 2.0f, diskOuter - 1.0f);
    }
    // Continuous movement handled in main loop with held keys
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        mousePressed = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    }
    shiftHeld = (mods & GLFW_MOD_SHIFT);
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    if (mousePressed) {
        float dx = float(x - lastMouseX);
        float dy = float(y - lastMouseY);
        camera.processMouseDrag(dx, dy, shiftHeld);
        lastMouseX = x;
        lastMouseY = y;
    }
}

void scrollCallback(GLFWwindow* window, double xoff, double yoff) {
    camera.processScroll(float(yoff));
}

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    glViewport(0, 0, w, h);
}

std::string getShaderPath(const std::string& name) {
    // Try several common locations
    std::vector<std::string> candidates = {
        "shaders/" + name,
        "../shaders/" + name,
        name
    };
    for (auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) return c;
    }
    return "shaders/" + name;
}

GLuint createFullscreenQuad() {
    float quad[] = {
        // pos      tex
        -1, -1,   0, 0,
         1, -1,   1, 0,
         1,  1,   1, 1,
        -1,  1,   0, 1
    };
    unsigned int indices[] = {0,1,2, 0,2,3};

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return vao;
}

// Generate a spacetime grid: radial + angular lines on the equatorial plane + some extra
// We generate line vertices (pairs)
std::vector<glm::vec3> generateGridLines(int rings = 22, int spokes = 36, float maxR = 38.0f) {
    std::vector<glm::vec3> verts;
    // Concentric rings
    for (int i = 1; i <= rings; ++i) {
        float r = (float(i) / rings) * maxR;
        int segs = std::max(24, int(spokes * (0.6f + 0.6f * r / maxR)));
        for (int j = 0; j < segs; ++j) {
            float a0 = (float(j) / segs) * 2.0f * 3.14159265f;
            float a1 = (float(j+1) / segs) * 2.0f * 3.14159265f;
            verts.emplace_back(r * cos(a0), r * sin(a0), 0.0f);
            verts.emplace_back(r * cos(a1), r * sin(a1), 0.0f);
        }
    }
    // Spokes
    for (int s = 0; s < spokes; ++s) {
        float ang = (float(s) / spokes) * 2.0f * 3.14159265f;
        float cosA = cos(ang), sinA = sin(ang);
        // from near horizon out
        int nseg = 18;
        for (int k = 0; k < nseg; ++k) {
            float r0 = 2.3f + (maxR - 2.3f) * (float(k) / nseg);
            float r1 = 2.3f + (maxR - 2.3f) * (float(k+1) / nseg);
            verts.emplace_back(r0 * cosA, r0 * sinA, 0.0f);
            verts.emplace_back(r1 * cosA, r1 * sinA, 0.0f);
        }
    }
    // Add a few "vertical" grid lines at different azimuths to show 3D curvature
    for (int s = 0; s < 12; ++s) {
        float ang = (float(s) / 12) * 2.0f * 3.14159265f;
        float c = cos(ang), sA = sin(ang);
        for (int k = 0; k < 7; ++k) {
            float r = 4.0f + k * 4.5f;
            // vertical-ish line segments (in z a little before warp)
            verts.emplace_back(r * c, r * sA, -1.5f);
            verts.emplace_back(r * c, r * sA,  1.8f);
        }
    }
    return verts;
}

GLuint createGridVAO(const std::vector<glm::vec3>& verts, GLuint& vboOut) {
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    vboOut = vbo;
    return vao;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Black Hole - Scientific GR Raytracer (OpenGL)", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync for smooth realtime

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // Init GL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.008f, 0.006f, 0.012f, 1.0f);

    // Shaders
    Shader bhShader;
    std::string bhVert = getShaderPath("blackhole.vert");
    std::string bhFrag = getShaderPath("blackhole.frag");
    if (!bhShader.load(bhVert, bhFrag)) {
        std::cerr << "Failed to load blackhole shaders\n";
        return -1;
    }

    Shader gridShader;
    std::string gVert = getShaderPath("grid.vert");
    std::string gFrag = getShaderPath("grid.frag");
    if (!gridShader.load(gVert, gFrag)) {
        std::cerr << "Failed to load grid shaders\n";
        return -1;
    }

    // Geometry
    GLuint quadVAO = createFullscreenQuad();

    auto gridVerts = generateGridLines(26, 42, 41.0f);
    GLuint gridVBO;
    GLuint gridVAO = createGridVAO(gridVerts, gridVBO);

    // Camera init
    camera.updateBasis();

    // Projection
    float aspect = float(WINDOW_WIDTH) / float(WINDOW_HEIGHT);

    // Main loop
    double lastTime = glfwGetTime();
    int frameCount = 0;
    double fpsTime = lastTime;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;

        // FPS counter in title occasionally
        frameCount++;
        if (now - fpsTime > 1.0) {
            std::string title = "Black Hole GR Sim | FPS: " + std::to_string(frameCount) +
                " | Steps:" + std::to_string(maxSteps) + " Step:" + std::to_string(stepSize).substr(0,4);
            glfwSetWindowTitle(window, title.c_str());
            frameCount = 0;
            fpsTime = now;
        }

        // Input continuous
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.processKey(GLFW_KEY_W, dt);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.processKey(GLFW_KEY_S, dt);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.processKey(GLFW_KEY_A, dt);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.processKey(GLFW_KEY_D, dt);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.processKey(GLFW_KEY_Q, dt);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.processKey(GLFW_KEY_E, dt);

        if (!paused) {
            simTime += dt * 0.9f;
        }

        camera.updateBasis();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        aspect = (h > 0) ? float(w) / float(h) : 1.0f;
        glViewport(0, 0, w, h);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ----- Render spacetime grid (warped) first -----
        glm::mat4 proj = glm::perspective(glm::radians(55.0f), aspect, 0.5f, 400.0f);
        glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
        glm::mat4 vp = proj * view;

        gridShader.use();
        gridShader.setMat4("uViewProj", vp);
        gridShader.setFloat("uTime", simTime);
        gridShader.setFloat("uWarp", gridWarp);
        gridShader.setFloat("uRs", rs);
        gridShader.setVec3("uCamPos", camera.position);
        gridShader.setFloat("uAlpha", gridAlpha);

        glBindVertexArray(gridVAO);
        glDrawArrays(GL_LINES, 0, (GLsizei)gridVerts.size());
        glBindVertexArray(0);

        // ----- Render black hole ray traced quad -----
        bhShader.use();

        // Camera uniforms
        bhShader.setVec3("uCamPos", camera.position);
        bhShader.setVec3("uCamRight", camera.right);
        bhShader.setVec3("uCamUp", camera.up);
        bhShader.setVec3("uCamForward", camera.forward);
        float fov = glm::radians(52.0f);
        bhShader.setFloat("uFov", fov);
        bhShader.setFloat("uAspect", aspect);

        // Physics
        bhShader.setFloat("uRs", rs);
        bhShader.setFloat("uDiskInner", diskInner);
        bhShader.setFloat("uDiskOuter", diskOuter);
        bhShader.setFloat("uTime", simTime);
        bhShader.setInt("uMaxSteps", maxSteps);
        bhShader.setFloat("uStepSize", stepSize);
        bhShader.setFloat("uExposure", exposure);
        bhShader.setFloat("uDiskBrightness", diskBrightness);
        bhShader.setFloat("uEpsilon", 0.0008f);

        glBindVertexArray(quadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        // HUD info would be nice but omitted (keep pure realtime window)

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
