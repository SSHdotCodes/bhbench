#define GL_SILENCE_DEPRECATION
#define GLEW_NO_GLU
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "camera.hpp"
#include "geodesic.hpp"
#include "gl_util.hpp"
#include "shader_sources.hpp"

// ---------------------------------------------------------------------------
// Minimal column-major mat4 helpers
// ---------------------------------------------------------------------------
struct Mat4 {
    float m[16] = {0};
    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += b.m[k * 4 + c] * a.m[i * 4 + k];
            r.m[i * 4 + c] = s;
        }
    return r;
}
inline Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.0f / std::tan(fovY * 0.5f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = (float)s.x; r.m[4] = (float)s.y; r.m[8] = (float)s.z;
    r.m[1] = (float)u.x; r.m[5] = (float)u.y; r.m[9] = (float)u.z;
    r.m[2] = (float)-f.x; r.m[6] = (float)-f.y; r.m[10] = (float)-f.z;
    r.m[12] = (float)-dot(s, eye);
    r.m[13] = (float)-dot(u, eye);
    r.m[14] = (float)dot(f, eye);
    return r;
}

// ---------------------------------------------------------------------------
// Geometry builders
// ---------------------------------------------------------------------------
struct Mesh {
    GLuint vao = 0, vbo = 0;
    int count = 0;
};
struct LineMesh : Mesh {
    std::vector<float> verts; // pos(3) + col(3) interleaved
};

static void uploadLines(LineMesh& m, std::vector<float>&& v) {
    m.verts = std::move(v);
    m.count = (int)(m.verts.size() / 6);
    if (!m.vao) { glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); }
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, m.verts.size() * sizeof(float), m.verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Spacetime grid scene: Flamm's paraboloid embedding, traced light rays,
// plunging test particle, event horizon sphere.
// ---------------------------------------------------------------------------
struct GridScene {
    LineMesh funnel;
    LineMesh markers;
    LineMesh horizonWire;
    LineMesh rays;
    LineMesh trail;
    Mesh sphere;
    LineMesh stars;

    double partX[3] = {9.0, 0.0, 0.0};
    double partP[3] = {-0.06, 3.55 / 9.0, 0.0};
    double partE = 0.0;
    std::vector<float> trailVerts;
    double respawnTimer = 0.0;
    bool paused = false;

    static double zf(double r) { return std::sqrt(8.0 * (r - geo::RS)); } // Flamm

    static std::array<double, 3> colForR(double r) {
        double t = (r - geo::RS) / (14.0 - geo::RS);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return {0.95 - 0.60 * t, 0.42 - 0.05 * t, 0.12 + 0.78 * t};
    }

    void init() {
        // ---- funnel wireframe (upper + lower sheets) ----
        std::vector<float> v;
        const double rings[] = {2.06, 2.2, 2.4, 2.6, 2.8, 3.0, 3.3, 3.6, 4.0,
                                4.5, 5.0, 6.0, 7.0, 8.0, 10.0, 12.0, 14.0};
        const int NSEG = 128;
        for (double r : rings) {
            auto c = colForR(r);
            for (int side = 0; side < 2; ++side) {
                double zs = zf(r) * (side == 0 ? 1.0 : -1.0);
                for (int i = 0; i < NSEG; ++i) {
                    double a0 = 2.0 * M_PI * i / NSEG;
                    double a1 = 2.0 * M_PI * (i + 1) / NSEG;
                    for (double a : {a0, a1}) {
                        v.push_back((float)(r * std::cos(a)));
                        v.push_back((float)(r * std::sin(a)));
                        v.push_back((float)zs);
                        v.push_back((float)c[0]); v.push_back((float)c[1]); v.push_back((float)c[2]);
                    }
                }
            }
        }
        const int NSPK = 24;
        const int NSP = 64;
        const double rLo = 2.06, rHi = 14.0;
        for (int s = 0; s < NSPK; ++s) {
            double a = 2.0 * M_PI * s / NSPK;
            double ca = std::cos(a), sa = std::sin(a);
            for (int side = 0; side < 2; ++side) {
                double sgn = side == 0 ? 1.0 : -1.0;
                for (int i = 0; i < NSP; ++i) {
                    double r0 = rLo + (rHi - rLo) * i / NSP;
                    double r1 = rLo + (rHi - rLo) * (i + 1) / NSP;
                    for (double r : {r0, r1}) {
                        auto c = colForR(r);
                        v.push_back((float)(r * ca));
                        v.push_back((float)(r * sa));
                        v.push_back((float)(sgn * zf(r)));
                        v.push_back((float)c[0]); v.push_back((float)c[1]); v.push_back((float)c[2]);
                    }
                }
            }
        }
        uploadLines(funnel, std::move(v));

        // ---- marker rings on the funnel: photon sphere (r=3), ISCO (r=6) ----
        v.clear();
        for (auto [r, cr, cg, cb] : {std::tuple{3.0, 1.0f, 0.85f, 0.25f},
                                     std::tuple{6.0, 0.35f, 0.75f, 1.0f}}) {
            for (int side = 0; side < 2; ++side) {
                double zs = zf(r) * (side == 0 ? 1.0 : -1.0);
                for (int i = 0; i < NSEG; ++i) {
                    double a0 = 2.0 * M_PI * i / NSEG;
                    double a1 = 2.0 * M_PI * (i + 1) / NSEG;
                    for (double a : {a0, a1}) {
                        v.push_back((float)(r * std::cos(a)));
                        v.push_back((float)(r * std::sin(a)));
                        v.push_back((float)zs);
                        v.push_back(cr); v.push_back(cg); v.push_back(cb);
                    }
                }
            }
        }
        uploadLines(markers, std::move(v));

        // ---- event horizon sphere ----
        std::vector<float> sv, sn;
        const int SEG = 40, ROWS = 22;
        for (int i = 0; i < SEG; ++i) {
            for (int j = 0; j < ROWS; ++j) {
                double t0 = M_PI * j / ROWS, t1 = M_PI * (j + 1) / ROWS;
                double p0 = 2.0 * M_PI * i / SEG, p1 = 2.0 * M_PI * (i + 1) / SEG;
                auto put = [&](double t, double p) {
                    double x = geo::RS * std::sin(t) * std::cos(p);
                    double y = geo::RS * std::sin(t) * std::sin(p);
                    double z = geo::RS * std::cos(t);
                    sv.insert(sv.end(), {(float)x, (float)y, (float)z});
                    sn.insert(sn.end(), {(float)(std::sin(t) * std::cos(p)),
                                         (float)(std::sin(t) * std::sin(p)),
                                         (float)std::cos(t)});
                };
                // two triangles
                put(t0, p0); put(t1, p0); put(t1, p1);
                put(t0, p0); put(t1, p1); put(t0, p1);
            }
        }
        glGenVertexArrays(1, &sphere.vao);
        glGenBuffers(2, &sphere.vbo); // [0] pos, [1] normal
        glBindVertexArray(sphere.vao);
        glBindBuffer(GL_ARRAY_BUFFER, sphere.vbo);
        glBufferData(GL_ARRAY_BUFFER, sv.size() * sizeof(float), sv.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, sphere.vbo + 1);
        glBufferData(GL_ARRAY_BUFFER, sn.size() * sizeof(float), sn.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        sphere.count = (int)(sv.size() / 3);
        glBindVertexArray(0);

        // ---- traced light rays ----
        buildRays();

        // ---- background stars ----
        std::vector<float> st;
        for (int i = 0; i < 500; ++i) {
            double z = 2.0 * ((i + 0.5) / 500.0) - 1.0;
            double a = 2.0 * M_PI * ((i * 2654435761u % 10000u) / 10000.0);
            double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            st.push_back((float)(70.0 * r * std::cos(a)));
            st.push_back((float)(70.0 * r * std::sin(a)));
            st.push_back((float)(70.0 * z));
        }
        glGenVertexArrays(1, &stars.vao);
        glGenBuffers(1, &stars.vbo);
        glBindVertexArray(stars.vao);
        glBindBuffer(GL_ARRAY_BUFFER, stars.vbo);
        glBufferData(GL_ARRAY_BUFFER, st.size() * sizeof(float), st.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        stars.count = (int)(st.size() / 3);
        glBindVertexArray(0);

        // ---- particle ----
        initParticle();
        glGenVertexArrays(1, &trail.vao);
        glGenBuffers(1, &trail.vbo);
        glBindVertexArray(trail.vao);
        glBindBuffer(GL_ARRAY_BUFFER, trail.vbo);
        glBufferData(GL_ARRAY_BUFFER, 8192 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindVertexArray(0);
        trail.count = 0;
    }

    void initParticle() {
        partX[0] = 9.0; partX[1] = 0.0; partX[2] = 0.0;
        partP[0] = -0.06; partP[1] = 3.55 / 9.0; partP[2] = 0.0;
        double r = 9.0, a = geo::lapse(r);
        double pp = a * partP[0] * partP[0] + partP[1] * partP[1]; // gamma^ij p_i p_j
        partE = std::sqrt(a * (1.0 + pp));
        geo::project(partP, partX, partE, 1.0);
        trailVerts.clear();
        respawnTimer = 0.0;
    }

    void buildRays() {
        std::vector<float> v;
        auto traceOne = [&](double sx, double sy, double sz, double tx, double ty, double tz, bool background) {
            double x0[3] = {sx, sy, sz};
            double d[3] = {tx - sx, ty - sy, tz - sz};
            double dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (double& c : d) c /= dl;
            double p0[3] = {d[0], d[1], d[2]};
            geo::RayResult rr = geo::traceRay(x0, p0, 1.0, 0.025, 0.004, 0.30, 40000);
            // reconstruct polyline by re-integrating with path recording
            double x[3] = {x0[0], x0[1], x0[2]}, p[3] = {d[0], d[1], d[2]};
            geo::project(p, x, 1.0, 0.0);
            std::vector<std::array<double, 3>> path;
            path.push_back({x[0], x[1], x[2]});
            double lastKept = 0.0;
            const int cap = rr.code == 2 ? 40000 : 40000;
            for (int i = 0; i < cap; ++i) {
                double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
                if (r <= geo::RS * 1.0005) break;
                if (r > geo::ESC_R) break;
                double h = geo::stepSize(r, 0.025, 0.004, 0.30);
                geo::rk4step(x, p, 1.0, 0.0, h);
                double dx = x[0] - path.back()[0], dy = x[1] - path.back()[1], dz = x[2] - path.back()[2];
                lastKept += std::sqrt(dx * dx + dy * dy + dz * dz);
                if (lastKept > 0.10) { path.push_back({x[0], x[1], x[2]}); lastKept = 0.0; }
            }
            bool captured = rr.code == 2;
            int n = (int)path.size();
            for (int i = 0; i + 1 < n; ++i) {
                for (int k = 0; k < 2; ++k) {
                    int idx = i + k;
                    double frac = n > 1 ? (double)idx / (n - 1) : 0.0;
                    double c[3] = {1.0 - 0.65 * frac, 0.95 - 0.55 * frac, 1.0 - 0.45 * frac};
                    if (!background) {
                        c[0] = 1.0 - 0.45 * frac; c[1] = 0.95 - 0.45 * frac; c[2] = 1.0 - 0.25 * frac;
                    } else {
                        c[0] = 0.55; c[1] = 0.62; c[2] = 0.95;
                    }
                    if (captured && frac > 0.80) {
                        double m = (frac - 0.80) / 0.20;
                        c[0] = c[0] + (1.0 - c[0]) * m;
                        c[1] = c[1] * (1.0 - m) + 0.25 * m;
                        c[2] = c[2] * (1.0 - m) + 0.15 * m;
                    }
                    v.push_back((float)path[idx][0]);
                    v.push_back((float)path[idx][1]);
                    v.push_back((float)path[idx][2]);
                    v.push_back((float)c[0]); v.push_back((float)c[1]); v.push_back((float)c[2]);
                }
            }
        };
        // fan of rays from a source above the hole, aimed across the disk plane
        const double azs[] = {0, 45, 90, 135, 180, 225, 270, 315};
        const double rts[] = {8.0, 5.5, 5.2, 3.4};
        for (double az : azs)
            for (double rt : rts) {
                double ca = std::cos(az * M_PI / 180.0), sa = std::sin(az * M_PI / 180.0);
                traceOne(0, 0, 30, rt * ca, rt * sa, 0, false);
            }
        // rays coming from behind the hole (background lensing)
        const double azs2[] = {0, 90, 180, 270};
        const double rts2[] = {9.0, 6.0, 4.5};
        for (double az : azs2)
            for (double rt : rts2) {
                double ca = std::cos(az * M_PI / 180.0), sa = std::sin(az * M_PI / 180.0);
                traceOne(0, 0, -30, rt * ca, rt * sa, 0, true);
            }
        uploadLines(rays, std::move(v));
    }

    void step(double dt) {
        if (paused) return;
        // sub-integrate the plunging orbit
        const int SUB = 5;
        for (int s = 0; s < SUB; ++s) {
            double r = std::sqrt(partX[0] * partX[0] + partX[1] * partX[1] + partX[2] * partX[2]);
            if (r <= geo::RS * 1.0005) {
                respawnTimer -= dt / SUB;
                if (respawnTimer <= 0.0) { initParticle(); }
                return;
            }
            geo::rk4step(partX, partP, partE, 1.0, 0.055);
            trailVerts.push_back((float)partX[0]);
            trailVerts.push_back((float)partX[1]);
            trailVerts.push_back((float)partX[2]);
        }
        // keep a rolling window
        const size_t maxVerts = 6000;
        if (trailVerts.size() > maxVerts)
            trailVerts.erase(trailVerts.begin(), trailVerts.begin() + (trailVerts.size() - maxVerts));
    }

    void uploadTrail() {
        size_t n = trailVerts.size() / 3;
        if (n < 2) { trail.count = 0; return; }
        std::vector<float> v;
        v.reserve(n * 6);
        for (size_t i = 0; i < n; ++i) {
            double frac = (double)i / (n - 1);
            float cr = (float)(0.15 + 0.85 * frac);
            float cg = (float)(0.20 + 0.75 * frac);
            float cb = (float)(0.45 + 0.50 * frac);
            v.push_back(trailVerts[i * 3 + 0]);
            v.push_back(trailVerts[i * 3 + 1]);
            v.push_back(trailVerts[i * 3 + 2]);
            v.push_back(cr); v.push_back(cg); v.push_back(cb);
        }
        glBindVertexArray(trail.vao);
        glBindBuffer(GL_ARRAY_BUFFER, trail.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * sizeof(float), v.data());
        trail.count = (int)n;
        glBindVertexArray(0);
    }
};

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
struct App {
    GLFWwindow* win = nullptr;
    int fbw = 0, fbh = 0;
    Camera cam;
    Camera camLens;   // saved per-mode cameras
    Camera camGrid;
    int mode = 0;     // 0 lensing, 1 grid
    bool modeInited[2] = {false, false};

    float scale = 0.75f;
    int maxIter = 640;
    float stepBase = 0.012f;

    GLuint quadVao = 0, quadVbo = 0;
    GLuint progLens = 0, progBright = 0, progBlur = 0, progCompose = 0;
    GLuint progGrid = 0, progSphere = 0, progStar = 0;

    FBO hdr, b1, b1h, b2;
    GridScene grid;

    bool snapshotMode = false;
    std::string snapOut;
    int snapW = 1280, snapH = 800;

    static void keyCB(GLFWwindow* w, int key, int sc, int action, int mods) {
        App* a = (App*)glfwGetWindowUserPointer(w);
        a->onKey(key, sc, action, mods);
    }
    static void mouseCB(GLFWwindow* w, double x, double y) {
        App* a = (App*)glfwGetWindowUserPointer(w);
        a->onMouse(x, y);
    }
    static void scrollCB(GLFWwindow* w, double dx, double dy) {
        App* a = (App*)glfwGetWindowUserPointer(w);
        a->cam.dolly(dy > 0 ? 0.88 : 1.14);
    }
    static void resizeCB(GLFWwindow* w, int fw, int fh) {
        App* a = (App*)glfwGetWindowUserPointer(w);
        a->fbw = fw; a->fbh = fh;
        a->rebuildFBOs();
    }

    bool dragging = false;
    double lastMX = 0, lastMY = 0;

    void onKey(int key, int sc, int action, int mods) {
        (void)sc; (void)mods;
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
        switch (key) {
            case GLFW_KEY_ESCAPE:
            case GLFW_KEY_Q: glfwSetWindowShouldClose(win, 1); break;
            case GLFW_KEY_G:
                saveCam();
                mode ^= 1;
                if (!modeInited[mode]) { modeInited[mode] = true; }
                restoreCam();
                break;
            case GLFW_KEY_R: resetCam(); break;
            case GLFW_KEY_P: grid.paused = !grid.paused; break;
            case GLFW_KEY_H: printHelp(); break;
            case GLFW_KEY_KP_ADD:
            case GLFW_KEY_EQUAL: bumpQuality(+1); break;
            case GLFW_KEY_KP_SUBTRACT:
            case GLFW_KEY_MINUS: bumpQuality(-1); break;
            case GLFW_KEY_LEFT: cam.orbit(0.05, 0); break;
            case GLFW_KEY_RIGHT: cam.orbit(-0.05, 0); break;
            case GLFW_KEY_UP: cam.orbit(0, 0.05); break;
            case GLFW_KEY_DOWN: cam.orbit(0, -0.05); break;
            default: break;
        }
    }

    void onMouse(double x, double y) {
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            if (!dragging) { dragging = true; } else {
                cam.orbit((lastMX - x) * 0.005, (lastMY - y) * 0.005);
            }
            lastMX = x; lastMY = y;
        } else dragging = false;
    }

    void bumpQuality(int d) {
        static const float scales[] = {0.40f, 0.50f, 0.60f, 0.75f, 0.90f, 1.0f};
        static const int iters[] = {280, 400, 520, 640, 800, 1100};
        static int qi = 3;
        qi = std::clamp(qi + d, 0, 5);
        scale = scales[qi];
        maxIter = iters[qi];
    }

    void saveCam() {
        if (mode == 0) camLens = cam; else camGrid = cam;
    }
    void restoreCam() {
        cam = (mode == 0) ? camLens : camGrid;
    }
    void resetCam() {
        if (mode == 0) {
            cam.azim = M_PI; cam.elev = 0.30; cam.dist = 25.14;
            cam.target = {0, 0, 0};
        } else {
            cam.azim = 2.4; cam.elev = 0.42; cam.dist = 32.0;
            cam.target = {0, 0, 0};
        }
        saveCam();
    }
    void printHelp() {
        std::printf(
            "\n  Black Hole — Schwarzschild ray tracer\n"
            "  ======================================\n"
            "  G          toggle lensing / spacetime grid\n"
            "  arrows     orbit camera\n"
            "  left drag  orbit camera\n"
            "  scroll     dolly in / out\n"
            "  + / -      render quality (scale, iterations)\n"
            "  P          pause the plunging test particle\n"
            "  R          reset camera\n"
            "  H          this help\n"
            "  Q / Esc    quit\n");
    }

    void rebuildFBOs() {
        if (!progLens) return;
        int w = std::max(1, (int)(fbw * scale));
        int h = std::max(1, (int)(fbh * scale));
        if (hdr.tex.w != w || hdr.tex.h != h) {
            hdr = makeFBO(w, h);
            b1 = makeFBO(w / 2, h / 2);
            b1h = makeFBO(w / 2, h / 2);
            b2 = makeFBO(w / 4, h / 4);
        }
    }

    void drawQuad() {
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    void renderLensing() {
        Vec3 fwd, right, up;
        cam.basis(fwd, right, up);
        double tanH = std::tan(cam.fovY * 0.5);
        double aspect = (double)hdr.tex.w / std::max(1, hdr.tex.h);

        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, hdr.fbo);
        glViewport(0, 0, hdr.tex.w, hdr.tex.h);
        glUseProgram(progLens);
        Vec3 p = cam.pos();
        glUniform3f(glGetUniformLocation(progLens, "uCamPos"), (float)p.x, (float)p.y, (float)p.z);
        glUniform3f(glGetUniformLocation(progLens, "uFwd"), (float)fwd.x, (float)fwd.y, (float)fwd.z);
        glUniform3f(glGetUniformLocation(progLens, "uRight"),
                    (float)(right.x * tanH * aspect), (float)(right.y * tanH * aspect), (float)(right.z * tanH * aspect));
        glUniform3f(glGetUniformLocation(progLens, "uUp"),
                    (float)(up.x * tanH), (float)(up.y * tanH), (float)(up.z * tanH));
        glUniform1i(glGetUniformLocation(progLens, "uMaxIter"), maxIter);
        glUniform1f(glGetUniformLocation(progLens, "uStepBase"), stepBase);
        glUniform1f(glGetUniformLocation(progLens, "uTpeak"), 12000.0f);
        drawQuad();

        // bloom
        auto blurPass = [&](GLuint fromTex, FBO& to, float dirx, float diry) {
            glBindFramebuffer(GL_FRAMEBUFFER, to.fbo);
            glViewport(0, 0, to.tex.w, to.tex.h);
            glUseProgram(progBlur);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fromTex);
            glUniform1i(glGetUniformLocation(progBlur, "uTex"), 0);
            glUniform2f(glGetUniformLocation(progBlur, "uDir"), dirx, diry);
            glUniform1f(glGetUniformLocation(progBlur, "uRes"), (float)to.tex.w);
            drawQuad();
        };
        glBindFramebuffer(GL_FRAMEBUFFER, b1.fbo);
        glViewport(0, 0, b1.tex.w, b1.tex.h);
        glUseProgram(progBright);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr.tex.id);
        glUniform1i(glGetUniformLocation(progBright, "uTex"), 0);
        drawQuad();
        blurPass(b1.tex.id, b1h, 1.0f, 0.0f);
        blurPass(b1h.tex.id, b2, 0.0f, 1.0f);

        // composite
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glUseProgram(progCompose);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr.tex.id);
        glUniform1i(glGetUniformLocation(progCompose, "uHDR"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, b2.tex.id);
        glUniform1i(glGetUniformLocation(progCompose, "uBloom"), 1);
        glUniform1f(glGetUniformLocation(progCompose, "uExposure"), 0.85f);
        drawQuad();
    }

    void renderGrid() {
        Vec3 p = cam.pos();
        Mat4 vp = mul(perspective((float)cam.fovY, (float)fbw / std::max(1, fbh), 0.1f, 400.0f),
                      lookAt(p, cam.target, Vec3(0, 1, 0)));

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.008f, 0.010f, 0.018f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto setMVP = [&](GLuint prog, const Mat4& m) {
            glUniformMatrix4fv(glGetUniformLocation(prog, "uMVP"), 1, GL_FALSE, m.m);
        };

        // stars
        glUseProgram(progStar);
        setMVP(progStar, vp);
        glBindVertexArray(grid.stars.vao);
        glDrawArrays(GL_POINTS, 0, grid.stars.count);
        glBindVertexArray(0);

        // funnel + markers
        glUseProgram(progGrid);
        setMVP(progGrid, vp);
        glUniform1f(glGetUniformLocation(progGrid, "uRotZ"), 0.0f);
        glBindVertexArray(grid.funnel.vao);
        glDrawArrays(GL_LINES, 0, grid.funnel.count);
        glBindVertexArray(grid.markers.vao);
        glDrawArrays(GL_LINES, 0, grid.markers.count);
        glBindVertexArray(0);

        // rays (additive)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBindVertexArray(grid.rays.vao);
        glDrawArrays(GL_LINES, 0, grid.rays.count);
        glBindVertexArray(0);

        // horizon sphere
        glUseProgram(progSphere);
        glUniformMatrix4fv(glGetUniformLocation(progSphere, "uMVP"), 1, GL_FALSE, vp.m);
        Mat4 model = Mat4::identity();
        glUniformMatrix4fv(glGetUniformLocation(progSphere, "uModel"), 1, GL_FALSE, model.m);
        glUniform3f(glGetUniformLocation(progSphere, "uCamPos"), (float)p.x, (float)p.y, (float)p.z);
        glUniform1f(glGetUniformLocation(progSphere, "uRotZ"), 0.0f);
        glBindVertexArray(grid.sphere.vao);
        glDrawArrays(GL_TRIANGLES, 0, grid.sphere.count);
        glBindVertexArray(0);

        // particle trail + particle
        grid.uploadTrail();
        glUseProgram(progGrid);
        setMVP(progGrid, vp);
        glUniform1f(glGetUniformLocation(progGrid, "uRotZ"), 0.0f);
        glBindVertexArray(grid.trail.vao);
        glDrawArrays(GL_LINE_STRIP, 0, grid.trail.count);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    void render() {
        if (mode == 0) renderLensing();
        else renderGrid();
    }

    void run() {
        // ---- window ----
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        int vw = 1440, vh = 900;
        if (snapshotMode) {
            vw = snapW; vh = snapH;
            // macOS uses the software renderer for hidden windows; show it
            // briefly so the GPU does the work.
        }
        win = glfwCreateWindow(vw, vh, "Black Hole — Schwarzschild Ray Tracer", nullptr, nullptr);
        if (!win) { std::fprintf(stderr, "window creation failed\n"); return; }
        if (snapshotMode) {
            glfwSetWindowPos(win, 0, 0);
        }
        glfwMakeContextCurrent(win);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) { std::fprintf(stderr, "glew init failed\n"); return; }
        std::fprintf(stderr, "renderer: %s\n", (const char*)glGetString(GL_RENDERER));
        std::fprintf(stderr, "version:  %s\n", (const char*)glGetString(GL_VERSION));
        glfwSwapInterval(1);
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glfwSetWindowUserPointer(win, this);
        glfwSetKeyCallback(win, keyCB);
        glfwSetCursorPosCallback(win, mouseCB);
        glfwSetScrollCallback(win, scrollCB);
        glfwSetFramebufferSizeCallback(win, resizeCB);

        // ---- GL state ----
        glGenVertexArrays(1, &quadVao);
        glGenBuffers(1, &quadVbo);
        glBindVertexArray(quadVao);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
        const float tri[] = {-1, -1, 3, -1, -1, 3};
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);

        progLens = buildProgram(QUAD_VERT, LENSING_FRAG);
        progBright = buildProgram(QUAD_VERT, BRIGHT_FRAG);
        progBlur = buildProgram(QUAD_VERT, BLUR_FRAG);
        progCompose = buildProgram(QUAD_VERT, COMPOSE_FRAG);
        progGrid = buildProgram(GRID_VERT, GRID_FRAG);
        progSphere = buildProgram(SPHERE_VERT, SPHERE_FRAG);
        progStar = buildProgram(STAR_VERT, STAR_FRAG);
        if (!progLens || !progGrid || !progSphere) { std::fprintf(stderr, "shader build failed\n"); return; }

        rebuildFBOs();
        grid.init();

        camLens.azim = M_PI; camLens.elev = 0.30; camLens.dist = 25.14;
        camGrid.azim = 2.4; camGrid.elev = 0.42; camGrid.dist = 32.0;
        resetCam();
        if (snapshotMode) cam = camLens;

        printHelp();

        double lastT = glfwGetTime();
        double fpsAcc = 0;
        int fpsN = 0;
        double simAcc = 0;

        while (!glfwWindowShouldClose(win)) {
            double t = glfwGetTime();
            double dt = t - lastT;
            lastT = t;
            simAcc += dt;
            if (simAcc > 0.016) { grid.step(simAcc); simAcc = 0; }
            // in lensing mode also step so the particle keeps spiralling
            if (mode == 1) {
                // camera auto-orbit if idle? no, keep user control
            }
            render();
            glfwSwapBuffers(win);
            glfwPollEvents();
            if (snapshotMode) break; // render exactly one frame, then read back

            if (!snapshotMode) {
                fpsAcc += dt; fpsN++;
                if (fpsAcc > 0.5) {
                    double fps = fpsN / fpsAcc;
                    char title[160];
                    std::snprintf(title, sizeof(title),
                                  "Black Hole — %s | %d×%d | %d iters | %.0f fps | G grid, arrows orbit, scroll zoom, Q quit",
                                  mode == 0 ? "lensing" : "spacetime grid", fbw, fbh, maxIter, fps);
                    glfwSetWindowTitle(win, title);
                    fpsAcc = 0; fpsN = 0;
                }
            } else if (fpsN == 0) {
                double fr = dt > 0 ? 1.0 / dt : 0;
                std::fprintf(stderr, "frame took %.1f ms\n", dt * 1000.0);
                fpsN = 1;
            }
        }
    }

    void snapshot() {
        run();
        // read back the last frame
        std::vector<unsigned char> px((size_t)fbw * fbh * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbw, fbh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        // flip vertically
        std::vector<unsigned char> flipped(px.size());
        for (int y = 0; y < fbh; ++y)
            std::memcpy(&flipped[(size_t)y * fbw * 3], &px[(size_t)(fbh - 1 - y) * fbw * 3],
                        (size_t)fbw * 3);
        savePPM(snapOut, fbw, fbh, flipped.data());
        std::printf("wrote %s (%dx%d)\n", snapOut.c_str(), fbw, fbh);
    }
};

int main(int argc, char** argv) {
    App app;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--shot") {
        app.snapshotMode = true;
        auto next = [&](int i, const char* dflt) -> std::string {
            if (i < (int)args.size()) return args[i];
            return dflt;
        };
        app.snapOut = next(1, "shot.ppm");
        app.snapW = std::stoi(next(2, "1280"));
        app.snapH = std::stoi(next(3, "800"));
        app.scale = std::stof(next(4, "1.0"));
        app.maxIter = std::stoi(next(5, "1100"));
        app.stepBase = std::stof(next(6, "0.004"));
        double azim = std::stod(next(7, "3.141592653589793"));
        double elev = std::stod(next(8, "0.30"));
        double dist = std::stod(next(9, "25.14"));
        int mode = std::stoi(next(10, "0"));
        app.mode = mode;
        app.camLens.azim = azim; app.camLens.elev = elev; app.camLens.dist = dist;
        if (mode == 1) {
            app.camGrid.azim = azim; app.camGrid.elev = elev; app.camGrid.dist = dist;
        }
        if (!glfwInit()) { std::fprintf(stderr, "glfw init failed\n"); return 1; }
        app.snapshot();
        glfwTerminate();
        return 0;
    }
    if (!glfwInit()) { std::fprintf(stderr, "glfw init failed\n"); return 1; }
    app.run();
    glfwTerminate();
    return 0;
}
