// Real-time black hole simulation: ray-traced null geodesics (Schwarzschild),
// relativistic accretion disk, lensed Flamm-paraboloid spacetime grid.
// OpenGL 3.3 core + GLFW. All heavy math runs per-pixel on the GPU.

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "shaders.h"

static int   gWinW = 1500, gWinH = 840;
static double gYaw = 0.45, gPitch = 0.16, gDist = 23.0;
static bool  gDragL = false, gDragR = false, gAutoRot = true;
static bool  gLens = true, gDisk = true, gGrid = true, gStars = true;
static double gLastX = 0, gLastY = 0;

static const float GRID_R = 10.0f;

static float funnelY(float r) {
    if (!gLens) return 0.0f;
    float z = -2.0f * std::sqrt(std::max(r - 1.0f, 0.0f));
    float t = std::min(std::max((GRID_R - r) / (GRID_R - GRID_R * 0.55f), 0.0f), 1.0f);
    return z * t * t * (3.0f - 2.0f * t);
}

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile error:\n%s\n", log);
        std::exit(1);
    }
    return s;
}

static GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link error:\n%s\n", log);
        std::exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static GLuint gFBO = 0, gFBOtex = 0;
static int   gRenderW = 0, gRenderH = 0;
static float gScale = 0.85f;

static void resizeFBO(int w, int h) {
    gRenderW = std::max(64, (int)(w * gScale));
    gRenderH = std::max(64, (int)(h * gScale));
    glBindTexture(GL_TEXTURE_2D, gFBOtex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gRenderW, gRenderH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

static void cursorPosCB(GLFWwindow*, double x, double y) {
    double dx = x - gLastX, dy = y - gLastY;
    gLastX = x; gLastY = y;
    if (gDragL || gDragR) {
        gYaw   -= dx * 0.0042;
        gPitch += dy * 0.0042;
        gPitch = std::min(std::max(gPitch, -1.45), 1.45);
    }
}

static void mouseBtnCB(GLFWwindow* win, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) gDragL = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) gDragR = (action == GLFW_PRESS);
    if (action == GLFW_PRESS) {
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        gLastX = x; gLastY = y;
    }
}

static void scrollCB(GLFWwindow*, double, double off) {
    gDist *= std::pow(0.88, off);
    gDist = std::min(std::max(gDist, 1.95), 60.0);
}

static void keyCB(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, GLFW_TRUE); break;
        case GLFW_KEY_B: gLens  = !gLens;  break;
        case GLFW_KEY_D: gDisk  = !gDisk;  break;
        case GLFW_KEY_G: gGrid  = !gGrid;  break;
        case GLFW_KEY_S: gStars = !gStars; break;
        case GLFW_KEY_R: gAutoRot = !gAutoRot; break;
        case GLFW_KEY_C: gLens = gDisk = gGrid = gStars = true;
                         gDist = 23.0; gPitch = 0.16; break;
    }
}

static void sizeCB(GLFWwindow*, int w, int h) {
    gWinW = w; gWinH = h;
}

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(gWinW, gWinH, "Black Hole", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::fprintf(stderr, "glewInit failed\n"); return 1; }
    glfwSwapInterval(1);

    glfwSetCursorPosCallback(win, cursorPosCB);
    glfwSetMouseButtonCallback(win, mouseBtnCB);
    glfwSetScrollCallback(win, scrollCB);
    glfwSetKeyCallback(win, keyCB);
    glfwSetFramebufferSizeCallback(win, sizeCB);

    GLuint progRay = link(VS_FULL, FS_RAY);
    GLuint progPre = link(VS_FULL, FS_PRESENT);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenTextures(1, &gFBOtex);
    glBindTexture(GL_TEXTURE_2D, gFBOtex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &gFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gFBO);

    GLFWvidmode const* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    int scrW = mode ? mode->width : 1512;
    glfwGetFramebufferSize(win, &gWinW, &gWinH);
    if (gWinW > scrW) gScale = 0.55f;
    resizeFBO(gWinW, gWinH);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gFBOtex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "warning: FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    double t0 = glfwGetTime(), fpsTime = 0;
    int frames = 0;
    double fpsAcc = 0;
    int slowWin = 0, fastWin = 0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        double dt = now - t0;
        t0 = now;
        glfwPollEvents();
        glfwGetFramebufferSize(win, &gWinW, &gWinH);
        static int lastW = 0, lastH = 0;
        static float lastScale = -1.0f;
        if (gWinW != lastW || gWinH != lastH || gScale != lastScale) {
            lastW = gWinW; lastH = gWinH; lastScale = gScale;
            resizeFBO(gWinW, gWinH);
        }

        if (gAutoRot && !gDragL && !gDragR) gYaw += 0.045 * dt;

        // camera position (y-up), kept outside the horizon and above the funnel
        gDist = std::max(gDist, 1.95);
        float cp = (float)std::cos(gPitch), sp = (float)std::sin(gPitch);
        float cwy = (float)std::cos(gYaw), swy = (float)std::sin(gYaw);
        float cx = gDist * cp * swy, cy2 = gDist * sp, cz = gDist * cp * cwy;
        float rr = std::sqrt(cx * cx + cy2 * cy2 + cz * cz);
        float floorY = funnelY(rr) + 0.12f;
        if (cy2 < floorY) cy2 = floorY;
        float camPos[3] = { cx, cy2, cz };

        float f[3] = { -camPos[0], -camPos[1], -camPos[2] };
        float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        f[0] /= fl; f[1] /= fl; f[2] /= fl;
        float up0[3] = { 0, 1, 0 };
        float rgt[3] = { f[1] * up0[2] - f[2] * up0[1],
                         f[2] * up0[0] - f[0] * up0[2],
                         f[0] * up0[1] - f[1] * up0[0] };
        float rl = std::sqrt(rgt[0] * rgt[0] + rgt[1] * rgt[1] + rgt[2] * rgt[2]);
        rgt[0] /= rl; rgt[1] /= rl; rgt[2] /= rl;
        float upv[3] = { rgt[1] * f[2] - rgt[2] * f[1],
                         rgt[2] * f[0] - rgt[0] * f[2],
                         rgt[0] * f[1] - rgt[1] * f[0] };

        // ---- pass 1: ray-trace into FBO at render scale ----
        glBindFramebuffer(GL_FRAMEBUFFER, gFBO);
        glViewport(0, 0, gRenderW, gRenderH);
        glUseProgram(progRay);
        glUniform2f(glGetUniformLocation(progRay, "uResolution"), gRenderW, gRenderH);
        glUniform3fv(glGetUniformLocation(progRay, "uCamPos"), 1, camPos);
        glUniform3fv(glGetUniformLocation(progRay, "uCamRight"), 1, rgt);
        glUniform3fv(glGetUniformLocation(progRay, "uCamUp"), 1, upv);
        glUniform3fv(glGetUniformLocation(progRay, "uCamFwd"), 1, f);
        glUniform1f(glGetUniformLocation(progRay, "uFovScale"),
                    std::tan(27.0 * 3.14159265 / 180.0));
        glUniform1f(glGetUniformLocation(progRay, "uTime"), (float)now);
        glUniform1i(glGetUniformLocation(progRay, "uLens"), gLens);
        glUniform1i(glGetUniformLocation(progRay, "uDisk"), gDisk);
        glUniform1i(glGetUniformLocation(progRay, "uGrid"), gGrid);
        glUniform1i(glGetUniformLocation(progRay, "uStars"), gStars);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- pass 2: present to window ----
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, gWinW, gWinH);
        glUseProgram(progPre);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gFBOtex);
        glUniform2f(glGetUniformLocation(progPre, "uWinRes"), gWinW, gWinH);
        glUniform1i(glGetUniformLocation(progPre, "uTex"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(win);

        // ---- fps + adaptive resolution ----
        frames++;
        fpsAcc += dt;
        if (fpsAcc > 0.5) {
            double fps = frames / fpsAcc;
            std::string title = "Black Hole  |  " + std::to_string((int)(fps + 0.5)) + " fps" +
                "  |  " + (gLens ? "GR: ON " : "GR: OFF ") +
                "  |  drag: orbit   wheel: zoom   B:relativity G:grid D:disk S:stars R:auto-rotate C:reset ESC:quit";
            glfwSetWindowTitle(win, title.c_str());
            frames = 0; fpsAcc = 0;

            if (dt > 0.034) { slowWin++; fastWin = 0; }
            else if (dt < 0.017) { fastWin++; slowWin = 0; }
            if (slowWin >= 2 && gScale > 0.45f) { gScale = std::max(0.45f, gScale - 0.1f); resizeFBO(gWinW, gWinH); slowWin = 0; }
            if (fastWin >= 4 && gScale < 1.0f)  { gScale = std::min(1.0f, gScale + 0.05f); resizeFBO(gWinW, gWinH); fastWin = 0; }
            (void)fpsTime;
        }
    }

    glDeleteFramebuffers(1, &gFBO);
    glDeleteTextures(1, &gFBOtex);
    glDeleteProgram(progRay);
    glDeleteProgram(progPre);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}