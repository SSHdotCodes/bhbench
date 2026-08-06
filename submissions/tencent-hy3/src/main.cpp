// ===========================================================================
//  Scientific real-time black hole simulation (Schwarzschild, G=c=1).
//
//  Features:
//   * Gravitational lensing via exact Schwarzschild photon geodesics (RK4).
//   * Accretion disk with relativistic Doppler beaming + thin-disk temperature.
//   * Black-hole shadow (photon capture) and lensed starfield background.
//   * Spacetime-curvature "trapdoor" embedding grid (Flamm's paraboloid).
//
//  Controls:
//   Mouse drag  : orbit camera around the hole
//   W/S         : move camera closer / farther
//   G           : toggle spacetime grid overlay
//   D           : toggle accretion-disk ray tracing
//   [ / ]       : decrease / increase render resolution (quality vs speed)
//   ESC         : quit
// ===========================================================================

#include "blackhole.h"
#include "spacetime_grid.h"
#include "gl_utils.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>

static int g_width = 900, g_height = 600;
static glm::vec3 g_camPos(0.0f, 6.0f, 22.0f);
static float g_yaw = 0.0f, g_pitch = 0.25f;
static bool g_showGrid = true;
static bool g_showDisk = true;
static int g_scale = 2; // render at width/scale for speed

static bh::Disk g_disk{ 6.0f, 18.0f, 0.4f }; // ISCO=6M, outer extends

static void computeCamera(glm::vec3& pos, glm::vec3& fwd, glm::vec3& right, glm::vec3& up) {
    // Orbit around origin based on yaw/pitch and radius from origin.
    const float radius = glm::length(g_camPos);
    g_camPos = glm::vec3(
        radius * std::cos(g_pitch) * std::sin(g_yaw),
        radius * std::sin(g_pitch),
        radius * std::cos(g_pitch) * std::cos(g_yaw));
    pos = g_camPos;
    fwd = glm::normalize(-pos);
    right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    up = glm::cross(right, fwd);
}

static void renderIntoBuffer(std::vector<uint8_t>& buf, int rw, int rh) {
    glm::vec3 pos, fwd, right, up;
    computeCamera(pos, fwd, right, up);
    const float fov = glm::radians(55.0f);
    const float aspect = (float)rw / (float)rh;
    const float tanF = std::tan(fov * 0.5f);

    // Adaptive steps based on distance for realtime.
    const int steps = 400;

    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            const float ndcx = (2.0f * (x + 0.5f) / rw - 1.0f) * aspect * tanF;
            const float ndcy = (2.0f * (y + 0.5f) / rh - 1.0f) * tanF;
            glm::vec3 dir = glm::normalize(fwd + ndcx * right + ndcy * up);

            glm::vec3 col(0.0f);
            if (g_showDisk) {
                bh::RayResult r = bh::traceRay(pos, dir, g_disk, steps, 1.0f);
                col = r.color;
                if (r.captured) col = glm::vec3(0.0f); // the shadow
            } else {
                // disk-less lensing: show lensed background + shadow only.
                bh::Disk nodisk{ 0.0f, 0.0f, 0.0f };
                bh::RayResult r = bh::traceRay(pos, dir, nodisk, steps, 1.0f);
                col = r.captured ? glm::vec3(0.0f) : r.color;
            }

            // tone map
            col = col / (col + glm::vec3(1.0f));
            col = glm::pow(col, glm::vec3(1.0f / 2.2f));

            const int idx = (y * rw + x) * 3;
            buf[idx + 0] = (uint8_t)(glm::clamp(col.r, 0.0f, 1.0f) * 255);
            buf[idx + 1] = (uint8_t)(glm::clamp(col.g, 0.0f, 1.0f) * 255);
            buf[idx + 2] = (uint8_t)(glm::clamp(col.b, 0.0f, 1.0f) * 255);
        }
    }
}

static void drawGrid() {
    bh::SpacetimeGrid grid;
    grid.build();
    glDisable(GL_LIGHTING);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    const float* v = grid.verts.data();
    const float* c = grid.cols.data();
    for (size_t i = 0; i < grid.vertexCount(); ++i) {
        glColor3f(c[i*3], c[i*3+1], c[i*3+2]);
        glVertex3f(v[i*3], v[i*3+1], v[i*3+2]);
    }
    glEnd();
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return 1;
    }
    // Request a legacy (compat) OpenGL context so glDrawPixels / glBegin
    // immediate-mode and the fixed-function pipeline are available, which
    // is what macOS exposes reliably without an external GL loader.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(g_width, g_height,
                                       "Black Hole - Schwarzschild Ray Tracer",
                                       nullptr, nullptr);
    if (!win) { std::cerr << "Failed to create window\n"; return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    double lastX = g_width/2.0, lastY = g_height/2.0;
    bool dragging = false;

    int rw = g_width / g_scale, rh = g_height / g_scale;
    std::vector<uint8_t> buf((size_t)rw * rh * 3);

    std::cout << "Controls: drag=orbit  W/S=zoom  G=grid  D=disk  [ ]=quality  ESC=quit\n";

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // input
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        if (glfwGetKey(win, GLFW_KEY_G) == GLFW_PRESS) g_showGrid = !g_showGrid;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_showDisk = !g_showDisk;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camPos *= 0.97f;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camPos *= 1.03f;
        if (glfwGetKey(win, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) {
            g_scale = glm::min(g_scale + 1, 8);
            rw = g_width / g_scale; rh = g_height / g_scale;
            buf.resize((size_t)rw * rh * 3);
        }
        if (glfwGetKey(win, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) {
            g_scale = glm::max(g_scale - 1, 1);
            rw = g_width / g_scale; rh = g_height / g_scale;
            buf.resize((size_t)rw * rh * 3);
        }

        // mouse orbit
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            double x, y; glfwGetCursorPos(win, &x, &y);
            if (dragging) {
                g_yaw   -= (x - lastX) * 0.005f;
                g_pitch += (y - lastY) * 0.005f;
                g_pitch = glm::clamp(g_pitch, -1.4f, 1.4f);
            }
            dragging = true; lastX = x; lastY = y;
        } else dragging = false;

        // ---- ray-trace the black hole into the CPU buffer ----
        renderIntoBuffer(buf, rw, rh);

        // ---- present ----
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glRasterPos2f(-1.0f, -1.0f);
        // draw the rendered image scaled to full screen.
        glPixelZoom((float)g_width / rw, (float)g_height / rh);
        glDrawPixels(rw, rh, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
        glPixelZoom(1.0f, 1.0f);

        // ---- overlay the spacetime grid in a corner inset ----
        if (g_showGrid) {
            glViewport(0, 0, 300, 200);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glMatrixMode(GL_PROJECTION);
            glm::mat4 P = glm::perspective(glm::radians(50.0f), 300.0f/200.0f, 0.1f, 200.0f);
            glLoadMatrixf(&P[0][0]);
            glMatrixMode(GL_MODELVIEW);
            glm::mat4 V = glm::lookAt(glm::vec3(0.0f, 14.0f, 26.0f),
                                      glm::vec3(0.0f, -6.0f, 0.0f),
                                      glm::vec3(0.0f, 0.0f, 1.0f));
            glLoadMatrixf(&V[0][0]);
            drawGrid();
            glViewport(0, 0, g_width, g_height);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(-1, 1, -1, 1, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
        }

        glfwSwapBuffers(win);
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
