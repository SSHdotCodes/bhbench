// ============================================================================
// Real-time Schwarzschild black hole — C++17 / OpenGL 4.1 (GPU ray tracer).
//
//   * gravitational lensing : per-pixel null-geodesic integration (RK4) of
//     d2u/dphi2 = 3u^2 - u in the photon's orbital plane (see blackhole.frag)
//   * accretion disk        : Novikov-Thorne temperature profile, exact
//     Doppler + gravitational redshift, relativistic beaming (I ~ g^4),
//     HDR bloom for the halos
//   * spacetime curvature   : lensed Schwarzschild coordinate grid (G) and a
//     Flamm-paraboloid embedding "trapdoor" view (TAB)
//
// Geometric units G = c = M = 1: horizon r = 2, photon sphere r = 3, ISCO 6.
// ============================================================================
#include <OpenGL/gl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ------------------------------------------------------------ tiny math ----
struct V3 { double x, y, z; };
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 vcross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static V3 vnorm(V3 a) {
    double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return {a.x / l, a.y / l, a.z / l};
}

struct M4 { float m[16]; };  // column-major
static M4 m4mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
static M4 m4persp(float fovy, float aspect, float zn, float zf) {
    M4 r{};
    float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
static M4 m4lookAt(V3 eye, V3 at, V3 up) {
    V3 f = vnorm(at - eye), s = vnorm(vcross(f, up)), u = vcross(s, f);
    M4 r{};
    r.m[0] = (float)s.x; r.m[4] = (float)s.y; r.m[8]  = (float)s.z;
    r.m[1] = (float)u.x; r.m[5] = (float)u.y; r.m[9]  = (float)u.z;
    r.m[2] = (float)-f.x; r.m[6] = (float)-f.y; r.m[10] = (float)-f.z;
    r.m[12] = (float)-(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = (float)-(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (float)(f.x * eye.x + f.y * eye.y + f.z * eye.z);
    r.m[15] = 1.0f;
    return r;
}

// ------------------------------------------------------- PNG screenshot ----
static uint32_t crcTab[256];
static void crcInit() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTab[i] = c;
    }
}
static uint32_t crc32b(const uint8_t* d, size_t n, uint32_t c) {
    for (size_t i = 0; i < n; i++) c = crcTab[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void pngChunk(std::ofstream& f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hdr;
    be32(hdr, (uint32_t)data.size());
    f.write((const char*)hdr.data(), 4);
    f.write(type, 4);
    if (!data.empty()) f.write((const char*)data.data(), data.size());
    uint32_t c = crc32b((const uint8_t*)type, 4, 0xFFFFFFFFu);
    if (!data.empty()) c = crc32b(data.data(), data.size(), c);
    std::vector<uint8_t> crc;
    be32(crc, c ^ 0xFFFFFFFFu);
    f.write((const char*)crc.data(), 4);
}
// rgb: tightly packed RGB rows, top row first
static bool savePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    crcInit();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    f.write((const char*)sig, 8);
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(2);       // 8-bit RGB
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    pngChunk(f, "IHDR", ihdr);

    std::vector<uint8_t> raw;                    // filter byte 0 + each row
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3,
                   rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    uint32_t a = 1, b = 0;                       // adler32
    for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }

    std::vector<uint8_t> idat;                   // zlib stream, stored blocks
    idat.push_back(0x78); idat.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        bool last = off + n == raw.size();
        idat.push_back(last ? 1 : 0);
        idat.push_back(n & 0xFF); idat.push_back((n >> 8) & 0xFF);
        idat.push_back(~n & 0xFF); idat.push_back((~n >> 8) & 0xFF);
        idat.insert(idat.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    be32(idat, (b << 16) | a);
    pngChunk(f, "IDAT", idat);
    pngChunk(f, "IEND", {});
    return true;
}

// ------------------------------------------------------------- shaders -----
static std::string loadText(const std::string& name) {
    for (const std::string& dir : {std::string(SHADER_DIR), std::string("shaders")}) {
        std::ifstream f(dir + "/" + name);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    std::fprintf(stderr, "FATAL: cannot open shader '%s' (looked in %s and ./shaders)\n",
                 name.c_str(), SHADER_DIR);
    std::exit(1);
}
static GLuint compileShader(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "FATAL: shader '%s' compile error:\n%s\n", name, log);
        std::exit(1);
    }
    return s;
}
static GLuint makeProgram(const char* vsName, const char* fsName) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, loadText(vsName), vsName);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, loadText(fsName), fsName);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "FATAL: link error (%s + %s):\n%s\n", vsName, fsName, log);
        std::exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}
static void setU1i(GLuint p, const char* n, int v)   { glUniform1i(glGetUniformLocation(p, n), v); }
static void setU1f(GLuint p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
static void setU2f(GLuint p, const char* n, float a, float b) { glUniform2f(glGetUniformLocation(p, n), a, b); }
static void setU3f(GLuint p, const char* n, V3 v) {
    glUniform3f(glGetUniformLocation(p, n), (float)v.x, (float)v.y, (float)v.z);
}

// ------------------------------------------------------------ app state ----
struct App {
    GLFWwindow* win = nullptr;
    int fbW = 0, fbH = 0;
    float renderScale = 1.0f;
    int renderW = 0, renderH = 0;

    GLuint progScene[3] = {0, 0, 0};   // black hole, pulsar, quasar
    GLuint progBright = 0, progBlur = 0, progComp = 0, progFun = 0;
    int scene = 0;
    GLuint progRay = 0;               // alias of progScene[scene]
    GLuint quadVAO = 0, quadVBO = 0;
    GLuint sceneFBO = 0, sceneTex = 0, sceneDepth = 0;
    GLuint bloomFBO[2] = {0, 0}, bloomTex[2] = {0, 0};
    int bloomW = 0, bloomH = 0;

    GLuint funVAO = 0, funVBO = 0; GLsizei funVerts = 0;
    GLuint capVAO = 0, capVBO = 0; GLsizei capVerts = 0;
    GLuint dynVAO = 0, dynVBO = 0;

    // camera (orbits the hole; disk lies in the world x-y plane, z up)
    double alpha = -2.30, beta = 0.14, dist = 26.0;
    bool dragging = false;
    double lastX = 0, lastY = 0;

    double simTime = 0.0;
    bool paused = false, autoOrbit = true;
    bool disk = true, grid = false, stars = true;
    int mode = 0;  // 0 = ray-traced view, 1 = embedding (funnel) view
    float exposure = 1.0f, diskGain = 1.0f, bloomStrength = 0.5f;

    long maxFrames = -1;      // headless-ish test mode: exit after N frames
    std::string shotPath;     // save a PNG on the last frame
    std::string recordPath;   // append raw RGB24 frames here (for ffmpeg rawvideo)
    FILE* recordFile = nullptr;
    int shotCounter = 0;
    bool fbDirty = true;
};
static App app;

// ------------------------------------------------------------- FBOs --------
static GLuint makeHDRTex(int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static void rebuildFBOs() {
    app.renderW = std::max(1, (int)(app.fbW * app.renderScale));
    app.renderH = std::max(1, (int)(app.fbH * app.renderScale));
    app.bloomW = std::max(1, app.renderW / 2);
    app.bloomH = std::max(1, app.renderH / 2);

    if (app.sceneFBO) {
        glDeleteFramebuffers(1, &app.sceneFBO);
        glDeleteTextures(1, &app.sceneTex);
        glDeleteRenderbuffers(1, &app.sceneDepth);
        glDeleteFramebuffers(2, app.bloomFBO);
        glDeleteTextures(2, app.bloomTex);
    }
    glGenFramebuffers(1, &app.sceneFBO);
    app.sceneTex = makeHDRTex(app.renderW, app.renderH);
    glGenRenderbuffers(1, &app.sceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, app.sceneDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, app.renderW, app.renderH);
    glBindFramebuffer(GL_FRAMEBUFFER, app.sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.sceneTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, app.sceneDepth);

    for (int i = 0; i < 2; i++) {
        glGenFramebuffers(1, &app.bloomFBO[i]);
        app.bloomTex[i] = makeHDRTex(app.bloomW, app.bloomH);
        glBindFramebuffer(GL_FRAMEBUFFER, app.bloomFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               app.bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    app.fbDirty = false;
}

// ----------------------------------------------------- funnel geometry -----
// Flamm's paraboloid: the exact isometric embedding of the t = const,
// theta = pi/2 slice of the Schwarzschild metric, z(r) = 2*sqrt(2M(r - 2M)).
// Displayed descending so the horizon sits at the bottom of the throat.
static const double FUN_RMAX = 40.0;
static double funnelZ(double r) {
    return 2.0 * std::sqrt(2.0 * (r - 2.0)) - 2.0 * std::sqrt(2.0 * (FUN_RMAX - 2.0));
}
static void pushV(std::vector<float>& v, double x, double y, double z, double r) {
    v.push_back((float)x); v.push_back((float)y); v.push_back((float)z); v.push_back((float)r);
}
static void buildFunnel() {
    std::vector<float> v;
    const double TWO_PI = 6.28318530717958647692;
    for (double r = 2.005; r <= FUN_RMAX; r += 0.10 + (r - 2.0) * 0.16) {
        int seg = 192;
        double z = funnelZ(r);
        for (int i = 0; i < seg; i++) {
            double a0 = TWO_PI * i / seg, a1 = TWO_PI * (i + 1) / seg;
            pushV(v, r * cos(a0), r * sin(a0), z, r);
            pushV(v, r * cos(a1), r * sin(a1), z, r);
        }
    }
    int NS = 36;
    for (int j = 0; j < NS; j++) {
        double a = TWO_PI * j / NS, ca = cos(a), sa = sin(a);
        double r = 2.005;
        while (r < FUN_RMAX) {
            double r2 = std::min(r + 0.06 + (r - 2.0) * 0.12, FUN_RMAX);
            pushV(v, r * ca, r * sa, funnelZ(r), r);
            pushV(v, r2 * ca, r2 * sa, funnelZ(r2), r2);
            r = r2;
        }
    }
    app.funVerts = (GLsizei)(v.size() / 4);
    glGenVertexArrays(1, &app.funVAO);
    glGenBuffers(1, &app.funVBO);
    glBindVertexArray(app.funVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.funVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (void*)12);

    // opaque cap sealing the throat — the "trapdoor" itself
    std::vector<float> c;
    int seg = 96;
    double rc = 2.005, zc = funnelZ(rc) - 0.05;
    for (int i = 0; i < seg; i++) {
        double a0 = TWO_PI * i / seg, a1 = TWO_PI * (i + 1) / seg;
        pushV(c, 0, 0, zc, rc);
        pushV(c, rc * cos(a0), rc * sin(a0), zc, rc);
        pushV(c, rc * cos(a1), rc * sin(a1), zc, rc);
    }
    app.capVerts = (GLsizei)(c.size() / 4);
    glGenVertexArrays(1, &app.capVAO);
    glGenBuffers(1, &app.capVBO);
    glBindVertexArray(app.capVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.capVBO);
    glBufferData(GL_ARRAY_BUFFER, c.size() * sizeof(float), c.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (void*)12);

    glGenVertexArrays(1, &app.dynVAO);
    glGenBuffers(1, &app.dynVBO);
    glBindVertexArray(app.dynVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.dynVBO);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (void*)12);
    glBindVertexArray(0);
}

// ------------------------------------------------------------ input --------
static void printHelp() {
    std::puts(
        "\nSchwarzschild black hole (M = 1, horizon r = 2M, photon sphere 3M, ISCO 6M)\n"
        "  drag       orbit camera        scroll     zoom\n"
        "  A          auto-orbit          SPACE      pause time\n"
        "  D          accretion disk      G          lensed spacetime grid\n"
        "  S          background stars    TAB / E    spacetime funnel view\n"
        "  1 / 2 / 3  render scale (3 = 4x supersampled)   - / =  exposure\n"
        "  P          screenshot          H          this help\n"
        "  Q / ESC    quit\n");
}
static void keyCB(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_Q: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_SPACE: app.paused = !app.paused; break;
        case GLFW_KEY_A: app.autoOrbit = !app.autoOrbit; break;
        case GLFW_KEY_D: app.disk = !app.disk; break;
        case GLFW_KEY_G: app.grid = !app.grid; break;
        case GLFW_KEY_S: app.stars = !app.stars; break;
        case GLFW_KEY_V:
            app.scene = (app.scene + 1) % 3;
            if (app.scene == 1) { app.dist = 17.0; app.beta = 0.25; }
            else if (app.scene == 2) { app.dist = 34.0; app.beta = 0.35; }
            else { app.dist = 26.0; app.beta = 0.14; }
            break;
        case GLFW_KEY_TAB:
        case GLFW_KEY_E: app.mode = 1 - app.mode; break;
        case GLFW_KEY_1: app.renderScale = 0.5f; app.fbDirty = true; break;
        case GLFW_KEY_2: app.renderScale = 1.0f; app.fbDirty = true; break;
        case GLFW_KEY_3: app.renderScale = 2.0f; app.fbDirty = true; break;  // 4x SSAA
        case GLFW_KEY_MINUS: app.exposure = std::max(0.1f, app.exposure / 1.25f); break;
        case GLFW_KEY_EQUAL: app.exposure = std::min(8.0f, app.exposure * 1.25f); break;
        case GLFW_KEY_P: app.shotCounter |= 0x10000; break;   // request screenshot
        case GLFW_KEY_H: printHelp(); break;
    }
}
static void mouseCB(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app.dragging = action == GLFW_PRESS;
        glfwGetCursorPos(w, &app.lastX, &app.lastY);
    }
}
static void cursorCB(GLFWwindow*, double x, double y) {
    if (!app.dragging) return;
    app.alpha -= (x - app.lastX) * 0.005;
    app.beta += (y - app.lastY) * 0.005;
    double lim = 1.52, minB = 0.02;   // keep camera off the disk plane exactly
    if (app.beta > lim) app.beta = lim;
    if (app.beta < -lim) app.beta = -lim;
    if (std::fabs(app.beta) < minB) app.beta = app.beta >= 0 ? minB : -minB;
    app.lastX = x; app.lastY = y;
}
static void scrollCB(GLFWwindow*, double, double dy) {
    app.dist *= std::exp(-dy * 0.08);
    app.dist = std::min(90.0, std::max(6.5, app.dist));
}
static void fbCB(GLFWwindow*, int w, int h) {
    app.fbW = w; app.fbH = h; app.fbDirty = true;
}

// -------------------------------------------------------------- passes -----
static void drawQuad() {
    glBindVertexArray(app.quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void renderRayTraced(V3 cam, V3 right, V3 up, V3 fwd) {
    glBindFramebuffer(GL_FRAMEBUFFER, app.sceneFBO);
    glViewport(0, 0, app.renderW, app.renderH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    app.progRay = app.progScene[app.scene];
    glUseProgram(app.progRay);
    setU2f(app.progRay, "uRes", (float)app.renderW, (float)app.renderH);
    setU1f(app.progRay, "uTime", (float)app.simTime);
    setU3f(app.progRay, "uCamPos", cam);
    setU3f(app.progRay, "uCamRight", right);
    setU3f(app.progRay, "uCamUp", up);
    setU3f(app.progRay, "uCamFwd", fwd);
    setU1f(app.progRay, "uTanHalfFov", std::tan(55.0f * 3.14159265f / 360.0f));
    setU1i(app.progRay, "uDisk", app.disk ? 1 : 0);
    setU1i(app.progRay, "uGrid", app.grid ? 1 : 0);
    setU1i(app.progRay, "uStars", app.stars ? 1 : 0);
    setU1f(app.progRay, "uDiskGain", app.diskGain);
    drawQuad();
}

static void renderFunnel() {
    glBindFramebuffer(GL_FRAMEBUFFER, app.sceneFBO);
    glViewport(0, 0, app.renderW, app.renderH);
    glClearColor(0.004f, 0.005f, 0.010f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    double betaF = std::max(app.beta, 0.50);
    V3 center = {0, 0, -7.5};
    V3 eye = {center.x + app.dist * 1.25 * cos(betaF) * cos(app.alpha),
              center.y + app.dist * 1.25 * cos(betaF) * sin(app.alpha),
              center.z + app.dist * 1.25 * sin(betaF)};
    M4 mvp = m4mul(m4persp(50.0f * 3.14159265f / 180.0f,
                           (float)app.renderW / (float)app.renderH, 0.1f, 400.0f),
                   m4lookAt(eye, center, {0, 0, 1}));

    glUseProgram(app.progFun);
    glUniformMatrix4fv(glGetUniformLocation(app.progFun, "uMVP"), 1, GL_FALSE, mvp.m);
    setU1f(app.progFun, "uTime", (float)app.simTime);
    setU1f(app.progFun, "uPointSize", 11.0f * app.renderH / 800.0f);

    // throat cap: opaque, writes depth
    setU1i(app.progFun, "uKind", 1);
    glBindVertexArray(app.capVAO);
    glDrawArrays(GL_TRIANGLES, 0, app.capVerts);

    // grid lines: additive, depth-tested against the cap but no depth writes
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    setU1i(app.progFun, "uKind", 0);
    glBindVertexArray(app.funVAO);
    glDrawArrays(GL_LINES, 0, app.funVerts);

    // two test masses on circular Keplerian orbits (inner one visibly faster),
    // trails drawn analytically behind them
    const double rp[2] = {8.5, 13.0}, ph0[2] = {0.0, 2.1};
    std::vector<float> dyn;
    const int TRAIL = 70;
    for (int p = 0; p < 2; p++) {
        double om = std::pow(rp[p], -1.5) * 8.0;   // same time scale as the disk
        double z = funnelZ(rp[p]) + 0.05;
        for (int k = TRAIL - 1; k >= 0; k--) {
            double th = ph0[p] + om * (app.simTime - 0.045 * k);
            pushV(dyn, rp[p] * cos(th), rp[p] * sin(th), z, rp[p]);
        }
    }
    for (int p = 0; p < 2; p++) {
        double om = std::pow(rp[p], -1.5) * 8.0;
        double th = ph0[p] + om * app.simTime;
        pushV(dyn, rp[p] * cos(th), rp[p] * sin(th), funnelZ(rp[p]) + 0.05, rp[p]);
    }
    glBindVertexArray(app.dynVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.dynVBO);
    glBufferData(GL_ARRAY_BUFFER, dyn.size() * sizeof(float), dyn.data(), GL_STREAM_DRAW);
    setU1i(app.progFun, "uKind", 0);
    glDrawArrays(GL_LINE_STRIP, 0, TRAIL);
    glDrawArrays(GL_LINE_STRIP, TRAIL, TRAIL);
    glEnable(GL_PROGRAM_POINT_SIZE);
    setU1i(app.progFun, "uKind", 2);
    glDrawArrays(GL_POINTS, 2 * TRAIL, 2);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
}

static void renderPost() {
    // bright pass
    glBindFramebuffer(GL_FRAMEBUFFER, app.bloomFBO[0]);
    glViewport(0, 0, app.bloomW, app.bloomH);
    glUseProgram(app.progBright);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.sceneTex);
    setU1i(app.progBright, "uScene", 0);
    drawQuad();

    // separable gaussian ping-pong
    glUseProgram(app.progBlur);
    setU1i(app.progBlur, "uTex", 0);
    int cur = 0;
    for (int i = 0; i < 8; i++) {
        int nxt = 1 - cur;
        glBindFramebuffer(GL_FRAMEBUFFER, app.bloomFBO[nxt]);
        glBindTexture(GL_TEXTURE_2D, app.bloomTex[cur]);
        if (i % 2 == 0) setU2f(app.progBlur, "uDir", 1.6f / app.bloomW, 0.0f);
        else            setU2f(app.progBlur, "uDir", 0.0f, 1.6f / app.bloomH);
        drawQuad();
        cur = nxt;
    }

    // composite to the window
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, app.fbW, app.fbH);
    glUseProgram(app.progComp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.sceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.bloomTex[cur]);
    setU1i(app.progComp, "uScene", 0);
    setU1i(app.progComp, "uBloom", 1);
    setU1f(app.progComp, "uExposure", app.exposure);
    setU1f(app.progComp, "uBloomStrength", app.bloomStrength);
    setU1f(app.progComp, "uDither", app.recordPath.empty() ? 1.0f : 0.0f);
    drawQuad();
    glActiveTexture(GL_TEXTURE0);
}

static void takeScreenshot(const std::string& path) {
    std::vector<uint8_t> rgba((size_t)app.fbW * app.fbH * 4);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, app.fbW, app.fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<uint8_t> rgb((size_t)app.fbW * app.fbH * 3);
    for (int y = 0; y < app.fbH; y++) {
        const uint8_t* src = &rgba[(size_t)(app.fbH - 1 - y) * app.fbW * 4];
        uint8_t* dst = &rgb[(size_t)y * app.fbW * 3];
        for (int x = 0; x < app.fbW; x++) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    if (savePNG(path, app.fbW, app.fbH, rgb))
        std::printf("saved %s (%dx%d)\n", path.c_str(), app.fbW, app.fbH);
    else
        std::fprintf(stderr, "could not write %s\n", path.c_str());
}

// --------------------------------------------------------------- main ------
int main(int argc, char** argv) {
    int winW = 1280, winH = 800;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--frames") app.maxFrames = std::atol(next());
        else if (a == "--shot") app.shotPath = next();
        else if (a == "--record") app.recordPath = next();
        else if (a == "--scale") { app.renderScale = (float)std::atof(next()); }
        else if (a == "--size") { std::sscanf(next(), "%dx%d", &winW, &winH); }
        else if (a == "--scene") {
            std::string sc = next();
            if (sc == "pulsar") { app.scene = 1; app.dist = 17.0; app.beta = 0.25; }
            else if (sc == "quasar") { app.scene = 2; app.dist = 34.0; app.beta = 0.35; }
        }
        else if (a == "--mode") {
            std::string m = next();
            if (m == "funnel") app.mode = 1;
            else if (m == "grid") { app.mode = 0; app.grid = true; app.disk = false; }
        }
        else if (a == "--no-rotate") app.autoOrbit = false;
        else if (a == "--help" || a == "-h") {
            std::puts("usage: blackhole [--size WxH] [--scale f] [--mode ray|grid|funnel]\n"
                      "                 [--frames N] [--shot out.png] [--record raw.rgb] [--no-rotate]");
            return 0;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "FATAL: glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    // deterministic capture mode: undecorated (so --size can span the full
    // display) — input callbacks are skipped below so stray clicks/drags
    // can't disturb the camera mid-recording
    if (app.maxFrames > 0) glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    app.win = glfwCreateWindow(winW, winH, "Schwarzschild black hole", nullptr, nullptr);
    if (!app.win) {
        std::fprintf(stderr, "FATAL: window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.win);
    glfwSwapInterval(app.maxFrames > 0 ? 0 : 1);   // uncap in benchmark mode
    glfwGetFramebufferSize(app.win, &app.fbW, &app.fbH);
    if (app.maxFrames <= 0) {
        glfwSetKeyCallback(app.win, keyCB);
        glfwSetMouseButtonCallback(app.win, mouseCB);
        glfwSetCursorPosCallback(app.win, cursorCB);
        glfwSetScrollCallback(app.win, scrollCB);
    }
    glfwSetFramebufferSizeCallback(app.win, fbCB);

    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    printHelp();

    app.progScene[0] = makeProgram("quad.vert", "blackhole.frag");
    app.progScene[1] = makeProgram("quad.vert", "pulsar.frag");
    app.progScene[2] = makeProgram("quad.vert", "quasar.frag");
    app.progBright = makeProgram("quad.vert", "bright.frag");
    app.progBlur   = makeProgram("quad.vert", "blur.frag");
    app.progComp   = makeProgram("quad.vert", "composite.frag");
    app.progFun    = makeProgram("funnel.vert", "funnel.frag");

    const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    glGenVertexArrays(1, &app.quadVAO);
    glGenBuffers(1, &app.quadVBO);
    glBindVertexArray(app.quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glBindVertexArray(0);

    buildFunnel();

    double prev = glfwGetTime(), fpsAcc = 0;
    int fpsN = 0;
    long frame = 0;
    double benchStart = 0;

    while (!glfwWindowShouldClose(app.win)) {
        glfwPollEvents();
        if (app.fbW == 0 || app.fbH == 0) continue;
        if (app.fbDirty) rebuildFBOs();

        double now = glfwGetTime();
        double dt = app.maxFrames > 0 ? 1.0 / 60.0 : now - prev;   // deterministic in test mode
        prev = now;
        if (!app.paused) {
            app.simTime += dt;
            if (app.autoOrbit) app.alpha += dt * 0.05;
        }

        double ca = cos(app.alpha), sa = sin(app.alpha);
        double cb = cos(app.beta), sb = sin(app.beta);
        V3 cam = {app.dist * cb * ca, app.dist * cb * sa, app.dist * sb};
        V3 fwd = vnorm({-cam.x, -cam.y, -cam.z});
        V3 right = vnorm(vcross(fwd, {0, 0, 1}));
        V3 up = vcross(right, fwd);

        if (app.mode == 1) renderFunnel();
        else renderRayTraced(cam, right, up, fwd);
        renderPost();

        if (app.shotCounter & 0x10000) {
            app.shotCounter &= 0xFFFF;
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s/../shots/shot-%03d.png",
                          SHADER_DIR, app.shotCounter++);
            takeScreenshot(buf);
        }
        if (app.maxFrames > 0 && frame == app.maxFrames - 1 && !app.shotPath.empty())
            takeScreenshot(app.shotPath);

        if (!app.recordPath.empty()) {
            if (!app.recordFile) app.recordFile = std::fopen(app.recordPath.c_str(), "wb");
            if (app.recordFile) {
                static std::vector<uint8_t> rgba, rgb;
                rgba.resize((size_t)app.fbW * app.fbH * 4);
                rgb.resize((size_t)app.fbW * app.fbH * 3);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, app.fbW, app.fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                for (int y = 0; y < app.fbH; y++) {
                    const uint8_t* src = &rgba[(size_t)(app.fbH - 1 - y) * app.fbW * 4];
                    uint8_t* dst = &rgb[(size_t)y * app.fbW * 3];
                    for (int x = 0; x < app.fbW; x++) {
                        dst[x * 3 + 0] = src[x * 4 + 0];
                        dst[x * 3 + 1] = src[x * 4 + 1];
                        dst[x * 3 + 2] = src[x * 4 + 2];
                    }
                }
                std::fwrite(rgb.data(), 1, rgb.size(), app.recordFile);
            }
        }

        glfwSwapBuffers(app.win);

        fpsAcc += glfwGetTime() - now; fpsN++;
        if (fpsN >= 30) {
            double frameMs = fpsAcc / fpsN * 1000.0;
            char title[256];
            std::snprintf(title, sizeof title,
                "Schwarzschild black hole  |  %s%s%s |  %.1f ms  |  scale %.2f  |  r_cam %.1f M",
                app.mode ? "spacetime funnel"
                         : (app.scene == 1 ? "pulsar" : (app.scene == 2 ? "quasar" : "black hole")),
                app.grid && !app.mode ? " + grid" : "",
                app.paused ? "  [paused]" : "",
                frameMs, app.renderScale, app.dist);
            glfwSetWindowTitle(app.win, title);
            fpsAcc = 0; fpsN = 0;
        }

        frame++;
        if (frame == 5) benchStart = glfwGetTime();
        if (app.maxFrames > 0 && frame >= app.maxFrames) break;
    }
    if (app.maxFrames > 5 && benchStart > 0) {
        double avg = (glfwGetTime() - benchStart) / (frame - 5);
        std::printf("avg frame: %.2f ms (%.0f fps) at %dx%d render res\n",
                    avg * 1000.0, 1.0 / avg, app.renderW, app.renderH);
    }

    if (app.recordFile) std::fclose(app.recordFile);
    glfwDestroyWindow(app.win);
    glfwTerminate();
    return 0;
}
