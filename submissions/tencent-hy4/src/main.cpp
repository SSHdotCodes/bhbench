// ============================================================================
//  Schwarzschild Black Hole -- real-time GPU ray-tracer / visualizer
//  ---------------------------------------------------------------------------
//  Build target: macOS (Apple OpenGL framework, GLSL 3.30 core) + GLFW + GLM.
//  All the relativistic physics lives in physics.hpp / shaders.hpp.
//
//  Modes:
//    1  Ray-traced black hole (gravitational lensing + disk + starfield)
//    2  Spacetime-curvature grid (Flamm paraboloid / "trapdoor in spacetime")
//    3  Split screen: both at once
//
//  Usage:
//     blackhole                     interactive real-time window (default)
//     blackhole --selftest          run the numerical physics checks (no window)
//     blackhole --headless out.png  render ONE frame to a PNG (no visible window)
//        options: --w N --h N --mode bh|grid|split --time T
//     blackhole --help              show controls
// ============================================================================

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <GL/glcorearb.h>   // Linux: also needs a glad/glew loader + -lGL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "physics.hpp"
#include "png.hpp"
#include "shaders.hpp"
#include "grid.hpp"

using namespace bh;

// ----------------------------------------------------------------------------
//  Application state
// ----------------------------------------------------------------------------
enum Mode { MODE_BH = 0, MODE_GRID = 1, MODE_SPLIT = 2 };

struct App {
    // viewing
    double az     = 0.30;     // azimuth (rad)
    double el     = 0.22;     // elevation above the equatorial plane (rad)
    double dist   = 30.0;     // camera distance (rs units)
    double fovDeg = 46.0;     // vertical field of view (deg)

    // display / physics tuning (also settable from the CLI for headless renders)
    float exposure   = 0.78f;
    float diskBright = 1.00f;
    float corona     = 0.09f;
    float tpeak      = 5400.0f;  // peak effective disk temperature (K, display)

    // physics / feature toggles
    bool disk     = true;
    bool lensing  = true;
    bool rel      = true;      // gravitational redshift + relativistic Doppler
    bool stars    = true;
    bool halo     = true;      // optically-thin coronal glow / infall animation
    bool paused   = false;

    // quality
    int   steps        = 340;   // max geodesic RK4 iterations per ray
    float renderScale  = 1.0f;  // internal-resolution multiplier

    // disk geometry
    float diskInner = 3.0f;    // ISCO = 6M = 3 rs
    float diskOuter = 9.0f;

    // runtime
    int   mode = MODE_BH;
    double simTime = 0.0;
    double wallTime = 0.0;
    bool  saveScreenshot = false;
    bool  dragging = false;
    double lastX = 0, lastY = 0;
    int   winW = 1280, winH = 800;

    GLFWwindow* window = nullptr;
};

static App* g_app = nullptr;

// ----------------------------------------------------------------------------
//  Small GL helpers
// ----------------------------------------------------------------------------
struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;
    GLint loc(const char* n){
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache[n] = l;
        return l;
    }
    void use(){ glUseProgram(id); }
};

static GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok){
        char log[8192]; GLsizei len = 0;
        glGetShaderInfoLog(s, sizeof(log), &len, log);
        std::fprintf(stderr, "  [shader compile error]\n%s\n", log);
        return 0;
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs){
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok){
        char log[8192]; GLsizei len = 0;
        glGetProgramInfoLog(p, sizeof(log), &len, log);
        std::fprintf(stderr, "  [program link error]\n%s\n", log);
        return 0;
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

struct Framebuffer {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
};

static void createFB(Framebuffer& fb, int w, int h){
    if (fb.fbo){
        glDeleteFramebuffers(1, &fb.fbo);
        glDeleteTextures(1, &fb.color);
        glDeleteRenderbuffers(1, &fb.depth);
    }
    fb.w = w; fb.h = h;
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    glGenTextures(1, &fb.color);
    glBindTexture(GL_TEXTURE_2D, fb.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.color, 0);

    glGenRenderbuffers(1, &fb.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb.depth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "  [warning] framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// GL objects (single window -> globals are fine)
static Program   g_bhProg, g_gridProg, g_presentProg;
static GLuint    g_emptyVAO = 0, g_gridVAO = 0, g_gridVBO = 0;
static int       g_gridVerts = 0;
static Framebuffer g_fb;

// ----------------------------------------------------------------------------
//  Camera
// ----------------------------------------------------------------------------
struct Cam { glm::vec3 pos, right, up, fwd; };

static Cam computeCamera(const App& a){
    // Keep a hair of elevation so the thin disk never becomes exactly edge-on
    // (a fully edge-on view would make plane-crossing detection degenerate).
    double el = a.el;
    if (std::fabs(el) < 0.012) el = 0.012;
    double ce = std::cos(el), se = std::sin(el);
    double ca = std::cos(a.az), sa = std::sin(a.az);
    glm::vec3 pos((float)(ce*ca), (float)se, (float)(ce*sa));
    pos *= (float)a.dist;
    glm::vec3 fwd = -glm::normalize(pos);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(fwd, worldUp));
    glm::vec3 up = glm::cross(right, fwd);
    return {pos, right, up, fwd};
}

// ----------------------------------------------------------------------------
//  Render passes (into g_fb)
// ----------------------------------------------------------------------------
static void renderBH(const App& a, const Cam& cam, int x, int y, int w, int h){
    glViewport(x, y, w, h);
    glScissor(x, y, w, h);
    glDisable(GL_DEPTH_TEST);
    g_bhProg.use();
    Program& P = g_bhProg;

    glUniform2f(P.loc("uRes"), (float)w, (float)h);
    glUniform1f(P.loc("uTime"), (float)a.simTime);
    glUniform3f(P.loc("uCamPos"),  cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(P.loc("uCamRight"),cam.right.x,cam.right.y,cam.right.z);
    glUniform3f(P.loc("uCamUp"),   cam.up.x,   cam.up.y,   cam.up.z);
    glUniform3f(P.loc("uCamFwd"),  cam.fwd.x,  cam.fwd.y,  cam.fwd.z);
    glUniform1f(P.loc("uTanHalfFov"), (float)std::tan(glm::radians(a.fovDeg)*0.5));
    glUniform1f(P.loc("uRs"), 1.0f);
    float escapeR = std::max(60.0f, std::max(4.0f*a.diskOuter, (float)(2.0*a.dist)));
    glUniform1f(P.loc("uEscapeR"), escapeR);
    glUniform1i(P.loc("uSteps"), a.steps);
    glUniform1i(P.loc("uDiskOn"),    a.disk ? 1 : 0);
    glUniform1i(P.loc("uLensingOn"), a.lensing ? 1 : 0);
    glUniform1i(P.loc("uRelOn"),     a.rel ? 1 : 0);
    glUniform1i(P.loc("uStarsOn"),   a.stars ? 1 : 0);
    glUniform1i(P.loc("uHaloOn"),    a.halo ? 1 : 0);
    glUniform1f(P.loc("uDiskInner"), a.diskInner);
    glUniform1f(P.loc("uDiskOuter"), a.diskOuter);
    glUniform1f(P.loc("uDiskBright"), a.diskBright);
    glUniform1f(P.loc("uExposure"), a.exposure);
    glUniform1f(P.loc("uTpeak"), a.tpeak);
    glUniform1f(P.loc("uCorona"), a.corona);
    glUniform1f(P.loc("uCoronaThick"), 0.9f);
    glUniform1f(P.loc("uSpinSign"), 1.0f);
    glUniform3f(P.loc("uAxis"), 0.0f, 1.0f, 0.0f);

    glBindVertexArray(g_emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void renderGrid(const App& a, const Cam& cam, int x, int y, int w, int h){
    glViewport(x, y, w, h);
    glScissor(x, y, w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    g_gridProg.use();
    Program& P = g_gridProg;

    glm::mat4 view = glm::lookAt(cam.pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians((float)a.fovDeg),
                                      (float)w / (float)h, 0.1f, 200.0f);
    glm::mat4 mvp = proj * view;
    glUniformMatrix4fv(P.loc("uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(P.loc("uTime"), (float)a.simTime);
    glUniform1f(P.loc("uHaloOn"), a.halo ? 1.0f : 0.0f);

    glBindVertexArray(g_gridVAO);
    glDrawArrays(GL_LINES, 0, g_gridVerts);
    glDisable(GL_DEPTH_TEST);
}

static void renderScene(const App& a, const Cam& cam){
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb.fbo);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, g_fb.w, g_fb.h);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    const int W = g_fb.w, H = g_fb.h;
    if (a.mode == MODE_BH){
        renderBH(a, cam, 0, 0, W, H);
    } else if (a.mode == MODE_GRID){
        renderGrid(a, cam, 0, 0, W, H);
    } else { // MODE_SPLIT
        renderBH(a, cam, 0, 0, W/2, H);
        renderGrid(a, cam, W/2, 0, W/2, H);
    }
    glDisable(GL_SCISSOR_TEST);
}

// Present the offscreen scene to the visible window via a textured quad.
// (Filtering on the FBO texture provides free upscaling when renderScale < 1.)
static void present(const App& a, int w, int h){
    (void)a;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fb.color);
    g_presentProg.use();
    glUniform1i(g_presentProg.loc("uTex"), 0);
    glUniform2f(g_presentProg.loc("uRes"), (float)w, (float)h);
    glBindVertexArray(g_emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void ensureFB(const App& a, int winW, int winH){
    float s = a.renderScale;
    s = std::min(s, 2000.0f / (float)winW);
    s = std::min(s, 1300.0f / (float)winH);
    int w = std::max(64, (int)(winW * s));
    int h = std::max(64, (int)(winH * s));
    if (w != g_fb.w || h != g_fb.h) createFB(g_fb, w, h);
}

static void readFrameToPNG(const char* path){
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<uint8_t> buf((size_t)g_fb.w * g_fb.h * 4);
    glReadPixels(0, 0, g_fb.w, g_fb.h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    bool ok = writePNG(path, g_fb.w, g_fb.h, 4, buf.data(), /*flipY=*/true);
    std::printf("  %s screenshot: %s (%dx%d)\n",
                ok ? "wrote" : "FAILED to write", path, g_fb.w, g_fb.h);
}

// ----------------------------------------------------------------------------
//  GL setup
// ----------------------------------------------------------------------------
static bool setupGL(){
#ifndef __APPLE__
    // Linux users: insert a glad/glew load here (glXxx symbols are not linked
    // directly). Apple's OpenGL.framework exports them, so no loader is needed.
    std::fprintf(stderr, "  [note] non-Apple platform: a GL function loader is required.\n");
#endif
    g_bhProg.id      = makeProgram(VS_FULLSCREEN, FS_BLACKHOLE);
    g_gridProg.id    = makeProgram(VS_GRID, FS_GRID);
    g_presentProg.id = makeProgram(VS_FULLSCREEN, FS_PRESENT);
    if (!g_bhProg.id || !g_gridProg.id || !g_presentProg.id){
        std::fprintf(stderr, "  shader setup failed.\n");
        return false;
    }

    // Empty VAO for the gl_VertexID fullscreen triangle.
    glGenVertexArrays(1, &g_emptyVAO);

    // Grid wireframe VBO.
    std::vector<float> grid;
    g_gridVerts = (int)buildGrid(grid);
    glGenVertexArrays(1, &g_gridVAO);
    glBindVertexArray(g_gridVAO);
    glGenBuffers(1, &g_gridVBO);
    glBindBuffer(GL_ARRAY_BUFFER, g_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, grid.size()*sizeof(float), grid.data(), GL_STATIC_DRAW);
    const GLsizei stride = 5 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
    glBindVertexArray(0);

    std::printf("  grid wireframe: %d vertices (two-sheet Flamm paraboloid)\n", g_gridVerts);
    return true;
}

// ----------------------------------------------------------------------------
//  Input callbacks
// ----------------------------------------------------------------------------
static const char* modeName(int m){
    return m == MODE_BH ? "LENSING" : m == MODE_GRID ? "SPACETIME GRID" : "SPLIT";
}

static void refreshTitle(){
    App& a = *g_app;
    char t[256];
    std::snprintf(t, sizeof(t),
        "Schwarzschild BH  |  %s  |  step=%d  res=%.2f  |  Disk:%s Lens:%s Rel:%s Stars:%s |  Rs=1  r_ISCO=3rs  b_crit=2.598rs",
        modeName(a.mode), a.steps, (double)a.renderScale,
        a.disk?"ON":"off", a.lensing?"ON":"off", a.rel?"ON":"off", a.stars?"ON":"off");
    glfwSetWindowTitle(a.window, t);
}

static void keyCB(GLFWwindow*, int key, int, int action, int){
    if (action != GLFW_PRESS) return;
    App& a = *g_app;
    switch (key){
        case GLFW_KEY_ESCAPE: case GLFW_KEY_Q: glfwSetWindowShouldClose(a.window, 1); break;
        case GLFW_KEY_1: a.mode = MODE_BH;   break;
        case GLFW_KEY_2: a.mode = MODE_GRID; break;
        case GLFW_KEY_3: a.mode = MODE_SPLIT;break;
        case GLFW_KEY_D: a.disk    = !a.disk;    break;
        case GLFW_KEY_L: a.lensing = !a.lensing; break;
        case GLFW_KEY_R: a.rel     = !a.rel;     break;
        case GLFW_KEY_S: a.stars   = !a.stars;   break;
        case GLFW_KEY_H: a.halo    = !a.halo;    break;
        case GLFW_KEY_P: a.saveScreenshot = true; break;
        case GLFW_KEY_SPACE: a.paused = !a.paused; break;
        case GLFW_KEY_LEFT_BRACKET:  a.steps = std::max(80,  a.steps - 40); break;
        case GLFW_KEY_RIGHT_BRACKET: a.steps = std::min(900, a.steps + 40); break;
        case GLFW_KEY_MINUS:  a.renderScale = std::max(0.40f, a.renderScale - 0.10f); break;
        case GLFW_KEY_EQUAL:  a.renderScale = std::min(1.00f, a.renderScale + 0.10f); break;
        default: break;
    }
    refreshTitle();
}

static void mouseButtonCB(GLFWwindow* w, int button, int action, int){
    App& a = *g_app;
    if (button == GLFW_MOUSE_BUTTON_LEFT){
        a.dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &a.lastX, &a.lastY);
    }
}
static void cursorCB(GLFWwindow*, double x, double y){
    App& a = *g_app;
    if (!a.dragging) return;
    double dx = x - a.lastX, dy = y - a.lastY;
    a.lastX = x; a.lastY = y;
    a.az -= dx * 0.005;
    a.el += dy * 0.005;
    a.el = std::clamp(a.el, -1.45, 1.45);
}
static void scrollCB(GLFWwindow*, double, double dy){
    App& a = *g_app;
    a.dist *= std::exp(-dy * 0.12);
    a.dist = std::clamp(a.dist, 5.0, 120.0);
}
static void framebufferCB(GLFWwindow*, int w, int h){
    App& a = *g_app;
    a.winW = w; a.winH = h;
}

static void printHelp(){
    std::printf(
"\n  ============================================================\n"
"   Schwarzschild Black Hole  -  real-time GR ray-tracer\n"
"  ============================================================\n"
"   Mouse drag         orbit camera (azimuth / inclination)\n"
"   Scroll             zoom (camera distance)\n"
"   1 / 2 / 3          Lensing view / Spacetime grid / Split screen\n"
"   D                  toggle accretion disk\n"
"   L                  toggle gravitational lensing (curvature on/off)\n"
"   R                  toggle relativistic redshift + Doppler beaming\n"
"   S                  toggle background starfield\n"
"   H                  toggle coronal halo / infall animation\n"
"   P                  save a PNG screenshot of the current frame\n"
"   [ / ]             fewer / more ray-marching steps (quality)\n"
"   - / =             lower / higher internal render resolution\n"
"   Space              pause / resume the disk animation\n"
"   Q / Esc            quit\n"
"  ------------------------------------------------------------\n"
"   Units: G=c=1, rs=1. Photon sphere r=1.5, ISCO r=3,\n"
"   critical impact parameter b=3sqrt(3)/2=2.598.\n"
"   Run with --selftest to verify the physics numerically.\n"
"  ============================================================\n\n");
}

// ----------------------------------------------------------------------------
//  main
// ----------------------------------------------------------------------------
int main(int argc, char** argv){
    setvbuf(stdout, nullptr, _IOLBF, 0);   // show console output immediately
    // ---- parse CLI ----
    bool selftest = false, headless = false;
    std::string outPath = "blackhole_frame.png";
    std::string modeArg = "bh";
    int reqW = 1280, reqH = 800;
    double fixedTime = 0.0;
    int benchFrames = 0;
    int autoFrames = 0;              // interactive: auto-quit after N frames (0 = never)
    App app;                         // holds all tunables; CLI overrides below
    for (int i = 1; i < argc; ++i){
        std::string s = argv[i];
        if (s == "--selftest") selftest = true;
        else if (s == "--headless" && i+1 < argc){ headless = true; outPath = argv[++i]; }
        else if (s == "--mode" && i+1 < argc) modeArg = argv[++i];
        else if (s == "--w" && i+1 < argc) reqW = std::atoi(argv[++i]);
        else if (s == "--h" && i+1 < argc) reqH = std::atoi(argv[++i]);
        else if (s == "--time" && i+1 < argc) fixedTime = std::atof(argv[++i]);
        // physics / camera overrides (handy for headless tuning)
        else if (s == "--exposure" && i+1 < argc) app.exposure   = std::atof(argv[++i]);
        else if (s == "--bright"   && i+1 < argc) app.diskBright = std::atof(argv[++i]);
        else if (s == "--corona"   && i+1 < argc) app.corona     = std::atof(argv[++i]);
        else if (s == "--tpeak"    && i+1 < argc) app.tpeak      = std::atof(argv[++i]);
        else if (s == "--dist"     && i+1 < argc) app.dist       = std::atof(argv[++i]);
        else if (s == "--el"       && i+1 < argc) app.el         = std::atof(argv[++i]);
        else if (s == "--az"       && i+1 < argc) app.az         = std::atof(argv[++i]);
        else if (s == "--fov"      && i+1 < argc) app.fovDeg     = std::atof(argv[++i]);
        else if (s == "--diskouter"&& i+1 < argc) app.diskOuter  = std::atof(argv[++i]);
        else if (s == "--inner"    && i+1 < argc) app.diskInner  = std::atof(argv[++i]);
        else if (s == "--steps"    && i+1 < argc) app.steps      = std::atoi(argv[++i]);
        else if (s == "--bench"    && i+1 < argc) benchFrames    = std::atoi(argv[++i]);
        else if (s == "--frames"   && i+1 < argc) autoFrames     = std::atoi(argv[++i]);
        // physics-comparison switches (also available as keys at runtime)
        else if (s == "--flat")    app.lensing = false;   // straight rays
        else if (s == "--norel")   app.rel     = false;   // no redshift/Doppler
        else if (s == "--nodisk")  app.disk    = false;
        else if (s == "--nostars") app.stars   = false;
        else if (s == "--nohalo")  app.halo    = false;
        else if (s == "--help" || s == "-h"){ printHelp(); return 0; }
    }
    if (selftest) return runSelfTest();

    g_app = &app;
    if (modeArg == "grid") app.mode = MODE_GRID;
    else if (modeArg == "split") app.mode = MODE_SPLIT;
    if (headless){
        app.winW = reqW; app.winH = reqH;
        app.renderScale = 1.0f;
        app.simTime = fixedTime;
    }

    if (!glfwInit()){
        std::fprintf(stderr, "  glfwInit() failed (no window server?).\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, headless ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    app.window = glfwCreateWindow(app.winW, app.winH,
                                  "Schwarzschild Black Hole", nullptr, nullptr);
    if (!app.window){
        std::fprintf(stderr, "  glfwCreateWindow() failed. A GPU/OpenGL 3.3 context\n"
                             "  could not be created (headless machine?).\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, keyCB);
    glfwSetMouseButtonCallback(app.window, mouseButtonCB);
    glfwSetCursorPosCallback(app.window, cursorCB);
    glfwSetScrollCallback(app.window, scrollCB);
    glfwSetFramebufferSizeCallback(app.window, framebufferCB);

    std::printf("\n  GL_VERSION : %s\n", (const char*)glGetString(GL_VERSION));
    std::printf("  GLSL       : %s\n", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::printf("  Renderer   : %s\n\n", (const char*)glGetString(GL_RENDERER));

    if (!setupGL()){ glfwTerminate(); return 1; }

    // initial framebuffer size (use the real framebuffer, which may be retina-2x)
    int fbw, fbh;
    glfwGetFramebufferSize(app.window, &fbw, &fbh);
    app.winW = fbw; app.winH = fbh;
    ensureFB(app, fbw, fbh);
    refreshTitle();

    // ---- headless: render one frame (or N for a benchmark), save PNG, quit ----
    if (headless){
        Cam cam = computeCamera(app);
        if (benchFrames > 0){
            glFinish();
            double t0 = glfwGetTime();
            for (int i = 0; i < benchFrames; ++i){
                app.simTime += 1.0 / 60.0;
                cam = computeCamera(app);
                renderScene(app, cam);
                glFinish();
            }
            double dt = glfwGetTime() - t0;
            std::printf("  benchmark: %d frames / %.3f s  =  %.2f ms/frame  (%.1f FPS)"
                        "  @ %dx%d internal, %d steps\n",
                        benchFrames, dt, 1000.0*dt/benchFrames,
                        benchFrames/dt, g_fb.w, g_fb.h, app.steps);
        } else {
            renderScene(app, cam);
            glFinish();
        }
        readFrameToPNG(outPath.c_str());
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 0;
    }

    printHelp();
    std::printf("  Rendering in real time. Drag to orbit, scroll to zoom.\n\n");

    // ---- interactive loop ----
    double last = glfwGetTime();
    double fpsAccum = 0.0; int fpsCount = 0; double fpsTimer = 0.0;
    long frameNo = 0;
    bool glChecked = false;
    while (!glfwWindowShouldClose(app.window)){
        double now = glfwGetTime();
        double dt = now - last; last = now;
        if (!app.paused) app.simTime += dt;
        app.wallTime += dt;

        glfwPollEvents();

        int w, h;
        glfwGetFramebufferSize(app.window, &w, &h);
        app.winW = w; app.winH = h;
        ensureFB(app, w, h);

        Cam cam = computeCamera(app);
        renderScene(app, cam);

        if (app.saveScreenshot){
            app.saveScreenshot = false;
            char name[128];
            std::snprintf(name, sizeof(name), "blackhole_%06.1fs.png", app.wallTime);
            readFrameToPNG(name);
        }

        // present: draw the offscreen scene texture to the window
        present(app, w, h);

        glfwSwapBuffers(app.window);

        // one-time sanity check on the whole CPU->GPU pipeline
        if (!glChecked){
            GLenum e = glGetError();
            if (e != GL_NO_ERROR)
                std::fprintf(stderr, "  [warning] GL error 0x%x after present\n", (unsigned)e);
            else
                std::printf("  GL pipeline OK (no GL errors on first presented frame).\n");
            glChecked = true;
        }

        frameNo++;
        if (autoFrames > 0 && frameNo >= autoFrames)
            glfwSetWindowShouldClose(app.window, 1);

        fpsAccum += dt; fpsCount++; fpsTimer += dt;
        if (fpsTimer > 0.6){
            char t[320];
            std::snprintf(t, sizeof(t),
                "Schwarzschild BH | %s | %.0f FPS | step=%d res=%.2f | Disk:%s Lens:%s Rel:%s Stars:%s | Rs=1 r_ISCO=3rs",
                modeName(app.mode), fpsCount/fpsAccum, app.steps, (double)app.renderScale,
                app.disk?"ON":"off", app.lensing?"ON":"off", app.rel?"ON":"off", app.stars?"ON":"off");
            glfwSetWindowTitle(app.window, t);
            fpsAccum = 0; fpsCount = 0; fpsTimer = 0;
        }
    }

    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
