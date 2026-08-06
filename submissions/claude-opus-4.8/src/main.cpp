// ============================================================================
//  Black Hole — a scientifically-grounded, real-time Schwarzschild simulator.
//
//  Mode 0 : relativistic ray-tracing of the black hole — gravitational lensing
//           of the sky, a Doppler/redshift-shaded accretion disk, the photon
//           ring and the shadow, all from integrating null geodesics per pixel.
//  Mode 1 : the "trapdoor in spacetime" — Flamm's paraboloid, the true spatial
//           geometry of the Schwarzschild slice, with the horizon, the disk and
//           matter spiralling in.
//
//  OpenGL 4.1 core (macOS) + GLFW.  See README.md.
//
//  Usage:
//     ./blackhole                          interactive realtime window
//     ./blackhole --shot out.ppm           render one still (ray-trace) and exit
//     ./blackhole --shot out.ppm --mode 1  render the spacetime-grid still
//     ./blackhole --shot out.ppm --size 1920 1080
// ============================================================================
#include <OpenGL/gl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "glmath.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// ----------------------------------------------------------------------------
//  Physical constants (units: Schwarzschild radius rs = 1, so M = 0.5).
// ----------------------------------------------------------------------------
static const float RS         = 1.0f;
static const float MASS       = 0.5f;
static const float DISK_INNER = 3.0f;    // ISCO = 6M = 3 rs
static const float DISK_OUTER = 10.0f;
static const float RMAX_GRID  = 32.0f;   // outer extent of the embedding diagram

// ----------------------------------------------------------------------------
//  Global state
// ----------------------------------------------------------------------------
struct CamState { float az, el, dist; };
static CamState gCam[4] = {
    {0.62f, 0.22f, 25.0f},   // mode 0: low elevation -> dramatic edge-on disk
    {0.70f, 0.52f, 46.0f},   // mode 1: 3/4 view of the funnel
    {0.30f, 1.28f, 42.0f},   // mode 2: near top-down, single-ray bending
    {0.85f, 0.62f, 48.0f},   // mode 3: 3/4 view of the tidal disruption
};
static const CamState gCamDefault[4] = {
    {0.62f, 0.22f, 25.0f}, {0.70f, 0.52f, 46.0f}, {0.30f, 1.28f, 42.0f}, {0.85f, 0.62f, 48.0f} };

static int   gMode        = 0;       // 0 ray-trace, 1 grid, 2 ray bending, 3 tidal disruption
static bool  gTdeRestart  = false;   // request a fresh star (set by key N)
static bool  gShowDisk    = true;
static int   gBgMode      = 0;       // 0 stars, 1 lensing grid
static int   gSteps       = 320;     // geodesic integration steps
static float gScale       = 0.80f;   // internal render scale (perf knob)
static float gExposure    = 1.0f;
static bool  gPaused      = false;

static bool   gDragging   = false;
static double gLastX = 0, gLastY = 0;
static double gSimTime    = 0.0;     // animation clock (pausable)

// Mode 2 — single light ray bending
static std::vector<Vec3> gPath;          // precomputed geodesic
static float  gRayB        = 3.2f;       // impact parameter (rs)
static float  gRayS        = 0.0f;       // animation head (index into gPath)
static float  gRayDuration = 7.5f;       // seconds to traverse the whole path (slow-mo)
static float  gRayHold     = 0.0f;       // pause timer at end of a run
static bool   gRayAuto     = true;       // auto-cycle through impact parameters
static bool   gRayDirty    = true;       // path needs rebuilding
static bool   gRayCaptured = false;
static float  gRayDeflect  = 0.0f;       // total deflection angle (deg)
static int    gRayCycleIdx = 0;
static const float gRayCycle[] = { 8.0f, 5.0f, 3.5f, 2.75f, 2.598f, 2.2f };
static const int   gRayCycleN  = 6;

// ----------------------------------------------------------------------------
//  Embedding-diagram geometry helpers (Flamm's paraboloid)
//    z(r) = 2 sqrt(rs (r - rs)),   oriented as a downward funnel.
// ----------------------------------------------------------------------------
static float embedZ(float r) { return 2.0f * std::sqrt(RS * std::max(r - RS, 0.0f)); }
static const float Z_MAX  = 2.0f * std::sqrt(RS * (RMAX_GRID - RS));
static const float Y_SCALE = 1.15f;
static float funnelY(float r) { return (embedZ(r) - Z_MAX) * Y_SCALE; }  // <= 0
static Vec3  surf(float r, float th) { return Vec3(r * std::cos(th), funnelY(r), r * std::sin(th)); }

// Blackbody color (matches the GLSL version), T in Kelvin.
static Vec3 blackbody(float T) {
    T = (T < 1000.f ? 1000.f : (T > 40000.f ? 40000.f : T)) / 100.f;
    float r, g, b;
    if (T <= 66.f) r = 255.f; else r = 329.698727446f * std::pow(T - 60.f, -0.1332047592f);
    if (T <= 66.f) g = 99.4708025861f * std::log(T) - 161.1195681661f;
    else           g = 288.1221695283f * std::pow(T - 60.f, -0.0755148492f);
    float bb;
    if (T >= 66.f) bb = 255.f;
    else if (T <= 19.f) bb = 0.f;
    else bb = 138.5177312231f * std::log(T - 10.f) - 305.0447927307f;
    b = bb;
    auto c = [](float v){ return v < 0 ? 0 : (v > 255 ? 255 : v) / 255.f; };
    return Vec3(c(r), c(g), c(b));
}

// ----------------------------------------------------------------------------
//  GL helpers
// ----------------------------------------------------------------------------
static std::string readFile(const std::string &path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "ERROR: cannot open shader %s\n", path.c_str()); std::exit(1); }
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static GLuint compile(GLenum type, const std::string &src, const char *name) {
    GLuint s = glCreateShader(type);
    const char *c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error (%s):\n%s\n", name, log);
        std::exit(1);
    }
    return s;
}
static GLuint makeProgram(const char *vsFile, const char *fsFile) {
    std::string dir = SHADER_DIR;
    GLuint vs = compile(GL_VERTEX_SHADER,   readFile(dir + "/" + vsFile), vsFile);
    GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(dir + "/" + fsFile), fsFile);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
               std::fprintf(stderr, "Link error:\n%s\n", log); std::exit(1); }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}
static void u1f(GLuint p, const char *n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
static void u1i(GLuint p, const char *n, int v)   { glUniform1i(glGetUniformLocation(p, n), v); }
static void u2f(GLuint p, const char *n, float a, float b) { glUniform2f(glGetUniformLocation(p, n), a, b); }
static void u3f(GLuint p, const char *n, Vec3 v)  { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }
static void uM4(GLuint p, const char *n, const Mat4 &m) { glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, m.m); }

// A static interleaved mesh: position(3) + color(3).
struct Mesh { GLuint vao = 0, vbo = 0; GLsizei count = 0; GLenum mode = GL_TRIANGLES; };
static Mesh makeMesh(const std::vector<float> &data, GLenum mode, GLenum usage = GL_STATIC_DRAW) {
    Mesh m; m.mode = mode; m.count = (GLsizei)(data.size() / 6);
    glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.empty() ? nullptr : data.data(), usage);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glBindVertexArray(0);
    return m;
}
static void push(std::vector<float> &v, Vec3 p, Vec3 c) {
    v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
    v.push_back(c.x); v.push_back(c.y); v.push_back(c.z);
}
static Mesh makeDynamicMesh(int maxVerts, GLenum mode) {
    Mesh m; m.mode = mode; m.count = 0;
    glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)maxVerts * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glBindVertexArray(0);
    return m;
}
static void updateMesh(Mesh &m, const std::vector<float> &data) {
    if (data.empty()) { m.count = 0; return; }
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * sizeof(float), data.data());
    m.count = (GLsizei)(data.size() / 6);
}

// ----------------------------------------------------------------------------
//  Build the embedding-diagram geometry
// ----------------------------------------------------------------------------
static Vec3 funnelColor(float r) {
    float depthN = (Z_MAX - embedZ(r)) / Z_MAX;           // 1 at throat, 0 at rim
    Vec3 rim(0.10f, 0.17f, 0.36f), thr(0.30f, 0.92f, 1.0f);
    float t = std::pow(depthN, 1.3f);
    return rim * (1.0f - t) + thr * t;
}
static Mesh buildFlammWire() {
    const int nRings = 48, nSeg = 128, nSpokes = 64;
    std::vector<float> v; v.reserve(nRings * nSeg * 12);
    auto radius = [&](int i) {
        float t = (float)i / (nRings - 1);
        return RS + (RMAX_GRID - RS) * std::pow(t, 2.2f);   // dense near the throat
    };
    for (int i = 0; i < nRings; ++i) {                       // concentric rings
        float r = radius(i); Vec3 col = funnelColor(r);
        for (int j = 0; j < nSeg; ++j) {
            float t0 = (float)j / nSeg * 6.2831853f, t1 = (float)(j + 1) / nSeg * 6.2831853f;
            push(v, surf(r, t0), col); push(v, surf(r, t1), col);
        }
    }
    for (int s = 0; s < nSpokes; ++s) {                     // radial spokes
        float th = (float)s / nSpokes * 6.2831853f;
        for (int i = 0; i < nRings - 1; ++i) {
            push(v, surf(radius(i), th),     funnelColor(radius(i)));
            push(v, surf(radius(i + 1), th), funnelColor(radius(i + 1)));
        }
    }
    return makeMesh(v, GL_LINES);
}
static Mesh buildHorizon() {                                 // black disk at the throat
    const int n = 96; std::vector<float> v;
    Vec3 c0(0, 0, 0); float y = funnelY(RS);
    push(v, Vec3(0, y, 0), c0);
    for (int j = 0; j <= n; ++j) {
        float th = (float)j / n * 6.2831853f;
        push(v, Vec3(RS * std::cos(th), y, RS * std::sin(th)), c0);
    }
    return makeMesh(v, GL_TRIANGLE_FAN);
}
static Mesh buildRim() {                                     // bright photon-ring hint
    const int n = 128; std::vector<float> v;
    Vec3 c(1.0f, 0.6f, 0.3f);
    for (int j = 0; j < n; ++j) push(v, surf(RS * 1.02f, (float)j / n * 6.2831853f), c);
    return makeMesh(v, GL_LINE_LOOP);
}
static Mesh buildDiskBand() {                                // glowing disk lying on the funnel
    const int nR = 18, nT = 160; std::vector<float> v;
    auto col = [&](float r) {
        float xin = DISK_INNER / r;
        float prof = std::pow(xin, 0.75f) * std::pow(std::max(1.f - std::sqrt(xin), 0.f), 0.25f);
        return blackbody(14000.f * prof) * (0.25f + 1.7f * prof);
    };
    for (int i = 0; i < nR; ++i) {
        float r0 = DISK_INNER + (DISK_OUTER - DISK_INNER) * (float)i / nR;
        float r1 = DISK_INNER + (DISK_OUTER - DISK_INNER) * (float)(i + 1) / nR;
        Vec3 c0 = col(r0), c1 = col(r1);
        for (int j = 0; j < nT; ++j) {
            float t0 = (float)j / nT * 6.2831853f, t1 = (float)(j + 1) / nT * 6.2831853f;
            Vec3 a = surf(r0, t0), b = surf(r1, t0), cc = surf(r1, t1), d = surf(r0, t1);
            push(v, a, c0); push(v, b, c1); push(v, cc, c1);
            push(v, a, c0); push(v, cc, c1); push(v, d, c0);
        }
    }
    return makeMesh(v, GL_TRIANGLES);
}

// ----------------------------------------------------------------------------
//  Mode 2: CPU null-geodesic integration for a single light ray.
//  Same physics as the shader: a = -(3/2) rs h^2 r / r^5,  h = |r x v|.
// ----------------------------------------------------------------------------
static Vec3 accelCPU(Vec3 p, Vec3 v) {
    Vec3 h = cross(p, v);
    float h2 = dot(h, h);
    float r = length(p);
    float r5 = r * r * r * r * r;
    return p * (-1.5f * RS * h2 / r5);
}
static void rk4CPU(Vec3 &p, Vec3 &v, float dt) {
    Vec3 k1p = v,                 k1v = accelCPU(p, v);
    Vec3 k2p = v + k1v * (0.5f * dt), k2v = accelCPU(p + k1p * (0.5f * dt), v + k1v * (0.5f * dt));
    Vec3 k3p = v + k2v * (0.5f * dt), k3v = accelCPU(p + k2p * (0.5f * dt), v + k2v * (0.5f * dt));
    Vec3 k4p = v + k3v * dt,       k4v = accelCPU(p + k3p * dt, v + k3v * dt);
    p = p + (k1p + k2p * 2.0f + k3p * 2.0f + k4p) * (dt / 6.0f);
    v = v + (k1v + k2v * 2.0f + k3v * 2.0f + k4v) * (dt / 6.0f);
}
// Integrate a ray entering from -x with perpendicular offset b (impact parameter).
static void integratePhoton(float b, std::vector<Vec3> &path, bool &captured, float &deflectDeg) {
    Vec3 pos(-16.0f, 0.0f, b), vel(1.0f, 0.0f, 0.0f);
    Vec3 vel0 = vel;
    path.clear(); path.push_back(pos); captured = false;
    for (int i = 0; i < 6000; ++i) {
        float r = length(pos);
        if (r < RS * 1.02f) { captured = true; path.push_back(normalize(pos) * RS); break; }
        if (r > 24.0f && i > 4) break;                       // escaped
        float dt = clampf(0.10f * (r - RS), 0.008f, 0.22f);
        rk4CPU(pos, vel, dt);
        path.push_back(pos);
    }
    Vec3 vf = normalize(vel);
    deflectDeg = std::acos(clampf(dot(vel0, vf), -1.0f, 1.0f)) * 180.0f / 3.14159265f;
}

// Flat (equatorial-plane) helpers for the bending scene.
static Mesh buildFlatPolarGrid() {
    std::vector<float> v; Vec3 col(0.06f, 0.13f, 0.22f);
    const int nT = 140, nS = 24;
    for (float r = 2.0f; r <= 22.001f; r += 2.0f)
        for (int j = 0; j < nT; ++j) {
            float t0 = (float)j / nT * 6.2831853f, t1 = (float)(j + 1) / nT * 6.2831853f;
            push(v, Vec3(r * std::cos(t0), 0, r * std::sin(t0)), col);
            push(v, Vec3(r * std::cos(t1), 0, r * std::sin(t1)), col);
        }
    for (int s = 0; s < nS; ++s) {
        float th = (float)s / nS * 6.2831853f;
        push(v, Vec3(2.0f * std::cos(th), 0, 2.0f * std::sin(th)), col);
        push(v, Vec3(22.0f * std::cos(th), 0, 22.0f * std::sin(th)), col);
    }
    return makeMesh(v, GL_LINES);
}
static Mesh buildFlatDisk(float rad, Vec3 c, float y) {
    const int n = 96; std::vector<float> v;
    push(v, Vec3(0, y, 0), c);
    for (int j = 0; j <= n; ++j) {
        float th = (float)j / n * 6.2831853f;
        push(v, Vec3(rad * std::cos(th), y, rad * std::sin(th)), c);
    }
    return makeMesh(v, GL_TRIANGLE_FAN);
}
static Mesh buildCircleLoop(float rad, Vec3 c, float y) {
    const int n = 160; std::vector<float> v;
    for (int j = 0; j < n; ++j) {
        float th = (float)j / n * 6.2831853f;
        push(v, Vec3(rad * std::cos(th), y, rad * std::sin(th)), c);
    }
    return makeMesh(v, GL_LINE_LOOP);
}

// Infalling tracer particles for the grid view.
struct Particle { float r, th; };
static std::vector<Particle> gParticles;
static float frand() { return (float)std::rand() / (float)RAND_MAX; }
static void initParticles(int n) {
    gParticles.resize(n);
    for (auto &p : gParticles) { p.r = 8.f + frand() * (RMAX_GRID - 10.f); p.th = frand() * 6.2831853f; }
}

// ----------------------------------------------------------------------------
//  FBO (HDR render target for the ray tracer)
// ----------------------------------------------------------------------------
struct FBO { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };
static void resizeFBO(FBO &f, int w, int h) {
    if (f.w == w && f.h == h && f.fbo) return;
    if (!f.fbo) { glGenFramebuffers(1, &f.fbo); glGenTextures(1, &f.tex); }
    f.w = w; f.h = h;
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ----------------------------------------------------------------------------
//  Shared render objects (built once in main)
// ----------------------------------------------------------------------------
static GLuint gProgBH = 0, gProgBlit = 0, gProgScene = 0, gEmptyVAO = 0;
static Mesh   gFlamm, gHorizon, gRim, gDisk, gPartMesh;
static std::vector<float> gPartData;
static FBO    gRT;
// Mode 2 meshes
static Mesh gFlatGrid, gHorizonFlat, gRingHorizon, gRingPhoton;
static Mesh gRayFull, gRayTrail, gRayHead, gRayRef;

// ----------------------------------------------------------------------------
//  Input callbacks
// ----------------------------------------------------------------------------
static void onMouseButton(GLFWwindow *w, int b, int action, int) {
    if (b == GLFW_MOUSE_BUTTON_LEFT) { gDragging = (action == GLFW_PRESS); glfwGetCursorPos(w, &gLastX, &gLastY); }
}
static void onCursor(GLFWwindow *, double x, double y) {
    if (!gDragging) return;
    double dx = x - gLastX, dy = y - gLastY; gLastX = x; gLastY = y;
    CamState &c = gCam[gMode];
    c.az -= (float)dx * 0.006f;
    c.el  = clampf(c.el + (float)dy * 0.006f, -1.45f, 1.45f);
}
static void onScroll(GLFWwindow *, double, double dy) {
    CamState &c = gCam[gMode];
    c.dist = clampf(c.dist * std::pow(0.9f, (float)dy), 3.5f, 130.0f);
}
static void onKey(GLFWwindow *w, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_TAB:    gMode = (gMode + 1) % 4; break;
        case GLFW_KEY_1:      gMode = 0; break;
        case GLFW_KEY_2:      gMode = 1; break;
        case GLFW_KEY_3:      gMode = 2; break;
        case GLFW_KEY_4:      gMode = 3; break;
        case GLFW_KEY_N:      if (gMode == 3) gTdeRestart = true; break;
        case GLFW_KEY_UP:     if (gMode == 2) { gRayAuto = false; gRayB = clampf(gRayB + 0.1f, 1.2f, 12.0f); gRayDirty = true; } break;
        case GLFW_KEY_DOWN:   if (gMode == 2) { gRayAuto = false; gRayB = clampf(gRayB - 0.1f, 1.2f, 12.0f); gRayDirty = true; } break;
        case GLFW_KEY_C:      if (gMode == 2) { gRayAuto = true;  gRayDirty = true; } break;
        case GLFW_KEY_D:      gShowDisk = !gShowDisk; break;
        case GLFW_KEY_B:      gBgMode = 1 - gBgMode; break;
        case GLFW_KEY_SPACE:  gPaused = !gPaused; break;
        case GLFW_KEY_R:      gCam[gMode] = gCamDefault[gMode]; break;
        case GLFW_KEY_LEFT_BRACKET:  gScale = clampf(gScale - 0.05f, 0.25f, 1.0f); break;
        case GLFW_KEY_RIGHT_BRACKET: gScale = clampf(gScale + 0.05f, 0.25f, 1.0f); break;
        case GLFW_KEY_MINUS:  gSteps = std::max(60, gSteps - 20); break;
        case GLFW_KEY_EQUAL:  gSteps = std::min(640, gSteps + 20); break;
        case GLFW_KEY_COMMA:  gExposure = clampf(gExposure - 0.1f, 0.2f, 4.0f); break;
        case GLFW_KEY_PERIOD: gExposure = clampf(gExposure + 0.1f, 0.2f, 4.0f); break;
        default: break;
    }
}

static Vec3 camPos(const CamState &c, Vec3 target) {
    Vec3 dir(std::cos(c.el) * std::cos(c.az), std::sin(c.el), std::cos(c.el) * std::sin(c.az));
    return target + dir * c.dist;
}

// ----------------------------------------------------------------------------
//  Render paths (draw into whatever framebuffer is requested)
// ----------------------------------------------------------------------------
static void renderRaytrace(GLuint targetFBO, int outW, int outH) {
    int rtW = std::max(1, (int)(outW * gScale)), rtH = std::max(1, (int)(outH * gScale));
    resizeFBO(gRT, rtW, rtH);

    Vec3 target(0, 0, 0);
    Vec3 eye = camPos(gCam[0], target);
    Vec3 fwd = normalize(target - eye);
    Vec3 right = normalize(cross(fwd, Vec3(0, 1, 0)));
    Vec3 up = cross(right, fwd);
    float tanHalfFov = std::tan(radians(50.0f) * 0.5f);

    glBindFramebuffer(GL_FRAMEBUFFER, gRT.fbo);
    glViewport(0, 0, rtW, rtH);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(gProgBH);
    u2f(gProgBH, "uResolution", (float)rtW, (float)rtH);
    u1f(gProgBH, "uTime", (float)gSimTime);
    u3f(gProgBH, "uCamPos", eye);
    u3f(gProgBH, "uCamRight", right);
    u3f(gProgBH, "uCamUp", up);
    u3f(gProgBH, "uCamFwd", fwd);
    u1f(gProgBH, "uTanHalfFov", tanHalfFov);
    u1i(gProgBH, "uSteps", gSteps);
    u1f(gProgBH, "uDiskInner", DISK_INNER);
    u1f(gProgBH, "uDiskOuter", DISK_OUTER);
    u1i(gProgBH, "uShowDisk", gShowDisk ? 1 : 0);
    u1i(gProgBH, "uBgMode", gBgMode);
    u1f(gProgBH, "uDiskBrightness", 0.7f);
    glBindVertexArray(gEmptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, outW, outH);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glUseProgram(gProgBlit);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRT.tex);
    u1i(gProgBlit, "uTex", 0);
    u2f(gProgBlit, "uTexel", 1.0f / rtW, 1.0f / rtH);
    u1f(gProgBlit, "uExposure", gExposure);
    glBindVertexArray(gEmptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void renderGrid(int outW, int outH, float dt) {
    glViewport(0, 0, outW, outH);
    glClearColor(0.012f, 0.014f, 0.022f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    float aspect = (float)outW / (float)outH;
    Vec3 target(0, funnelY(RS) * 0.45f, 0);
    Vec3 eye = camPos(gCam[1], target);
    Mat4 mvp = mul(perspective(radians(52.0f), aspect, 0.05f, 500.0f), lookAt(eye, target, Vec3(0, 1, 0)));

    glUseProgram(gProgScene);
    uM4(gProgScene, "uMVP", mvp);
    u1f(gProgScene, "uPointSize", 1.0f);
    u1i(gProgScene, "uRound", 0);

    glDisable(GL_BLEND); glDepthMask(GL_TRUE);          // horizon (opaque)
    u1f(gProgScene, "uAlpha", 1.0f);
    glBindVertexArray(gHorizon.vao); glDrawArrays(gHorizon.mode, 0, gHorizon.count);

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // funnel wireframe
    u1f(gProgScene, "uAlpha", 0.85f);
    glBindVertexArray(gFlamm.vao); glDrawArrays(gFlamm.mode, 0, gFlamm.count);

    u1f(gProgScene, "uAlpha", 1.0f);                    // bright rim
    glBindVertexArray(gRim.vao); glDrawArrays(gRim.mode, 0, gRim.count);

    if (gShowDisk) {                                    // disk band (additive)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
        u1f(gProgScene, "uAlpha", 0.9f);
        glBindVertexArray(gDisk.vao); glDrawArrays(gDisk.mode, 0, gDisk.count);
        glDepthMask(GL_TRUE);
    }

    for (auto &p : gParticles) {                        // advance infalling particles
        float omega = std::sqrt(MASS) / std::pow(p.r, 1.5f);
        p.th += omega * dt * 2.2f;
        p.r  -= dt * (0.6f + 2.2f / p.r);
        if (p.r < RS + 0.05f) { p.r = 10.f + frand() * (RMAX_GRID - 12.f); p.th = frand() * 6.2831853f; }
    }
    for (size_t i = 0; i < gParticles.size(); ++i) {
        Vec3 pos = surf(gParticles[i].r, gParticles[i].th); pos.y += 0.15f;
        float close = clampf((RMAX_GRID - gParticles[i].r) / RMAX_GRID, 0.f, 1.f);
        Vec3 c = Vec3(0.6f, 0.8f, 1.0f) * (0.5f + 1.3f * close);
        float *d = &gPartData[i * 6];
        d[0] = pos.x; d[1] = pos.y; d[2] = pos.z; d[3] = c.x; d[4] = c.y; d[5] = c.z;
    }
    glBindBuffer(GL_ARRAY_BUFFER, gPartMesh.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gPartData.size() * sizeof(float), gPartData.data());
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
    u1f(gProgScene, "uPointSize", 7.0f); u1i(gProgScene, "uRound", 1); u1f(gProgScene, "uAlpha", 0.9f);
    glBindVertexArray(gPartMesh.vao); glDrawArrays(GL_POINTS, 0, gPartMesh.count);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

// ----------------------------------------------------------------------------
//  Mode 2 — single light ray bending around the hole (slow-motion).
// ----------------------------------------------------------------------------
static void rebuildRayPath() {
    if (gRayAuto) gRayB = gRayCycle[gRayCycleIdx];
    integratePhoton(gRayB, gPath, gRayCaptured, gRayDeflect);

    std::vector<float> full;                        // dim full trajectory (guide)
    Vec3 dim(0.13f, 0.26f, 0.40f);
    for (auto &p : gPath) push(full, Vec3(p.x, 0.05f, p.z), dim);
    updateMesh(gRayFull, full);

    std::vector<float> ref;                         // undeflected straight reference
    Vec3 rc(0.22f, 0.20f, 0.12f);
    push(ref, Vec3(-16.0f, 0.04f, gRayB), rc);
    push(ref, Vec3( 28.0f, 0.04f, gRayB), rc);
    updateMesh(gRayRef, ref);

    gRayS = 0.0f; gRayHold = 0.0f; gRayDirty = false;
}

static void renderRayBend(int outW, int outH, float dt) {
    int n = (int)gPath.size();
    // advance the animation head (slow-mo)
    if (!gPaused && n > 1) {
        if (gRayHold > 0.0f) {
            gRayHold -= dt;
            if (gRayHold <= 0.0f && gRayAuto) { gRayCycleIdx = (gRayCycleIdx + 1) % gRayCycleN; gRayDirty = true; }
        } else {
            gRayS += dt * (float)(n - 1) / gRayDuration;
            if (gRayS >= n - 1) { gRayS = n - 1; gRayHold = 1.4f; }
        }
    }
    if (gRayDirty) { rebuildRayPath(); n = (int)gPath.size(); }

    glViewport(0, 0, outW, outH);
    glClearColor(0.010f, 0.012f, 0.020f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(2.0f);

    float aspect = (float)outW / (float)outH;
    Vec3 target(0, 0, 0);
    Vec3 eye = camPos(gCam[2], target);
    Mat4 mvp = mul(perspective(radians(52.0f), aspect, 0.05f, 500.0f), lookAt(eye, target, Vec3(0, 1, 0)));

    glUseProgram(gProgScene);
    uM4(gProgScene, "uMVP", mvp);
    u1f(gProgScene, "uPointSize", 1.0f);
    u1i(gProgScene, "uRound", 0);

    // flat reference grid
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    u1f(gProgScene, "uAlpha", 0.5f);
    glBindVertexArray(gFlatGrid.vao); glDrawArrays(gFlatGrid.mode, 0, gFlatGrid.count);

    // event horizon (black) + photon sphere + bright rim
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    u1f(gProgScene, "uAlpha", 1.0f);
    glBindVertexArray(gHorizonFlat.vao); glDrawArrays(gHorizonFlat.mode, 0, gHorizonFlat.count);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
    u1f(gProgScene, "uAlpha", 0.5f);
    glBindVertexArray(gRingPhoton.vao); glDrawArrays(gRingPhoton.mode, 0, gRingPhoton.count);
    u1f(gProgScene, "uAlpha", 1.0f);
    glBindVertexArray(gRingHorizon.vao); glDrawArrays(gRingHorizon.mode, 0, gRingHorizon.count);

    // undeflected reference + dim full path (additive)
    u1f(gProgScene, "uAlpha", 0.5f);
    glBindVertexArray(gRayRef.vao); glDrawArrays(GL_LINES, 0, gRayRef.count);
    u1f(gProgScene, "uAlpha", 0.7f);
    glBindVertexArray(gRayFull.vao); glDrawArrays(GL_LINE_STRIP, 0, gRayFull.count);

    // bright animated trail up to the head, brightness ramping toward the head
    int head = std::min(n - 1, std::max(1, (int)gRayS));
    std::vector<float> trail;
    for (int i = 0; i <= head; ++i) {
        float f = (float)i / (float)head;            // 0 tail .. 1 head
        Vec3 c = Vec3(0.45f, 0.8f, 1.0f) * (0.25f + 0.95f * f);
        push(trail, Vec3(gPath[i].x, 0.07f, gPath[i].z), c);
    }
    updateMesh(gRayTrail, trail);
    u1f(gProgScene, "uAlpha", 1.0f);
    glBindVertexArray(gRayTrail.vao);
    glDrawArrays(GL_LINE_STRIP, 0, gRayTrail.count);          // continuous line
    u1i(gProgScene, "uRound", 1); u1f(gProgScene, "uPointSize", 5.0f);
    glDrawArrays(GL_POINTS, 0, gRayTrail.count);              // glow thickness

    // glowing photon head
    std::vector<float> hd; push(hd, Vec3(gPath[head].x, 0.09f, gPath[head].z), Vec3(1.0f, 1.0f, 1.0f));
    updateMesh(gRayHead, hd);
    u1f(gProgScene, "uPointSize", 16.0f);
    glBindVertexArray(gRayHead.vao); glDrawArrays(GL_POINTS, 0, 1);

    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glLineWidth(1.0f);
}

// ============================================================================
//  Mode 3 — Tidal Disruption Event (a star torn apart -> accretion disk).
//
//  Massive-particle dynamics use the Paczynski-Wiita pseudo-Newtonian potential
//  Phi = -GM/(r - rs), which reproduces the ISCO (3 rs), strong precession, and
//  plunge/capture inside the marginally-bound orbit — all in a fast Newtonian
//  integrator. The star is rigid until it reaches the tidal radius, then each
//  particle is released with the common center-of-mass velocity; the spread in
//  position becomes a spread in orbital energy (bound debris falls back, unbound
//  debris escapes). Mild dissipation circularizes the bound stream into a disk.
// ============================================================================
static const int   TDE_N     = 9000;
static const float TDE_RSTAR = 1.4f;      // star radius (rs)
static const float TDE_RT    = 14.0f;     // tidal radius (disruption trigger)
static const float TDE_PERI  = 3.5f;      // center-of-mass pericenter (rs)
static const float TDE_R0    = 60.0f;     // start distance (rs)
static const float TDE_EAT   = 1.06f;     // capture radius
static float TDE_SPEED       = 65.0f;     // simulation units per real second
static float gTdeKc          = 0.045f;    // radial-velocity damping (circularization)
static float gTdeKy          = 0.050f;    // vertical (disk-flattening) damping
static float gTdeShotT       = 360.0f;    // --tdet: sim time for a still

struct TPart { Vec3 pos, vel; bool dead, bound; };
static std::vector<TPart> gStar;
static std::vector<Vec3>  gOffsets;
static Vec3  gComPos, gComVel;
static bool  gDisrupted = false;
static float gTdeTime = 0.0f, gTimeSinceDisrupt = 0.0f;
static int   gTdeAlive = 0, gTdeEaten = 0;
static std::vector<float> gTdeData;
static Mesh  gTdeMesh, gBHSphere;

static Mesh buildSphere(float rad, Vec3 col) {
    const int nLat = 24, nLon = 32; std::vector<float> v;
    auto P = [&](int i, int j) {
        float a = 3.14159265f * (float)i / nLat;            // 0..pi
        float b = 6.2831853f * (float)j / nLon;
        return Vec3(rad * std::sin(a) * std::cos(b), rad * std::cos(a), rad * std::sin(a) * std::sin(b));
    };
    for (int i = 0; i < nLat; ++i)
        for (int j = 0; j < nLon; ++j) {
            Vec3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
            push(v, a, col); push(v, b, col); push(v, c, col);
            push(v, a, col); push(v, c, col); push(v, d, col);
        }
    return makeMesh(v, GL_TRIANGLES);
}

static Vec3 tdeAccel(Vec3 p) {                               // Paczynski-Wiita gravity
    float r = length(p); float rr = r - RS; if (rr < 0.05f) rr = 0.05f;
    return p * (-MASS / (rr * rr * r));
}
static Vec3 tdeDissip(Vec3 p, Vec3 v, float ramp) {         // dissipation -> circular disk
    float r = length(p);
    if (ramp <= 0.0f || r > 42.0f || r < RS + 0.25f) return Vec3(0, 0, 0);
    // Damp only the radial velocity (strictly dissipative, conserves angular
    // momentum) -> each particle circularizes at the radius set by its own L,
    // so the bound debris settles into a *filled* disk, not a single ring.
    Vec3 rhat = p * (1.0f / r);
    float vr = dot(v, rhat);
    Vec3 a = rhat * (-gTdeKc * vr);
    a.y += -gTdeKy * v.y;                                    // flatten toward the plane
    return a * ramp;
}
static void tdeStepCOM(float h) {
    Vec3 a1 = tdeAccel(gComPos); gComVel = gComVel + a1 * (0.5f * h); gComPos = gComPos + gComVel * h;
    Vec3 a2 = tdeAccel(gComPos); gComVel = gComVel + a2 * (0.5f * h);
}
static void tdeStepPart(TPart &p, float h, float ramp) {
    Vec3 a1 = tdeAccel(p.pos) + (p.bound ? tdeDissip(p.pos, p.vel, ramp) : Vec3(0, 0, 0));
    p.vel = p.vel + a1 * (0.5f * h); p.pos = p.pos + p.vel * h;
    Vec3 a2 = tdeAccel(p.pos) + (p.bound ? tdeDissip(p.pos, p.vel, ramp) : Vec3(0, 0, 0));
    p.vel = p.vel + a2 * (0.5f * h);
}
static void throwStar() {
    gStar.resize(TDE_N); gOffsets.resize(TDE_N);
    float rp = TDE_PERI, r0 = TDE_R0;
    float L  = rp * std::sqrt(2.0f * MASS / (rp - RS));      // parabolic (E=0) with pericenter rp
    float v0 = std::sqrt(2.0f * MASS / (r0 - RS));
    float vt = L / r0, vr = -std::sqrt(std::max(v0 * v0 - vt * vt, 0.0f));
    gComPos = Vec3(-r0, 0, 0);
    gComVel = Vec3(-vr, 0.0f, vt);                           // r_hat=(-1,0,0), theta_hat=(0,0,1)
    for (int i = 0; i < TDE_N; ++i) {
        Vec3 o; do { o = Vec3(frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1); } while (dot(o, o) > 1.0f);
        o = o * TDE_RSTAR; gOffsets[i] = o;
        gStar[i] = { gComPos + o, gComVel, false, false };
    }
    gDisrupted = false; gTdeTime = 0; gTimeSinceDisrupt = 0; gTdeAlive = TDE_N; gTdeEaten = 0; gTdeRestart = false;
}
static void stepTDE(float simDt) {
    const int sub = 8; float h = simDt / sub;
    gTdeTime += simDt;
    if (!gDisrupted) {
        for (int k = 0; k < sub; ++k) tdeStepCOM(h);
        for (int i = 0; i < TDE_N; ++i) gStar[i].pos = gComPos + gOffsets[i];
        if (length(gComPos) < TDE_RT) {                      // disruption
            gDisrupted = true; gTimeSinceDisrupt = 0;
            for (int i = 0; i < TDE_N; ++i) {
                gStar[i].pos = gComPos + gOffsets[i]; gStar[i].vel = gComVel;
                float r = length(gStar[i].pos);
                float E = 0.5f * dot(gComVel, gComVel) - MASS / (r - RS);
                gStar[i].bound = (E < 0.0f);
            }
        }
    } else {
        gTimeSinceDisrupt += simDt;
        float ramp = clampf((gTimeSinceDisrupt - 20.0f) / 120.0f, 0.0f, 1.0f);
        for (int i = 0; i < TDE_N; ++i) {
            if (gStar[i].dead) continue;
            for (int k = 0; k < sub; ++k) tdeStepPart(gStar[i], h, ramp);
            if (length(gStar[i].pos) < TDE_EAT) { gStar[i].dead = true; gTdeAlive--; gTdeEaten++; }
        }
    }
}
static void updateTDE(float realdt) {
    if (gTdeRestart || gStar.empty()) throwStar();
    if (gPaused) return;
    float simDt = realdt * TDE_SPEED;
    if (simDt > 2.0f) simDt = 2.0f;                          // clamp on frame hitches
    stepTDE(simDt);
    if (gTimeSinceDisrupt > 1600.0f) throwStar();            // auto-loop the demo
}
static void renderTDE(int outW, int outH, float realdt) {
    updateTDE(realdt);

    glViewport(0, 0, outW, outH);
    glClearColor(0.008f, 0.010f, 0.018f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    float aspect = (float)outW / (float)outH;
    Vec3 target(0, 0, 0);
    Vec3 eye = camPos(gCam[3], target);
    Mat4 mvp = mul(perspective(radians(52.0f), aspect, 0.05f, 800.0f), lookAt(eye, target, Vec3(0, 1, 0)));
    glUseProgram(gProgScene);
    uM4(gProgScene, "uMVP", mvp);
    u1f(gProgScene, "uPointSize", 1.0f); u1i(gProgScene, "uRound", 0);

    // black hole (occludes far-side debris) + rings + reference grid
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    u1f(gProgScene, "uAlpha", 1.0f);
    glBindVertexArray(gBHSphere.vao); glDrawArrays(gBHSphere.mode, 0, gBHSphere.count);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
    u1f(gProgScene, "uAlpha", 0.5f);
    glBindVertexArray(gRingPhoton.vao);  glDrawArrays(gRingPhoton.mode, 0, gRingPhoton.count);
    glBindVertexArray(gRingHorizon.vao); glDrawArrays(gRingHorizon.mode, 0, gRingHorizon.count);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    u1f(gProgScene, "uAlpha", 0.22f);
    glBindVertexArray(gFlatGrid.vao); glDrawArrays(gFlatGrid.mode, 0, gFlatGrid.count);

    // debris particles, colored by speed (slow = warm star, fast = hot blue-white)
    for (int i = 0; i < TDE_N; ++i) {
        float *d = &gTdeData[i * 6];
        if (gStar[i].dead) { d[0] = d[1] = d[2] = 0; d[3] = d[4] = d[5] = 0; continue; }
        Vec3 p = gStar[i].pos;
        float sp = length(gStar[i].vel);
        float t = clampf((sp - 0.10f) / 0.45f, 0.0f, 1.0f);
        Vec3 cool(1.0f, 0.80f, 0.50f), hot(0.7f, 0.85f, 1.0f);
        Vec3 c = (cool * (1.0f - t) + hot * t) * (0.55f + 1.9f * t);
        d[0] = p.x; d[1] = p.y; d[2] = p.z; d[3] = c.x; d[4] = c.y; d[5] = c.z;
    }
    updateMesh(gTdeMesh, gTdeData);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
    u1i(gProgScene, "uRound", 1); u1f(gProgScene, "uPointSize", gDisrupted ? 4.0f : 5.0f);
    u1f(gProgScene, "uAlpha", 0.9f);
    glBindVertexArray(gTdeMesh.vao); glDrawArrays(GL_POINTS, 0, gTdeMesh.count);

    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

// ----------------------------------------------------------------------------
//  Headless still export (PPM, P6). Renders into an LDR FBO and reads it back.
// ----------------------------------------------------------------------------
static void writePPM(const char *path, int w, int h, const std::vector<unsigned char> &rgba) {
    std::FILE *f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)                    // flip: GL origin is bottom-left
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = &rgba[(y * w + x) * 4];
            std::fputc(p[0], f); std::fputc(p[1], f); std::fputc(p[2], f);
        }
    std::fclose(f);
}
static void renderShot(const char *path, int mode, int w, int h) {
    GLuint fbo, color, depth;
    glGenFramebuffers(1, &fbo); glGenTextures(1, &color); glGenRenderbuffers(1, &depth);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "shot FBO incomplete\n");

    if (mode == 0) renderRaytrace(fbo, w, h);
    else if (mode == 1) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); renderGrid(w, h, 0.0f); }
    else if (mode == 2) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gRayAuto = false; gRayDirty = false;
        rebuildRayPath();
        gRayS = (float)gPath.size() - 1.0f;     // full bent path for the still
        renderRayBend(w, h, 0.0f);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        throwStar();
        while (gTdeTime < gTdeShotT) { float d = gTdeShotT - gTdeTime; stepTDE(d > 2.0f ? 2.0f : d); }
        gPaused = true;                          // freeze; renderTDE won't advance
        renderTDE(w, h, 0.0f);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<unsigned char> px(w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    writePPM(path, w, h, px);
    std::printf("wrote %s (%dx%d, mode %d)\n", path, w, h, mode);
}

int main(int argc, char **argv) {
    bool shot = false, bench = false, rayBSet = false; std::string shotPath = "/tmp/bh.ppm";
    int shotMode = 0, shotW = 1280, shotH = 720;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bench")) { bench = true; continue; }
        if (!std::strcmp(argv[i], "--shot")) {
            shot = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') shotPath = argv[++i];
        } else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) shotMode = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) { shotW = std::atoi(argv[++i]); shotH = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--bg") && i + 1 < argc) gBgMode = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--nodisk")) gShowDisk = false;
        else if (!std::strcmp(argv[i], "--rayb") && i + 1 < argc) { gRayB = (float)std::atof(argv[++i]); rayBSet = true; }
        else if (!std::strcmp(argv[i], "--tdet") && i + 1 < argc) gTdeShotT = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--cam") && i + 3 < argc) {
            gCam[0].az = gCam[1].az = (float)std::atof(argv[++i]);
            gCam[0].el = gCam[1].el = (float)std::atof(argv[++i]);
            gCam[0].dist = gCam[1].dist = (float)std::atof(argv[++i]);
        }
    }

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (shot || bench) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    int winW = shot ? shotW : 1280, winH = shot ? shotH : 720;
    GLFWwindow *win = glfwCreateWindow(winW, winH, "Black Hole", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);

    std::printf("OpenGL %s | %s\n", (const char *)glGetString(GL_VERSION),
                (const char *)glGetString(GL_RENDERER));

    gProgBH    = makeProgram("fullscreen.vert", "blackhole.frag");
    gProgBlit  = makeProgram("fullscreen.vert", "blit.frag");
    gProgScene = makeProgram("scene.vert", "scene.frag");
    glGenVertexArrays(1, &gEmptyVAO);

    gFlamm = buildFlammWire(); gHorizon = buildHorizon(); gRim = buildRim(); gDisk = buildDiskBand();
    std::srand(1337); initParticles(240);
    gPartData.assign(gParticles.size() * 6, 0.0f);
    gPartMesh = makeMesh(gPartData, GL_POINTS, GL_DYNAMIC_DRAW);
    glEnable(GL_PROGRAM_POINT_SIZE);

    gFlatGrid    = buildFlatPolarGrid();
    gHorizonFlat = buildFlatDisk(RS, Vec3(0, 0, 0), 0.0f);
    gRingHorizon = buildCircleLoop(RS,        Vec3(1.0f, 0.55f, 0.25f), 0.03f);
    gRingPhoton  = buildCircleLoop(1.5f * RS, Vec3(0.55f, 0.40f, 0.75f), 0.03f);
    gRayFull  = makeDynamicMesh(6200, GL_LINE_STRIP);
    gRayTrail = makeDynamicMesh(6200, GL_LINE_STRIP);
    gRayHead  = makeDynamicMesh(1, GL_POINTS);
    gRayRef   = makeDynamicMesh(2, GL_LINES);

    gBHSphere = buildSphere(RS * 0.99f, Vec3(0.02f, 0.02f, 0.03f));
    gTdeData.assign(TDE_N * 6, 0.0f);
    gTdeMesh = makeDynamicMesh(TDE_N, GL_POINTS);
    throwStar();

    if (bench) {
        const int W = 1280, H = 720, N = 180;
        for (int i = 0; i < 10; ++i) renderRaytrace(0, W, H);   // warmup
        glFinish();
        double t0 = glfwGetTime();
        for (int i = 0; i < N; ++i) renderRaytrace(0, W, H);
        glFinish();
        double el = glfwGetTime() - t0;
        std::printf("BENCH: %d frames @ %dx%d (scale %.2f, steps %d) = %.3fs -> %.1f fps\n",
                    N, W, H, gScale, gSteps, el, N / el);
        glfwDestroyWindow(win); glfwTerminate();
        return 0;
    }

    if (shot) {
        gScale = 1.0f; gSteps = 440; gSimTime = 4.0; gMode = shotMode;
        if (shotMode == 2 && !rayBSet) gRayB = 2.75f;
        renderShot(shotPath.c_str(), shotMode, shotW, shotH);
        glfwDestroyWindow(win); glfwTerminate();
        return 0;
    }

    glfwSwapInterval(1);                                  // vsync
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);
    glfwSetKeyCallback(win, onKey);
    std::printf(
        "\n=== Black Hole — controls ===\n"
        "  Mouse drag : orbit camera        Scroll : zoom\n"
        "  TAB        : cycle view   1:ray-trace  2:spacetime grid  3:light-ray bending  4:tidal disruption\n"
        "  D          : accretion disk on/off\n"
        "  B          : background  stars <-> lensing grid\n"
        "  [ ]        : render resolution down/up    - = : geodesic steps down/up\n"
        "  , .        : exposure      SPACE: pause    R: reset cam    ESC: quit\n"
        "  mode 3 (light-ray bending): Up/Down impact parameter   C: resume auto-cycle\n"
        "  mode 4 (tidal disruption): N: throw a new star\n\n");

    gMode = shotMode;                                    // --mode N sets the initial view too

    double prevTime = glfwGetTime(), fpsTimer = prevTime; int frames = 0; double fps = 0;
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        double dt = now - prevTime; prevTime = now;
        if (!gPaused) gSimTime += dt;

        int fbW, fbH; glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) { glfwPollEvents(); continue; }

        if (gMode == 0)      renderRaytrace(0, fbW, fbH);
        else if (gMode == 1) renderGrid(fbW, fbH, gPaused ? 0.0f : (float)dt);
        else if (gMode == 2) renderRayBend(fbW, fbH, gPaused ? 0.0f : (float)dt);
        else                 renderTDE(fbW, fbH, (float)dt);

        if (++frames, now - fpsTimer > 0.4) { fps = frames / (now - fpsTimer); frames = 0; fpsTimer = now; }
        char title[256];
        if (gMode == 3)
            std::snprintf(title, sizeof(title),
                "Black Hole  |  Tidal Disruption  |  %s  |  eaten %d / %d  |  %s  |  %.0f fps",
                gDisrupted ? "star DISRUPTED -> stream/disk" : "star approaching",
                gTdeEaten, TDE_N, gPaused ? "paused" : "N: new star", fps);
        else if (gMode == 2)
            std::snprintf(title, sizeof(title),
                "Black Hole  |  Light-Ray Bending  |  impact b=%.3f rs (b_crit=2.598)  |  %s%s%.0f deg  |  %s  %.0f fps",
                gRayB, gRayCaptured ? "CAPTURED" : "deflected ",
                gRayCaptured ? "" : "", gRayCaptured ? 0.0f : gRayDeflect,
                gRayAuto ? "auto-cycle" : "manual (Up/Down, C=auto)", fps);
        else
            std::snprintf(title, sizeof(title),
                "Black Hole  |  %s  |  disk:%s  bg:%s  scale:%.2f  steps:%d  exp:%.1f  |  %.0f fps",
                gMode == 0 ? "Relativistic Ray-Trace" : "Spacetime Curvature (Flamm)",
                gShowDisk ? "on" : "off", gBgMode == 0 ? "stars" : "grid",
                gScale, gSteps, gExposure, fps);
        glfwSetWindowTitle(win, title);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
