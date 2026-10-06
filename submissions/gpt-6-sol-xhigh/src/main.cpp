#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

namespace {
constexpr float pi = 3.14159265358979323846f;

struct Vertex {
    float x, y, z;
    float r, g, b;
};

struct UiVertex {
    float x, y;
    float r, g, b;
};

struct App {
    GLFWwindow* window = nullptr;
    GLuint rayProgram = 0;
    GLuint gridProgram = 0;
    GLuint uiProgram = 0;
    GLuint fullscreenVao = 0;
    GLuint gridVao = 0;
    GLuint gridVbo = 0;
    GLuint uiVao = 0;
    GLuint uiVbo = 0;
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLsizei gridVertexCount = 0;
    int framebufferWidth = 1500;
    int framebufferHeight = 850;
    int renderWidth = 0;
    int renderHeight = 0;
    int quality = 3;
    float azimuth = 0.0f;
    float elevation = 0.34f;
    float cameraRadius = 39.0f;
    float simulationTime = 0.0f;
    bool dragging = false;
    bool paused = false;
    bool showGrid = true;
    bool showDisk = true;
    bool showHalo = true;
    bool screenshotRequested = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    std::string capturePath;
    double benchmarkSeconds = 0.0;
};

std::string readFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("cannot open shader: " + path);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

GLuint compileShader(GLenum type, const std::string& source, const std::string& label) {
    const char* text = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(label + ": " + log);
    }
    return shader;
}

GLuint makeProgram(const std::string& vertexFile, const std::string& fragmentFile) {
    const std::string root = BH_SHADER_DIR;
    GLuint vertex = compileShader(GL_VERTEX_SHADER, readFile(root + "/" + vertexFile), vertexFile);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, readFile(root + "/" + fragmentFile), fragmentFile);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("shader link: " + log);
    }
    return program;
}

void uniform1i(GLuint program, const char* name, int value) {
    glUniform1i(glGetUniformLocation(program, name), value);
}

void uniform1f(GLuint program, const char* name, float value) {
    glUniform1f(glGetUniformLocation(program, name), value);
}

float embeddingHeight(float radius) {
    // z(r) = 2 sqrt(2M(r - 2M)); a vertical translation puts the outer rim
    // at zero so the horizon appears at the bottom of the funnel.
    return 2.0f * std::sqrt(2.0f * (radius - 2.0f)) - 2.0f * std::sqrt(32.0f);
}

void appendLine(std::vector<Vertex>& vertices,
                float r1, float a1, float r2, float a2,
                float red, float green, float blue) {
    vertices.push_back({r1 * std::cos(a1), embeddingHeight(r1), r1 * std::sin(a1),
                        red, green, blue});
    vertices.push_back({r2 * std::cos(a2), embeddingHeight(r2), r2 * std::sin(a2),
                        red, green, blue});
}

void createGrid(App& app) {
    std::vector<Vertex> lines;
    const float rings[] = {2.025f, 2.3f, 2.7f, 3.0f, 3.5f, 4.2f,
                           5.0f, 6.0f, 7.5f, 9.0f, 11.0f, 13.0f, 15.5f, 18.0f};
    for (float r : rings) {
        const bool horizon = r < 2.1f;
        const bool photon = std::abs(r - 3.0f) < 0.01f;
        const bool isco = std::abs(r - 6.0f) < 0.01f;
        const float red = horizon ? 1.0f : (isco ? 1.0f : (photon ? 0.40f : 0.13f));
        const float green = horizon ? 0.40f : (isco ? 0.72f : (photon ? 0.61f : 0.35f));
        const float blue = horizon ? 0.17f : (isco ? 0.36f : (photon ? 1.0f : 0.51f));
        for (int j = 0; j < 128; ++j) {
            const float a = 2.0f * pi * j / 128.0f;
            const float b = 2.0f * pi * (j + 1) / 128.0f;
            appendLine(lines, r, a, r, b, red, green, blue);
        }
    }
    for (int j = 0; j < 32; ++j) {
        const float a = 2.0f * pi * j / 32.0f;
        for (int k = 0; k < 80; ++k) {
            const float r1 = 2.025f + (18.0f - 2.025f) * k / 80.0f;
            const float r2 = 2.025f + (18.0f - 2.025f) * (k + 1) / 80.0f;
            appendLine(lines, r1, a, r2, a, 0.12f, 0.34f, 0.49f);
        }
    }
    app.gridVertexCount = static_cast<GLsizei>(lines.size());
    glGenVertexArrays(1, &app.gridVao);
    glGenBuffers(1, &app.gridVbo);
    glBindVertexArray(app.gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.gridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(Vertex)),
                 lines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
}

using Glyph = std::array<unsigned char, 7>;

const Glyph& glyph(char c) {
    static const std::unordered_map<char, Glyph> font = {
        {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
        {'C', {15,16,16,16,16,16,15}}, {'D', {30,17,17,17,17,17,30}},
        {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
        {'G', {15,16,16,23,17,17,15}}, {'H', {17,17,17,31,17,17,17}},
        {'I', {31,4,4,4,4,4,31}}, {'J', {7,2,2,2,18,18,12}},
        {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
        {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
        {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
        {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
        {'S', {15,16,16,14,1,1,30}}, {'T', {31,4,4,4,4,4,4}},
        {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
        {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
        {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
        {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
        {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
        {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,16,30,1,1,30}},
        {'6', {14,16,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
        {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,1,14}},
        {'=', {0,31,0,31,0,0,0}}, {':', {0,4,4,0,4,4,0}},
        {'.', {0,0,0,0,0,12,12}}, {'/', {1,1,2,4,8,16,16}},
        {'|', {4,4,4,4,4,4,4}}, {'-', {0,0,0,31,0,0,0}},
        {'[', {14,8,8,8,8,8,14}}, {']', {14,2,2,2,2,2,14}},
        {' ', {0,0,0,0,0,0,0}}
    };
    static const Glyph blank{0,0,0,0,0,0,0};
    const auto it = font.find(c);
    return it == font.end() ? blank : it->second;
}

void addPixel(std::vector<UiVertex>& vertices, float x, float y, float size,
              float r, float g, float b) {
    const UiVertex a{x,y,r,g,b};
    const UiVertex bb{x+size,y,r,g,b};
    const UiVertex c{x+size,y+size,r,g,b};
    const UiVertex d{x,y+size,r,g,b};
    vertices.insert(vertices.end(), {a,bb,c,a,c,d});
}

void addText(std::vector<UiVertex>& vertices, float x, float y, float size,
             const std::string& text, float r, float g, float b) {
    for (char ch : text) {
        const auto& bitmap = glyph(ch);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if ((bitmap[row] >> (4 - col)) & 1)
                    addPixel(vertices, x + col * size, y + row * size,
                             size * 0.83f, r, g, b);
        x += size * 6.0f;
    }
}

void drawUi(App& app, int imageWidth) {
    std::vector<UiVertex> pixels;
    const float scale = 2.0f * app.framebufferHeight / 850.0f;
    const float left = 26.0f * scale;
    const float panel = imageWidth + 22.0f * scale;
    const float height = static_cast<float>(app.framebufferHeight);
    addText(pixels, left, 25.0f * scale, scale,
            "SCHWARZSCHILD  NULL GEODESICS", 0.73f, 0.86f, 1.0f);
    addText(pixels, left, height - 37.0f * scale, scale,
            "G=C=M=1   ZERO SPIN", 0.53f, 0.64f, 0.75f);
    addText(pixels, panel, 25.0f * scale, scale,
            "SPATIAL SLICE", 0.82f, 0.90f, 1.0f);
    addText(pixels, panel, 47.0f * scale, scale * 0.88f,
            "FLAMM EMBEDDING", 0.48f, 0.65f, 0.77f);
    addText(pixels, panel, height * 0.755f, scale * 0.88f,
            "R=2M  EVENT HORIZON", 1.0f, 0.44f, 0.22f);
    addText(pixels, panel, height * 0.795f, scale * 0.88f,
            "R=3M  PHOTON SPHERE", 0.49f, 0.68f, 1.0f);
    addText(pixels, panel, height * 0.835f, scale * 0.88f,
            "R=6M  DISK ISCO", 1.0f, 0.75f, 0.40f);
    addText(pixels, panel, height * 0.900f, scale * 0.82f,
            "2D SPACE AT FIXED TIME", 0.44f, 0.58f, 0.68f);
    addText(pixels, panel, height * 0.935f, scale * 0.82f,
            "DRAG ORBIT  SCROLL ZOOM", 0.44f, 0.58f, 0.68f);

    glViewport(0, 0, app.framebufferWidth, app.framebufferHeight);
    glUseProgram(app.uiProgram);
    glUniform2f(glGetUniformLocation(app.uiProgram, "uWindowSize"),
                static_cast<float>(app.framebufferWidth),
                static_cast<float>(app.framebufferHeight));
    glBindVertexArray(app.uiVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.uiVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(pixels.size() * sizeof(UiVertex)),
                 pixels.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(pixels.size()));
}

void createRenderTarget(App& app, int width, int height) {
    if (app.framebuffer) glDeleteFramebuffers(1, &app.framebuffer);
    if (app.colorTexture) glDeleteTextures(1, &app.colorTexture);
    app.renderWidth = width;
    app.renderHeight = height;
    glGenTextures(1, &app.colorTexture);
    glBindTexture(GL_TEXTURE_2D, app.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &app.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, app.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           app.colorTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("offscreen framebuffer is incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void savePPM(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot save screenshot: " + path);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row) {
        out.write(reinterpret_cast<const char*>(pixels.data() +
                  static_cast<size_t>(row) * width * 3), static_cast<std::streamsize>(width * 3));
    }
    std::cout << "saved " << path << '\n';
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto& app = *static_cast<App*>(glfwGetWindowUserPointer(window));
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        case GLFW_KEY_SPACE: app.paused = !app.paused; break;
        case GLFW_KEY_G: app.showGrid = !app.showGrid; break;
        case GLFW_KEY_D: app.showDisk = !app.showDisk; break;
        case GLFW_KEY_H: app.showHalo = !app.showHalo; break;
        case GLFW_KEY_LEFT_BRACKET: app.quality = std::max(0, app.quality - 1); break;
        case GLFW_KEY_RIGHT_BRACKET: app.quality = std::min(3, app.quality + 1); break;
        case GLFW_KEY_R:
            app.azimuth = 0.0f;
            app.elevation = 0.34f;
            app.cameraRadius = 39.0f;
            break;
        case GLFW_KEY_S: app.screenshotRequested = true; break;
        default: break;
    }
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& app = *static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app.dragging) {
        app.azimuth += static_cast<float>((x - app.lastMouseX) * 0.005);
        app.elevation = std::clamp(app.elevation +
                        static_cast<float>((y - app.lastMouseY) * 0.005),
                        -1.33f, 1.33f);
    }
    app.lastMouseX = x;
    app.lastMouseY = y;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto& app = *static_cast<App*>(glfwGetWindowUserPointer(window));
    app.dragging = action == GLFW_PRESS;
    glfwGetCursorPos(window, &app.lastMouseX, &app.lastMouseY);
}

void scrollCallback(GLFWwindow* window, double, double y) {
    auto& app = *static_cast<App*>(glfwGetWindowUserPointer(window));
    app.cameraRadius = std::clamp(app.cameraRadius * std::exp(-0.11f * static_cast<float>(y)),
                                  19.0f, 90.0f);
}

void draw(App& app) {
    glfwGetFramebufferSize(app.window, &app.framebufferWidth, &app.framebufferHeight);
    if (app.framebufferWidth <= 0 || app.framebufferHeight <= 0) return;
    const int imageWidth = static_cast<int>(app.framebufferWidth * 0.71f);
    const float scales[] = {0.40f, 0.55f, 0.70f, 1.0f};
    const int targetWidth = std::max(1, static_cast<int>(imageWidth * scales[app.quality]));
    const int targetHeight = std::max(1, static_cast<int>(app.framebufferHeight * scales[app.quality]));
    if (targetWidth != app.renderWidth || targetHeight != app.renderHeight)
        createRenderTarget(app, targetWidth, targetHeight);

    glBindFramebuffer(GL_FRAMEBUFFER, app.framebuffer);
    glViewport(0, 0, targetWidth, targetHeight);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(app.rayProgram);
    glUniform2f(glGetUniformLocation(app.rayProgram, "uResolution"),
                static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    uniform1f(app.rayProgram, "uTime", app.simulationTime);
    uniform1f(app.rayProgram, "uCameraRadius", app.cameraRadius);
    uniform1f(app.rayProgram, "uAzimuth", app.azimuth);
    uniform1f(app.rayProgram, "uElevation", app.elevation);
    uniform1i(app.rayProgram, "uShowDisk", app.showDisk ? 1 : 0);
    uniform1i(app.rayProgram, "uShowHalo", app.showHalo ? 1 : 0);
    glBindVertexArray(app.fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, app.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, targetWidth, targetHeight,
                      0, 0, imageWidth, app.framebufferHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    const int panelWidth = app.framebufferWidth - imageWidth;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(imageWidth, 0, panelWidth, app.framebufferHeight);
    glClearColor(0.006f, 0.014f, 0.026f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (app.showGrid) {
        glViewport(imageWidth, 0, panelWidth, app.framebufferHeight);
        glUseProgram(app.gridProgram);
        uniform1f(app.gridProgram, "uTurn", 0.55f + 0.005f * app.simulationTime);
        uniform1f(app.gridProgram, "uAspect",
                  static_cast<float>(panelWidth) / app.framebufferHeight);
        glBindVertexArray(app.gridVao);
        glDrawArrays(GL_LINES, 0, app.gridVertexCount);
    }
    glDisable(GL_SCISSOR_TEST);
    drawUi(app, imageWidth);
}

void glfwError(int code, const char* message) {
    std::cerr << "glfw " << code << ": " << message << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        App app;
        if (argc == 3 && std::string(argv[1]) == "--capture") app.capturePath = argv[2];
        else if (argc == 3 && std::string(argv[1]) == "--benchmark") {
            app.benchmarkSeconds = std::stod(argv[2]);
            if (app.benchmarkSeconds <= 0.0) throw std::runtime_error("benchmark duration must be positive");
        }
        else if (argc != 1) {
            std::cerr << "usage: black-hole [--capture output.ppm | --benchmark seconds]\n";
            return EXIT_FAILURE;
        }
        glfwSetErrorCallback(glfwError);
        if (!glfwInit()) throw std::runtime_error("glfw initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
        app.window = glfwCreateWindow(1500, 850, "Schwarzschild black hole", nullptr, nullptr);
        if (!app.window) throw std::runtime_error("could not create OpenGL window");
        glfwMakeContextCurrent(app.window);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(app.window, &app);
        glfwSetKeyCallback(app.window, keyCallback);
        glfwSetCursorPosCallback(app.window, cursorCallback);
        glfwSetMouseButtonCallback(app.window, mouseButtonCallback);
        glfwSetScrollCallback(app.window, scrollCallback);
        app.rayProgram = makeProgram("screen.vert", "blackhole.frag");
        app.gridProgram = makeProgram("grid.vert", "grid.frag");
        app.uiProgram = makeProgram("ui.vert", "ui.frag");
        glGenVertexArrays(1, &app.fullscreenVao);
        createGrid(app);
        glGenVertexArrays(1, &app.uiVao);
        glGenBuffers(1, &app.uiVbo);
        glBindVertexArray(app.uiVao);
        glBindBuffer(GL_ARRAY_BUFFER, app.uiVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);
        glfwShowWindow(app.window);
        glfwFocusWindow(app.window);
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << '\n';
        std::cout << "drag: orbit | scroll: zoom | [ ]: quality | space: pause | "
                     "d: disk | h: halo | g: grid | r: reset | s: screenshot | esc: quit\n";

        double previous = glfwGetTime();
        const double benchmarkStart = previous;
        double titleAt = previous;
        int frames = 0;
        int totalFrames = 0;
        while (!glfwWindowShouldClose(app.window)) {
            double now = glfwGetTime();
            float dt = static_cast<float>(std::min(now - previous, 0.1));
            previous = now;
            glfwPollEvents();
            if (!app.paused) app.simulationTime += dt * 18.0f;
            draw(app);
            if (app.screenshotRequested) {
                savePPM("black-hole-screenshot.ppm", app.framebufferWidth,
                        app.framebufferHeight);
                app.screenshotRequested = false;
            }
            if (!app.capturePath.empty() && totalFrames == 4) {
                savePPM(app.capturePath, app.framebufferWidth, app.framebufferHeight);
                glfwSetWindowShouldClose(app.window, GLFW_TRUE);
            }
            glfwSwapBuffers(app.window);
            ++frames;
            ++totalFrames;
            if (now - titleAt >= 1.0) {
                if (app.benchmarkSeconds > 0.0)
                    std::cout << std::fixed << std::setprecision(1)
                              << frames / (now - titleAt) << " fps\n";
                std::ostringstream title;
                title << "Schwarzschild black hole | " << std::fixed << std::setprecision(1)
                      << frames / (now - titleAt) << " fps | drag orbit, scroll zoom, "
                      << "[ ] quality, G grid, D disk, H halo, space pause";
                glfwSetWindowTitle(app.window, title.str().c_str());
                titleAt = now;
                frames = 0;
            }
            if (app.benchmarkSeconds > 0.0 && now - benchmarkStart >= app.benchmarkSeconds)
                glfwSetWindowShouldClose(app.window, GLFW_TRUE);
        }
        glDeleteFramebuffers(1, &app.framebuffer);
        glDeleteTextures(1, &app.colorTexture);
        glDeleteBuffers(1, &app.gridVbo);
        glDeleteBuffers(1, &app.uiVbo);
        glDeleteVertexArrays(1, &app.gridVao);
        glDeleteVertexArrays(1, &app.uiVao);
        glDeleteVertexArrays(1, &app.fullscreenVao);
        glDeleteProgram(app.gridProgram);
        glDeleteProgram(app.uiProgram);
        glDeleteProgram(app.rayProgram);
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        glfwTerminate();
        return EXIT_FAILURE;
    }
}
