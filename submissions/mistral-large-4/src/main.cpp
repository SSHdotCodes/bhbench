// Black hole simulation — real-time Schwarzschild ray tracer.
//
//  * Gravitational lensing: per-pixel ray tracing of exact null geodesics
//    (RK4 integration in the orbital plane), running in a fragment shader.
//  * Accretion disk: thin Keplerian disk with relativistic Doppler beaming,
//    blackbody spectrum and a volumetric halo.
//  * Spacetime curvature: Flamm-paraboloid wireframe grid ("trapdoor").
//
// Controls:
//   left-drag  orbit camera      scroll  zoom
//   R reset camera   G toggle grid   H toggle halo   SPACE pause   ESC quit

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shaders.h"

namespace {

constexpr float kRs       = 1.0f;   // Schwarzschild radius (world units)
constexpr float kDiskRin  = 3.0f;   // ISCO = 6M = 3 rs
constexpr float kDiskRout = 18.0f;
constexpr float kDiskBrightness = 1.2f;
constexpr float kFovY = 50.0f * 3.14159265358979f / 180.0f;

// ---------- tiny vec/mat helpers ----------
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalize(Vec3 a) { return a * (1.0f / length(a)); }

struct Mat4 {
    float m[16];
    float& operator[](int i) { return m[i]; }
    float operator[](int i) const { return m[i]; }
};
Mat4 matMul(const Mat4& a, const Mat4& b) {  // column-major, out = a * b
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + i] * b[c * 4 + k];
            r[c * 4 + i] = s;
        }
    return r;
}
Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 m = {{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
               -dot(s, eye), -dot(u, eye), dot(f, eye), 1}};
    return m;
}
Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    float t = 1.0f / std::tan(fovy * 0.5f);
    Mat4 m = {{t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1,
               0, 0, (2 * zf * zn) / (zn - zf), 0}};
    return m;
}

// ---------- GL helpers ----------
GLuint compileShader(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader %s] compile error:\n%s\n", name, log);
        std::exit(1);
    }
    return s;
}
GLuint linkProgram(const char* vsSrc, const char* fsSrc, const char* name) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc, name);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc, name);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[program %s] link error:\n%s\n", name, log);
        std::exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// ---------- camera ----------
struct OrbitCam {
    float az = 0.6f;
    float el = 0.34f;
    float dist = 15.0f;
    Vec3 pos() const {
        return {dist * std::cos(el) * std::sin(az), dist * std::sin(el),
                dist * std::cos(el) * std::cos(az)};
    }
};

// ---------- input state ----------
struct Input {
    OrbitCam cam;
    bool showGrid = true;
    bool showHalo = true;
    bool paused = false;
    bool dragging = false;
    double lastX = 0, lastY = 0;
    bool prevKey[GLFW_KEY_LAST + 1] = {};
};

bool keyPressed(GLFWwindow* win, int key, Input& in) {
    bool now = glfwGetKey(win, key) == GLFW_PRESS;
    bool edge = now && !in.prevKey[key];
    in.prevKey[key] = now;
    return edge;
}

void scrollCb(GLFWwindow* win, double, double yoff) {
    Input* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    in->cam.dist *= std::exp(-yoff * 0.12f);
    in->cam.dist = std::max(4.5f, std::min(60.0f, in->cam.dist));
}
void mouseBtnCb(GLFWwindow* win, int button, int action, int) {
    Input* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        in->dragging = (action == GLFW_PRESS);
        if (in->dragging) {
            glfwGetCursorPos(win, &in->lastX, &in->lastY);
        }
    }
}
void cursorCb(GLFWwindow* win, double x, double y) {
    Input* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in->dragging) return;
    double dx = x - in->lastX, dy = y - in->lastY;
    in->lastX = x;
    in->lastY = y;
    in->cam.az += float(dx) * 0.005f;
    in->cam.el = std::max(-1.45f, std::min(1.45f, in->cam.el + float(dy) * 0.005f));
}

// ---------- spacetime grid mesh (line list) ----------
// Vertices: (r, phi, color.rgb); the vertex shader lifts them onto the
// Flamm paraboloid. Rings + spokes + horizon ring + photon-sphere ring.
std::vector<float> buildGridMesh() {
    std::vector<float> v;
    auto line = [&](float r0, float p0, float r1, float p1, Vec3 c) {
        v.insert(v.end(), {r0, p0, c.x, c.y, c.z});
        v.insert(v.end(), {r1, p1, c.x, c.y, c.z});
    };
    const Vec3 gridCol{0.35f, 0.85f, 1.0f};
    const Vec3 horizonCol{2.5f, 1.25f, 0.5f};
    const Vec3 photonCol{1.0f, 0.7f, 0.4f};
    const int NR = 34, NS = 128, NSP = 48;
    const float r0 = 1.03f, r1 = 16.0f;
    auto ringR = [&](int i) {
        return r0 * std::pow(r1 / r0, float(i) / float(NR - 1));
    };
    const float twoPi = 2.0f * 3.14159265358979f;
    for (int i = 0; i < NR; i++) {
        float r = ringR(i);
        for (int j = 0; j < NS; j++) {
            float a = twoPi * float(j) / NS, b = twoPi * float(j + 1) / NS;
            line(r, a, r, b, gridCol);
        }
    }
    for (int j = 0; j < NSP; j++) {
        float a = twoPi * float(j) / NSP;
        for (int i = 0; i < NR - 1; i++) line(ringR(i), a, ringR(i + 1), a, gridCol);
    }
    for (int j = 0; j < 96; j++) {  // event horizon rim
        float a = twoPi * float(j) / 96, b = twoPi * float(j + 1) / 96;
        line(kRs, a, kRs, b, horizonCol);
    }
    for (int j = 0; j < 96; j++) {  // photon sphere
        float a = twoPi * float(j) / 96, b = twoPi * float(j + 1) / 96;
        line(1.5f * kRs, a, 1.5f * kRs, b, photonCol);
    }
    return v;
}

void writePPM(const char* path, const unsigned char* rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s for writing\n", path);
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<unsigned char> row(size_t(w) * 3);
    for (int y = h - 1; y >= 0; y--) {  // flip vertically (GL origin is bottom-left)
        std::memcpy(row.data(), rgb + size_t(y) * w * 3, row.size());
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    std::printf("wrote %s (%dx%d)\n", path, w, h);
}

}  // namespace

int main(int argc, char** argv) {
    int width = 1280, height = 720;
    float renderScale = -1.0f;  // < 0 = auto (retina-aware)
    float camAz = -1.0f, camEl = -1.0f, camDist = -1.0f;
    int testFrames = 0;
    bool halo = true, grid = true, debug = false;
    std::string testOut;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--test" && i + 2 < argc) {
            testFrames = std::atoi(argv[++i]);
            testOut = argv[++i];
        } else if (a == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (a == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (a == "--scale" && i + 1 < argc) {
            renderScale = float(std::atof(argv[++i]));
        } else if (a == "--no-halo") {
            halo = false;
        } else if (a == "--no-grid") {
            grid = false;
        } else if (a == "--debug-hits") {
            debug = true;
        } else if (a == "--az" && i + 1 < argc) {
            camAz = float(std::atof(argv[++i]));
        } else if (a == "--el" && i + 1 < argc) {
            camEl = float(std::atof(argv[++i]));
        } else if (a == "--dist" && i + 1 < argc) {
            camDist = float(std::atof(argv[++i]));
        } else {
            std::printf("usage: blackhole [--width W] [--height H] [--scale S] "
                        "[--no-halo] [--no-grid] [--test FRAMES OUT.ppm]\n");
            return (a == "--help") ? 0 : 1;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    if (testFrames > 0) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(width, height,
                                       "Black Hole - Schwarzschild ray tracer",
                                       nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    // auto render scale: on high-DPI (retina) displays render slightly below
    // native resolution so the geodesic integrator stays comfortably realtime
    if (renderScale < 0.0f) {
        int winW = 0, winH = 0;
        glfwGetWindowSize(win, &winW, &winH);
        int fbW0 = 0, fbH0 = 0;
        glfwGetFramebufferSize(win, &fbW0, &fbH0);
        renderScale = (fbW0 > winW) ? 0.75f : 1.0f;
    }

    std::printf("GL renderer: %s\n", glGetString(GL_RENDERER));

    GLuint rtProg = linkProgram(kRtVs, kRtFs, "raytrace");
    GLuint blitProg = linkProgram(kRtVs, kBlitFs, "blit");
    GLuint gridProg = linkProgram(kGridVs, kGridFs, "grid");

    GLuint triVao;
    glGenVertexArrays(1, &triVao);
    glBindVertexArray(triVao);

    // ray-trace target (offscreen, optional downscale)
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    int rtW = std::max(1, int(fbW * renderScale));
    int rtH = std::max(1, int(fbH * renderScale));
    GLuint rtTex, rtFbo;
    glGenTextures(1, &rtTex);
    glBindTexture(GL_TEXTURE_2D, rtTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rtW, rtH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &rtFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rtFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "ray-trace FBO incomplete\n");
        return 1;
    }

    // grid mesh
    std::vector<float> mesh = buildGridMesh();
    GLuint gridVao, gridVbo;
    glGenVertexArrays(1, &gridVao);
    glBindVertexArray(gridVao);
    glGenBuffers(1, &gridVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size() * sizeof(float)), mesh.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    const GLsizei gridVertCount = GLsizei(mesh.size() / 5);

    // uniform locations
    struct { GLint res, camPos, fwd, right, up, tanFov, time, rs, rin, rout, bright, halo, debug; } uRt;
    uRt.res    = glGetUniformLocation(rtProg, "uRes");
    uRt.camPos = glGetUniformLocation(rtProg, "uCamPos");
    uRt.fwd    = glGetUniformLocation(rtProg, "uCamFwd");
    uRt.right  = glGetUniformLocation(rtProg, "uCamRight");
    uRt.up     = glGetUniformLocation(rtProg, "uCamUp");
    uRt.tanFov = glGetUniformLocation(rtProg, "uTanHalfFov");
    uRt.time   = glGetUniformLocation(rtProg, "uTime");
    uRt.rs     = glGetUniformLocation(rtProg, "uRs");
    uRt.rin    = glGetUniformLocation(rtProg, "uDiskRin");
    uRt.rout   = glGetUniformLocation(rtProg, "uDiskRout");
    uRt.bright = glGetUniformLocation(rtProg, "uDiskBrightness");
    uRt.halo   = glGetUniformLocation(rtProg, "uHaloStrength");
    uRt.debug  = glGetUniformLocation(rtProg, "uDebug");
    GLint uBlitTex = glGetUniformLocation(blitProg, "uTex");
    GLint uGridVp = glGetUniformLocation(gridProg, "uViewProj");
    GLint uGridTime = glGetUniformLocation(gridProg, "uTime");
    GLint uGridRs = glGetUniformLocation(gridProg, "uRs");

    // constant uniforms
    glUseProgram(rtProg);
    glUniform1f(uRt.rs, kRs);
    glUniform1f(uRt.rin, kDiskRin);
    glUniform1f(uRt.rout, kDiskRout);
    glUniform1f(uRt.bright, kDiskBrightness);
    glUseProgram(gridProg);
    glUniform1f(uGridRs, kRs);

    Input in;
    in.showHalo = halo;
    in.showGrid = grid;
    if (camAz >= 0.0f) in.cam.az = camAz;
    if (camEl >= 0.0f) in.cam.el = camEl;
    if (camDist >= 0.0f) in.cam.dist = camDist;
    glfwSetWindowUserPointer(win, &in);
    glfwSetScrollCallback(win, scrollCb);
    glfwSetMouseButtonCallback(win, mouseBtnCb);
    glfwSetCursorPosCallback(win, cursorCb);

    if (testFrames == 0) {
        std::printf("\nControls: left-drag = orbit | scroll = zoom | R = reset | "
                    "G = grid | H = halo | SPACE = pause | ESC = quit\n\n");
    }

    double simTime = 0.0;
    double prevT = glfwGetTime();
    double fpsWindowStart = prevT;
    int fpsFrames = 0;
    int frame = 0;
    double frameTimeAccum = 0.0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        if (!in.paused) simTime += now - prevT;
        prevT = now;

        if (keyPressed(win, GLFW_KEY_ESCAPE, in)) glfwSetWindowShouldClose(win, 1);
        if (keyPressed(win, GLFW_KEY_R, in)) in.cam = OrbitCam{};
        if (keyPressed(win, GLFW_KEY_G, in)) in.showGrid = !in.showGrid;
        if (keyPressed(win, GLFW_KEY_H, in)) in.showHalo = !in.showHalo;
        if (keyPressed(win, GLFW_KEY_SPACE, in)) in.paused = !in.paused;

        // handle window resize
        int nfbW, nfbH;
        glfwGetFramebufferSize(win, &nfbW, &nfbH);
        if (nfbW != fbW || nfbH != fbH) {
            fbW = nfbW;
            fbH = nfbH;
            rtW = std::max(1, int(fbW * renderScale));
            rtH = std::max(1, int(fbH * renderScale));
            glBindTexture(GL_TEXTURE_2D, rtTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rtW, rtH, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
        }

        Vec3 camPos = in.cam.pos();
        Vec3 fwd = normalize(Vec3{0, 0, 0} - camPos);
        Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
        Vec3 up = cross(right, fwd);

        double t0 = glfwGetTime();

        // 1) ray-trace the black hole scene into the offscreen target
        glBindFramebuffer(GL_FRAMEBUFFER, rtFbo);
        glViewport(0, 0, rtW, rtH);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(rtProg);
        glUniform2f(uRt.res, float(rtW), float(rtH));
        glUniform3f(uRt.camPos, camPos.x, camPos.y, camPos.z);
        glUniform3f(uRt.fwd, fwd.x, fwd.y, fwd.z);
        glUniform3f(uRt.right, right.x, right.y, right.z);
        glUniform3f(uRt.up, up.x, up.y, up.z);
        glUniform1f(uRt.tanFov, std::tan(kFovY * 0.5f));
        glUniform1f(uRt.time, float(simTime));
        glUniform1f(uRt.halo, in.showHalo ? 1.0f : 0.0f);
        glUniform1i(uRt.debug, debug ? 1 : 0);
        glBindVertexArray(triVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 2) blit to the window
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glUseProgram(blitProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rtTex);
        glUniform1i(uBlitTex, 0);
        glBindVertexArray(triVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 3) spacetime curvature grid overlay
        if (in.showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glUseProgram(gridProg);
            Mat4 view = lookAt(camPos, Vec3{0, 0, 0}, Vec3{0, 1, 0});
            Mat4 proj = perspective(kFovY, float(fbW) / float(fbH), 0.1f, 200.0f);
            Mat4 vp = matMul(proj, view);
            glUniformMatrix4fv(uGridVp, 1, GL_FALSE, vp.m);
            glUniform1f(uGridTime, float(simTime));
            glBindVertexArray(gridVao);
            glDrawArrays(GL_LINES, 0, gridVertCount);
            glDisable(GL_BLEND);
        }

        frameTimeAccum += glfwGetTime() - t0;
        frame++;

        if (testFrames > 0) {
            if (frame >= testFrames) {
                std::vector<unsigned char> px(size_t(fbW) * fbH * 3);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glFinish();
                glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                writePPM(testOut.c_str(), px.data(), fbW, fbH);
                std::printf("avg frame time: %.2f ms (%.1f FPS)\n",
                            frameTimeAccum / frame * 1000.0, frame / frameTimeAccum);
                break;
            }
        } else {
            glfwSwapBuffers(win);
        }
        glfwPollEvents();

        fpsFrames++;
        double tNow = glfwGetTime();
        if (tNow - fpsWindowStart >= 1.0) {
            double fps = fpsFrames / (tNow - fpsWindowStart);
            char title[128];
            std::snprintf(title, sizeof(title),
                          "Black Hole - Schwarzschild ray tracer - %.0f FPS", fps);
            glfwSetWindowTitle(win, title);
            std::printf("%.1f FPS (render target %dx%d)\n", fps, rtW, rtH);
            std::fflush(stdout);
            fpsFrames = 0;
            fpsWindowStart = tNow;
        }
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
