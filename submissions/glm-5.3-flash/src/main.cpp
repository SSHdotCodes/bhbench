// ============================================================================
//  black-hole-cpp-ox -- realtime Schwarzschild black hole (OpenGL / GLSL)
//
//  * Ray tracing of exact null geodesics -> gravitational lensing
//  * Novikov-Thorne accretion disk with Doppler beaming + gravitational shift
//  * Volumetric halos + bloom
//  * Lensed spacetime grid mode (G) and Flamm-paraboloid "trapdoor" mode (F)
// ============================================================================
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>
#include <mach-o/dyld.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <string>
#include <vector>

#include "math3.h"
#include "png_write.h"

// ------------------------------------------------------------------ util ---
static std::string shaderDir;

static GLuint compile(GLenum type, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "Cannot open shader %s\n", path.c_str()); exit(1); }
    std::string src;
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
    fclose(f);
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "Shader compile error (%s):\n%s\n", path.c_str(), log);
        exit(1);
    }
    return s;
}
static GLuint link(std::initializer_list<std::pair<GLenum, std::string>> parts) {
    GLuint p = glCreateProgram();
    std::vector<GLuint> shaders;
    for (const auto& kv : parts) {
        GLuint s = compile(kv.first, shaderDir + "/" + kv.second);
        glAttachShader(p, s); shaders.push_back(s);
    }
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        exit(1);
    }
    for (GLuint s : shaders) glDeleteShader(s);
    return p;
}

struct Prog {
    GLuint id = 0;
    std::vector<std::pair<std::string, GLint>> cache;
    GLint u(const char* name) {
        for (auto& c : cache) if (c.first == name) return c.second;
        GLint l = glGetUniformLocation(id, name);
        cache.push_back({name, l});
        return l;
    }
};

// ------------------------------------------------------------------ state --
struct Camera {
    float yaw = 0.55f, pitch = 0.10f, dist = 13.5f;
    Vec3 pos() const {
        float cp = cosf(pitch);
        return Vec3(dist * cp * cosf(yaw), dist * sinf(pitch), dist * cp * sinf(yaw));
    }
    void basis(Vec3& r, Vec3& u, Vec3& f) const {
        Vec3 eye = pos();
        f = normalize(Vec3(0, 0, 0) - eye);
        r = normalize(cross(f, Vec3(0, 1, 0)));
        u = cross(r, f);
    }
};

static struct {
    int fbW = 0, fbH = 0;
    float resScale = 0.6f;
    int maxSteps = 340;
    float stepBase = 0.16f;
    bool gridMode = false;
    bool funnelMode = false;
    bool paused = false;
    bool autoOrbit = true;
    float exposure = 1.0f;
    float bloomStrength = 0.35f;
    float tpeak = 6400.0f;
    float diskSpeed = 1.0f;
    Camera camRay{0.55f, 0.16f, 27.0f};
    Camera camFunnel{0.55f, 0.35f, 30.0f};
    Camera* cam = &camRay;
    double lastInteract = -100.0;
    bool shotQueued = false;
    std::string shotPath;
} S;

// ------------------------------------------------------------------ GL -----
static Prog pBH, pBright, pBlur, pComp, pFunnel, pPoints, pSolid;
static GLuint sceneFBO = 0, sceneTex = 0;
static GLuint bloomFBO[2] = {0, 0}, bloomTex[2] = {0, 0};
static int bw = 0, bh = 0, sw = 0, sh = 0;

static GLuint quadVAO = 0;
static GLuint funnelVAO = 0, funnelVBO = 0, funnelIBO = 0;
static int funnelIndexCount = 0;
static GLuint ptsVAO = 0, ptsVBO = 0;
static int ptCount = 600;
static GLuint sphVAO = 0, sphVBO = 0, sphIBO = 0;
static int sphIndexCount = 0;

static void makeTexFBO(GLuint& fbo, GLuint& tex, int w, int h) {
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void resizeTargets(int fbw, int fbh) {
    S.fbW = fbw; S.fbH = fbh;
    sw = std::max(64, (int)(fbw * S.resScale));
    sh = std::max(64, (int)(fbh * S.resScale));
    bw = std::max(8, sw / 4); bh = std::max(8, sh / 4);
    makeTexFBO(sceneFBO, sceneTex, sw, sh);
    makeTexFBO(bloomFBO[0], bloomTex[0], bw, bh);
    makeTexFBO(bloomFBO[1], bloomTex[1], bw, bh);
    printf("[resize] fb %dx%d  render %dx%d  bloom %dx%d\n", fbw, fbh, sw, sh, bw, bh);
    fflush(stdout);
}

static void drawQuad() {
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ---------------------------------------------------------------- meshes ---
static float funnelDepth(float r) { return 2.0f * std::sqrt(std::max(r - 1.0f, 0.0f)); }

static void buildFunnel() {
    const int NA = 180, NR = 120;
    const float rMin = 0.88f, rMax = 24.0f;
    std::vector<float> v;
    v.reserve((size_t)(NA + 1) * (NR + 1) * 5);
    for (int i = 0; i <= NA; ++i) {
        float a = (float)i / NA * 2.0f * 3.14159265f;
        for (int j = 0; j <= NR; ++j) {
            float t = (float)j / NR;
            float r = rMin * std::pow(rMax / rMin, t);
            v.push_back(r * std::cos(a));
            v.push_back(funnelDepth(r) - 9.6f);   // trapdoor: rim at y=0, throat plunges down
            v.push_back(r * std::sin(a));
            v.push_back((float)i);
            v.push_back(t * NR * 0.5f);
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve((size_t)NA * NR * 6);
    for (int i = 0; i < NA; ++i)
        for (int j = 0; j < NR; ++j) {
            uint32_t a = (uint32_t)(i * (NR + 1) + j);
            uint32_t b = a + 1, c = a + NR + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    funnelIndexCount = (int)idx.size();
    glGenVertexArrays(1, &funnelVAO);
    glBindVertexArray(funnelVAO);
    glGenBuffers(1, &funnelVBO);
    glBindBuffer(GL_ARRAY_BUFFER, funnelVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &funnelIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, funnelIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
    glBindVertexArray(0);
}

static void buildParticles() {
    std::vector<float> s;
    s.reserve((size_t)ptCount * 4);
    for (int i = 0; i < ptCount; ++i) {
        float r0 = 3.0f + 20.0f * std::pow((float)rand() / RAND_MAX, 0.6f);
        float phi0 = (float)rand() / RAND_MAX * 2.0f * 3.14159265f;
        float speed = 0.5f + (float)rand() / RAND_MAX;
        float size = 1.5f + 2.5f * (float)rand() / RAND_MAX;
        s.insert(s.end(), {r0, phi0, speed, size});
    }
    glGenVertexArrays(1, &ptsVAO);
    glBindVertexArray(ptsVAO);
    glGenBuffers(1, &ptsVBO);
    glBindBuffer(GL_ARRAY_BUFFER, ptsVBO);
    glBufferData(GL_ARRAY_BUFFER, s.size() * sizeof(float), s.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glBindVertexArray(0);
}

static void buildSphere() {
    const int SL = 48, ST = 32;
    std::vector<float> v;
    for (int t = 0; t <= ST; ++t) {
        float ph = (float)t / ST * 3.14159265f;
        for (int s = 0; s <= SL; ++s) {
            float th = (float)s / SL * 2.0f * 3.14159265f;
            float x = std::sin(ph) * std::cos(th), y = std::cos(ph), z = std::sin(ph) * std::sin(th);
            v.insert(v.end(), {x, y, z, x, y, z});
        }
    }
    std::vector<uint32_t> idx;
    for (int t = 0; t < ST; ++t)
        for (int s = 0; s < SL; ++s) {
            uint32_t a = (uint32_t)(t * (SL + 1) + s);
            uint32_t b = a + 1, c = a + SL + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    sphIndexCount = (int)idx.size();
    glGenVertexArrays(1, &sphVAO);
    glBindVertexArray(sphVAO);
    glGenBuffers(1, &sphVBO);
    glBindBuffer(GL_ARRAY_BUFFER, sphVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &sphIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);
}

// ------------------------------------------------------------- rendering ---
static double simTime = 0.0;

static void renderScene(double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glViewport(0, 0, sw, sh);
    glUseProgram(pBH.id);

    Vec3 eye = S.cam->pos();
    Vec3 r, u, f;
    S.cam->basis(r, u, f);
    float B[9] = { r.x, r.y, r.z,  u.x, u.y, u.z,  f.x, f.y, f.z };

    glUniform2f(pBH.u("uRes"), (float)sw, (float)sh);
    glUniform1f(pBH.u("uTime"), (float)time);
    glUniform3f(pBH.u("uCamPos"), eye.x, eye.y, eye.z);
    glUniformMatrix3fv(pBH.u("uCamBasis"), 1, GL_FALSE, B);
    glUniform1f(pBH.u("uTanHalfFov"), std::tan(55.0f * 3.14159265f / 360.0f));
    glUniform1i(pBH.u("uMaxSteps"), S.maxSteps);
    glUniform1f(pBH.u("uStepBase"), S.stepBase);
    glUniform1f(pBH.u("uDiskInner"), 3.0f);
    glUniform1f(pBH.u("uDiskOuter"), 14.0f);
    glUniform1f(pBH.u("uTpeak"), S.tpeak);
    glUniform1f(pBH.u("uDiskSpeed"), S.diskSpeed);
    glUniform1i(pBH.u("uGridMode"), S.gridMode ? 1 : 0);
    glUniform1f(pBH.u("uExposure"), 1.0f);
    drawQuad();
}

static void renderPost() {
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
    glViewport(0, 0, bw, bh);
    glUseProgram(pBright.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(pBright.u("uTex"), 0);
    glUniform2f(pBright.u("uTexel"), 1.0f / sw, 1.0f / sh);
    glUniform1f(pBright.u("uThreshold"), 1.5f);
    drawQuad();

    glUseProgram(pBlur.id);
    glUniform1i(pBlur.u("uTex"), 0);
    for (int it = 0; it < 2; ++it) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[1]);
        glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
        glUniform2f(pBlur.u("uDir"), 1.0f / bw, 0.0f);
        drawQuad();
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
        glBindTexture(GL_TEXTURE_2D, bloomTex[1]);
        glUniform2f(pBlur.u("uDir"), 0.0f, 1.0f / bh);
        drawQuad();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, S.fbW, S.fbH);
    glUseProgram(pComp.id);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
    glUniform1i(pComp.u("uScene"), 0);
    glUniform1i(pComp.u("uBloom"), 1);
    glUniform1f(pComp.u("uBloomStrength"), S.bloomStrength);
    glUniform1f(pComp.u("uExposure"), S.exposure);
    glUniform2f(pComp.u("uRes"), (float)S.fbW, (float)S.fbH);
    drawQuad();
    glActiveTexture(GL_TEXTURE0);
}

static void renderFunnel(double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, S.fbW, S.fbH);
    glClearColor(0.004f, 0.005f, 0.009f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float aspect = (float)S.fbW / (float)S.fbH;
    Vec3 eye = S.cam->pos();
    Mat4 V = Mat4::lookAt(eye, Vec3(0, -4.5f, 0), Vec3(0, 1, 0));
    Mat4 P = Mat4::perspective(45.0f * 3.14159265f / 180.0f, aspect, 0.1f, 200.0f);
    Mat4 VP = P * V;

    glUseProgram(pFunnel.id);
    glUniformMatrix4fv(pFunnel.u("uMVP"), 1, GL_FALSE, VP.m);
    glUniform3f(pFunnel.u("uCamPos"), eye.x, eye.y, eye.z);
    glUniform1f(pFunnel.u("uRMax"), 24.0f);
    glBindVertexArray(funnelVAO);
    glDrawElements(GL_TRIANGLES, funnelIndexCount, GL_UNSIGNED_INT, 0);

    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glUseProgram(pPoints.id);
    glUniformMatrix4fv(pPoints.u("uMVP"), 1, GL_FALSE, VP.m);
    glUniform1f(pPoints.u("uTime"), (float)time);
    glUniform1f(pPoints.u("uDepthScale"), 1.0f);
    glBindVertexArray(ptsVAO);
    glDrawArrays(GL_POINTS, 0, ptCount);
    glDepthMask(GL_TRUE);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(pSolid.id);
    Mat4 M = Mat4::identity(); M.m[13] = -9.3f; M.m[0] = M.m[5] = M.m[10] = 1.25f;            // horizon ball at throat bottom
    Mat4 MVPs = VP * M;
    glUniformMatrix4fv(pSolid.u("uMVP"), 1, GL_FALSE, MVPs.m);
    glUniformMatrix4fv(pSolid.u("uModel"), 1, GL_FALSE, M.m);
    glUniform3f(pSolid.u("uCamPos"), eye.x, eye.y, eye.z);
    glBindVertexArray(sphVAO);
    glDrawElements(GL_TRIANGLES, sphIndexCount, GL_UNSIGNED_INT, 0);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
}

static void grabScreenshot(const std::string& path) {
    glFinish();
    int w = S.fbW, h = S.fbH;
    std::vector<uint8_t> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    if (pngw::writeRGB(path.c_str(), w, h, px.data()))
        printf("[shot] wrote %s\n", path.c_str());
    else
        printf("[shot] FAILED to write %s\n", path.c_str());
    fflush(stdout);
}

// ------------------------------------------------------------------ input --
static double lastX = 0, lastY = 0;
static bool dragging = false;

static void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    S.lastInteract = glfwGetTime();
    auto setScale = [&](float s) {
        S.resScale = s;
        resizeTargets(S.fbW, S.fbH);
        printf("[quality] render scale %.0f%%\n", s * 100);
    };
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
        case GLFW_KEY_SPACE:  S.paused = !S.paused; break;
        case GLFW_KEY_G:      S.gridMode = !S.gridMode;
                              printf("[mode] spacetime grid %s\n", S.gridMode ? "ON" : "OFF"); break;
        case GLFW_KEY_F:      S.funnelMode = !S.funnelMode;
                              S.cam = S.funnelMode ? &S.camFunnel : &S.camRay;
                              printf("[mode] %s\n", S.funnelMode ?
                                  "Flamm paraboloid (trapdoor)" : "raytraced black hole"); break;
        case GLFW_KEY_A:      S.autoOrbit = !S.autoOrbit; break;
        case GLFW_KEY_R:      *S.cam = S.funnelMode ? Camera{0.55f, 0.35f, 30.0f}
                                                    : Camera{0.55f, 0.16f, 27.0f}; break;
        case GLFW_KEY_P:      S.shotQueued = true; break;
        case GLFW_KEY_1: setScale(0.35f); break;
        case GLFW_KEY_2: setScale(0.60f); break;
        case GLFW_KEY_3: setScale(0.85f); break;
        case GLFW_KEY_4: setScale(1.00f); break;
        case GLFW_KEY_5: setScale(1.50f); break;
        case GLFW_KEY_LEFT_BRACKET:  S.maxSteps = std::max(96,  S.maxSteps - 64);
                                     printf("[steps] %d\n", S.maxSteps); break;
        case GLFW_KEY_RIGHT_BRACKET: S.maxSteps = std::min(700, S.maxSteps + 64);
                                     printf("[steps] %d\n", S.maxSteps); break;
        case GLFW_KEY_MINUS: S.exposure = std::max(0.2f, S.exposure - 0.15f); break;
        case GLFW_KEY_EQUAL: S.exposure = std::min(4.0f, S.exposure + 0.15f); break;
    }
    fflush(stdout);
}

static void mouseButton(GLFWwindow* w, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &lastX, &lastY);
        if (dragging) S.lastInteract = glfwGetTime();
    }
}
static void mouseMove(GLFWwindow* w, double x, double y) {
    if (!dragging) return;
    float dx = (float)(x - lastX), dy = (float)(y - lastY);
    lastX = x; lastY = y;
    S.cam->yaw   -= dx * 0.005f;
    S.cam->pitch += dy * 0.005f;
    S.cam->pitch = std::max(-1.45f, std::min(1.45f, S.cam->pitch));
    S.lastInteract = glfwGetTime();
}
static void scroll(GLFWwindow* w, double, double dy) {
    S.cam->dist *= (dy > 0) ? 0.90f : 1.10f;
    S.cam->dist = std::max(S.funnelMode ? 6.0f : 1.35f, std::min(60.0f, S.cam->dist));
    S.lastInteract = glfwGetTime();
}
static void framebufferSize(GLFWwindow*, int w, int h) { resizeTargets(w, h); }

// ------------------------------------------------------------------- main --
int main() {
    const char* e;
    int autoquitFrames = (e = getenv("BH_AUTOQUIT_FRAMES")) ? atoi(e) : 0;
    if ((e = getenv("BH_QUALITY")))
        S.resScale = std::min(1.5f, std::max(0.2f, (float)atof(e)));
    if ((e = getenv("BH_STEPS"))) S.maxSteps = atoi(e);
    if ((e = getenv("BH_EXPOSURE"))) S.exposure = (float)atof(e);
    std::string startMode = (e = getenv("BH_START_MODE")) ? e : "";
    if ((e = getenv("BH_SHOT"))) S.shotPath = e;

    {
        char buf[4096];
        uint32_t sz = sizeof buf;
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::string exe = buf;
            size_t slash = exe.find_last_of('/');
            std::string base = (slash == std::string::npos) ? "." : exe.substr(0, slash);
            const std::string candidates[] = {base + "/shaders", base + "/../../src/shaders", "src/shaders"};
            for (const std::string& c : candidates) {
                FILE* f = fopen((c + "/quad.vs").c_str(), "rb");
                if (f) { fclose(f); shaderDir = c; break; }
            }
        }
    }
    printf("shader dir: %s\n", shaderDir.c_str());

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    GLFWwindow* win = glfwCreateWindow(1600, 1000,
        "black-hole-cpp-ox  |  Schwarzschild ray tracer", nullptr, nullptr);
    if (!win) { fprintf(stderr, "Window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    printf("GL_RENDERER: %s\nGL_VERSION:  %s\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION));

    pBH.id     = link({{GL_VERTEX_SHADER, "quad.vs"}, {GL_FRAGMENT_SHADER, "blackhole.frag"}});
    pBright.id = link({{GL_VERTEX_SHADER, "quad.vs"}, {GL_FRAGMENT_SHADER, "bright.frag"}});
    pBlur.id   = link({{GL_VERTEX_SHADER, "quad.vs"}, {GL_FRAGMENT_SHADER, "blur.frag"}});
    pComp.id   = link({{GL_VERTEX_SHADER, "quad.vs"}, {GL_FRAGMENT_SHADER, "composite.frag"}});
    pFunnel.id = link({{GL_VERTEX_SHADER, "funnel.vert"}, {GL_FRAGMENT_SHADER, "funnel.frag"}});
    pPoints.id = link({{GL_VERTEX_SHADER, "particles.vert"}, {GL_FRAGMENT_SHADER, "particles.frag"}});
    pSolid.id  = link({{GL_VERTEX_SHADER, "solid.vert"}, {GL_FRAGMENT_SHADER, "solid.frag"}});

    glGenVertexArrays(1, &quadVAO);
    buildFunnel(); buildParticles(); buildSphere();

    if (startMode == "grid")  S.gridMode = true;
    if (startMode == "funnel") { S.funnelMode = true; S.cam = &S.camFunnel; }

    int fw, fh;
    glfwGetFramebufferSize(win, &fw, &fh);
    resizeTargets(fw, fh);

    glfwSetKeyCallback(win, keyCallback);
    glfwSetMouseButtonCallback(win, mouseButton);
    glfwSetCursorPosCallback(win, mouseMove);
    glfwSetScrollCallback(win, scroll);
    glfwSetFramebufferSizeCallback(win, framebufferSize);

    printf("\nControls: drag=orbit  wheel=zoom  G=spacetime grid  F=trapdoor funnel\n"
           "          1-5=quality  [ ]=steps  +/-=exposure  A=auto-orbit  P=screenshot\n"
           "          Space=pause  R=reset  Esc=quit\n\n");
    fflush(stdout);

    auto t0 = std::chrono::steady_clock::now();
    int fpsFrames = 0;
    double fpsTimer = 0, lastNow = glfwGetTime();
    int totalFrames = 0;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt = now - lastNow; lastNow = now;
        if (!S.paused) simTime += dt;

        if (S.autoOrbit && now - S.lastInteract > 3.0) S.cam->yaw += (float)dt * 0.05f;

        if (S.funnelMode) renderFunnel(simTime);
        else { renderScene(simTime); renderPost(); }
        glfwSwapBuffers(win);

        fpsFrames++; totalFrames++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            char title[256];
            snprintf(title, sizeof title,
                     "black-hole-cpp-ox  |  %s%s  |  %d steps  |  %.0f fps (%.1f ms)  |  %dx%d",
                     S.funnelMode ? "Flamm funnel" : (S.gridMode ? "spacetime grid" : "accretion disk"),
                     S.paused ? "  [paused]" : "", S.maxSteps,
                     fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames, sw, sh);
            glfwSetWindowTitle(win, title);
            printf("fps %.1f  (%.2f ms)\n", fpsFrames / fpsTimer, 1000.0 * fpsTimer / fpsFrames);
            fflush(stdout);
            fpsFrames = 0; fpsTimer = 0;
        }

        if (S.shotQueued) {
            grabScreenshot(S.shotPath.empty() ? "screenshot.png" : S.shotPath);
            S.shotQueued = false;
        }
        if (autoquitFrames > 0 && totalFrames >= autoquitFrames) {
            if (!S.shotPath.empty()) grabScreenshot(S.shotPath);
            auto t1 = std::chrono::steady_clock::now();
            double el = std::chrono::duration<double>(t1 - t0).count();
            printf("[autoquit] %d frames in %.2fs -> %.1f fps avg\n", totalFrames, el, totalFrames / el);
            fflush(stdout);
            break;
        }
    }
    glfwTerminate();
    return 0;
}
