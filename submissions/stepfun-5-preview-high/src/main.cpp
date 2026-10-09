// ---------------------------------------------------------------------------
//  black-hole-cpp-step5
//  Real-time, scientifically-grounded black hole ray tracer (macOS / OpenGL).
//
//    * Schwarzschild null geodesics integrated on the GPU (RK4, adaptive
//      step)  ->  gravitational lensing, photon ring, black-hole shadow.
//    * Relativistic accretion disk (thin disk, ISCO = 6M, T(r) ~ r^-3/4,
//      blackbody colour, gravitational + Doppler redshift, g^4 beaming).
//    * Lens-warped 3D spacetime grid + Flamm paraboloid "trapdoor" view.
//    * Bloom halo around the photon ring and bright stars.
//    * Temporal accumulation (TAA-style) for a clean image when the camera
//      is still, single-sample while moving => runs fully in real time.
//
//  Controls (see printHelp).
// ---------------------------------------------------------------------------
#define GL_SILENCE_DEPRECATION 1
#define GLFW_INCLUDE_NONE 1     // keep macOS gl.h out; we use OpenGL/gl3.h

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
  #include <OpenGL/gl3.h>
#else
  #error "This project targets macOS (OpenGL 4.1 core profile, Metal-backed)."
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <sys/stat.h>

#include "shaders_embedded.h"

// ------------------------------ helpers ------------------------------------
static void die(const char *msg)
{
    fprintf(stderr, "\n*** FATAL: %s\n", msg);
    exit(1);
}

static std::string readFile(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0)
    {
        size_t got = fread(&s[0], 1, size_t(n), f);
        s.resize(got);
    }
    fclose(f);
    return s;
}

static double fileMTime(const std::string &path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return -1.0;
    return double(st.st_mtime) + double(st.st_mtimespec.tv_nsec) * 1e-9;
}

extern "C" int _NSGetExecutablePath(char *buf, unsigned int *size);
static std::string getExeDir()
{
    unsigned int sz = 4096;
    std::vector<char> buf(sz);
    if (_NSGetExecutablePath(buf.data(), &sz) != 0) return ".";
    std::string p(buf.data());
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

// ------------------------------ shader programs -----------------------------
struct Program
{
    GLuint id = 0;
    GLint loc(const char *name) const { return glGetUniformLocation(id, name); }
};

static GLuint compileStage(GLenum type, const std::string &src, const std::string &tag)
{
    GLuint s = glCreateShader(type);
    const char *c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[8192];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "\n*** shader compile error [%s]\n%s\n", tag.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool buildProgram(Program &p, const std::vector<std::pair<GLenum, std::string>> &stages,
                         const std::vector<std::string> &tags)
{
    std::vector<GLuint> ids;
    for (size_t i = 0; i < stages.size(); ++i)
    {
        GLuint s = compileStage(stages[i].first, stages[i].second, tags[i]);
        if (!s)
        {
            for (auto x : ids) glDeleteShader(x);
            return false;
        }
        ids.push_back(s);
    }
    GLuint prog = glCreateProgram();
    for (auto s : ids) glAttachShader(prog, s);
    glLinkProgram(prog);
    for (auto s : ids) glDeleteShader(s);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[8192];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "\n*** program link error\n%s\n", log);
        glDeleteProgram(prog);
        return false;
    }
    if (p.id) glDeleteProgram(p.id);
    p.id = prog;
    return true;
}

// directory holding the .glsl sources (empty => use embedded copies)
static std::string g_shaderDir;
static std::string findShader(const std::string &name)
{
    std::vector<std::string> dirs;
    if (!g_shaderDir.empty()) dirs.push_back(g_shaderDir);
    if (const char *env = getenv("BLACKHOLE_SHADERS")) dirs.push_back(env);
    dirs.push_back("shaders");
    dirs.push_back(getExeDir() + "/shaders");
    dirs.push_back(getExeDir() + "/../shaders");
#ifdef BLACKHOLE_SHADERS_DIR
    dirs.push_back(BLACKHOLE_SHADERS_DIR);
#endif
    for (auto &d : dirs)
    {
        std::string s = readFile(d + "/" + name);
        if (!s.empty())
        {
            g_shaderDir = d;
            return s;
        }
    }
    for (auto &e : kEmbeddedShaders)
        if (name == e.first) return e.second;
    fprintf(stderr, "shader source '%s' not found (disk or embedded)\n", name.c_str());
    return {};
}

// ------------------------------ render targets ------------------------------
struct Target
{
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;

    void make(int w_, int h_, GLenum internal = GL_RGBA16F)
    {
        release();
        w = std::max(1, w_);
        h = std::max(1, h_);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            die("framebuffer incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void release()
    {
        if (tex) glDeleteTextures(1, &tex);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        tex = fbo = 0;
        w = h = 0;
    }
};

// ------------------------------ application ---------------------------------
struct App
{
    GLFWwindow *win = nullptr;
    int fbW = 0, fbH = 0;             // framebuffer size (device pixels)

    Target accum[2];                  // ping-pong accumulation (scene res)
    Target bloomA, bloomB;            // bloom chain (scene res / 2)
    int accumIdx = 0;
    GLuint vao = 0;

    Program progScene, progBright, progBlur, progComposite;

    // camera
    float yaw = 0.9f, pitch = 0.55f, dist = 20.0f;
    float fovDeg = 52.0f;
    int   winW = 1280, winH = 800;

    // scene settings
    int   scene = 0;                  // 0 full, 1 grid, 2 lensing, 3 funnel
    int   layers = 1 | 2 | 4;         // stars | disk | grid
    float exposure = 1.0f;
    float bloomStrength = 0.85f;
    float renderScale = 0.9f;
    int   maxSteps = 320;
    float gridSpacing = 5.0f;
    float timeScale = 1.0f;
    bool  paused = false;
    float simTime = 0.0f;

    // accumulation
    int   sampleCount = 0;
    float accumAlphaMax = 0.05f;
    int   accumWarmup = 48;
    bool  singleSample = false;   // --single: raw single-sample frames

    // input
    bool dragging = false;
    double lastX = 0, lastY = 0;
    std::array<bool, 512> prevKeys{};

    // scripted exit
    int   exitFrames = 0;
    float exitSeconds = 0.0f;
    long  frameIndex = 0;

    // stats
    double lastTitleTime = 0.0;
    int    frames = 0;
    double fps = 0.0;

    // ---------------- shaders -------------------------------------------------
    bool loadPrograms()
    {
        std::string vert = findShader("quad.vert");
        bool ok = true;
        ok &= buildProgram(progScene,
                           {{GL_VERTEX_SHADER, vert}, {GL_FRAGMENT_SHADER, findShader("blackhole.frag")}},
                           {"quad.vert", "blackhole.frag"});
        ok &= buildProgram(progBright,
                           {{GL_VERTEX_SHADER, vert}, {GL_FRAGMENT_SHADER, findShader("brightpass.frag")}},
                           {"quad.vert", "brightpass.frag"});
        ok &= buildProgram(progBlur,
                           {{GL_VERTEX_SHADER, vert}, {GL_FRAGMENT_SHADER, findShader("blur.frag")}},
                           {"quad.vert", "blur.frag"});
        ok &= buildProgram(progComposite,
                           {{GL_VERTEX_SHADER, vert}, {GL_FRAGMENT_SHADER, findShader("composite.frag")}},
                           {"quad.vert", "composite.frag"});
        return ok;
    }

    // hot reload: if the .glsl files change on disk, recompile while running
    void maybeHotReload()
    {
        static const char *names[5] = {"quad.vert", "blackhole.frag", "brightpass.frag",
                                       "blur.frag", "composite.frag"};
        static double times[5] = {-1, -1, -1, -1, -1};
        static double lastCheck = 0.0;
        double now = glfwGetTime();
        if (g_shaderDir.empty() || now - lastCheck < 1.0) return;
        lastCheck = now;
        bool changed = false;
        for (int i = 0; i < 5; ++i)
        {
            double m = fileMTime(g_shaderDir + "/" + names[i]);
            if (m < 0) return;                     // files vanished: keep current
            if (times[i] > 0.0 && std::abs(m - times[i]) > 1e-6) changed = true;
            times[i] = m;
        }
        if (changed)
        {
            printf("[hot-reload] shaders changed on disk, recompiling...\n");
            if (loadPrograms()) printf("[hot-reload] OK\n");
        }
    }

    // ---------------- camera --------------------------------------------------
    void camPos(float &x, float &y, float &z) const
    {
        x = dist * std::cos(pitch) * std::cos(yaw);
        y = dist * std::sin(pitch);
        z = dist * std::cos(pitch) * std::sin(yaw);
    }

    bool camMoved()
    {
        static bool have = false;
        static float ly = 0, lp = 0, ld = 0, lf = 0;
        bool moved = !have || ly != yaw || lp != pitch || ld != dist || lf != fovDeg;
        ly = yaw; lp = pitch; ld = dist; lf = fovDeg; have = true;
        return moved;
    }

    // ---------------- targets / accumulation ---------------------------------
    void resizeTargets()
    {
        // cap the rendered pixel count (retina framebuffers are 2x) so the
        // integrator stays at interactive frame rates; accumulation takes
        // care of image quality when the camera is still.
        const double maxPixels = 2.6e6;
        double scale = renderScale;
        double px = double(fbW) * double(fbH) * scale * scale;
        if (px > maxPixels) scale *= std::sqrt(maxPixels / px);
        int w = std::max(1, int(fbW * scale));
        int h = std::max(1, int(fbH * scale));
        if (accum[0].w == w && accum[0].h == h && bloomA.fbo) return;
        accum[0].make(w, h);
        accum[1].make(w, h);
        bloomA.make(w / 2, h / 2);
        bloomB.make(w / 2, h / 2);
        resetAccum();
    }

    void resetAccum() { sampleCount = 0; }

    void printHelp()
    {
        printf(
            "\n"
            "================================================================\n"
            "  BLACK HOLE -- Schwarzschild geodesic ray tracer (real time)\n"
            "================================================================\n"
            "  mouse drag    orbit the camera\n"
            "  wheel         zoom in / out\n"
            "  1 2 3 4       scene:  full / spacetime grid / lensing / funnel\n"
            "  S D G         toggle stars / disk / spacetime grid\n"
            "  J L I K       orbit (yaw / pitch)        U O   zoom\n"
            "  up/down       exposure            left/right  bloom strength\n"
            "  [ ]           render scale        - / =       integration steps\n"
            "  , .           disk time scale     space        pause\n"
            "  R             reset camera+settings   H help    ESC quit\n"
            "  F             fullscreen\n"
            "================================================================\n\n");
    }
};

static App g;

// ------------------------------ GLFW callbacks -------------------------------
static void cbFramebufferSize(GLFWwindow *, int w, int h)
{
    g.fbW = std::max(1, w);
    g.fbH = std::max(1, h);
}

static void cbMouseButton(GLFWwindow *w, int button, int action, int)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS)
    {
        g.dragging = true;
        glfwGetCursorPos(w, &g.lastX, &g.lastY);
    }
    else g.dragging = false;
}

static void cbCursorPos(GLFWwindow *, double x, double y)
{
    if (!g.dragging) return;
    float dx = float(x - g.lastX), dy = float(y - g.lastY);
    g.lastX = x; g.lastY = y;
    g.yaw   -= dx * 0.0055f;
    g.pitch += dy * 0.0055f;
    g.pitch = std::clamp(g.pitch, -1.55f, 1.55f);
}

static void cbScroll(GLFWwindow *, double, double dy)
{
    g.dist = std::clamp(g.dist * std::exp(-float(dy) * 0.12f), 3.2f, 90.0f);
}

static void cbKey(GLFWwindow *w, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q)
        glfwSetWindowShouldClose(w, GLFW_TRUE);
}

// ------------------------------ drawing --------------------------------------
static void drawFullscreen()
{
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void bindTarget(const Target &t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
}

// ------------------------------ main -----------------------------------------
int main(int argc, char **argv)
{
    // ---- command line options (make the sim scriptable / reproducible) ----
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto nextF = [&](float def) { return (i + 1 < argc) ? float(atof(argv[++i])) : def; };
        auto nextI = [&](int def)   { return (i + 1 < argc) ? atoi(argv[++i]) : def; };
        if      (a == "--shaders" && i + 1 < argc) g_shaderDir = argv[++i];
        else if (a == "--yaw")      g.yaw = nextF(g.yaw);
        else if (a == "--pitch")    g.pitch = std::clamp(nextF(g.pitch), -1.55f, 1.55f);
        else if (a == "--dist")     g.dist = std::clamp(nextF(g.dist), 3.2f, 90.0f);
        else if (a == "--fov")      g.fovDeg = std::clamp(nextF(g.fovDeg), 10.0f, 100.0f);
        else if (a == "--scene")
        {
            g.scene = std::clamp(nextI(0), 0, 3);
            g.layers = (g.scene == 0) ? 7 : (g.scene == 1) ? 4 : (g.scene == 2) ? 1 : g.layers;
        }
        else if (a == "--layers")   g.layers = nextI(g.layers);
        else if (a == "--scale")    g.renderScale = std::clamp(nextF(g.renderScale), 0.4f, 1.25f);
        else if (a == "--steps")    g.maxSteps = std::clamp(nextI(g.maxSteps), 64, 640);
        else if (a == "--exposure") g.exposure = std::clamp(nextF(g.exposure), 0.05f, 12.0f);
        else if (a == "--bloom")    g.bloomStrength = std::clamp(nextF(g.bloomStrength), 0.0f, 3.0f);
        else if (a == "--timescale")g.timeScale = std::clamp(nextF(g.timeScale), 0.0f, 4.0f);
        else if (a == "--grid")     g.gridSpacing = std::clamp(nextF(g.gridSpacing), 0.5f, 8.0f);
        else if (a == "--paused")   g.paused = true;
        else if (a == "--single")   g.singleSample = true;
        else if (a == "--exit-frames")   g.exitFrames = std::max(0, nextI(0));
        else if (a == "--exit-seconds")  g.exitSeconds = nextF(0.0f);
        else if (a == "--help")
        {
            printf("usage: black-hole [--shaders DIR] [--scene 0..3] [--yaw R] [--pitch R]\n"
                   "                  [--dist R] [--fov DEG] [--layers N] [--scale F]\n"
                   "                  [--steps N] [--exposure F] [--bloom F] [--timescale F]\n"
                   "                  [--grid F] [--paused] [--single]\n"
                   "                  [--exit-frames N] [--exit-seconds F]\n");
            return 0;
        }
    }

    if (!glfwInit()) die("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWmonitor *mon = glfwGetPrimaryMonitor();
    const GLFWvidmode *vm = mon ? glfwGetVideoMode(mon) : nullptr;
    g.winW = vm ? std::min(1600, vm->width - 80) : 1280;
    g.winH = vm ? std::min(1000, vm->height - 80) : 800;

    g.win = glfwCreateWindow(g.winW, g.winH, "Black Hole - Schwarzschild Ray Tracer", nullptr, nullptr);
    if (!g.win) die("glfwCreateWindow failed");
    glfwMakeContextCurrent(g.win);
    glfwSwapInterval(1);

    glfwGetFramebufferSize(g.win, &g.fbW, &g.fbH);
    glfwSetWindowUserPointer(g.win, &g);
    glfwSetFramebufferSizeCallback(g.win, cbFramebufferSize);
    glfwSetMouseButtonCallback(g.win, cbMouseButton);
    glfwSetCursorPosCallback(g.win, cbCursorPos);
    glfwSetScrollCallback(g.win, cbScroll);
    glfwSetKeyCallback(g.win, cbKey);

    printf("GL_VERSION  : %s\nGL_RENDERER : %s\n",
           (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    if (!g.loadPrograms()) die("failed to compile shaders");

    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);          // core profile requires a bound VAO
    g.resizeTargets();
    g.printHelp();

    double lastFrame = glfwGetTime();
    double simAccum = 0.0;
    double startTime = glfwGetTime();

    while (!glfwWindowShouldClose(g.win))
    {
        double now = glfwGetTime();
        double dt = std::min(now - lastFrame, 0.1);
        lastFrame = now;
        simAccum += g.paused ? 0.0 : dt * g.timeScale;
        g.simTime = float(simAccum);

        glfwPollEvents();
        g.maybeHotReload();

        // ---------------- edge-triggered keys -----------------------------
        auto edge = [&](int k) {
            bool down = glfwGetKey(g.win, k) == GLFW_PRESS;
            bool e = down && !g.prevKeys[k];
            g.prevKeys[k] = down;
            return e;
        };
        if (edge(GLFW_KEY_1)) { g.scene = 0; g.layers = 1 | 2 | 4; g.resetAccum(); }
        if (edge(GLFW_KEY_2)) { g.scene = 1; g.layers = 4;       g.resetAccum(); }
        if (edge(GLFW_KEY_3)) { g.scene = 2; g.layers = 1;       g.resetAccum(); }
        if (edge(GLFW_KEY_4)) { g.scene = 3;                     g.resetAccum(); }
        if (edge(GLFW_KEY_S)) { if (g.scene == 3) { g.scene = 0; g.layers = 4; } g.layers ^= 1; g.resetAccum(); }
        if (edge(GLFW_KEY_D)) { if (g.scene == 3) { g.scene = 0; g.layers = 4; } g.layers ^= 2; g.resetAccum(); }
        if (edge(GLFW_KEY_G)) { if (g.scene == 3) { g.scene = 0; g.layers = 1; } g.layers ^= 4; g.resetAccum(); }
        if (edge(GLFW_KEY_SPACE)) g.paused = !g.paused;
        if (edge(GLFW_KEY_H)) g.printHelp();
        if (edge(GLFW_KEY_UP))   g.exposure = std::min(g.exposure * 1.15f, 12.0f);
        if (edge(GLFW_KEY_DOWN)) g.exposure = std::max(g.exposure / 1.15f, 0.05f);
        if (edge(GLFW_KEY_RIGHT)) g.bloomStrength = std::min(g.bloomStrength * 1.15f, 3.0f);
        if (edge(GLFW_KEY_LEFT))  g.bloomStrength = std::max(g.bloomStrength / 1.15f, 0.0f);
        if (edge(GLFW_KEY_J)) { g.yaw   -= 0.06f; }
        if (edge(GLFW_KEY_L)) { g.yaw   += 0.06f; }
        if (edge(GLFW_KEY_I)) { g.pitch  = std::min(g.pitch + 0.05f, 1.55f); }
        if (edge(GLFW_KEY_K)) { g.pitch  = std::max(g.pitch - 0.05f, -1.55f); }
        if (edge(GLFW_KEY_U)) { g.dist   = std::max(3.2f, g.dist * 0.88f); }
        if (edge(GLFW_KEY_O)) { g.dist   = std::min(90.0f, g.dist / 0.88f); }
        if (edge(GLFW_KEY_LEFT_BRACKET))  { g.renderScale = std::max(0.4f,  g.renderScale - 0.1f); g.resizeTargets(); }
        if (edge(GLFW_KEY_RIGHT_BRACKET)) { g.renderScale = std::min(1.25f, g.renderScale + 0.1f); g.resizeTargets(); }
        if (edge(GLFW_KEY_MINUS)) g.maxSteps = std::max(64, g.maxSteps - 32);
        if (edge(GLFW_KEY_EQUAL)) g.maxSteps = std::min(640, g.maxSteps + 32);
        if (edge(GLFW_KEY_COMMA)) g.timeScale = std::max(0.0f, g.timeScale - 0.25f);
        if (edge(GLFW_KEY_PERIOD)) g.timeScale = std::min(4.0f, g.timeScale + 0.25f);
        if (edge(GLFW_KEY_R))
        {
            g.yaw = 0.9f; g.pitch = 0.55f; g.dist = 20.0f;
            g.exposure = 1.0f; g.bloomStrength = 0.85f;
            g.scene = 0; g.layers = 1 | 2 | 4;
            g.renderScale = 0.9f; g.maxSteps = 320;
            g.timeScale = 1.0f; g.gridSpacing = 5.0f;
            g.resizeTargets();
        }
        if (edge(GLFW_KEY_F))
        {
            static bool full = false;
            full = !full;
            const GLFWvidmode *v = glfwGetVideoMode(glfwGetPrimaryMonitor());
            glfwSetWindowMonitor(g.win, full ? glfwGetPrimaryMonitor() : nullptr,
                                 full ? 0 : 100, full ? 0 : 100,
                                 full ? v->width : g.winW, full ? v->height : g.winH, 0);
        }

        // ---------------- camera / accumulation ---------------------------
        if (g.camMoved()) g.resetAccum();
        glfwGetFramebufferSize(g.win, &g.fbW, &g.fbH);
        if (g.fbW > 0 && g.fbH > 0) g.resizeTargets();

        int sw = g.accum[g.accumIdx].w, sh = g.accum[g.accumIdx].h;

        // camera basis (raw floats, no external math lib)
        float cx, cy, cz;
        g.camPos(cx, cy, cz);
        float fx = -cx, fy = -cy, fz = -cz;
        float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fl; fy /= fl; fz /= fl;
        float rx = fz, ry = 0.0f, rz = -fx;            // right = fwd x (0,1,0)
        float rl = std::sqrt(rx * rx + rz * rz);
        if (rl < 1e-6f) { rx = 1.f; ry = 0.f; rz = 0.f; }
        else { rx /= rl; rz /= rl; }
        float ux = ry * fz - rz * fy;                  // up = right x fwd
        float uy = rz * fx - rx * fz;
        float uz = rx * fy - ry * fx;

        // sub-pixel jitter (temporal AA)
        float jx = 0.f, jy = 0.f;
        if (!g.singleSample && g.sampleCount > 0)
        {
            unsigned s = unsigned(g.sampleCount) * 1664525u + 1013904223u;
            jx = float((s >> 8) & 1023) / 1023.f - 0.5f;
            jy = float((s >> 18) & 1023) / 1023.f - 0.5f;
        }
        float alpha = g.singleSample ? 1.0f
                     : (g.sampleCount < g.accumWarmup ? 1.0f / float(g.sampleCount + 1) : g.accumAlphaMax);
        if (g.singleSample) g.sampleCount = 0;

        // ---------------- pass 1 : ray trace -> accumulation ---------------
        bindTarget(g.accum[g.accumIdx]);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendColor(0, 0, 0, alpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

        glUseProgram(g.progScene.id);
        glUniform2f(g.progScene.loc("uResolution"), float(sw), float(sh));
        glUniform3f(g.progScene.loc("uCamPos"), cx, cy, cz);
        glUniform3f(g.progScene.loc("uCamFwd"), fx, fy, fz);
        glUniform3f(g.progScene.loc("uCamRight"), rx, ry, rz);
        glUniform3f(g.progScene.loc("uCamUp"), ux, uy, uz);
        glUniform1f(g.progScene.loc("uTanHalfFov"), std::tan(g.fovDeg * float(M_PI) / 360.0f));
        glUniform2f(g.progScene.loc("uJitter"), jx, jy);
        glUniform1f(g.progScene.loc("uTime"), g.simTime);
        glUniform1i(g.progScene.loc("uMaxSteps"), g.maxSteps);
        glUniform1f(g.progScene.loc("uStepScale"), 1.0f);
        glUniform1i(g.progScene.loc("uLayers"), g.layers);
        glUniform1i(g.progScene.loc("uScene"), g.scene);
        glUniform1f(g.progScene.loc("uGridSpacing"), g.gridSpacing);
        glUniform1f(g.progScene.loc("uGridGain"),    g.scene == 1 ? 3.2f : 1.0f);
        glUniform1f(g.progScene.loc("uGridFade"),    g.scene == 1 ? 0.016f : 0.050f);
        glUniform1f(g.progScene.loc("uTimeScale"), g.timeScale);
        drawFullscreen();
        glDisable(GL_BLEND);

        g.sampleCount = std::min(g.sampleCount + 1, 4096);

        // ---------------- pass 2 : bloom / halo ---------------------------
        glUseProgram(g.progBright.id);
        glUniform1i(g.progBright.loc("uTex"), 0);
        glUniform2f(g.progBright.loc("uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        glUniform1f(g.progBright.loc("uThreshold"), 1.15f);
        glUniform1f(g.progBright.loc("uKnee"), 0.60f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g.accum[g.accumIdx].tex);
        bindTarget(g.bloomA);
        drawFullscreen();

        glUseProgram(g.progBlur.id);
        glUniform1i(g.progBlur.loc("uTex"), 0);
        glUniform2f(g.progBlur.loc("uTexel"), 2.0f / float(sw), 2.0f / float(sh));
        const float radius[3] = { 1.0f, 2.5f, 6.0f };
        for (int p = 0; p < 3; ++p)
        {
            float r = radius[p];
            glBindTexture(GL_TEXTURE_2D, g.bloomA.tex);
            glUniform2f(g.progBlur.loc("uDir"), r, 0.0f);
            bindTarget(g.bloomB);
            drawFullscreen();

            glBindTexture(GL_TEXTURE_2D, g.bloomB.tex);
            glUniform2f(g.progBlur.loc("uDir"), 0.0f, r);
            bindTarget(g.bloomA);
            drawFullscreen();
        }

        // ---------------- pass 3 : composite to screen --------------------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g.fbW, g.fbH);
        glUseProgram(g.progComposite.id);
        glUniform1i(g.progComposite.loc("uScene"), 0);
        glUniform1i(g.progComposite.loc("uBloom"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g.accum[g.accumIdx].tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g.bloomA.tex);
        glUniform1f(g.progComposite.loc("uBloomStrength"), g.bloomStrength);
        glUniform1f(g.progComposite.loc("uExposure"), g.exposure);
        glUniform1f(g.progComposite.loc("uVignette"), 1.0f);
        glUniform1f(g.progComposite.loc("uTime"), float(now));
        drawFullscreen();
        glActiveTexture(GL_TEXTURE0);

        g.accumIdx = 1 - g.accumIdx;
        glfwSwapBuffers(g.win);

        if ((g.exitFrames > 0 && g.frameIndex >= g.exitFrames) ||
            (g.exitSeconds > 0.0 && now - startTime >= g.exitSeconds))
            glfwSetWindowShouldClose(g.win, GLFW_TRUE);

        // ---------------- stats -------------------------------------------
        g.frameIndex++;
        g.frames++;
        if (now - g.lastTitleTime > 0.5)
        {
            g.fps = g.frames / (now - g.lastTitleTime);
            g.frames = 0;
            g.lastTitleTime = now;
            const char *sn[] = {"full", "grid", "lensing", "funnel"};
            char title[256];
            snprintf(title, sizeof(title),
                     "Black Hole | %s | stars%d disk%d grid%d | %.0f fps | samples %d | scale %.2f | steps %d%s | t x%.2f",
                     sn[std::clamp(g.scene, 0, 3)],
                     g.layers & 1, (g.layers >> 1) & 1, (g.layers >> 2) & 1,
                     g.fps, g.sampleCount, g.renderScale, g.maxSteps,
                     g.paused ? " PAUSED" : "", g.timeScale);
            glfwSetWindowTitle(g.win, title);
        }
    }

    glfwDestroyWindow(g.win);
    glfwTerminate();
    return 0;
}
