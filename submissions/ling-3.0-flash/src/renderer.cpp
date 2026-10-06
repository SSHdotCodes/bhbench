#include "renderer.h"

Renderer* g_renderer = nullptr;

Renderer::Renderer() : blackhole({0, 0, 0}, 10.0) {
    rayTracer = new RayTracer(&blackhole);

    camera.pos = Vec3(0, 25, 50);
    camera.lookAt = Vec3(0, 0, 0);
    camera.up = Vec3(0, 1, 0);
    camera.fov = 60.0 * M_PI / 180.0;
    camera.aspect = (double)screenWidth / screenHeight;
    camera.focusDist = camDistance;
}

Renderer::~Renderer() {
    delete rayTracer;
}

bool Renderer::init() {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    window = glfwCreateWindow(screenWidth, screenHeight,
        "Black Hole Simulation - Gravitational Lensing, Accretion Disk & Spacetime Curvature",
        nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glGenBuffers(1, &gridIBO);

    glGenVertexArrays(1, &diskVAO);
    glGenBuffers(1, &diskVBO);

    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);

    glGenVertexArrays(1, &horizonVAO);
    glGenBuffers(1, &horizonVBO);

    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetWindowSizeCallback(window, resizeCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    g_renderer = this;

    return true;
}

void Renderer::updateCamera() {
    camDistance = 50.0;
    camera.pos = Vec3(
        camDistance * std::cos(camAngleY) * std::cos(camAngleX),
        camDistance * std::sin(camAngleX),
        camDistance * std::sin(camAngleY) * std::cos(camAngleX)
    );
    camera.lookAt = Vec3(0, 0, 0);
    camera.up = Vec3(0, 1, 0);
    camera.fov = 60.0 * M_PI / 180.0;
    camera.aspect = (double)screenWidth / screenHeight;
    camera.lensRadius = 0.0;
    camera.focusDist = camDistance;
}

void Renderer::updateGridGL() {
    std::vector<float> gridVerts;
    std::vector<uint32_t> gridIdx;

    int divisions = (int)(spacetime.gridSize / spacetime.gridSpacing);

    for (int ix = -divisions / 2; ix <= divisions / 2; ix++) {
        for (int iz = -divisions / 2; iz <= divisions / 2; iz++) {
            Vec3 gridPos(ix * spacetime.gridSpacing, 0, iz * spacetime.gridSpacing);
            Vec3 deformed = spacetime.getDeformedPosition(gridPos, blackhole);
            Vec3 color = spacetime.getGridColor(deformed, blackhole);

            gridVerts.push_back(deformed.x);
            gridVerts.push_back(deformed.y);
            gridVerts.push_back(deformed.z);
            gridVerts.push_back(color.x);
            gridVerts.push_back(color.y);
            gridVerts.push_back(color.z);
        }
    }

    for (auto idx : spacetime.indices) {
        gridIdx.push_back(idx);
    }

    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, gridIdx.size() * sizeof(uint32_t), gridIdx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::updateDiskGL() {
    std::vector<float> diskVerts;

    for (int i = 0; i < accretion.particleCount; i++) {
        Vec3 pos = accretion.particles[i];
        Vec3 rel = pos - blackhole.position;
        double dist = rel.length();

        double heightFade = std::exp(-std::abs(rel.y) * 20.0);
        double radialFade = std::exp(-std::pow((dist - blackhole.ISCO) / 10.0, 2.0));
        double innerFade = 1.0;
        if (dist < blackhole.photonSphere * 1.5) {
            innerFade = smoothstep(blackhole.photonSphere * 1.5, blackhole.schwarzschildRadius * 2.0, dist);
        }

        double alpha = heightFade * radialFade * innerFade * 0.8;

        diskVerts.push_back(pos.x);
        diskVerts.push_back(pos.y);
        diskVerts.push_back(pos.z);
        diskVerts.push_back(accretion.colors[i].x);
        diskVerts.push_back(accretion.colors[i].y);
        diskVerts.push_back(accretion.colors[i].z);
        diskVerts.push_back(alpha);
    }

    glBindVertexArray(diskVAO);
    glBindBuffer(GL_ARRAY_BUFFER, diskVBO);
    glBufferData(GL_ARRAY_BUFFER, diskVerts.size() * sizeof(float), diskVerts.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::renderFrame() {
    double currentTime = glfwGetTime();
    frameDelta = currentTime - lastFrame;
    lastFrame = currentTime;
    time += frameDelta;

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, screenWidth, screenHeight);

    updateCamera();
    renderBackground();

    if (showLensing) {
        renderLensingEffect();
    }

    if (showDisk) {
        renderAccretionDisk();
    }

    if (showGrid) {
        renderSpacetimeGrid();
    }

    renderHorizon();
    renderStars();

    glfwSwapBuffers(window);
    glfwPollEvents();
}

GLuint Renderer::compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    return shader;
}

GLuint Renderer::createProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compileShader(vertSrc, GL_VERTEX_SHADER);
    GLuint fs = compileShader(fragSrc, GL_FRAGMENT_SHADER);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

void Renderer::renderBackground() {
    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aBrightness;
out vec3 fragColor;
out float fragBrightness;
void main() {
    gl_Position = vec4(aPos, 1.0);
    gl_PointSize = 3.0;
    fragColor = aColor;
    fragBrightness = aBrightness;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragColor;
in float fragBrightness;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor * fragBrightness, 1.0);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    updateDiskGL();

    std::vector<float> starVerts;
    for (int i = 0; i < starfield.count; i++) {
        starVerts.push_back(starfield.stars[i].x);
        starVerts.push_back(starfield.stars[i].y);
        starVerts.push_back(starfield.stars[i].z);
        starVerts.push_back(starfield.colors[i].x);
        starVerts.push_back(starfield.colors[i].y);
        starVerts.push_back(starfield.colors[i].z);
        starVerts.push_back(starfield.brightnesses[i]);
    }

    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, starVerts.size() * sizeof(float), starVerts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));

    glDrawArrays(GL_POINTS, 0, starfield.count);
    glBindVertexArray(0);
    glDeleteProgram(prog);
}

void Renderer::renderLensingEffect() {
    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 fragPos;
void main() {
    gl_Position = vec4(aPos, 1.0);
    fragPos = aPos;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragPos;
out vec4 FragColor;
uniform vec3 uBHPos;
uniform float uSR;
uniform float uPS;
uniform float uTime;

void main() {
    vec3 rel = fragPos - uBHPos;
    float dist = length(rel);

    if (dist > uPS * 3.0) discard;

    float innerGlow = smoothstep(uSR * 2.0, uSR * 0.5, dist);
    float photonRing = smoothstep(uPS * 1.2, uPS * 0.8, dist) *
                       smoothstep(uPS * 2.0, uPS * 1.5, dist) * 3.0;
    float doppler = sin(rel.y * 5.0 + uTime * 2.0) * 0.3 + 0.7;

    vec3 innerColor = vec3(1.0, 0.8, 0.3) * innerGlow * 0.5;
    vec3 ringColor = vec3(1.0, 0.6, 0.1) * photonRing * doppler;
    vec3 finalColor = innerColor + ringColor;

    float alpha = innerGlow * 0.3 + photonRing * 0.5;
    FragColor = vec4(finalColor, alpha);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    glUniform3f(glGetUniformLocation(prog, "uBHPos"), 0, 0, 0);
    glUniform1f(glGetUniformLocation(prog, "uSR"), (float)blackhole.schwarzschildRadius);
    glUniform1f(glGetUniformLocation(prog, "uPS"), (float)blackhole.photonSphere);
    glUniform1f(glGetUniformLocation(prog, "uTime"), (float)time);

    int segments = 64;
    std::vector<float> lensVerts;
    for (int i = 0; i < segments; i++) {
        double angle1 = (double)i / segments * 2.0 * M_PI;
        double angle2 = (double)(i + 1) / segments * 2.0 * M_PI;

        for (double r = blackhole.photonSphere * 0.8; r <= blackhole.photonSphere * 2.5; r += 0.3) {
            lensVerts.push_back(r * std::cos(angle1));
            lensVerts.push_back(0.0);
            lensVerts.push_back(r * std::sin(angle1));

            lensVerts.push_back(r * std::cos(angle2));
            lensVerts.push_back(0.0);
            lensVerts.push_back(r * std::sin(angle2));
        }
    }

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, lensVerts.size() * sizeof(float), lensVerts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)lensVerts.size() / 3);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
}

void Renderer::renderAccretionDisk() {
    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aDiskColor;
layout(location = 2) in float aAlpha;
out vec3 fragColor;
out float fragAlpha;
void main() {
    gl_Position = vec4(aPos, 1.0);
    fragColor = aDiskColor;
    fragAlpha = aAlpha;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragColor;
in float fragAlpha;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, fragAlpha);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    updateDiskGL();
    glBindVertexArray(diskVAO);
    glDrawArrays(GL_POINTS, 0, accretion.particleCount);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDeleteProgram(prog);
}

void Renderer::renderSpacetimeGrid() {
    updateGridGL();

    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 fragColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    fragColor = aColor;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, 0.5);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    glBindVertexArray(gridVAO);
    glDrawElements(GL_TRIANGLES, spacetime.indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDeleteProgram(prog);
}

void Renderer::renderHorizon() {
    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 fragColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    fragColor = aColor;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor, 0.9);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    glDisable(GL_DEPTH_TEST);

    std::vector<float> horizonVerts;
    int segments = 128;
    for (int i = 0; i < segments; i++) {
        double angle1 = (double)i / segments * 2.0 * M_PI;
        double angle2 = (double)(i + 1) / segments * 2.0 * M_PI;
        double r = blackhole.schwarzschildRadius * 1.01;

        for (int ring = 0; ring < 3; ring++) {
            double rr = r * (1.0 + ring * 0.05);
            horizonVerts.push_back(rr * std::cos(angle1));
            horizonVerts.push_back(0.0);
            horizonVerts.push_back(rr * std::sin(angle1));
            if (ring == 0) {
                horizonVerts.push_back(1.0); horizonVerts.push_back(0.0); horizonVerts.push_back(0.0);
            } else if (ring == 1) {
                horizonVerts.push_back(1.0); horizonVerts.push_back(0.3); horizonVerts.push_back(0.0);
            } else {
                horizonVerts.push_back(0.5); horizonVerts.push_back(0.0); horizonVerts.push_back(0.0);
            }
            horizonVerts.push_back(0.6);

            horizonVerts.push_back(rr * std::cos(angle2));
            horizonVerts.push_back(0.0);
            horizonVerts.push_back(rr * std::sin(angle2));
            if (ring == 0) {
                horizonVerts.push_back(1.0); horizonVerts.push_back(0.0); horizonVerts.push_back(0.0);
            } else if (ring == 1) {
                horizonVerts.push_back(1.0); horizonVerts.push_back(0.3); horizonVerts.push_back(0.0);
            } else {
                horizonVerts.push_back(0.5); horizonVerts.push_back(0.0); horizonVerts.push_back(0.0);
            }
            horizonVerts.push_back(0.6);
        }
    }

    glBindVertexArray(horizonVAO);
    glBindBuffer(GL_ARRAY_BUFFER, horizonVBO);
    glBufferData(GL_ARRAY_BUFFER, horizonVerts.size() * sizeof(float), horizonVerts.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)(horizonVerts.size() / 6));
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    glDeleteProgram(prog);
}

void Renderer::renderStars() {
    const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aBrightness;
out vec3 fragColor;
out float fragBrightness;
void main() {
    gl_Position = vec4(aPos, 1.0);
    gl_PointSize = 2.0;
    fragColor = aColor;
    fragBrightness = aBrightness;
}
)";
    const char* fragSrc = R"(
#version 330 core
in vec3 fragColor;
in float fragBrightness;
out vec4 FragColor;
void main() {
    FragColor = vec4(fragColor * fragBrightness, 1.0);
}
)";

    GLuint prog = createProgram(vertSrc, fragSrc);
    glUseProgram(prog);

    glBindVertexArray(starVAO);
    glDrawArrays(GL_POINTS, 0, starfield.count);
    glBindVertexArray(0);

    glDeleteProgram(prog);
}

void Renderer::run() {
    while (!shouldClose()) {
        renderFrame();
    }
    glfwTerminate();
}

bool Renderer::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Renderer::cursorPosCallback(GLFWwindow*, double xpos, double ypos) {
    Renderer* self = g_renderer;
    if (!self || !self->camRotating) return;
    static double lastX = xpos, lastY = ypos;
    double dx = xpos - lastX;
    double dy = ypos - lastY;
    self->camAngleY += dx * 0.005;
    self->camAngleX += dy * 0.005;
    self->camAngleX = clamp(self->camAngleX, -1.5, 1.5);
    lastX = xpos;
    lastY = ypos;
}

void Renderer::mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    Renderer* self = g_renderer;
    if (!self) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        self->camRotating = true;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        self->camRotating = false;
    }
}

void Renderer::scrollCallback(GLFWwindow*, double, double yoff) {
    Renderer* self = g_renderer;
    if (!self) return;
    self->camDistance *= (1.0 - yoff * 0.1);
    self->camDistance = clamp(self->camDistance, 10.0, 200.0);
}

void Renderer::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    Renderer* self = g_renderer;
    if (!self) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
            case GLFW_KEY_G: self->showGrid = !self->showGrid; break;
            case GLFW_KEY_D: self->showDisk = !self->showDisk; break;
            case GLFW_KEY_S: self->showStars = !self->showStars; break;
            case GLFW_KEY_L: self->showLensing = !self->showLensing; break;
            case GLFW_KEY_R: self->camAngleX = 0.3; self->camAngleY = 0.0; self->camDistance = 50.0; break;
            case GLFW_KEY_SPACE: self->camRotating = !self->camRotating; break;
            case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
            case GLFW_KEY_1: self->blackhole.mass = 5.0; break;
            case GLFW_KEY_2: self->blackhole.mass = 10.0; break;
            case GLFW_KEY_3: self->blackhole.mass = 20.0; break;
        }
    }
}

void Renderer::resizeCallback(GLFWwindow*, int width, int height) {
    Renderer* self = g_renderer;
    if (!self) return;
    self->screenWidth = width;
    self->screenHeight = height;
    glViewport(0, 0, width, height);
}

void Renderer::render() {
    accretion.update(frameDelta);
    renderFrame();
}