// Relativistic black hole ray tracer — Schwarzschild spacetime.
// C++17 / OpenGL 4.1 / GLFW. See README.md for the physics model.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl_helpers.h"
#include "math.h"
#include "font.h"
#include "selftest.h"
#include <algorithm>
#include <chrono>

// ---------------- app state ----------------
static struct {
    double azim = 35.0 * M_PI / 180.0;   // camera azimuth
    double incl = 80.0 * M_PI / 180.0;   // polar angle from +y (disk normal)
    double dist = 26.0;                  // camera radius, units of RS
    double simTime = 0.0;
    bool pause = false;
    bool grid = true, disk = true, stars = true, bloom = true, hud = true;
    int quality = 1;                     // 1 high, 2 medium, 3 fast
    bool autoScale = true;
    float renderScale = 0.75f;
    float exposure = 1.0f;
} S;

static GLFWwindow* gWin = nullptr;
static double gLastX = 0, gLastY = 0;
static bool gDragging = false;
static std::string gCapturePath;
static int gCaptureFrames = 40;
static bool gWantCapture = false;
static bool gCaptureExit = false;   // exit after capture (CLI mode only)

// ---------------- callbacks ----------------
static void keyCB(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: case GLFW_KEY_Q: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_G: S.grid = !S.grid; break;
        case GLFW_KEY_D: S.disk = !S.disk; break;
        case GLFW_KEY_S: S.stars = !S.stars; break;
        case GLFW_KEY_B: S.bloom = !S.bloom; break;
        case GLFW_KEY_H: S.hud = !S.hud; break;
        case GLFW_KEY_SPACE: S.pause = !S.pause; break;
        case GLFW_KEY_1: S.quality = 1; break;
        case GLFW_KEY_2: S.quality = 2; break;
        case GLFW_KEY_3: S.quality = 3; break;
        case GLFW_KEY_0: S.autoScale = !S.autoScale; break;
        case GLFW_KEY_LEFT_BRACKET:  S.exposure = std::max(0.15f, S.exposure / 1.25f); break;
        case GLFW_KEY_RIGHT_BRACKET: S.exposure = std::min(8.0f, S.exposure * 1.25f); break;
        case GLFW_KEY_R:
            S.azim = 35.0*M_PI/180.0; S.incl = 80.0*M_PI/180.0; S.dist = 26.0;
            S.exposure = 1.0f; break;
        case GLFW_KEY_P: gWantCapture = true; gCapturePath = "capture.png"; break;
        case GLFW_KEY_F: {
            static bool fs = false; fs = !fs;
            if (fs) {
                GLFWmonitor* m = glfwGetPrimaryMonitor();
                const GLFWvidmode* v = glfwGetVideoMode(m);
                glfwSetWindowMonitor(w, m, 0, 0, v->width, v->height, v->refreshRate);
            } else glfwSetWindowMonitor(w, nullptr, 80, 80, 1440, 900, 0);
            break;
        }
    }
}
static void mouseCB(GLFWwindow*, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gDragging = (action == GLFW_PRESS);
        glfwGetCursorPos(gWin, &gLastX, &gLastY);
    }
}
static void cursorCB(GLFWwindow*, double x, double y) {
    if (!gDragging) return;
    S.azim -= (x - gLastX) * 0.005;
    S.incl = std::min(M_PI - 0.035, std::max(0.035, S.incl - (y - gLastY) * 0.005));
    gLastX = x; gLastY = y;
}
static void scrollCB(GLFWwindow*, double, double dy) {
    S.dist = std::min(80.0, std::max(5.0, S.dist * std::exp(-dy * 0.0012)));
}

// ---------------- render targets ----------------
struct Target { GLuint fbo = 0, tex = 0, depth = 0; int w = 0, h = 0; };
static void destroyTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.tex) glDeleteTextures(1, &t.tex);
    if (t.depth) glDeleteRenderbuffers(1, &t.depth);
    t = {};
}
static void makeTarget(Target& t, int w, int h, bool withDepth) {
    destroyTarget(t);
    t.w = w; t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (withDepth) {
        glGenRenderbuffers(1, &t.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "incomplete framebuffer\n"); exit(1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------- spacetime grid (Flamm paraboloid) ----------------
// Exact isometric embedding of the t=const, equatorial spatial slice:
//   y(r) = -2 sqrt(RS (r - RS)),  RS = 1.
static GLuint gGridVAO = 0, gGridVBO = 0; static int gGridVerts = 0;
static void buildGrid() {
    std::vector<float> v;
    auto pt = [](double r, double a) -> Vec3 {
        return {(float)(r*std::cos(a)), (float)(-2.0*std::sqrt(r - 1.0)), (float)(r*std::sin(a))};
    };
    auto alpha = [](double r) {
        double t = std::min(1.0, (r - 1.0) / 15.0);
        return (float)(0.55 * std::pow(1.0 - t, 1.4) + 0.05);
    };
    auto add = [&](Vec3 p, double r) {
        float b = 0.35f + 0.65f * (float)std::max(0.0, 1.0 - (r-1.0)/15.0);
        v.insert(v.end(), {p.x, p.y, p.z, 0.30f*b, 0.78f*b, 1.0f*b, alpha(r)});
    };
    const int NR = 26, NS = 96, NSEG = 80, NSPOKE = 36;
    for (int i = 0; i < NR; ++i) {
        double r = 1.02 + 15.0 * std::pow((double)i/(NR-1), 1.8);
        for (int j = 0; j < NS; ++j) {
            double a0 = 2*M_PI*j/NS, a1 = 2*M_PI*(j+1)/NS;
            add(pt(r, a0), r); add(pt(r, a1), r);
        }
    }
    for (int k = 0; k < NSPOKE; ++k) {
        double a = 2*M_PI*k/NSPOKE;
        for (int j = 0; j < NSEG; ++j) {
            double r0 = 1.02 + 15.0*(double)j/NSEG, r1 = 1.02 + 15.0*(double)(j+1)/NSEG;
            add(pt(r0, a), r0); add(pt(r1, a), r1);
        }
    }
    gGridVerts = (int)(v.size() / 7);
    glGenVertexArrays(1, &gGridVAO);
    glGenBuffers(1, &gGridVBO);
    glBindVertexArray(gGridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gGridVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, (void*)12);
    glBindVertexArray(0);
}

// ---------------- HUD text ----------------
static GLuint gFontTex = 0, gTextVAO = 0, gTextVBO = 0;
static std::vector<float> gTextVerts;
static void buildFont() {
    unsigned char atlas[128*48] = {};
    for (int c = 32; c < 127; ++c) {
        int col = (c-32) % 16, row = (c-32) / 16;
        for (int py = 0; py < 7; ++py)
            for (int px = 0; px < 5; ++px)
                if (FONT_5x7[c-32][py] & (1 << (4-px)))
                    atlas[(row*8 + (6-py))*128 + col*8 + px] = 255;
    }
    glGenTextures(1, &gFontTex);
    glBindTexture(GL_TEXTURE_2D, gFontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 48, 0, GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &gTextVAO);
    glGenBuffers(1, &gTextVBO);
    glBindVertexArray(gTextVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gTextVBO);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glBindVertexArray(0);
}
static void textLine(float x, float y, const char* s, float scale) {
    for (; *s; ++s, x += 6*scale) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        float u0 = ((c-32)%16)*8.0f/128.0f, u1 = u0 + 5.0f/128.0f;
        float v0 = ((c-32)/16)*8.0f/48.0f,  v1 = v0 + 7.0f/48.0f;
        float x1 = x + 5*scale, y1 = y + 7*scale;
        gTextVerts.insert(gTextVerts.end(), {x,y,u0,v1, x1,y,u1,v1, x,y1,u0,v0,
                                             x1,y,u1,v1, x1,y1,u1,v0, x,y1,u0,v0});
    }
}

// ---------------- helpers ----------------
static GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }
static void drawQuad() { glDrawArrays(GL_TRIANGLES, 0, 3); }

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i+1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--selftest") return test::run();
        else if (a == "--capture") { gCapturePath = next(); gWantCapture = true; gCaptureExit = true; }
        else if (a == "--frames") gCaptureFrames = std::atoi(next().c_str());
        else if (a == "--scale") { S.renderScale = std::atof(next().c_str()); S.autoScale = false; }
        else if (a == "--incl") S.incl = std::atof(next().c_str()) * M_PI / 180.0;
        else if (a == "--dist") S.dist = std::atof(next().c_str());
        else if (a == "--azim") S.azim = std::atof(next().c_str()) * M_PI / 180.0;
        else if (a == "--time") S.simTime = std::atof(next().c_str());
        else if (a == "--exposure") S.exposure = std::atof(next().c_str());
        else if (a == "--no-grid") S.grid = false;
        else if (a == "--no-disk") S.disk = false;
        else if (a == "--no-hud") S.hud = false;
    }

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    gWin = glfwCreateWindow(1440, 900, "Schwarzschild Black Hole — Relativistic Ray Tracer", nullptr, nullptr);
    if (!gWin) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(gWin);
    glfwSwapInterval(1);
    glfwSetKeyCallback(gWin, keyCB);
    glfwSetMouseButtonCallback(gWin, mouseCB);
    glfwSetCursorPosCallback(gWin, cursorCB);
    glfwSetScrollCallback(gWin, scrollCB);
    printf("OpenGL %s\n", glGetString(GL_VERSION));

    std::string dir = SHADER_DIR;
    GLuint rayProg   = loadProgram(dir, "quad.vert", "raytrace.frag");
    GLuint gridProg  = loadProgram(dir, "grid.vert", "grid.frag");
    GLuint brightProg= loadProgram(dir, "quad.vert", "bright.frag");
    GLuint blurProg  = loadProgram(dir, "quad.vert", "blur.frag");
    GLuint compProg  = loadProgram(dir, "quad.vert", "composite.frag");
    GLuint textProg  = loadProgram(dir, "text.vert", "text.frag");
    GLuint emptyVAO; glGenVertexArrays(1, &emptyVAO);
    buildGrid(); buildFont();

    GLuint blackTex; glGenTextures(1, &blackTex);
    glBindTexture(GL_TEXTURE_2D, blackTex);
    unsigned char zero[4] = {0,0,0,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);

    Target scene, bloomA, bloomB;
    int sceneW = 0, sceneH = 0;

    double prev = glfwGetTime(), fpsT = prev; int frames = 0; float fps = 60;
    int frameNo = 0;

    while (!glfwWindowShouldClose(gWin)) {
        double now = glfwGetTime();
        double dt = std::min(0.1, now - prev); prev = now;
        if (!S.pause) S.simTime += dt * 6.0;   // geometric time units, 6x playback
        ++frames; ++frameNo;
        if (now - fpsT >= 1.0) {
            fps = frames / (float)(now - fpsT); frames = 0; fpsT = now;
            if (S.autoScale && !gWantCapture) {
                float old = S.renderScale;
                if (fps < 38) S.renderScale = std::max(0.40f, S.renderScale - 0.10f);
                else if (fps > 57) S.renderScale = std::min(1.0f, S.renderScale + 0.05f);
                if (old != S.renderScale) sceneW = 0; // force rebuild
            }
        }

        int winW, winH, fbW, fbH;
        glfwGetWindowSize(gWin, &winW, &winH);
        glfwGetFramebufferSize(gWin, &fbW, &fbH);
        int sw = std::max(64, (int)(fbW * S.renderScale));
        int sh = std::max(64, (int)(fbH * S.renderScale));
        if (sw != sceneW || sh != sceneH) {
            sceneW = sw; sceneH = sh;
            makeTarget(scene, sw, sh, true);
            makeTarget(bloomA, std::max(1, sw/4), std::max(1, sh/4), false);
            makeTarget(bloomB, bloomA.w, bloomA.h, false);
        }

        // camera
        Vec3 eye = {(float)(S.dist*std::sin(S.incl)*std::cos(S.azim)),
                    (float)(S.dist*std::cos(S.incl)),
                    (float)(S.dist*std::sin(S.incl)*std::sin(S.azim))};
        Vec3 fwd = normalize(-eye);
        Vec3 right = normalize(cross(fwd, {0,1,0}));
        Vec3 up = cross(right, fwd);
        float aspect = (float)winW / winH;
        Mat4 view = lookAt(eye, {0,0,0}, up);
        Mat4 proj = perspective(45.0f * (float)M_PI / 180.0f, aspect, 0.1f, 500.0f);
        Mat4 pv = mul(proj, view);
        float stepScale = S.quality == 1 ? 0.8f : (S.quality == 2 ? 1.2f : 1.8f);
        int maxSteps = S.quality == 1 ? 600 : (S.quality == 2 ? 420 : 300);

        // ---- pass 1: geodesic ray trace ----
        // Depth TEST off (sky fragments write depth 1.0, which would fail
        // GL_LESS against the cleared buffer); depth WRITE on for the grid.
        glBindFramebuffer(GL_FRAMEBUFFER, scene.fbo);
        glViewport(0, 0, sceneW, sceneH);
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(rayProg);
        glBindVertexArray(emptyVAO);
        glUniform2f(U(rayProg,"uResolution"), sceneW, sceneH);
        glUniform3f(U(rayProg,"uCamPos"), eye.x, eye.y, eye.z);
        glUniform3f(U(rayProg,"uCamRight"), right.x, right.y, right.z);
        glUniform3f(U(rayProg,"uCamUp"), up.x, up.y, up.z);
        glUniform3f(U(rayProg,"uCamFwd"), fwd.x, fwd.y, fwd.z);
        glUniform1f(U(rayProg,"uTanHalfFov"), std::tan(22.5f*(float)M_PI/180.0f));
        glUniform1f(U(rayProg,"uAspect"), (float)sceneW/sceneH);
        glUniformMatrix4fv(U(rayProg,"uPV"), 1, GL_FALSE, pv.m);
        glUniform1f(U(rayProg,"uTime"), (float)S.simTime);
        glUniform1f(U(rayProg,"uStepScale"), stepScale);
        glUniform1i(U(rayProg,"uMaxSteps"), maxSteps);
        glUniform1f(U(rayProg,"uDiskInner"), 3.0f);
        glUniform1f(U(rayProg,"uDiskOuter"), 12.0f);
        glUniform1f(U(rayProg,"uDiskTemp"), 5500.0f);
        glUniform1f(U(rayProg,"uShowDisk"), S.disk ? 1 : 0);
        glUniform1f(U(rayProg,"uShowStars"), S.stars ? 1 : 0);
        drawQuad();

        // ---- pass 2: spacetime embedding grid ----
        if (S.grid) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(gridProg);
            glUniformMatrix4fv(U(gridProg,"uPV"), 1, GL_FALSE, pv.m);
            glUniform3f(U(gridProg,"uCamPos"), eye.x, eye.y, eye.z);
            glBindVertexArray(gGridVAO);
            glDrawArrays(GL_LINES, 0, gGridVerts);
            glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        }

        // ---- pass 3: bloom ----
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(emptyVAO);
        if (S.bloom) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo);
            glViewport(0, 0, bloomA.w, bloomA.h);
            glUseProgram(brightProg);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, scene.tex);
            glUniform1i(U(brightProg,"uTex"), 0);
            glUniform2f(U(brightProg,"uTexel"), 1.0f/sceneW, 1.0f/sceneH);
            drawQuad();
            glUseProgram(blurProg);
            glUniform1i(U(blurProg,"uTex"), 0);
            for (int i = 0; i < 2; ++i) {
                glBindFramebuffer(GL_FRAMEBUFFER, bloomB.fbo);
                glBindTexture(GL_TEXTURE_2D, bloomA.tex);
                glUniform2f(U(blurProg,"uDir"), 1.0f/bloomA.w, 0);
                drawQuad();
                glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo);
                glBindTexture(GL_TEXTURE_2D, bloomB.tex);
                glUniform2f(U(blurProg,"uDir"), 0, 1.0f/bloomA.h);
                drawQuad();
            }
        }

        // ---- pass 4: tonemap to screen ----
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glUseProgram(compProg);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, scene.tex);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, S.bloom ? bloomA.tex : blackTex);
        glUniform1i(U(compProg,"uScene"), 0);
        glUniform1i(U(compProg,"uBloom"), 1);
        glUniform1f(U(compProg,"uExposure"), S.exposure);
        glUniform1f(U(compProg,"uBloomStrength"), S.bloom ? 0.40f : 0.0f);
        drawQuad();

        // ---- pass 5: HUD ----
        if (S.hud) {
            char l1[160], l2[160], l3[160];
            snprintf(l1, sizeof l1, "SCHWARZSCHILD BLACK HOLE   RS=1  M=0.5   r_photon=1.5  r_ISCO=3  b_shadow=2.598");
            snprintf(l2, sizeof l2, "FPS %.0f   scale %.2f   quality %s   incl %.1f deg   cam r=%.1f RS   t=%.1f",
                     fps, S.renderScale, S.quality==1?"HIGH":(S.quality==2?"MED":"FAST"),
                     S.incl*180/M_PI, S.dist, S.simTime);
            snprintf(l3, sizeof l3, "drag orbit  wheel zoom  |  G grid(%s) D disk(%s) S stars(%s) B bloom(%s)  |  1/2/3 quality 0 auto  [ ] exposure  SPACE pause  P shot  R reset",
                     S.grid?"on":"off", S.disk?"on":"off", S.stars?"on":"off", S.bloom?"on":"off");
            gTextVerts.clear();
            textLine(12, 10, l1, 2); textLine(12, 28, l2, 2); textLine(12, 46, l3, 2);
            if (S.grid) textLine(12, 64, "grid: Flamm paraboloid  y = -2 sqrt(RS (r - RS))  - exact embedding of the equatorial spatial slice", 2);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(textProg);
            glUniform2f(U(textProg,"uScreen"), winW, winH);
            glUniform4f(U(textProg,"uColor"), 0.85f, 0.92f, 1.0f, 0.9f);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gFontTex);
            glUniform1i(U(textProg,"uAtlas"), 0);
            glBindVertexArray(gTextVAO);
            glBindBuffer(GL_ARRAY_BUFFER, gTextVBO);
            glBufferData(GL_ARRAY_BUFFER, gTextVerts.size()*sizeof(float), gTextVerts.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (int)(gTextVerts.size()/4));
            glDisable(GL_BLEND);
        }

        // ---- screenshot ----
        bool captureNow = gWantCapture && (frameNo >= gCaptureFrames || glfwGetKey(gWin, GLFW_KEY_P) == GLFW_PRESS);
        if (gWantCapture && frameNo >= gCaptureFrames) {
            std::vector<unsigned char> px((size_t)fbW*fbH*3);
            glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            std::string path = gCapturePath.empty() ? "capture.png" : gCapturePath;
            if (savePNG(path, fbW, fbH, px.data())) printf("saved %s (%dx%d)\n", path.c_str(), fbW, fbH);
            else fprintf(stderr, "failed to save %s\n", path.c_str());
            if (gCaptureExit) break;
            gWantCapture = false;   // interactive P: keep running
        }
        (void)captureNow;
        glfwSwapBuffers(gWin);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}
