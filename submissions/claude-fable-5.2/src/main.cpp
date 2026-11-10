// =============================================================================
//  Kerr black hole — real-time general-relativistic ray tracer
//
//  Every pixel integrates a null geodesic of the Kerr metric on the GPU
//  (shaders/kerr_geodesic.glsl + raytrace.frag).  This file is the host:
//  window and input, the observer's tetrad, the physical tables the shader
//  samples (Novikov-Thorne temperature profile, blackbody colours), the
//  embedding-diagram mesh, and the frame pipeline
//
//      ray trace -> spacetime grid -> [progressive accumulation] -> bloom
//                -> exposure / tone curve / sRGB -> read-out
//
//  Units: G = c = M = 1.
// =============================================================================
#include "gl_util.hpp"
#include "physics.hpp"
#include "blackbody.hpp"
#include "hud_text.hpp"

#include <algorithm>
#include <cstdarg>
#include <ctime>

using glu::Program;
using glu::Target;

namespace {

constexpr double kPi = kerr::kPi;
constexpr double kDeg = kPi / 180.0;
constexpr double kMaxSpin = 0.998;        // Thorne's limit for an accreting hole

// ---------------------------------------------------------------------------
// Simulation state
// ---------------------------------------------------------------------------
struct State {
    // black hole
    double spin = 0.90;

    // observer (static, at Kerr-Schild position r, inclination from the spin axis)
    double camR = 62.0, camIncl = 78.0, camAz = 0.35, lookZ = -5.0;
    double tgtR = 62.0, tgtIncl = 78.0, tgtAz = 0.35, tgtLookZ = -5.0;
    double fovY = 40.0;

    // accretion disk
    double diskOuter = 22.0;
    double Tpeak = 5000.0;
    double contrast = 2.2;
    bool disk = true, doppler = true, turbulence = true, limb = true;

    // presentation
    double exposure = 5.0;
    double skyGain = 0.22;
    double bloom = 0.08;
    int tonemap = 2;
    double toneContrast = 1.3;
    bool sky = true, redshiftMap = false;
    bool funnel = true, lensedGrid = false;
    int gridStyle = 0;
    double funnelRim = 46.0;
    bool hud = true;

    // time
    double simTime = 0.0, timeRate = 6.0;
    bool paused = false, autoOrbit = true;

    // derived
    double rh = 0, risco = 0, rphCo = 0, rphCounter = 0;
};

struct Options {
    int width = 1440, height = 900;
    std::string screenshot;
    int frames = 0;                 // > 0: render this many frames, then exit
    double fixedScale = 0.0;        // > 0: disable dynamic resolution
    double targetFps = 60.0;
    int skySize = 2048;
    bool bench = false;
    bool fullscreen = false;
    int preset = 0;
};

State S;
Options opt;

bool gViewChanged = true;           // anything that invalidates accumulated frames
bool gTablesDirty = true;           // spin changed: rebuild disk table and funnel
bool gDragging = false;
double gLastX = 0, gLastY = 0;
std::string gToast;
double gToastUntil = 0;
bool gWantScreenshot = false;

void toast(const std::string& msg) {
    gToast = msg;
    gToastUntil = glfwGetTime() + 3.0;
}

// ---------------------------------------------------------------------------
// Viewpoints
// ---------------------------------------------------------------------------
void applyPreset(int n) {
    S.redshiftMap = false;
    S.lensedGrid = false;
    S.disk = true; S.doppler = true; S.turbulence = true; S.sky = true;
    S.tgtLookZ = 0.0;
    S.Tpeak = 5000; S.exposure = 5.0; S.diskOuter = 22;
    switch (n) {
        default:
        case 1:   // overview: lensed disk on top of the embedding funnel
            S.spin = 0.90; S.tgtR = 62; S.tgtIncl = 78; S.funnel = true; S.tgtLookZ = -5.0;
            toast("1  overview - Kerr a = 0.9 with the spacetime funnel");
            break;
        case 2:   // near edge-on, fast spin
            S.spin = 0.998; S.tgtR = 34; S.tgtIncl = 86; S.funnel = false;
            S.Tpeak = 5500; S.diskOuter = 20;
            toast("2  edge-on, near-extremal spin a = 0.998");
            break;
        case 3:   // Schwarzschild, the classic Luminet (1979) configuration
            S.spin = 0.0; S.tgtR = 60; S.tgtIncl = 80; S.funnel = false; S.turbulence = false;
            S.Tpeak = 4500; S.exposure = 2.5;
            toast("3  Schwarzschild hole, smooth Novikov-Thorne disk (Luminet 1979)");
            break;
        case 4:   // nearly face-on, like M87*
            S.spin = 0.94; S.tgtR = 38; S.tgtIncl = 17; S.funnel = false;
            S.exposure = 4.0; S.diskOuter = 18;
            toast("4  nearly face-on (i = 17 deg, M87*-like geometry)");
            break;
        case 5:   // the trapdoor in profile
            S.spin = 0.90; S.tgtR = 105; S.tgtIncl = 84; S.funnel = true; S.tgtLookZ = -9.0;
            toast("5  the trapdoor - embedding diagram of the equatorial plane");
            break;
        case 6:   // close in
            S.spin = 0.90; S.tgtR = 14; S.tgtIncl = 84; S.funnel = false;
            S.exposure = 3.0;
            toast("6  close approach - observer at r = 14 M");
            break;
        case 7:   // lensed coordinate grid, no disk
            S.spin = 0.90; S.tgtR = 40; S.tgtIncl = 75; S.funnel = false; S.lensedGrid = true; S.disk = false;
            toast("7  equatorial coordinate grid seen through the lens (disk off)");
            break;
    }
    gTablesDirty = true;
    gViewChanged = true;
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------
void onKey(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    gViewChanged = true;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_SPACE:  S.paused = !S.paused; toast(S.paused ? "paused - refining image" : "running"); break;
        case GLFW_KEY_G:      S.funnel = !S.funnel; S.tgtLookZ = S.funnel ? -5.0 : 0.0;
                              toast(S.funnel ? "spacetime funnel on" : "spacetime funnel off"); break;
        case GLFW_KEY_K:      S.gridStyle = 1 - S.gridStyle;
                              toast(S.gridStyle ? "funnel grid: draped Cartesian" : "funnel grid: polar, rings at equal proper distance"); break;
        case GLFW_KEY_L:      S.lensedGrid = !S.lensedGrid; toast(S.lensedGrid ? "lensed equatorial grid on" : "lensed equatorial grid off"); break;
        case GLFW_KEY_D:      S.disk = !S.disk; toast(S.disk ? "accretion disk on" : "accretion disk off"); break;
        case GLFW_KEY_B:      S.doppler = !S.doppler; toast(S.doppler ? "Doppler boosting + gravitational redshift on" : "Doppler boosting + gravitational redshift OFF (unphysical)"); break;
        case GLFW_KEY_T:      S.turbulence = !S.turbulence; toast(S.turbulence ? "disk turbulence on" : "smooth Novikov-Thorne disk"); break;
        case GLFW_KEY_N:      S.limb = !S.limb; toast(S.limb ? "limb darkening on" : "limb darkening off (isotropic emission)"); break;
        case GLFW_KEY_V:      S.redshiftMap = !S.redshiftMap; toast(S.redshiftMap ? "redshift map: blue = blueshifted, red = redshifted" : "physical colours"); break;
        case GLFW_KEY_X:      S.sky = !S.sky; break;
        case GLFW_KEY_O:      S.autoOrbit = !S.autoOrbit; toast(S.autoOrbit ? "auto-orbit on" : "auto-orbit off"); break;
        case GLFW_KEY_H:      S.hud = !S.hud; break;
        case GLFW_KEY_S:      gWantScreenshot = true; break;
        case GLFW_KEY_R:      applyPreset(1); break;
        case GLFW_KEY_F: {
            static int wx, wy, ww, wh;
            if (glfwGetWindowMonitor(w)) {
                glfwSetWindowMonitor(w, nullptr, wx, wy, ww, wh, 0);
            } else {
                glfwGetWindowPos(w, &wx, &wy);
                glfwGetWindowSize(w, &ww, &wh);
                GLFWmonitor* m = glfwGetPrimaryMonitor();
                const GLFWvidmode* vm = glfwGetVideoMode(m);
                glfwSetWindowMonitor(w, m, 0, 0, vm->width, vm->height, vm->refreshRate);
            }
            break;
        }
        default:
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_7) applyPreset(key - GLFW_KEY_0);
            break;
    }
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gDragging = action == GLFW_PRESS;
        glfwGetCursorPos(w, &gLastX, &gLastY);
    }
}

void onCursor(GLFWwindow*, double x, double y) {
    if (gDragging) {
        S.tgtAz -= (x - gLastX) * 0.0055;
        S.tgtIncl = std::clamp(S.tgtIncl - (y - gLastY) * 0.28, 1.0, 179.0);
        gViewChanged = true;
    }
    gLastX = x;
    gLastY = y;
}

void onScroll(GLFWwindow*, double, double dy) {
    S.tgtR = std::clamp(S.tgtR * std::exp(-dy * 0.06), 5.0, 600.0);
    gViewChanged = true;
}

// Keys that act while held.
void pollHeldKeys(GLFWwindow* w, double dt) {
    auto down = [&](int k) { return glfwGetKey(w, k) == GLFW_PRESS; };
    bool fine = down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT);
    double k = fine ? 0.2 : 1.0;
    if (down(GLFW_KEY_LEFT) || down(GLFW_KEY_RIGHT)) {
        double d = (down(GLFW_KEY_RIGHT) ? 1.0 : -1.0) * 0.35 * k * dt;
        S.spin = std::clamp(S.spin + d, -kMaxSpin, kMaxSpin);
        gTablesDirty = gViewChanged = true;
    }
    if (down(GLFW_KEY_UP) || down(GLFW_KEY_DOWN)) {
        S.Tpeak = std::clamp(S.Tpeak * std::exp((down(GLFW_KEY_UP) ? 1.0 : -1.0) * 0.8 * k * dt), 1500.0, 60000.0);
        gViewChanged = true;
    }
    if (down(GLFW_KEY_LEFT_BRACKET) || down(GLFW_KEY_RIGHT_BRACKET)) {
        S.exposure = std::clamp(S.exposure * std::exp((down(GLFW_KEY_RIGHT_BRACKET) ? 1.0 : -1.0) * 1.2 * k * dt), 0.02, 200.0);
        gViewChanged = true;
    }
    if (down(GLFW_KEY_COMMA) || down(GLFW_KEY_PERIOD)) {
        S.timeRate = std::clamp(S.timeRate * std::exp((down(GLFW_KEY_PERIOD) ? 1.0 : -1.0) * 1.2 * dt), 0.25, 200.0);
    }
    if (down(GLFW_KEY_MINUS) || down(GLFW_KEY_EQUAL)) {
        S.diskOuter = std::clamp(S.diskOuter * std::exp((down(GLFW_KEY_EQUAL) ? 1.0 : -1.0) * 0.8 * k * dt), 4.0, 55.0);
        gViewChanged = true;
    }
    if (down(GLFW_KEY_9) || down(GLFW_KEY_0)) {
        S.fovY = std::clamp(S.fovY * std::exp((down(GLFW_KEY_0) ? 1.0 : -1.0) * 0.8 * k * dt), 8.0, 110.0);
        gViewChanged = true;
    }
}

// ---------------------------------------------------------------------------
// GPU resources
// ---------------------------------------------------------------------------
struct Gpu {
    Program raytrace, skyGen, grid, bloomDown, bloomUp, composite, hud, accumulate;
    GLuint vaoEmpty = 0;

    GLuint texBlackbody = 0, texDiskLUT = 0, texSky = 0;
    blackbody::Table bbTable;

    // scene: HDR colour + distance of first opaque hit
    GLuint fboScene = 0, fboSceneColor = 0, texScene = 0, texDepth = 0;
    int sceneW = 0, sceneH = 0;
    Target accum;
    std::vector<Target> bloom;

    // funnel mesh
    GLuint vaoGrid = 0, vboGrid = 0, iboGrid = 0;
    GLsizei gridIndexCount = 0;
    double funnelDepth = 0;

    GLuint queries[2] = {0, 0};
    int queryIndex = 0;
    bool queryPending[2] = {false, false};
};
Gpu G;

void drawFullscreen() {
    glBindVertexArray(G.vaoEmpty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// (Re)allocates the off-screen targets; returns true if they were recreated.
bool createSceneTargets(int w, int h) {
    if (w == G.sceneW && h == G.sceneH) return false;
    if (G.texScene) {
        glDeleteTextures(1, &G.texScene);
        glDeleteTextures(1, &G.texDepth);
        glDeleteFramebuffers(1, &G.fboScene);
        glDeleteFramebuffers(1, &G.fboSceneColor);
    }
    G.accum.destroy();
    for (Target& t : G.bloom) t.destroy();
    G.bloom.clear();

    G.sceneW = w;
    G.sceneH = h;
    G.texScene = glu::makeTexture2D(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, nullptr, GL_LINEAR);
    G.texDepth = glu::makeTexture2D(w, h, GL_R32F, GL_RED, GL_FLOAT, nullptr, GL_NEAREST);

    glGenFramebuffers(1, &G.fboScene);
    glBindFramebuffer(GL_FRAMEBUFFER, G.fboScene);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G.texScene, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, G.texDepth, 0);
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    glu::checkFramebuffer("scene");

    // colour-only view of the same texture, so the grid pass can sample depth
    glGenFramebuffers(1, &G.fboSceneColor);
    glBindFramebuffer(GL_FRAMEBUFFER, G.fboSceneColor);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G.texScene, 0);
    glu::checkFramebuffer("scene colour");

    G.accum = glu::makeTarget(w, h, GL_RGBA32F, GL_LINEAR);

    int bw = w / 2, bh = h / 2;
    for (int i = 0; i < 7 && bw >= 8 && bh >= 8; ++i) {
        G.bloom.push_back(glu::makeTarget(bw, bh, GL_RGBA16F, GL_LINEAR));
        bw /= 2;
        bh /= 2;
    }
    return true;
}

void buildBlackbodyTexture() {
    G.bbTable = blackbody::build();
    G.texBlackbody = glu::makeTexture2D(G.bbTable.n, 1, GL_RGBA32F, GL_RGBA, GL_FLOAT, G.bbTable.rgba.data(), GL_LINEAR);
}

// Normalised Novikov-Thorne temperature profile T(r)/T_max = (F/F_max)^(1/4).
constexpr double kLutRmax = 60.0;
void buildDiskTexture() {
    kerr::DiskFlux flux;
    flux.build(S.spin, kLutRmax);
    const int n = 1024;
    std::vector<float> lut(n);
    for (int i = 0; i < n; ++i) {
        double u = double(i) / (n - 1);
        double r = flux.rIn + (flux.rMax - flux.rIn) * u * u;
        lut[i] = float(std::pow(std::max(flux.flux(r), 0.0) / flux.Fmax, 0.25));
    }
    if (!G.texDiskLUT) G.texDiskLUT = glu::makeTexture2D(n, 1, GL_R32F, GL_RED, GL_FLOAT, lut.data(), GL_LINEAR);
    else {
        glBindTexture(GL_TEXTURE_2D, G.texDiskLUT);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RED, GL_FLOAT, lut.data());
    }
}

// Embedding-diagram mesh: rim at z = 0, throat (the horizon) hanging below.
void buildFunnelMesh() {
    const int NR = 200, NP = 192;
    auto e = kerr::embedding(S.spin, S.funnelRim, NR);
    double zRim = e.back().z;
    G.funnelDepth = zRim;
    std::vector<float> v;
    v.reserve(size_t(NR + 1) * (NP + 1) * 8);
    for (int i = 0; i <= NR; ++i) {
        for (int j = 0; j <= NP; ++j) {
            double phi = 2.0 * kPi * j / NP;
            v.push_back(float(e[i].Rc * std::cos(phi)));
            v.push_back(float(e[i].Rc * std::sin(phi)));
            v.push_back(float(e[i].z - zRim));
            v.push_back(float(e[i].r));
            v.push_back(float(phi));
            v.push_back(float(e[i].tau));
            v.push_back(float(e[i].ell));
            v.push_back(float(e[i].lapse));
        }
    }
    std::vector<unsigned> idx;
    idx.reserve(size_t(NR) * NP * 6);
    for (int i = 0; i < NR; ++i)
        for (int j = 0; j < NP; ++j) {
            unsigned a = i * (NP + 1) + j, b = a + 1, c = a + (NP + 1), d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    if (!G.vaoGrid) {
        glGenVertexArrays(1, &G.vaoGrid);
        glGenBuffers(1, &G.vboGrid);
        glGenBuffers(1, &G.iboGrid);
    }
    glBindVertexArray(G.vaoGrid);
    glBindBuffer(GL_ARRAY_BUFFER, G.vboGrid);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, G.iboGrid);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_DYNAMIC_DRAW);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
    glBindVertexArray(0);
    G.gridIndexCount = GLsizei(idx.size());
}

void rebuildSpinTables() {
    S.rh = kerr::horizon(S.spin);
    S.risco = kerr::isco(S.spin);
    S.rphCo = kerr::photonOrbit(S.spin, true);
    S.rphCounter = kerr::photonOrbit(S.spin, false);
    buildDiskTexture();
    buildFunnelMesh();
    gTablesDirty = false;
}

// Bake the procedural sky into a mip-mapped cube map.
void buildSky(int size) {
    glGenTextures(1, &G.texSky);
    glBindTexture(GL_TEXTURE_CUBE_MAP, G.texSky);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (glGetError() == GL_NO_ERROR && maxAniso > 1.0f)
        glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 8.0f));

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);
    glDisable(GL_BLEND);
    G.skyGen.use();
    glu::bindTexture(0, GL_TEXTURE_2D, G.texBlackbody);
    G.skyGen.set("uBlackbody", 0);
    G.skyGen.set("uLogT", float(G.bbTable.logTmin), float(G.bbTable.logTmax));
    G.skyGen.set("uLutN", float(G.bbTable.n));
    G.skyGen.set("uTexel", float(2.0 / size));
    G.skyGen.set("uFirstLayer", 2);
    for (int f = 0; f < 6; ++f) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, G.texSky, 0);
        G.skyGen.set("uFace", f);
        drawFullscreen();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glBindTexture(GL_TEXTURE_CUBE_MAP, G.texSky);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
}

// ---------------------------------------------------------------------------
// Read-out
// ---------------------------------------------------------------------------
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

struct HudPanel {
    GLuint tex = 0;
    int w = 0, h = 0;
};
HudPanel gInfoPanel, gHelpPanel;

void uploadPanel(HudPanel& p, const std::vector<hud::Line>& lines, float contentScale, float backing) {
    hud::Bitmap bmp;
    if (!hud::render(lines, 11.0f, contentScale, backing, bmp)) return;
    if (!p.tex) glGenTextures(1, &p.tex);
    glBindTexture(GL_TEXTURE_2D, p.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bmp.w, bmp.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p.w = bmp.w;
    p.h = bmp.h;
}

void updateHud(float contentScale, double fps, double gpuMs, double scale, int accumCount) {
    using hud::Line;
    using hud::Style;
    std::vector<Line> L;
    L.push_back({"KERR BLACK HOLE \u00b7 real-time geodesic ray tracing", Style::Title});
    L.push_back({fmt("spin a = %+.3f M   horizon %.3f M   ISCO %.3f M   photon orbits %.3f / %.3f M", S.spin, S.rh, S.risco,
                     S.rphCo, S.rphCounter)});
    L.push_back({fmt("observer r = %.1f M   inclination %.1f\u00b0   field of view %.0f\u00b0", S.camR, S.camIncl, S.fovY)});
    if (S.disk)
        L.push_back({fmt("disk  Novikov\u2013Thorne   T_max %.0f K   r < %.0f M   %s%s%s", S.Tpeak, S.diskOuter,
                         S.doppler ? "Doppler + redshift" : "NO Doppler / redshift", S.limb ? " \u00b7 limb darkening" : "",
                         S.turbulence ? " \u00b7 turbulence" : "")});
    else
        L.push_back({"disk  off"});
    if (S.funnel) {
        L.push_back({fmt("funnel  equatorial plane, embedded isometrically \u00b7 depth %.1f M \u00b7 rings every 2 M of ruler distance",
                         G.funnelDepth),
                     Style::Accent});
        L.push_back({"        colour = clock rate (blue \u2192 red: time slows) \u00b7 rings: red horizon \u00b7 white ergosphere \u00b7 yellow photon orbits \u00b7 green ISCO",
                     Style::Accent});
    }
    if (S.lensedGrid) L.push_back({"lensed grid  equatorial coordinates (2 M \u00d7 15\u00b0) seen along curved light paths", Style::Accent});
    std::string perf = fmt("t = %.0f M   %s%.1f M/s   \u00b7   %d\u00d7%d (%.0f%%)   %.0f fps   %.1f ms", S.simTime,
                           S.paused ? "paused  " : "\u00d7", S.timeRate, G.sceneW, G.sceneH, scale * 100.0, fps, gpuMs);
    if (accumCount > 1) perf += fmt("   \u00b7   refining %d\u00d7", accumCount);
    L.push_back({perf, Style::Dim});
    if (glfwGetTime() < gToastUntil) L.push_back({"\u25b8 " + gToast, Style::Title});
    uploadPanel(gInfoPanel, L, contentScale, 0.30f);

    static bool helpBuilt = false;
    static float helpScale = 0;
    if (!helpBuilt || helpScale != contentScale) {
        std::vector<Line> H;
        H.push_back({"drag orbit \u00b7 scroll zoom \u00b7 \u2190\u2192 spin \u00b7 \u2191\u2193 temperature \u00b7 [ ] exposure \u00b7 , . time \u00b7 - = disk size \u00b7 9 0 field of view",
                     Style::Dim});
        H.push_back({"G funnel \u00b7 K grid style \u00b7 L lensed grid \u00b7 D disk \u00b7 B Doppler \u00b7 T turbulence \u00b7 N limb darkening \u00b7 V redshift map \u00b7 X sky",
                     Style::Dim});
        H.push_back({"1\u20137 views \u00b7 Space pause + refine \u00b7 O auto-orbit \u00b7 S screenshot \u00b7 F fullscreen \u00b7 H hide text \u00b7 Esc quit",
                     Style::Dim});
        uploadPanel(gHelpPanel, H, contentScale, 0.30f);
        helpBuilt = true;
        helpScale = contentScale;
    }
}

void saveScreenshot(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 3), flipped(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[size_t(y) * w * 3], &px[size_t(h - 1 - y) * w * 3], size_t(w) * 3);
    if (glu::writePNG(path, w, h, flipped)) {
        std::printf("saved %s (%dx%d)\n", path.c_str(), w, h);
        toast("saved " + path);
    } else {
        std::fprintf(stderr, "could not write %s\n", path.c_str());
    }
}

double halton(int i, int base) {
    double f = 1.0, r = 0.0;
    while (i > 0) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}

void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if (a == "--size") std::sscanf(next(), "%dx%d", &opt.width, &opt.height);
        else if (a == "--screenshot") opt.screenshot = next();
        else if (a == "--frames") opt.frames = std::atoi(next());
        else if (a == "--scale") opt.fixedScale = std::atof(next());
        else if (a == "--fps") opt.targetFps = std::atof(next());
        else if (a == "--sky-size") opt.skySize = std::atoi(next());
        else if (a == "--bench") opt.bench = true;
        else if (a == "--fullscreen") opt.fullscreen = true;
        else if (a == "--preset") {          // later options override the preset
            opt.preset = std::atoi(next());
            applyPreset(opt.preset);
            S.camR = S.tgtR; S.camIncl = S.tgtIncl; S.lookZ = S.tgtLookZ;
        }
        else if (a == "--incl") S.camIncl = S.tgtIncl = std::atof(next());
        else if (a == "--dist") S.camR = S.tgtR = std::atof(next());
        else if (a == "--az") S.camAz = S.tgtAz = std::atof(next()) * kDeg;
        else if (a == "--fov") S.fovY = std::atof(next());
        else if (a == "--temp") S.Tpeak = std::atof(next());
        else if (a == "--exposure") S.exposure = std::atof(next());
        else if (a == "--time") S.simTime = std::atof(next());
        else if (a == "--tonemap") S.tonemap = std::atoi(next());
        else if (a == "--tone-contrast") S.toneContrast = std::atof(next());
        else if (a == "--bloom") S.bloom = std::atof(next());
        else if (a == "--sky") S.skyGain = std::atof(next());
        else if (a == "--contrast") S.contrast = std::atof(next());
        else if (a == "--disk-outer") S.diskOuter = std::atof(next());
        else if (a == "--grid") { S.funnel = std::atoi(next()) != 0; S.lookZ = S.tgtLookZ = S.funnel ? -5.0 : 0.0; }
        else if (a == "--spin") { S.spin = std::clamp(std::atof(next()), -kMaxSpin, kMaxSpin); }
        else if (a == "--grid-style") S.gridStyle = std::atoi(next());
        else if (a == "--lensed-grid") S.lensedGrid = std::atoi(next()) != 0;
        else if (a == "--disk") S.disk = std::atoi(next()) != 0;
        else if (a == "--doppler") S.doppler = std::atoi(next()) != 0;
        else if (a == "--turbulence") S.turbulence = std::atoi(next()) != 0;
        else if (a == "--redshift-map") S.redshiftMap = std::atoi(next()) != 0;
        else if (a == "--look-z") S.lookZ = S.tgtLookZ = std::atof(next());
        else if (a == "--paused") S.paused = true;
        else if (a == "--no-orbit") S.autoOrbit = false;
        else if (a == "--no-hud") S.hud = false;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "Kerr black hole - real-time geodesic ray tracer\n\n"
                "  --size WxH          window size in points (default 1440x900)\n"
                "  --fullscreen        start fullscreen\n"
                "  --fps N             frame rate the dynamic resolution aims for (default 60)\n"
                "  --scale S           fixed render scale 0.2..1 (disables dynamic resolution)\n"
                "  --spin A            a/M in [-0.998, 0.998]    --incl DEG   --dist R   --az DEG   --fov DEG\n"
                "  --temp K            peak disk temperature     --exposure E\n"
                "  --grid 0|1  --grid-style 0|1  --lensed-grid 0|1  --disk 0|1  --doppler 0|1  --turbulence 0|1\n"
                "  --preset N          start in view N (1..7)\n"
                "  --paused  --no-orbit  --no-hud  --time T\n"
                "  --screenshot FILE --frames N    render N frames, save a PNG, exit\n"
                "  --bench             uncapped frame rate, print timings\n");
            std::exit(0);
        }
    }
}

}  // namespace

// =============================================================================
int main(int argc, char** argv) {
    parseArgs(argc, argv);

    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW initialisation failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

    GLFWmonitor* monitor = opt.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    if (monitor) {
        const GLFWvidmode* vm = glfwGetVideoMode(monitor);
        opt.width = vm->width;
        opt.height = vm->height;
    }
    GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "Kerr Black Hole — real-time geodesic ray tracing", monitor, nullptr);
    if (!window) {
        std::fprintf(stderr, "could not create an OpenGL 4.1 core window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(opt.bench ? 0 : 1);
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursor);
    glfwSetScrollCallback(window, onScroll);

    std::printf("OpenGL %s  |  %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    glGenVertexArrays(1, &G.vaoEmpty);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glDisable(GL_DEPTH_TEST);

    G.raytrace   = glu::makeProgram("fullscreen.vert", "raytrace.frag");
    G.skyGen     = glu::makeProgram("fullscreen.vert", "sky_gen.frag");
    G.grid       = glu::makeProgram("grid.vert", "grid.frag");
    G.bloomDown  = glu::makeProgram("fullscreen.vert", "bloom_down.frag");
    G.bloomUp    = glu::makeProgram("fullscreen.vert", "bloom_up.frag");
    G.composite  = glu::makeProgram("fullscreen.vert", "composite.frag");
    G.hud        = glu::makeProgram("hud.vert", "hud.frag");
    G.accumulate = glu::makeProgram("fullscreen.vert", "accumulate.frag");
    glGenQueries(2, G.queries);

    buildBlackbodyTexture();
    buildSky(opt.skySize);
    if (opt.preset == 0 && S.funnel) toast("G toggles the spacetime funnel \u00b7 1\u20137 switch views \u00b7 drag to orbit");

    double renderScale = opt.fixedScale > 0 ? opt.fixedScale : 0.7;
    double liveScale = renderScale;       // scale to return to after a refined still
    double gpuMs = 0.0, fpsSmooth = 60.0;
    double lastTime = glfwGetTime(), lastHud = -1.0, lastResize = 0.0;
    int accumCount = 0;
    const int kMaxAccum = 96;
    long frameIndex = 0;
    double benchSum = 0;
    int benchN = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt = std::min(now - lastTime, 0.1);
        lastTime = now;
        if (opt.frames > 0) dt = 1.0 / 60.0;          // deterministic stepping for captures

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) { glfwWaitEventsTimeout(0.1); continue; }
        float csx = 1, csy = 1;
        glfwGetWindowContentScale(window, &csx, &csy);

        // ---------------- update ----------------
        pollHeldKeys(window, dt);
        if (!S.paused) {
            S.simTime += dt * S.timeRate;
            if (S.autoOrbit && !gDragging) S.tgtAz += dt * 0.045;
            gViewChanged = true;
        }
        {
            double k = 1.0 - std::exp(-dt * 9.0);
            double pr = S.camR, pi = S.camIncl, pa = S.camAz, pz = S.lookZ;
            S.camR += (S.tgtR - S.camR) * k;
            S.camIncl += (S.tgtIncl - S.camIncl) * k;
            S.camAz += (S.tgtAz - S.camAz) * k;
            S.lookZ += (S.tgtLookZ - S.lookZ) * k;
            if (std::fabs(S.camR - pr) > 1e-5 * S.camR || std::fabs(S.camIncl - pi) > 1e-4 || std::fabs(S.camAz - pa) > 1e-6 ||
                std::fabs(S.lookZ - pz) > 1e-4)
                gViewChanged = true;
        }
        if (gTablesDirty) rebuildSpinTables();

        // Frozen view -> refine: full resolution, finer steps, jittered accumulation.
        bool frozen = !gViewChanged;
        if (gViewChanged) {
            if (accumCount > 0 && opt.fixedScale <= 0) renderScale = liveScale;
            accumCount = 0;
        }
        gViewChanged = false;
        if (frozen && opt.fixedScale <= 0 && accumCount == 0) {
            liveScale = renderScale;
            renderScale = 1.0;
        }

        int wantW = std::max(64, int(fbW * renderScale + 0.5)) & ~1;
        int wantH = std::max(64, int(fbH * renderScale + 0.5)) & ~1;
        if (createSceneTargets(wantW, wantH)) accumCount = 0;     // e.g. window resized mid-refinement

        // ---------------- camera ----------------
        double inc = S.camIncl * kDeg;
        double eye[3] = {S.camR * std::sin(inc) * std::cos(S.camAz), S.camR * std::sin(inc) * std::sin(S.camAz),
                         S.camR * std::cos(inc)};
        // keep the observer outside the ergoregion, where static frames exist
        {
            double rk = kerr::ksRadius(eye, S.spin);
            double rmin = 2.25;
            if (rk < rmin) for (double& c : eye) c *= rmin / rk;
        }
        double fwd[3] = {-eye[0], -eye[1], S.lookZ - eye[2]};
        double fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        for (double& c : fwd) c /= fl;
        double right[3] = {fwd[1], -fwd[0], 0.0};                 // fwd x z
        double rl = std::sqrt(right[0] * right[0] + right[1] * right[1]);
        for (double& c : right) c /= rl;
        double up[3] = {right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                        right[0] * fwd[1] - right[1] * fwd[0]};   // right x fwd
        kerr::Tetrad tet;
        kerr::staticTetrad(eye, -S.spin, fwd, right, up, tet);    // chart spin = -a (time-reversed)

        double aspect = double(fbW) / fbH;
        double tanHalf = std::tan(S.fovY * kDeg * 0.5);

        bool renderScene = accumCount < kMaxAccum;
        bool refining = frozen;

        glBeginQuery(GL_TIME_ELAPSED, G.queries[G.queryIndex]);
        glDisable(GL_BLEND);

        if (renderScene) {
            // ---------------- 1. ray trace ----------------
            glBindFramebuffer(GL_FRAMEBUFFER, G.fboScene);
            glViewport(0, 0, G.sceneW, G.sceneH);
            Program& P = G.raytrace;
            P.use();
            P.set("uResolution", float(G.sceneW), float(G.sceneH));
            if (refining) P.set("uJitter", float(halton(accumCount + 1, 2) - 0.5), float(halton(accumCount + 1, 3) - 0.5));
            else P.set("uJitter", 0.0f, 0.0f);
            P.set("uTanHalfFov", tanHalf);
            P.set("uAspect", aspect);
            P.set("uPixelAngle", 2.0 * tanHalf / G.sceneH);
            P.set("uCamPos", float(eye[0]), float(eye[1]), float(eye[2]));
            auto setLeg = [&](const char* n, int A) {
                P.set(n, float(tet.e[A][1]), float(tet.e[A][2]), float(tet.e[A][3]), float(tet.e[A][0]));
            };
            setLeg("uTetT", 0); setLeg("uTetR", 1); setLeg("uTetU", 2); setLeg("uTetF", 3);
            P.set("uSpin", S.spin);
            P.set("uRh", S.rh);
            P.set("uRfar", 60.0f);
            P.set("uEps", refining ? 0.1f : 0.2f);
            P.set("uMaxSteps", refining ? 600 : 260);
            P.set("uDiskIn", S.risco);
            P.set("uDiskOut", std::max(S.diskOuter, S.risco + 0.5));
            P.set("uLutRmax", kLutRmax);
            P.set("uTpeak", S.Tpeak);
            P.set("uLogT", float(G.bbTable.logTmin), float(G.bbTable.logTmax));
            P.set("uLutN", float(G.bbTable.n));
            P.set("uLogLumRef", G.bbTable.logLuminance(S.Tpeak));
            P.set("uDiskContrast", S.contrast);
            P.set("uTime", S.simTime);
            // the sky is scenery: keep its on-screen brightness independent of the disk exposure
            P.set("uSkyGain", S.skyGain * 5.0 / S.exposure);
            int flags = (S.doppler ? 1 : 0) | (S.turbulence ? 2 : 0) | (S.limb ? 4 : 0) | (S.disk ? 8 : 0) |
                        (S.sky ? 16 : 0) | (S.redshiftMap ? 32 : 0) | (S.lensedGrid ? 64 : 0);
            P.set("uFlags", flags);
            glu::bindTexture(0, GL_TEXTURE_CUBE_MAP, G.texSky);
            glu::bindTexture(1, GL_TEXTURE_2D, G.texBlackbody);
            glu::bindTexture(2, GL_TEXTURE_2D, G.texDiskLUT);
            P.set("uSky", 0);
            P.set("uBlackbody", 1);
            P.set("uDiskLUT", 2);
            drawFullscreen();

            // ---------------- 2. spacetime funnel ----------------
            if (S.funnel) {
                glBindFramebuffer(GL_FRAMEBUFFER, G.fboSceneColor);
                glViewport(0, 0, G.sceneW, G.sceneH);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                Program& Q = G.grid;
                Q.use();
                glu::Mat4 proj = glu::perspective(S.fovY * kDeg, aspect, 0.05, 4000.0);
                glu::Mat4 view = glu::viewMatrix(eye, right, up, fwd);
                glu::Mat4 vp = glu::mul(proj, view);
                if (refining) {     // same sub-pixel jitter as the ray tracer
                    float jx = float((halton(accumCount + 1, 2) - 0.5) * 2.0 / G.sceneW);
                    float jy = float((halton(accumCount + 1, 3) - 0.5) * 2.0 / G.sceneH);
                    glu::Mat4 j{};
                    j.m[0] = j.m[5] = j.m[10] = j.m[15] = 1.0f;
                    j.m[12] = -jx;
                    j.m[13] = -jy;
                    vp = glu::mul(j, vp);
                }
                Q.setMat4("uViewProj", vp.m);
                Q.set("uCamPos", float(eye[0]), float(eye[1]), float(eye[2]));
                Q.set("uResolution", float(G.sceneW), float(G.sceneH));
                Q.set("uTime", std::fmod(S.simTime, 24.0));
                Q.set("uRimR", S.funnelRim);
                Q.set("uRh", S.rh);
                Q.set("uRph", float(S.rphCo), float(S.rphCounter));
                Q.set("uRisco", S.risco);
                Q.set("uRergo", std::fabs(S.spin) > 1e-3 ? 2.0f : -10.0f);   // equatorial static limit
                Q.set("uRingSpacing", 2.0f);
                Q.set("uStyle", S.gridStyle);
                Q.set("uIntensity", float(0.9 / S.exposure));
                glu::bindTexture(0, GL_TEXTURE_2D, G.texDepth);
                Q.set("uSceneDepth", 0);
                glBindVertexArray(G.vaoGrid);
                glDrawElements(GL_TRIANGLES, G.gridIndexCount, GL_UNSIGNED_INT, nullptr);
                glDisable(GL_BLEND);
            }

            // ---------------- 3. progressive accumulation ----------------
            if (refining) {
                G.accum.bind();
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                G.accumulate.use();
                glu::bindTexture(0, GL_TEXTURE_2D, G.texScene);
                G.accumulate.set("uSrc", 0);
                G.accumulate.set("uWeight", 1.0f / float(accumCount + 1));
                drawFullscreen();
                glDisable(GL_BLEND);
                ++accumCount;
            }
        }
        GLuint sceneTex = (refining && accumCount > 0) ? G.accum.tex : G.texScene;

        // ---------------- 4. bloom (camera glare) ----------------
        if (renderScene && !G.bloom.empty()) {
            G.bloomDown.use();
            G.bloomDown.set("uSrc", 0);
            for (size_t i = 0; i < G.bloom.size(); ++i) {
                G.bloom[i].bind();
                GLuint src = i == 0 ? sceneTex : G.bloom[i - 1].tex;
                int sw = i == 0 ? G.sceneW : G.bloom[i - 1].w, sh = i == 0 ? G.sceneH : G.bloom[i - 1].h;
                glu::bindTexture(0, GL_TEXTURE_2D, src);
                G.bloomDown.set("uTexel", 1.0f / sw, 1.0f / sh);
                G.bloomDown.set("uFirst", i == 0 ? 1 : 0);
                drawFullscreen();
            }
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            G.bloomUp.use();
            G.bloomUp.set("uSrc", 0);
            G.bloomUp.set("uRadius", 1.0f);
            for (int i = int(G.bloom.size()) - 2; i >= 0; --i) {
                G.bloom[i].bind();
                glu::bindTexture(0, GL_TEXTURE_2D, G.bloom[i + 1].tex);
                G.bloomUp.set("uTexel", 1.0f / G.bloom[i + 1].w, 1.0f / G.bloom[i + 1].h);
                drawFullscreen();
            }
            glDisable(GL_BLEND);
        }

        // ---------------- 5. composite to the window ----------------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        G.composite.use();
        glu::bindTexture(0, GL_TEXTURE_2D, sceneTex);
        glu::bindTexture(1, GL_TEXTURE_2D, G.bloom.empty() ? sceneTex : G.bloom[0].tex);
        G.composite.set("uScene", 0);
        G.composite.set("uBloom", 1);
        G.composite.set("uSceneSize", float(G.sceneW), float(G.sceneH));
        G.composite.set("uExposure", S.exposure);
        // the up-sampled chain holds the sum of all levels
        G.composite.set("uBloomStrength", S.bloom);
        G.composite.set("uBloomNorm", G.bloom.empty() ? 1.0f : 1.0f / float(G.bloom.size()));
        G.composite.set("uTonemap", S.tonemap);
        G.composite.set("uContrast", S.toneContrast);
        drawFullscreen();

        glEndQuery(GL_TIME_ELAPSED);
        G.queryPending[G.queryIndex] = true;
        G.queryIndex ^= 1;
        if (G.queryPending[G.queryIndex]) {       // result of the previous frame, if it is in yet
            GLuint ready = 0;
            glGetQueryObjectuiv(G.queries[G.queryIndex], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(G.queries[G.queryIndex], GL_QUERY_RESULT, &ns);
                double ms = double(ns) * 1e-6;
                if (renderScene) gpuMs = gpuMs > 0 ? gpuMs * 0.8 + ms * 0.2 : ms;
            }
            G.queryPending[G.queryIndex] = false;
        }

        // capture before the read-out is drawn
        bool lastCaptureFrame = opt.frames > 0 && frameIndex + 1 >= opt.frames;
        if (!opt.screenshot.empty() && lastCaptureFrame && S.hud == false) saveScreenshot(opt.screenshot, fbW, fbH);

        // ---------------- 6. read-out ----------------
        if (dt > 0) fpsSmooth = fpsSmooth * 0.95 + (1.0 / std::max(dt, 1e-4)) * 0.05;
        if (S.hud) {
            if (now - lastHud > 0.2) {
                updateHud(csx, fpsSmooth, gpuMs, renderScale, accumCount);
                lastHud = now;
            }
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            G.hud.use();
            G.hud.set("uTex", 0);
            G.hud.set("uOpacity", 1.0f);
            glBindVertexArray(G.vaoEmpty);
            float margin = 12.0f * csx;
            auto drawPanel = [&](const HudPanel& p, bool top) {
                if (!p.tex) return;
                float w = 2.0f * p.w / fbW, h = 2.0f * p.h / fbH;
                float x = -1.0f + 2.0f * margin / fbW;
                float y = top ? 1.0f - 2.0f * margin / fbH - h : -1.0f + 2.0f * margin / fbH;
                G.hud.set("uRect", x, y, w, h);
                glu::bindTexture(0, GL_TEXTURE_2D, p.tex);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            };
            drawPanel(gInfoPanel, true);
            drawPanel(gHelpPanel, false);
            glDisable(GL_BLEND);
        }

        if (!opt.screenshot.empty() && lastCaptureFrame && S.hud) saveScreenshot(opt.screenshot, fbW, fbH);
        if (gWantScreenshot) {
            gWantScreenshot = false;
            char name[64];
            std::time_t tt = std::time(nullptr);
            std::strftime(name, sizeof name, "blackhole-%Y%m%d-%H%M%S.png", std::localtime(&tt));
            saveScreenshot(name, fbW, fbH);
        }

        glfwSwapBuffers(window);
        ++frameIndex;

        // ---------------- dynamic resolution ----------------
        if (opt.fixedScale <= 0 && !refining && gpuMs > 0.0 && now - lastResize > 0.25) {
            double budget = 1000.0 / opt.targetFps * 0.80;
            double desired = std::clamp(renderScale * std::sqrt(budget / gpuMs), 0.30, 1.0);
            bool toFull = desired >= 1.0 && renderScale < 1.0;
            if (desired < renderScale * 0.96 || desired > renderScale * 1.06 || toFull) {
                renderScale += (desired - renderScale) * (desired < renderScale ? 0.7 : 0.35);
                if (renderScale > 0.97) renderScale = 1.0;
                renderScale = std::clamp(renderScale, 0.30, 1.0);
                lastResize = now;
            }
        }

        if (opt.bench) {
            glFinish();
            double ms = (glfwGetTime() - now) * 1000.0;
            if (frameIndex > 10) { benchSum += ms; ++benchN; }
            if (frameIndex % 60 == 0 && benchN > 0)
                std::printf("frame %ld: %.2f ms avg (%.0f fps)  gpu query %.2f ms  scene %dx%d\n", frameIndex, benchSum / benchN,
                            1000.0 * benchN / benchSum, gpuMs, G.sceneW, G.sceneH);
        }
        if (opt.frames > 0 && frameIndex >= opt.frames) break;
    }

    if (opt.frames > 0)
        std::printf("last frame: scene %dx%d (scale %.2f), %.2f ms GPU, %.0f fps\n", G.sceneW, G.sceneH, renderScale, gpuMs, fpsSmooth);
    if (opt.bench && benchN > 0)
        std::printf("bench: %.2f ms per frame (%.0f fps) at %dx%d\n", benchSum / benchN, 1000.0 * benchN / benchSum, G.sceneW, G.sceneH);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
