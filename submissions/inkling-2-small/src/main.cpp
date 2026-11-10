// main.cpp — Schwarzschild black hole, real-time general-relativistic ray tracer.
// Opens a live window (OpenGL 4.1). See README.md for the physics and controls.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Shaders.h"
#include "Mat4.h"
#include "Grid.h"
#include "Hud.h"

using namespace bhlib;

// ------------------------------------------------------------ tiny helpers
static std::string glInfoLog(GLuint obj, bool isProgram) {
    int len = 0;
    if (isProgram) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
    else           glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
    std::string log(size_t(len > 1 ? len : 1), '\0');
    if (isProgram) glGetProgramInfoLog(obj, len, nullptr, &log[0]);
    else           glGetShaderInfoLog(obj, len, nullptr, &log[0]);
    return log;
}

static GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "Shader %s failed:\n%s\n", name, glInfoLog(s, false).c_str());
        exit(1);
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs, const char* name) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, name);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, name);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "Program %s failed to link:\n%s\n", name, glInfoLog(p, true).c_str());
        exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static GLuint makeBuffer(GLenum target, const void* data, size_t bytes, GLenum usage) {
    GLuint b = 0;
    glGenBuffers(1, &b);
    glBindBuffer(target, b);
    glBufferData(target, bytes, data, usage);
    return b;
}

// ------------------------------------------------------------ app state
struct AppState {
    GLFWwindow* window = nullptr;
    int fbWidth = 1600, fbHeight = 900;

    OrbitCamera camera;
    bool showGrid = true;
    bool showDisk = true;
    int  haloIndex = 1;                       // 0: off, 1: 0.9, 2: 1.7
    bool lensing = true;
    bool stars = true;
    bool showPhotonRing = true;
    bool showIscoRing = true;
    bool autoQuality = true;
    bool autoOrbit = true;
    bool showHud = true;
    bool paused = false;
    bool fullscreen = false;

    int   scaleIndex = 0;                     // index into kSceneScales
    float exposure = 1.0f;
    float animTime = 0.0f;
    float fpsEma = 60.0f;
    float qualityTimer = 0.0f;
    int   debugView = 0;                      // raytrace false-colour debug mode

    bool dragging = false;
    double lastX = 0.0, lastY = 0.0;

    static constexpr float kSceneScales[5] = {1.0f, 0.85f, 0.7f, 0.55f, 0.4f};
    static constexpr float kHaloStrengths[3] = {0.0f, 0.9f, 1.7f};

    float sceneScale() const { return kSceneScales[scaleIndex]; }
    float haloStrength() const { return kHaloStrengths[haloIndex]; }
};

static AppState g;

// ------------------------------------------------------------ callbacks
static void cursorPosCb(GLFWwindow*, double x, double y) {
    if (!g.dragging) { g.lastX = x; g.lastY = y; return; }
    double dx = x - g.lastX, dy = y - g.lastY;
    g.lastX = x; g.lastY = y;
    g.camera.orbit(float(-dx) * 0.0052f, float(dy) * 0.0052f);
}

static void mouseButtonCb(GLFWwindow*, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT ||
        button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) g.dragging = true;
        else if (action == GLFW_RELEASE) g.dragging = false;
    }
}

static void scrollCb(GLFWwindow*, double, double y) { g.camera.dolly(float(y * 48.0)); }

static void framebufferSizeCb(GLFWwindow*, int w, int h) {
    g.fbWidth = w > 1 ? w : 1;
    g.fbHeight = h > 1 ? h : 1;
}

static void keyCb(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(g.window, GLFW_TRUE); break;
        case GLFW_KEY_1: g.showGrid = !g.showGrid; break;
        case GLFW_KEY_2: g.showDisk = !g.showDisk; break;
        case GLFW_KEY_3: g.haloIndex = (g.haloIndex + 1) % 3; break;
        case GLFW_KEY_4: g.lensing = !g.lensing; break;
        case GLFW_KEY_5: g.stars = !g.stars; break;
        case GLFW_KEY_6: g.camera.elevation = 0.035f; break;    // edge-on (in the disk plane)
        case GLFW_KEY_7: g.camera.elevation = 0.785f; break;    // ~45 degrees
        case GLFW_KEY_8: g.camera.elevation = 1.45f; break;     // face-on, down the throat
        case GLFW_KEY_P: g.showPhotonRing = !g.showPhotonRing; break;
        case GLFW_KEY_I: g.showIscoRing = !g.showIscoRing; break;
        case GLFW_KEY_A: g.autoQuality = !g.autoQuality; break;
        case GLFW_KEY_F: g.autoOrbit = !g.autoOrbit; break;
        case GLFW_KEY_H: g.showHud = !g.showHud; break;
        case GLFW_KEY_R: g.camera.reset(); break;
        case GLFW_KEY_SPACE: g.paused = !g.paused; break;
        case GLFW_KEY_MINUS: g.exposure = fmaxf(0.35f, g.exposure / 1.25f); break;
        case GLFW_KEY_EQUAL: g.exposure = fminf(2.6f, g.exposure * 1.25f); break;
        case GLFW_KEY_LEFT_BRACKET:
            if (g.scaleIndex > 0) --g.scaleIndex;
            g.autoQuality = false;
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            if (g.scaleIndex < 4) ++g.scaleIndex;
            g.autoQuality = false;
            break;
        case GLFW_KEY_9: {
            g.fullscreen = !g.fullscreen;
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            if (g.fullscreen) {
                const GLFWvidmode* vm = glfwGetVideoMode(mon);
                glfwSetWindowMonitor(g.window, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            } else {
                glfwSetWindowMonitor(g.window, nullptr, 80, 80, 1600, 900, GLFW_DONT_CARE);
            }
            break;
        }
        default: break;
    }
}

// ------------------------------------------------------------ geometry
struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0, vertexCount = 0;
};

static Mesh buildLineMesh(const std::vector<GridVertex>& v) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    m.vbo = makeBuffer(GL_ARRAY_BUFFER, v.data(), v.size() * sizeof(GridVertex), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)(3 * sizeof(float)));
    m.vertexCount = (GLsizei)v.size();
    glBindVertexArray(0);
    return m;
}

static Mesh buildInterleavedMesh(const std::vector<float>& data, // pos + depth? (4 floats) or pos+normal (6)
                                 const std::vector<unsigned int>& idx,
                                 int floatsPerVertex, bool withSecondAttrib) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    m.vbo = makeBuffer(GL_ARRAY_BUFFER, data.data(), data.size() * sizeof(float), GL_STATIC_DRAW);
    m.ebo = makeBuffer(GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(unsigned int), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, floatsPerVertex * sizeof(float), (void*)0);
    if (withSecondAttrib) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, floatsPerVertex == 6 ? 3 : 1, GL_FLOAT, GL_FALSE,
                              floatsPerVertex * sizeof(float), (void*)(3 * sizeof(float)));
    }
    m.indexCount = (GLsizei)idx.size();
    glBindVertexArray(0);
    return m;
}

static Mesh buildSphereMesh(const SphereGeometry& s) {
    return buildInterleavedMesh(s.verts, s.indices, 6, true);
}

static Mesh buildRingMesh(const RingGeometry& ring) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    m.vbo = makeBuffer(GL_ARRAY_BUFFER, ring.verts.data(), ring.verts.size() * sizeof(float), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    m.vertexCount = (GLsizei)(ring.verts.size() / 3);
    glBindVertexArray(0);
    return m;
}

// ------------------------------------------------------------ HUD drawing
struct HudRenderer {
    GLuint prog = 0, vao = 0, vbo = 0;
    GLint locViewport = -1;
    HudBatch batch;

    void init() {
        prog = makeProgram(shaders::kHudVert, shaders::kHudFrag, "hud");
        locViewport = glGetUniformLocation(prog, "uViewportPx");
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        vbo = makeBuffer(GL_ARRAY_BUFFER, nullptr, 400000 * sizeof(float), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    void flush(float vpW, float vpH) {
        if (batch.verts.empty()) { batch.clear(); return; }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, batch.verts.size() * sizeof(float), batch.verts.data());
        glUseProgram(prog);
        glUniform2f(locViewport, vpW, vpH);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(batch.verts.size() / 6));
        glBindVertexArray(0);
        batch.clear();
    }
};

// ------------------------------------------------------------ entry
int main(int argc, char** argv) {
    const char* screenshotPath = nullptr;
    int reqW = 1600, reqH = 900;
    float reqTime = 4.2f;
    float camAz = -0.62f, camEl = 0.30f, camDist = 38.0f;
    bool hudInScreenshot = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
        else if (a == "--width" && i + 1 < argc) reqW = atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) reqH = atoi(argv[++i]);
        else if (a == "--time" && i + 1 < argc) reqTime = (float)atof(argv[++i]);
        else if (a == "--cam" && i + 1 < argc) {
            if (sscanf(argv[++i], "%f,%f,%f", &camAz, &camEl, &camDist) != 3)
                fprintf(stderr, "warning: --cam expects az,el,dist\n");
        }
        else if (a == "--hud") hudInScreenshot = true;
        else if (a == "--grid" && i + 1 < argc) g.showGrid = atoi(argv[++i]) != 0;
        else if (a == "--disk" && i + 1 < argc) g.showDisk = atoi(argv[++i]) != 0;
        else if (a == "--stars" && i + 1 < argc) g.stars = atoi(argv[++i]) != 0;
        else if (a == "--lensing" && i + 1 < argc) g.lensing = atoi(argv[++i]) != 0;
        else if (a == "--halo" && i + 1 < argc) g.haloIndex = atoi(argv[++i]);
        else if (a == "--exposure" && i + 1 < argc) g.exposure = (float)atof(argv[++i]);
        else if (a == "--debug" && i + 1 < argc) g.debugView = atoi(argv[++i]);
        else if (a == "--ring" && i + 1 < argc) g.showPhotonRing = atoi(argv[++i]) != 0;
        else if (a == "--help" || a == "-h") {
            printf("Usage: blackhole [--screenshot out.ppm] [--width W] [--height H]\n"
                   "                 [--time seconds] [--cam az,el,dist] [--hud]\n"
                   "                 [--grid 0|1] [--disk 0|1] [--stars 0|1] [--lensing 0|1]\n"
                   "                 [--halo 0|1|2] [--exposure x] [--ring 0|1]\n");
            return 0;
        }
    }
    bool headless = screenshotPath != nullptr;

    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, headless ? GLFW_FALSE : GLFW_TRUE);
    g.camera.azimuth = camAz;
    g.camera.elevation = camEl;
    g.camera.distance = camDist;
    if (headless) { g.autoOrbit = false; g.showHud = hudInScreenshot; }

    g.window = glfwCreateWindow(reqW, reqH,
        "Schwarzschild Black Hole — General Relativistic Ray Tracer", nullptr, nullptr);
    if (!g.window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g.window);
    glfwSwapInterval(headless ? 0 : 1);
    glfwGetFramebufferSize(g.window, &g.fbWidth, &g.fbHeight);
    glfwSetCursorPosCallback(g.window, cursorPosCb);
    glfwSetMouseButtonCallback(g.window, mouseButtonCb);
    glfwSetScrollCallback(g.window, scrollCb);
    glfwSetFramebufferSizeCallback(g.window, framebufferSizeCb);
    glfwSetKeyCallback(g.window, keyCb);

    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(glewErr));
        return 1;
    }
    printf("OpenGL %s | GLSL %s | %s\n", glGetString(GL_VERSION),
           glGetString(GL_SHADING_LANGUAGE_VERSION), glGetString(GL_RENDERER));

    // ---- programs
    GLuint progRay = makeProgram(shaders::kQuadVert, shaders::kRaytraceFrag, "raytrace");
    GLuint progGrid = makeProgram(shaders::kGridVert, shaders::kGridFrag, "grid");
    GLuint progSphere = makeProgram(shaders::kSolidVert, shaders::kHorizonFrag, "horizon");
    GLuint progRing = makeProgram(shaders::kRingVert, shaders::kRingFrag, "ring");
    HudRenderer hud; hud.init();

    GLint r_Resolution = glGetUniformLocation(progRay, "uResolution");
    GLint r_CamPos = glGetUniformLocation(progRay, "uCamPos");
    GLint r_CamFwd = glGetUniformLocation(progRay, "uCamFwd");
    GLint r_CamRight = glGetUniformLocation(progRay, "uCamRight");
    GLint r_CamUp = glGetUniformLocation(progRay, "uCamUp");
    GLint r_TanFov = glGetUniformLocation(progRay, "uTanHalfFov");
    GLint r_Aspect = glGetUniformLocation(progRay, "uAspect");
    GLint r_Time = glGetUniformLocation(progRay, "uTime");
    GLint r_Exposure = glGetUniformLocation(progRay, "uExposure");
    GLint r_Lensing = glGetUniformLocation(progRay, "uLensing");
    GLint r_DiskOn = glGetUniformLocation(progRay, "uDiskOn");
    GLint r_StarsOn = glGetUniformLocation(progRay, "uStarsOn");
    GLint r_Halo = glGetUniformLocation(progRay, "uHaloStrength");
    GLint r_DiskInner = glGetUniformLocation(progRay, "uDiskInner");
    GLint r_DiskOuter = glGetUniformLocation(progRay, "uDiskOuter");
    GLint r_DiskTemp = glGetUniformLocation(progRay, "uDiskPeakTemp");
    GLint r_DiskIntensity = glGetUniformLocation(progRay, "uDiskIntensity");
    GLint r_DiskSwirl = glGetUniformLocation(progRay, "uDiskSwirl");
    GLint r_Debug = glGetUniformLocation(progRay, "uDebug");
    GLint g_ViewProj = glGetUniformLocation(progGrid, "uViewProj");
    GLint g_Time = glGetUniformLocation(progGrid, "uTime");
    GLint g_Alpha = glGetUniformLocation(progGrid, "uGridAlpha");
    GLint s_ViewProj = glGetUniformLocation(progSphere, "uViewProj");
    GLint s_CamPos = glGetUniformLocation(progSphere, "uCamPos");
    GLint rg_ViewProj = glGetUniformLocation(progRing, "uViewProj");
    GLint rg_Color = glGetUniformLocation(progRing, "uRingColor");
    GLint rg_Alpha = glGetUniformLocation(progRing, "uRingAlpha");

    // ---- geometry
    GridGeometry grid; grid.build(50.0f, 1.35f);
    SphereGeometry sphere; sphere.build(2.0f, 40, 64);
    RingGeometry photonRing; photonRing.build(3.0f, 256);
    RingGeometry iscoRing; iscoRing.build(6.0f, 256);
    Mesh gridLines = buildLineMesh(grid.lineVerts);
    Mesh sphereMesh = buildSphereMesh(sphere);
    Mesh photonRingMesh = buildRingMesh(photonRing);
    Mesh iscoRingMesh = buildRingMesh(iscoRing);

    GLuint quadVao = 0, quadVbo = 0;
    {
        float verts[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glGenVertexArrays(1, &quadVao);
        glBindVertexArray(quadVao);
        quadVbo = makeBuffer(GL_ARRAY_BUFFER, verts, sizeof(verts), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }

    if (!headless) {
        printf("\nSchwarzschild Black Hole — controls:\n"
               "  DRAG orbit | WHEEL zoom | 1 grid | 2 disk | 3 halo cycle | 4 lensing\n"
               "  5 stars | 6 edge-on | 7 45deg | 8 top | P photon ring | I ISCO ring\n"
               "  A auto-quality | [ ] quality | - = exposure | F auto-orbit | H HUD\n"
               "  SPACE pause | 9 fullscreen | R reset camera | ESC quit\n\n");
    }

    const HudBatch::Color kTitle{0.55f, 0.86f, 1.0f, 0.98f};
    const HudBatch::Color kDim{0.62f, 0.72f, 0.80f, 0.85f};
    const HudBatch::Color kText{0.88f, 0.93f, 0.97f, 0.95f};
    const HudBatch::Color kOn{0.55f, 1.0f, 0.65f, 0.95f};
    const HudBatch::Color kOff{1.0f, 0.62f, 0.42f, 0.95f};
    const HudBatch::Color kAccent{0.45f, 0.75f, 1.0f, 0.90f};
    const HudBatch::Color kPanel{0.02f, 0.05f, 0.09f, 0.62f};
    const HudBatch::Color kPanelEdge{0.30f, 0.65f, 0.90f, 0.35f};

    double lastTime = glfwGetTime();
    int frameCount = 0;

    while (!glfwWindowShouldClose(g.window)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;
        if (dt > 0.0f) g.fpsEma = g.fpsEma * 0.94f + (1.0f / dt) * 0.06f;
        if (headless) g.animTime = reqTime;
        else if (!g.paused) g.animTime += dt;
        if (g.autoOrbit && !g.dragging) g.camera.orbit(0.085f * dt, 0.0f);

        g.qualityTimer += dt;
        if (g.autoQuality && g.qualityTimer > 2.0f) {
            g.qualityTimer = 0.0f;
            if (g.fpsEma < 42.0f && g.scaleIndex < 4) ++g.scaleIndex;
            else if (g.fpsEma > 57.5f && g.scaleIndex > 0) --g.scaleIndex;
        }

        glfwGetFramebufferSize(g.window, &g.fbWidth, &g.fbHeight);
        const int W = g.fbWidth, H = g.fbHeight;
        const float aspectNow = float(W) / float(H);
        const float s = headless ? 1.0f : g.sceneScale();
        const int sW = int(W * s), sH = int(H * s);

        // ---- pass 1: geodesic ray trace (into scaled viewport)
        Vec3 cam = g.camera.position();
        Vec3 fwd = g.camera.fwd();
        Vec3 right = g.camera.right();
        Vec3 up = g.camera.up();
        glViewport(0, 0, sW, sH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(progRay);
        glUniform2f(r_Resolution, float(sW), float(sH));
        glUniform3f(r_CamPos, cam.x, cam.y, cam.z);
        glUniform3f(r_CamFwd, fwd.x, fwd.y, fwd.z);
        glUniform3f(r_CamRight, right.x, right.y, right.z);
        glUniform3f(r_CamUp, up.x, up.y, up.z);
        glUniform1f(r_TanFov, g.camera.tanHalfFov());
        glUniform1f(r_Aspect, aspectNow);
        glUniform1f(r_Time, g.animTime);
        glUniform1f(r_Exposure, g.exposure);
        glUniform1i(r_Lensing, g.lensing ? 1 : 0);
        glUniform1i(r_DiskOn, g.showDisk ? 1 : 0);
        glUniform1i(r_StarsOn, g.stars ? 1 : 0);
        glUniform1f(r_Halo, g.haloStrength());
        glUniform1f(r_DiskInner, 6.0f);   // ISCO of a Schwarzschild (a* = 0) BH
        glUniform1f(r_DiskOuter, 30.0f);
        glUniform1f(r_DiskTemp, 6200.0f);
        glUniform1f(r_DiskIntensity, 0.85f);
        glUniform1f(r_DiskSwirl, 0.85f);
        glUniform1i(r_Debug, g.debugView);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        // ---- view-projection matrix (grid world space)
        Mat4 proj = Mat4::perspective(g.camera.fovDeg * 3.14159265f / 180.0f, aspectNow, 0.5f, 500.0f);
        float eye[3] = {cam.x, cam.y, cam.z}, ctr[3] = {0, 0, 0}, upc[3] = {0, 1, 0};
        Mat4 vp = Mat4::multiply(proj, Mat4::lookAt(eye, ctr, upc));

        // ---- pass 2: horizon occluder (writes depth; grid tests against it)
        glViewport(0, 0, W, H);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glUseProgram(progSphere);
        glUniformMatrix4fv(s_ViewProj, 1, GL_FALSE, vp.m);
        glUniform3f(s_CamPos, cam.x, cam.y, cam.z);
        glBindVertexArray(sphereMesh.vao);
        glDrawElements(GL_TRIANGLES, sphereMesh.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        // ---- pass 3: spacetime grid (Flamm's paraboloid), additive
        if (g.showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(progGrid);
            glUniformMatrix4fv(g_ViewProj, 1, GL_FALSE, vp.m);
            glUniform1f(g_Time, g.animTime);
            glUniform1f(g_Alpha, 0.9f);    // luminous wireframe
            glBindVertexArray(gridLines.vao);
            glDrawArrays(GL_LINES, 0, gridLines.vertexCount);
            glBindVertexArray(0);
        }

        // ---- pass 4: marker rings (photon sphere 3M, ISCO 6M)
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glUseProgram(progRing);
        glUniformMatrix4fv(rg_ViewProj, 1, GL_FALSE, vp.m);
        if (g.showPhotonRing) {
            glUniform3f(rg_Color, 1.0f, 0.58f, 0.22f);
            glUniform1f(rg_Alpha, 0.55f);
            glBindVertexArray(photonRingMesh.vao);
            glDrawArrays(GL_LINE_STRIP, 0, photonRingMesh.vertexCount);
            glBindVertexArray(0);
        }
        if (g.showIscoRing) {
            glUniform3f(rg_Color, 0.38f, 0.78f, 1.0f);
            glUniform1f(rg_Alpha, 0.5f);
            glBindVertexArray(iscoRingMesh.vao);
            glDrawArrays(GL_LINE_STRIP, 0, iscoRingMesh.vertexCount);
            glBindVertexArray(0);
        }

        // ---- pass 5: HUD at full resolution
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        if (g.showHud) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            HudBatch& b = hud.batch;
            const float pad = 14.0f;
            float x = pad, y = pad;
            const float panelW = 490.0f, panelH = 262.0f;
            b.addRect(x - 8, y - 8, panelW, panelH, kPanel);
            b.addRectOutline(x - 8, y - 8, panelW, panelH, kPanelEdge, 2.0f);

            b.drawText(x, y, 2.3f, "SCHWARZSCHILD BLACK HOLE", kTitle); y += 19;
            b.drawText(x, y, 1.55f, "GENERAL RELATIVISTIC RAY TRACER", kDim); y += 15;
            char fpsLine[72];
            snprintf(fpsLine, sizeof(fpsLine), "FPS:%3d  QUALITY:%d%%%s%s",
                     (int)std::min(999.0f, g.fpsEma), (int)lroundf(g.sceneScale() * 100.0f),
                     g.autoQuality ? " AUTO" : " MAN ", g.paused ? "  PAUSED" : "");
            b.drawText(x, y, 1.75f, fpsLine, kText); y += 16;
            char camLine[72];
            snprintf(camLine, sizeof(camLine), "DIST:%.1f M  ELEV:%+.0f DEG  FOV:%.0f",
                     g.camera.distance, g.camera.elevation * 57.29578f, g.camera.fovDeg);
            b.drawText(x, y, 1.75f, camLine, kText); y += 16;
            b.drawText(x, y, 1.6f, "DISK: T_PEAK~6200K    R_IN=ISCO=6M", kDim); y += 16;
            b.drawText(x, y, 1.6f, "A* = 0  GEOM. UNITS G=C=M=1", kDim); y += 17;

            const float kx = 4.2f * 6.0f * 1.75f;
            auto line = [&](const char* keyLabel, const std::string& desc, bool on) {
                b.drawText(x, y, 1.75f, keyLabel, kAccent);
                b.drawText(x + kx, y, 1.75f, desc + (on ? ":ON" : ":OFF"), on ? kOn : kOff);
                y += 15.0f;
            };
            line("[1]", "SPACETIME GRID (FLAMM)", g.showGrid);
            line("[2]", "ACCRETION DISK", g.showDisk);
            {
                b.drawText(x, y, 1.75f, "[3]", kAccent);
                const char* haloState[3] = {"PHOTON HALO:OFF", "PHOTON HALO:0.9", "PHOTON HALO:1.7"};
                b.drawText(x + kx, y, 1.75f, haloState[g.haloIndex], g.haloIndex == 0 ? kOff : kOn);
                y += 15.0f;
            }
            line("[4]", "LENSING (GR GEODESICS)", g.lensing);
            line("[5]", "STARFIELD", g.stars);
            line("[P]", "PHOTON SPHERE RING (3M)", g.showPhotonRing);
            line("[I]", "ISCO RING (6M)", g.showIscoRing);
            y += 4;
            b.drawText(x, y, 1.45f, "REDSHIFT 1+Z=1/SQRT(1-2/R)", kDim); y += 14;
            b.drawText(x, y, 1.3f, "ISCO RS=.816   3M RS=.577   HORIZON RS=0", kDim);

            std::string hint = "DRAG:ORBIT  WHEEL:ZOOM  6:EDGE-ON  7:45DEG  8:TOP  SPACE:PAUSE  H:HUD  R:RESET  ESC:QUIT";
            float hintW = HudBatch::textWidth(hint, 1.55f) + 20.0f;
            b.addRect((W - hintW) * 0.5f, float(H) - 30.0f, hintW, 22.0f, {0.02f, 0.05f, 0.09f, 0.55f});
            b.drawText((W - hintW) * 0.5f + 10.0f, float(H) - 25.0f, 1.4f, hint, kDim);

            hud.flush(float(W), float(H));
        }
        glDisable(GL_BLEND);

        // ---- readback must happen before swap (default FB contents are undefined after)
        if (headless && frameCount >= 3) {
            std::vector<unsigned char> pixels(size_t(W) * size_t(H) * 3);
            glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::vector<unsigned char> flipped(pixels.size());
            size_t row = size_t(W) * 3;
            for (int y = 0; y < H; ++y)
                memcpy(flipped.data() + size_t(y) * row,
                       pixels.data() + size_t(H - 1 - y) * row, row);
            FILE* f = fopen(screenshotPath, "wb");
            if (!f) { fprintf(stderr, "cannot open %s\n", screenshotPath); return 1; }
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            fwrite(flipped.data(), 1, flipped.size(), f);
            fclose(f);
            printf("screenshot written to %s (%dx%d)\n", screenshotPath, W, H);
            return 0;
        }
        ++frameCount;

        glfwSwapBuffers(g.window);
        glfwPollEvents();
    }

    glfwDestroyWindow(g.window);
    glfwTerminate();
    return 0;
}
