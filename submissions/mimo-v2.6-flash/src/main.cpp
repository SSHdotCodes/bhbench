// Black hole renderer: realtime gravitational lensing, accretion disk + halo,
// and the Flamm-paraboloid spacetime sheet.
//
// Modes
//   1  ray-traced Schwarzschild lensing (disk, halo, star field)
//   2  spacetime curvature sheet ("trapdoor"), Flamm paraboloid + infall demo
//   3  null-geodesic gallery: exact photon orbits vs. flat-space references
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "image_io.hpp"
#include "math3d.hpp"
#include "physics.hpp"
#include "scene.hpp"
#include "selftest.hpp"
#include "shader.hpp"

namespace {

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct Options {
    int width = 0, height = 0;      // 0 = auto
    float scale = 0.75f;            // ray-trace render scale
    bool headless = false;
    int frames = 3;
    std::string out = "capture.png";
    bool selftest = false;
    int mode = 1;
    float dist = 50.f, yaw = 0.85f, pitch = 0.45f;
    float exposure = 0.3f;
    int steps = 640;
    double mass = 10.0;             // solar masses
    double mdot = 1e-7;             // solar masses per year
    bool physicalColors = false;
    bool halo = true, disk = true, stars = true;
    bool doppler = true;
    bool camSet = false;           // user passed --dist/--yaw/--pitch
    float tempScale = -1.f;         // < 0 => derived from the physical peak temperature
    std::string shaderDir;
};

void printUsage() {
    std::printf(
        "blackhole [options]\n"
        "  --headless           render offscreen and save a screenshot, no window\n"
        "  --frames N           frames to render before capturing (default 3)\n"
        "  --out FILE.png       capture path\n"
        "  --width W --height H window / capture size (default: fit display)\n"
        "  --scale S            ray-trace resolution scale, 0.25..1.5 (default 0.75)\n"
        "  --mode N             start in mode 1, 2 or 3\n"
        "  --dist D --yaw Y --pitch P   camera (radians, lengths in M)\n"
        "  --exposure E         tone-mapping exposure\n"
        "  --steps N            max RK4 steps per ray (default 640)\n"
        "  --mass M             black hole mass in solar masses (default 10)\n"
        "  --mdot X             accretion rate in Msun/yr (default 1e-7)\n"
        "  --temp-scale T       display temperature mapping divisor (default: auto)\n"
        "  --physical           use true physical colours (no display mapping)\n"
        "  --no-halo --no-disk --no-stars --no-doppler\n"
        "  --shaders DIR        GLSL directory\n"
        "  --selftest           run the physics self test and exit\n");
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](double& v) { if (i + 1 < argc) v = std::atof(argv[++i]); };
        auto nexti = [&](int& v) { if (i + 1 < argc) v = std::atoi(argv[++i]); };
        auto nexts = [&](std::string& v) { if (i + 1 < argc) v = argv[++i]; };
        if (a == "--headless") o.headless = true;
        else if (a == "--selftest") o.selftest = true;
        else if (a == "--physical") o.physicalColors = true;
        else if (a == "--no-halo") o.halo = false;
        else if (a == "--no-disk") o.disk = false;
        else if (a == "--no-stars") o.stars = false;
        else if (a == "--no-doppler") o.doppler = false;
        else if (a == "--frames") nexti(o.frames);
        else if (a == "--width") nexti(o.width);
        else if (a == "--height") nexti(o.height);
        else if (a == "--mode") nexti(o.mode);
        else if (a == "--steps") nexti(o.steps);
        else if (a == "--out") nexts(o.out);
        else if (a == "--shaders") nexts(o.shaderDir);
        else if (a == "--scale") { double v = o.scale; next(v); o.scale = float(v); }
        else if (a == "--dist") { double v = o.dist; next(v); o.dist = float(v); o.camSet = true; }
        else if (a == "--yaw") { double v = o.yaw; next(v); o.yaw = float(v); o.camSet = true; }
        else if (a == "--pitch") { double v = o.pitch; next(v); o.pitch = float(v); o.camSet = true; }
        else if (a == "--exposure") { double v = o.exposure; next(v); o.exposure = float(v); }
        else if (a == "--mass") next(o.mass);
        else if (a == "--mdot") next(o.mdot);
        else if (a == "--temp-scale") { double v = -1; next(v); o.tempScale = float(v); }
        else if (a == "-h" || a == "--help") { printUsage(); std::exit(0); }
        else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); printUsage(); std::exit(2); }
    }
    if (o.mode < 1) o.mode = 1;
    if (o.mode > 3) o.mode = 3;
    if (o.frames < 1) o.frames = 1;
    return o;
}

std::string findShaderDir(const Options& o) {
    std::vector<std::string> cands;
    if (!o.shaderDir.empty()) cands.push_back(o.shaderDir);
    cands.push_back(BH_SHADER_DIR);
    cands.push_back("shaders");
    cands.push_back("black-hole-cpp-mimolocalfast/shaders");
    cands.push_back("../shaders");
    for (const auto& c : cands) {
        if (bh::readTextFile(c + "/fullscreen.vert").size() > 10) return c;
    }
    return o.shaderDir.empty() ? std::string(BH_SHADER_DIR) : o.shaderDir;
}

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------
struct RT {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool valid() const { return fbo != 0; }
};

void destroyRT(RT& rt);

bool makeRT(RT& rt, int w, int h, bool hdr) {
    destroyRT(rt);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    glGenTextures(1, &rt.tex);
    glBindTexture(GL_TEXTURE_2D, rt.tex);
    GLenum fmt = hdr ? GL_RGBA16F : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), w, h, 0, GL_RGBA,
                 hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &rt.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { destroyRT(rt); return false; }
    rt.w = w;
    rt.h = h;
    return true;
}

void destroyRT(RT& rt) {
    if (rt.fbo) glDeleteFramebuffers(1, &rt.fbo);
    if (rt.tex) glDeleteTextures(1, &rt.tex);
    rt = RT{};
}

void bindRT(const RT* rt) {
    if (rt) {
        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        glViewport(0, 0, rt->w, rt->h);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct App {
    GLFWwindow* win = nullptr;
    int fbw = 0, fbh = 0;
    Options opt;
    std::string shaderDir;
    bh::DiskParams diskParams;

    bh::Program rayProg, gridProg, lineProg, brightProg, blurProg, compProg;
    GLuint triVAO = 0, triVBO = 0;
    GLuint bbTex = 0;

    RT hdr, bloomA, bloomB;
    bool hdrOK = false;

    // mode 2 meshes
    bh::Mesh flammSurf, flammMirror, flammLines;
    bh::Mesh ringHorizon, ringPhoton, ringIsco;
    bh::Mesh trailA, trailB, ptA, ptB;
    // mode 3
    bh::GeoScene geo;

    // per-mode camera: index 0 = mode 1, 1 = mode 2, 2 = mode 3
    float dist[3] = {50.f, 105.f, 45.f};
    float yaw[3] = {0.85f, 0.55f, 0.f};
    float pitch[3] = {0.45f, 0.52f, 0.f};
    float orthoZoom = 1.f;

    int mode = 1;
    float exposure = 1.f;
    float scale = 0.75f;
    int steps = 640;
    bool showHalo = true, showDisk = true, showStars = true, showBridge = false;
    bool doppler = true;
    bool paused = false;
    bool physicalColors = false;
    float tempScale = 1.f;
    float time = 0.f;

    // input
    bool dragging = false;
    double lastX = 0, lastY = 0;
    bool dragged = false;
};

App g;

const char* kHelp =
    "keys:  1 lensing view | 2 spacetime sheet | 3 geodesic gallery\n"
    "       drag: orbit    scroll: zoom    arrows: yaw/pitch   W/S: distance\n"
    "       [ ]: render scale   ; ': ray steps   +/-: exposure\n"
    "       T: physical/display colours   H/K/N: halo/disk/stars   B: bridge\n"
    "       Space: pause   R: reset camera   Esc: quit\n";

// ---------------------------------------------------------------------------
// Fullscreen triangle
// ---------------------------------------------------------------------------
void drawFullscreen() {
    glBindVertexArray(g.triVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void drawMesh(const bh::Mesh& m) {
    if (m.valid()) m.draw();
}

// ---------------------------------------------------------------------------
// Mode 1: ray-traced lensing
// ---------------------------------------------------------------------------
void setRayUniforms(const bh::Program& p, float aspect) {
    (void)aspect;
    float cp = std::cos(g.pitch[g.mode - 1]), sp = std::sin(g.pitch[g.mode - 1]);
    float cy = std::cos(g.yaw[g.mode - 1]), sy = std::sin(g.yaw[g.mode - 1]);
    bh::Vec3 pos(g.dist[g.mode - 1] * cp * cy, g.dist[g.mode - 1] * cp * sy,
                 g.dist[g.mode - 1] * sp);
    bh::Vec3 fwd = bh::normalize(bh::Vec3(0, 0, 0) - pos);
    bh::Vec3 right = bh::normalize(bh::cross(fwd, bh::Vec3(0, 0, 1)));
    bh::Vec3 up = bh::cross(right, fwd);

    p.set("uRes", float(g.fbw), float(g.fbh));
    p.set("uTime", g.time);
    p.set("uCamPos", pos.x, pos.y, pos.z);
    p.set("uCamRight", right.x, right.y, right.z);
    p.set("uCamUp", up.x, up.y, up.z);
    p.set("uCamFwd", fwd.x, fwd.y, fwd.z);
    p.set("uTanHalfFov", std::tan(0.5f * (50.0f * 3.14159265f / 180.0f)));
    p.set("uMaxSteps", g.steps);
    p.set("uStepMax", 0.2f);
    p.set("uRFar", 300.0f);
    p.set("uDiskInner", float(bh::R_ISCO));
    p.set("uDiskOuter", 24.0f);
    p.set("uHaloOuter", 22.0f);
    p.set("uHaloH0", 0.04f);
    p.set("uHaloFlare", 0.30f);
    p.set("uHaloEmis", 0.09f);
    p.set("uHaloOpacity", 3.0f);   // optically thin: tau ~ 0.3 across the atmosphere
    p.set("uF0", float(g.diskParams.F0));
    p.set("uFPeak", float(g.diskParams.FPeak));
    p.set("uTempScale", g.tempScale);
    p.set("uTurb", 0.26f);
    p.set("uBgScale", 3.0f);   // compensate the lower exposure
    p.set("uShowDisk", g.showDisk ? 1 : 0);
    p.set("uShowHalo", g.showHalo ? 1 : 0);
    p.set("uShowStars", g.showStars ? 1 : 0);
    p.set("uDoppler", g.doppler ? 1 : 0);
    p.set("uBB", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.bbTex);
}

void renderRaytrace() {
    // HDR ray tracing pass
    bindRT(&g.hdr);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    g.rayProg.use();
    setRayUniforms(g.rayProg, float(g.hdr.w) / float(g.hdr.h));
    drawFullscreen();

    if (g.hdrOK) {
        // bright pass + 4x downsample
        bindRT(&g.bloomA);
        g.brightProg.use();
        g.brightProg.set("uTex", 0);
        g.brightProg.set("uThreshold", 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g.hdr.tex);
        drawFullscreen();

        // separable gaussian blur
        bindRT(&g.bloomB);
        g.blurProg.use();
        g.blurProg.set("uTex", 0);
        g.blurProg.set("uDir", 1.f / float(g.bloomB.w), 0.f);
        glBindTexture(GL_TEXTURE_2D, g.bloomA.tex);
        drawFullscreen();

        bindRT(&g.bloomA);
        g.blurProg.set("uDir", 0.f, 1.f / float(g.bloomA.h));
        glBindTexture(GL_TEXTURE_2D, g.bloomB.tex);
        drawFullscreen();
    }

    // composite to the window
    bindRT(nullptr);
    glViewport(0, 0, g.fbw, g.fbh);
    g.compProg.use();
    g.compProg.set("uHDR", 0);
    g.compProg.set("uBloom", 1);
    g.compProg.set("uExposure", g.exposure);
    g.compProg.set("uBloomStrength", g.hdrOK ? 0.55f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.hdr.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g.hdrOK ? g.bloomA.tex : g.hdr.tex);
    glActiveTexture(GL_TEXTURE0);
    drawFullscreen();
}

// ---------------------------------------------------------------------------
// Mode 2: spacetime curvature sheet
// ---------------------------------------------------------------------------
bh::Mat4 gridMVP(float aspect) {
    bh::Mat4 proj = bh::perspective(50.f * 3.14159265f / 180.f, aspect, 0.5f, 2000.f);
    float cp = std::cos(g.pitch[1]), sp = std::sin(g.pitch[1]);
    float cy = std::cos(g.yaw[1]), sy = std::sin(g.yaw[1]);
    bh::Vec3 pos(g.dist[1] * cp * cy, g.dist[1] * cp * sy, g.dist[1] * sp);
    bh::Mat4 view = bh::lookAt(pos, bh::Vec3(0, 0, 0), bh::Vec3(0, 0, 1));
    return bh::mul(proj, view);
}

void renderGrid(float aspect) {
    bh::Mat4 mvp = gridMVP(aspect);
    float cp = std::cos(g.pitch[1]), sp = std::sin(g.pitch[1]);
    float cy = std::cos(g.yaw[1]), sy = std::sin(g.yaw[1]);
    bh::Vec3 camPos(g.dist[1] * cp * cy, g.dist[1] * cp * sy, g.dist[1] * sp);
    glViewport(0, 0, g.fbw, g.fbh);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glClearColor(0.010f, 0.012f, 0.020f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    g.gridProg.use();
    g.gridProg.set("uMVP", mvp.data());
    g.gridProg.set("uLightDir", 0.4f, 0.75f, 0.55f);
    g.gridProg.set("uCamPos", camPos.x, camPos.y, camPos.z);
    if (g.showBridge) drawMesh(g.flammMirror);
    drawMesh(g.flammSurf);

    // additive highlights
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    g.lineProg.use();
    g.lineProg.set("uMVP", mvp.data());
    g.lineProg.set("uPointSizeMul", float(g.fbh) / 900.f);
    g.lineProg.set("uRoundPoints", 0.f);
    drawMesh(g.flammLines);
    drawMesh(g.ringPhoton);
    drawMesh(g.ringIsco);
    drawMesh(g.ringHorizon);
    drawMesh(g.trailA);
    drawMesh(g.trailB);
    g.lineProg.set("uRoundPoints", 1.f);
    drawMesh(g.ptA);
    drawMesh(g.ptB);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Mode 3: null-geodesic gallery
// ---------------------------------------------------------------------------
void renderGeo(float aspect) {
    float half = 48.f / g.orthoZoom;
    bh::Mat4 mvp = bh::ortho(-half * aspect, half * aspect, -half, half, -10.f, 10.f);
    glViewport(0, 0, g.fbw, g.fbh);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.010f, 0.012f, 0.020f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    g.lineProg.use();
    g.lineProg.set("uMVP", mvp.data());
    g.lineProg.set("uPointSizeMul", 1.f);
    g.lineProg.set("uRoundPoints", 0.f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawMesh(g.geo.shadow);
    drawMesh(g.geo.flat);
    drawMesh(g.geo.circles);
    drawMesh(g.geo.strips);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void renderFrame() {
    float aspect = (g.fbh > 0) ? float(g.fbw) / float(g.fbh) : 1.f;
    if (g.mode == 1) renderRaytrace();
    else if (g.mode == 2) renderGrid(aspect);
    else renderGeo(aspect);
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
void onKey(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(g.win, 1); break;
        case GLFW_KEY_1: g.mode = 1; break;
        case GLFW_KEY_2: g.mode = 2; break;
        case GLFW_KEY_3: g.mode = 3; break;
        case GLFW_KEY_SPACE: g.paused = !g.paused; break;
        case GLFW_KEY_H: g.showHalo = !g.showHalo; break;
        case GLFW_KEY_K: g.showDisk = !g.showDisk; break;
        case GLFW_KEY_N: g.showStars = !g.showStars; break;
        case GLFW_KEY_B: g.showBridge = !g.showBridge; break;
        case GLFW_KEY_T: g.physicalColors = !g.physicalColors; break;
        case GLFW_KEY_R:
            g.yaw[0] = 0.85f; g.pitch[0] = 0.45f; g.dist[0] = 50.f;
            g.yaw[1] = 0.55f; g.pitch[1] = 0.52f; g.dist[1] = 105.f;
            g.orthoZoom = 1.f;
            break;
        case GLFW_KEY_LEFT_BRACKET: g.scale = std::fmax(0.25f, g.scale - 0.1f); break;
        case GLFW_KEY_RIGHT_BRACKET: g.scale = std::fmin(1.5f, g.scale + 0.1f); break;
        case GLFW_KEY_SEMICOLON: g.steps = std::max(160, g.steps - 80); break;
        case GLFW_KEY_APOSTROPHE: g.steps = std::min(4000, g.steps + 80); break;
        case GLFW_KEY_MINUS: case GLFW_KEY_KP_SUBTRACT:
            g.exposure = std::fmax(0.05f, g.exposure * 0.85f); break;
        case GLFW_KEY_EQUAL: case GLFW_KEY_KP_ADD:
            g.exposure = std::fmin(40.f, g.exposure * 1.18f); break;
        default: break;
    }
    g.tempScale = g.physicalColors ? 1.f : g.diskParams.tempScale;
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        g.dragging = (action == GLFW_PRESS);
        if (g.dragging) {
            glfwGetCursorPos(w, &g.lastX, &g.lastY);
            g.dragged = false;
        }
    }
}

void onCursor(GLFWwindow* w, double x, double y) {
    if (!g.dragging) return;
    float dx = float(x - g.lastX), dy = float(y - g.lastY);
    g.lastX = x;
    g.lastY = y;
    if (g.mode == 3) {
        g.orthoZoom = std::fmax(0.2f, std::fmin(8.f, g.orthoZoom * std::exp(-dy * 0.005f)));
        return;
    }
    int i = g.mode - 1;
    g.yaw[i] += dx * 0.006f;
    g.pitch[i] = std::fmax(-1.50f, std::fmin(1.50f, g.pitch[i] + dy * 0.005f));
    // never look exactly along +z (degenerate camera basis)
    if (std::fabs(g.pitch[i]) < 2e-3f) g.pitch[i] = (g.pitch[i] >= 0 ? 2e-3f : -2e-3f);
    g.dragged = true;
}

void onScroll(GLFWwindow*, double, double dy) {
    if (g.mode == 3) {
        g.orthoZoom = std::fmax(0.2f, std::fmin(8.f, g.orthoZoom * std::exp(float(dy) * 0.12f)));
        return;
    }
    int i = g.mode - 1;
    g.dist[i] = std::fmax(g.mode == 1 ? 3.2f : 8.f,
                          std::fmin(400.f, g.dist[i] * std::exp(-float(dy) * 0.12f)));
}

void pollContinuous(float dt) {
    if (g.mode == 3) return;
    int i = g.mode - 1;
    float rot = 1.4f * dt, zoom = std::fmax(0.6f, g.dist[i] * 0.9f) * dt;
    if (glfwGetKey(g.win, GLFW_KEY_LEFT) == GLFW_PRESS) g.yaw[i] -= rot;
    if (glfwGetKey(g.win, GLFW_KEY_RIGHT) == GLFW_PRESS) g.yaw[i] += rot;
    if (glfwGetKey(g.win, GLFW_KEY_UP) == GLFW_PRESS)
        g.pitch[i] = std::fmin(1.50f, g.pitch[i] + rot);
    if (glfwGetKey(g.win, GLFW_KEY_DOWN) == GLFW_PRESS)
        g.pitch[i] = std::fmax(-1.50f, g.pitch[i] - rot);
    if (glfwGetKey(g.win, GLFW_KEY_W) == GLFW_PRESS)
        g.dist[i] = std::fmax(g.mode == 1 ? 3.2f : 8.f, g.dist[i] - zoom);
    if (glfwGetKey(g.win, GLFW_KEY_S) == GLFW_PRESS)
        g.dist[i] = std::fmin(400.f, g.dist[i] + zoom);
}

// ---------------------------------------------------------------------------
// Particle trails (mode 2)
// ---------------------------------------------------------------------------
void buildInfallDemo() {
    static std::vector<double> table;
    if (table.empty()) bh::infallCoordinateTimeTable(40.0, 0.05, 1000, table);

    auto pointAt = [](double r, float phi) {
        double z = bh::flammZ(std::fmax(r, bh::RS));
        bh::LineVtx v{};
        v.p[0] = float(r * std::cos(double(phi)));
        v.p[1] = float(r * std::sin(double(phi)));
        v.p[2] = float(z);
        v.size = 1.f;
        return v;
    };

    const float phiA = 0.55f, phiB = -0.75f;
    const float r0 = 40.0f;

    // ---- particle A: proper time ----
    {
        const float cycle = 7.0f;
        float ph = std::fmod(g.time, cycle) / cycle;
        double tau = ph * ((2.0 / 3.0) * (std::pow(double(r0), 1.5) -
                                          std::pow(double(bh::RS), 1.5)) /
                           std::sqrt(double(bh::RS)));
        double rNow = (ph >= 0.999f) ? bh::RS : bh::infallProperRadius(tau, double(r0));
        int n = int(g.trailA.count);
        size_t nn = size_t(n);
        std::vector<bh::LineVtx> tr(nn);
        for (int i = 0; i < n; ++i) {
            float f = float(i) / float(n - 1);
            float tt = ph * f;
            double t2 = tt * ((2.0 / 3.0) * (std::pow(double(r0), 1.5) -
                                              std::pow(double(bh::RS), 1.5)) /
                              std::sqrt(double(bh::RS)));
            double rr = bh::infallProperRadius(t2, double(r0));
            tr[size_t(i)] = pointAt(rr, phiA);
            float a = 0.55f * f * f;
            tr[size_t(i)].c[0] = 0.25f * a; tr[size_t(i)].c[1] = 0.85f * a;
            tr[size_t(i)].c[2] = 1.00f * a; tr[size_t(i)].c[3] = a;
        }
        g.trailA.update(tr.data(), tr.size() * sizeof(bh::LineVtx));

        bh::LineVtx p = pointAt(rNow, phiA);
        p.c[0] = 0.45f; p.c[1] = 0.95f; p.c[2] = 1.0f; p.c[3] = 1.0f;
        p.size = 13.f;
        g.ptA.update(&p, sizeof p);
    }

    // ---- particle B: Schwarzschild coordinate time ----
    {
        const float cycle = 9.0f;
        float ph = std::fmod(g.time + 1.7f, cycle) / cycle;
        double f = (ph * 60.0) / 50.0;  // table covers t in [0,50] M with dt = 0.05
        if (f > 1.0) f = 1.0;
        double idx = f * 1000.0;
        int i0 = int(idx);
        if (i0 > 999) i0 = 999;
        double fr = idx - i0;
        double rNow = table[size_t(i0)] * (1.0 - fr) + table[size_t(i0) + 1] * fr;
        if (rNow < bh::RS) rNow = bh::RS;

        int n = int(g.trailB.count);
        size_t nn = size_t(n);
        std::vector<bh::LineVtx> tr(nn);
        for (int i = 0; i < n; ++i) {
            float f2 = float(i) / float(n - 1);
            double fi = f * 1000.0 * f2;
            int j0 = int(fi);
            if (j0 > 999) j0 = 999;
            double fr2 = fi - j0;
            double rr = table[size_t(j0)] * (1.0 - fr2) + table[size_t(j0) + 1] * fr2;
            if (rr < bh::RS) rr = bh::RS;
            tr[size_t(i)] = pointAt(rr, phiB);
            float a = 0.55f * f2 * f2;
            tr[size_t(i)].c[0] = 1.00f * a; tr[size_t(i)].c[1] = 0.62f * a;
            tr[size_t(i)].c[2] = 0.18f * a; tr[size_t(i)].c[3] = a;
        }
        g.trailB.update(tr.data(), tr.size() * sizeof(bh::LineVtx));

        bh::LineVtx p = pointAt(rNow, phiB);
        p.c[0] = 1.0f; p.c[1] = 0.72f; p.c[2] = 0.25f; p.c[3] = 1.0f;
        p.size = 13.f;
        g.ptB.update(&p, sizeof p);
    }
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------
bool capture(const std::string& path) {
    std::vector<uint8_t> buf(size_t(g.fbw) * size_t(g.fbh) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, g.fbw, g.fbh, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    // flip vertically (OpenGL reads bottom-up)
    std::vector<uint8_t> row(size_t(g.fbw) * 3);
    for (int y = 0; y < g.fbh / 2; ++y) {
        uint8_t* a = buf.data() + size_t(y) * size_t(g.fbw) * 3;
        uint8_t* b = buf.data() + size_t(g.fbh - 1 - y) * size_t(g.fbw) * 3;
        std::memcpy(row.data(), a, row.size());
        std::memcpy(a, b, row.size());
        std::memcpy(b, row.data(), row.size());
    }
    return bh::writeImage(path, g.fbw, g.fbh, buf.data());
}

// ---------------------------------------------------------------------------
// GL setup
// ---------------------------------------------------------------------------
bool loadPrograms() {
    const std::string& d = g.shaderDir;
    bool ok = true;
    ok &= g.rayProg.load(d + "/fullscreen.vert", d + "/raytrace.frag");
    ok &= g.gridProg.load(d + "/grid.vert", d + "/grid.frag");
    ok &= g.lineProg.load(d + "/lines.vert", d + "/lines.frag");
    ok &= g.brightProg.load(d + "/fullscreen.vert", d + "/brightpass.frag");
    ok &= g.blurProg.load(d + "/fullscreen.vert", d + "/blur.frag");
    ok &= g.compProg.load(d + "/fullscreen.vert", d + "/composite.frag");
    return ok;
}

void makeFullscreenTriangle() {
    const float tri[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    glGenVertexArrays(1, &g.triVAO);
    glBindVertexArray(g.triVAO);
    glGenBuffers(1, &g.triVBO);
    glBindBuffer(GL_ARRAY_BUFFER, g.triVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void makeBlackbodyTexture() {
    const int N = 1024;
    std::vector<float> lut = bh::buildBlackbodyLUT(N);
    glGenTextures(1, &g.bbTex);
    glBindTexture(GL_TEXTURE_2D, g.bbTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, N, 1, 0, GL_RGBA, GL_FLOAT, lut.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool recreateTargets() {
    int w = int(std::lround(float(g.fbw) * g.scale));
    int h = int(std::lround(float(g.fbh) * g.scale));
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    g.hdrOK = makeRT(g.hdr, w, h, true);
    if (g.hdrOK) {
        int bw = std::max(4, w / 4), bh = std::max(4, h / 4);
        if (!makeRT(g.bloomA, bw, bh, true) || !makeRT(g.bloomB, bw, bh, true)) g.hdrOK = false;
    }
    if (!g.hdrOK) makeRT(g.hdr, w, h, false);  // LDR fallback
    return g.hdr.valid();
}

void buildScene() {
    g.flammSurf = bh::buildFlammSurface(false, 150, 192);
    g.flammMirror = bh::buildFlammSurface(true, 110, 128);
    g.flammLines = bh::buildFlammGridLines(56, 150, 34);

    const float hor[4] = {1.00f, 0.35f, 0.25f, 1.00f};
    const float pho[4] = {1.00f, 0.72f, 0.20f, 0.85f};
    const float isco[4] = {0.35f, 1.00f, 0.55f, 0.75f};
    g.ringHorizon = bh::buildCircle(bh::RS, 0.0, hor, 192);
    g.ringPhoton = bh::buildCircle(bh::R_PHOTON, bh::flammZ(bh::R_PHOTON), pho, 160);
    g.ringIsco = bh::buildCircle(bh::R_ISCO, bh::flammZ(bh::R_ISCO), isco, 160);

    g.trailA = bh::buildDynamicStrip(96);
    g.trailB = bh::buildDynamicStrip(96);
    g.ptA = bh::buildSinglePoint(13.f, pho);
    g.ptB = bh::buildSinglePoint(13.f, isco);
    g.geo = bh::buildGeoScene();
}

// ---------------------------------------------------------------------------
}  // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.selftest) return bh::runSelfTests() == 0 ? 0 : 1;

    g.opt = opt;
    g.mode = opt.mode;
    g.scale = opt.scale;
    g.exposure = opt.exposure;
    g.steps = opt.steps;
    g.showHalo = opt.halo;
    g.showDisk = opt.disk;
    g.showStars = opt.stars;
    g.doppler = opt.doppler;
    g.physicalColors = opt.physicalColors;
    g.diskParams = bh::makeDiskParams(opt.mass, opt.mdot, opt.physicalColors);
    g.tempScale = opt.physicalColors ? 1.f
                                     : (opt.tempScale > 0.f ? opt.tempScale
                                                            : float(g.diskParams.tempScale));
    if (opt.camSet) {
        for (int i = 0; i < 3; ++i) {
            g.dist[i] = opt.dist;
            g.yaw[i] = opt.yaw;
            g.pitch[i] = opt.pitch;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, opt.headless ? 0 : 4);
    glfwWindowHint(GLFW_VISIBLE, opt.headless ? GLFW_FALSE : GLFW_TRUE);

    int W = opt.width, H = opt.height;
    if (W <= 0 || H <= 0) {
        if (opt.headless) { W = 1600; H = 900; }
        else {
            GLFWmonitor* m = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = m ? glfwGetVideoMode(m) : nullptr;
            int wa = vm ? vm->width : 1600, ha = vm ? vm->height : 900;
            if (m) {
                int mx, my, mw, mh;
                glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
                wa = mw; ha = mh;
            }
            W = std::max(800, std::min(2400, int(wa * 0.72)));
            H = std::max(560, std::min(1500, int(ha * 0.74)));
        }
    }

    g.win = glfwCreateWindow(W, H, "blackhole", nullptr, nullptr);
    if (!g.win) {
        std::fprintf(stderr, "window creation failed (OpenGL 4.1 core)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g.win);
    glfwSwapInterval(opt.headless ? 0 : 1);
    glfwSetKeyCallback(g.win, onKey);
    glfwSetMouseButtonCallback(g.win, onMouseButton);
    glfwSetCursorPosCallback(g.win, onCursor);
    glfwSetScrollCallback(g.win, onScroll);

#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::fprintf(stderr, "glewInit failed\n"); return 1; }
    glGetError();
#endif

    g.shaderDir = findShaderDir(opt);
    std::printf("shader dir: %s\n", g.shaderDir.c_str());
    if (!loadPrograms()) {
        std::fprintf(stderr, "failed to load shaders\n");
        return 1;
    }
    makeFullscreenTriangle();
    makeBlackbodyTexture();
    buildScene();

    std::printf("\n%s", kHelp);
    std::printf("disk: M = %.4g Msun, Mdot = %.4g Msun/yr, T_peak = %.4g K, display scale = %.4g\n",
                g.diskParams.massMsun, g.diskParams.mdotMsunPerYr, g.diskParams.TPhysPeak,
                g.diskParams.tempScale);

    glfwGetFramebufferSize(g.win, &g.fbw, &g.fbh);
    if (!recreateTargets()) {
        std::fprintf(stderr, "framebuffer setup failed\n");
        return 1;
    }
    buildInfallDemo();

    auto t0 = std::chrono::steady_clock::now();
    double last = 0.0, fpsT = 0.0;
    int fpsN = 0;
    float fps = 0.f;
    int frame = 0;
    int lastW = 0, lastH = 0;
    bool running = true;

    while (running && !glfwWindowShouldClose(g.win)) {
        glfwPollEvents();

        glfwGetFramebufferSize(g.win, &g.fbw, &g.fbh);
        if (g.fbw != lastW || g.fbh != lastH) {
            lastW = g.fbw; lastH = g.fbh;
            if (!recreateTargets()) { std::fprintf(stderr, "realloc failed\n"); return 1; }
        }

        auto now = std::chrono::steady_clock::now();
        double t = std::chrono::duration<double>(now - t0).count();
        float dt = float(t - last);
        last = t;
        if (dt > 0.1f) dt = 0.1f;
        if (!g.paused) g.time += dt;

        pollContinuous(dt);
        if (g.mode == 2) buildInfallDemo();
        renderFrame();

        ++frame;
        ++fpsN;
        if (t - fpsT > 0.5) {
            fps = float(fpsN / (t - fpsT));
            fpsT = t;
            fpsN = 0;
            char title[256];
            const char* nm = g.mode == 1 ? "lensing" : (g.mode == 2 ? "spacetime sheet" : "geodesics");
            std::snprintf(title, sizeof title,
                          "blackhole | [%d] %s | %.0f fps | %dx%d scale %.2f | steps %d | dist %.1fM | exp %.2f",
                          g.mode, nm, fps, g.fbw, g.fbh, g.scale, g.steps,
                          g.dist[g.mode - 1], g.exposure);
            if (!opt.headless) glfwSetWindowTitle(g.win, title);
        }

        if (opt.headless && frame >= opt.frames) {
            capture(opt.out);
            double dt = std::chrono::duration<double>(now - t0).count();
            if (dt > 0.0)
                std::printf("[bench] %d frames at %dx%d (ray scale %.2f) = %.1f fps "
                            " (%.1f ms/frame)\n",
                            frame, g.fbw, g.fbh, g.scale, frame / dt, 1000.0 * dt / frame);
            running = false;
            continue;
        }
        glfwSwapBuffers(g.win);
    }

    g.flammSurf.destroy(); g.flammMirror.destroy(); g.flammLines.destroy();
    g.ringHorizon.destroy(); g.ringPhoton.destroy(); g.ringIsco.destroy();
    g.trailA.destroy(); g.trailB.destroy(); g.ptA.destroy(); g.ptB.destroy();
    g.geo.strips.destroy(); g.geo.flat.destroy(); g.geo.shadow.destroy(); g.geo.circles.destroy();
    destroyRT(g.hdr); destroyRT(g.bloomA); destroyRT(g.bloomB);
    if (g.bbTex) glDeleteTextures(1, &g.bbTex);
    if (g.triVBO) glDeleteBuffers(1, &g.triVBO);
    if (g.triVAO) glDeleteVertexArrays(1, &g.triVAO);
    g.rayProg.destroy(); g.gridProg.destroy(); g.lineProg.destroy();
    g.brightProg.destroy(); g.blurProg.destroy(); g.compProg.destroy();
    glfwDestroyWindow(g.win);
    glfwTerminate();
    return 0;
}
