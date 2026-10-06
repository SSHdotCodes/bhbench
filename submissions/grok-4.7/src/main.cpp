// Realtime Schwarzschild observer.
// The image is a backward integration of null geodesics. The inset is Flamm's
// paraboloid, the isometric embedding of the equatorial spatial slice.

#include "font5x7.hpp"
#include "physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalize(Vec3 a) {
    float n = length(a);
    return n < 1e-8f ? Vec3{0.f, 0.f, 1.f} : a * (1.f / n);
}

struct Mat4 {
    float m[16]{};
};

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 c;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
            c.m[col * 4 + row] = s;
        }
    }
    return c;
}

Mat4 perspective(float fovy, float aspect, float n, float f) {
    float t = 1.f / std::tan(fovy * 0.5f);
    Mat4 m;
    m.m[0] = t / aspect;
    m.m[5] = t;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1.f;
    m.m[14] = (2.f * f * n) / (n - f);
    return m;
}

Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 m;
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[12] = -dot(s, eye);
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[13] = -dot(u, eye);
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[14] = dot(f, eye);
    m.m[15] = 1.f;
    return m;
}

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float radius, phi, kind;
};
static_assert(sizeof(Vertex) == 9 * sizeof(float), "Vertex must be tightly packed");

constexpr float kRMin = 2.045f;
constexpr float kRMax = 15.5f;

float flamm_z(float r) {
    return std::sqrt(8.f * (r - 2.f)) - std::sqrt(8.f * (kRMax - 2.f));
}

struct App {
    GLFWwindow* win = nullptr;
    int fbw = 1, fbh = 1, winw = 1, winh = 1;
    int rw = 0, rh = 0, bw = 0, bh = 0;
    GLuint hdrFbo = 0, hdrTex = 0;
    GLuint bloomFbo[2] = {0, 0};
    GLuint bloomTex[2] = {0, 0};
    GLuint fluxTex = 0;
    GLuint quadVao = 0, quadVbo = 0;
    GLuint meshVao = 0, meshVbo = 0, meshEbo = 0;
    GLuint partVao = 0, partVbo = 0;
    GLuint hudVao = 0, hudVbo = 0;
    GLuint traceProg = 0, postProg = 0, embedProg = 0, hudProg = 0;
    GLint meshCount = 0;

    int view = 0; // 0 observatory, 1 rays, 2 embedding
    int quality = 1;
    int color_mode = 0;
    int step_limit = 170;
    float coord_step = 0.072f;
    bool flat = false;
    bool show_disk = true;
    bool show_halo = true;
    bool show_grid = true;
    bool bloom = true;
    bool paused = false;
    bool show_hud = true;
    bool dragging = false;
    bool dirty_fbo = true;
    bool fullscreen = false;
    int restore_x = 80, restore_y = 60, restore_w = 1400, restore_h = 880;

    double radius = 70.0;
    double theta = 1.15;
    double phi = 0.0;
    double last_x = 0, last_y = 0;
    float fov_y = 46.f * kPi / 180.f;
    float exposure = 1.05f;
    float disk_gain = 1.55f;
    float halo_gain = 0.07f;
    float bloom_strength = 0.16f;
    float tmax = 8200.f;
    float source0[3] = {0, 0, -1};
    float source1[3] = {0.35f, 0.78f, 0.52f};
    double sim_time = 0.0;
    float embed_yaw = 0.6f;
    float embed_pitch = 0.42f;
    float embed_dist = 34.f;
    float fps = 0.f;

    bh::FluxTable flux;
    std::vector<Vertex> particles;
    std::vector<float> hud;
    int inset_x = 0, inset_y = 0, inset_w = 0, inset_h = 0;

    struct {
        GLint resolution, aspect, tanHalf, camPos, right, up, fwd, flux, fluxIn, fluxOut, fluxN;
        GLint diskIn, diskOut, time, coordStep, stepLimit, colorMode, flat, showDisk, showHalo;
        GLint showGrid, diskGain, haloGain, tmax, source0, source1;
    } tr{};
    struct {
        GLint tex, hdr, bloom, mode, exposure, texel, direction, blurScale, bloomStrength, bloomOn;
    } post{};
    struct {
        GLint mvp, pointSize, eye;
    } emb{};
};

App app;

void glfw_error(int code, const char* desc) { std::fprintf(stderr, "GLFW %d: %s\n", code, desc); }

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "Could not read %s\n", path.c_str());
        std::exit(1);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GLuint compile_shader(GLenum type, const std::string& src, const char* label) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[12000];
    log[0] = 0;
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    if (!ok) {
        std::fprintf(stderr, "Shader compile failed (%s):\n%s\n", label, log);
        std::exit(1);
    }
    if (log[0]) std::fprintf(stderr, "Shader note (%s):\n%s\n", label, log);
    return s;
}

GLuint link_program(GLuint vs, GLuint fs, const char* label) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    char log[12000];
    log[0] = 0;
    glGetProgramInfoLog(p, sizeof log, nullptr, log);
    glDetachShader(p, vs);
    glDetachShader(p, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        std::fprintf(stderr, "Program link failed (%s):\n%s\n", label, log);
        std::exit(1);
    }
    if (log[0]) std::fprintf(stderr, "Link note (%s):\n%s\n", label, log);
    return p;
}

void need_uniform(GLint loc, const char* name) {
    if (loc < 0) std::fprintf(stderr, "warning: uniform %s was optimized out or misspelled\n", name);
}

void place_sources() {
    bh::CameraFrame cam = bh::make_camera(app.radius, app.theta, app.phi);
    // A background galaxy just off the line of sight, so lensing makes arcs
    // rather than a divergent full Einstein ring.
    double sx = -cam.pos[0] / cam.r + 0.34 * cam.up[0];
    double sy = -cam.pos[1] / cam.r + 0.34 * cam.up[1];
    double sz = -cam.pos[2] / cam.r + 0.34 * cam.up[2];
    double sn = std::sqrt(sx * sx + sy * sy + sz * sz);
    app.source0[0] = static_cast<float>(sx / sn);
    app.source0[1] = static_cast<float>(sy / sn);
    app.source0[2] = static_cast<float>(sz / sn);
    float s0 = 0.42f, s1 = 0.72f, s2 = 0.55f;
    float n = std::sqrt(s0 * s0 + s1 * s1 + s2 * s2);
    app.source1[0] = s0 / n;
    app.source1[1] = s1 / n;
    app.source1[2] = s2 / n;
}

void apply_quality() {
    const int steps[] = {120, 170, 220, 276};
    const float coord[] = {0.115f, 0.072f, 0.048f, 0.030f};
    int q = std::clamp(app.quality, 0, 3);
    app.quality = q;
    app.step_limit = steps[q];
    app.coord_step = coord[q];
    app.dirty_fbo = true;
}

float quality_scale() {
    const float target[] = {380000.f, 780000.f, 1300000.f, 0.f};
    if (app.quality >= 3) return 1.f;
    float area = static_cast<float>(std::max(app.fbw, 1) * std::max(app.fbh, 1));
    return std::clamp(std::sqrt(target[app.quality] / area), 0.22f, 1.f);
}

void destroy_targets() {
    if (app.hdrFbo) glDeleteFramebuffers(1, &app.hdrFbo);
    if (app.hdrTex) glDeleteTextures(1, &app.hdrTex);
    if (app.bloomFbo[0]) glDeleteFramebuffers(2, app.bloomFbo);
    if (app.bloomTex[0]) glDeleteTextures(2, app.bloomTex);
    app.hdrFbo = app.hdrTex = 0;
    app.bloomFbo[0] = app.bloomFbo[1] = 0;
    app.bloomTex[0] = app.bloomTex[1] = 0;
    app.rw = app.rh = 0;
}

GLuint make_color_tex(int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint make_fbo(GLuint tex) {
    GLuint f = 0;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "Framebuffer is incomplete\n");
        std::exit(1);
    }
    return f;
}

void ensure_targets() {
    glfwGetFramebufferSize(app.win, &app.fbw, &app.fbh);
    glfwGetWindowSize(app.win, &app.winw, &app.winh);
    if (app.fbw < 2 || app.fbh < 2) return;
    int rw = std::max(32, static_cast<int>(std::lround(app.fbw * quality_scale())));
    int rh = std::max(32, static_cast<int>(std::lround(app.fbh * quality_scale())));
    if (!app.dirty_fbo && rw == app.rw && rh == app.rh && app.hdrFbo) return;
    destroy_targets();
    app.rw = rw;
    app.rh = rh;
    app.bw = std::max(8, rw / 2);
    app.bh = std::max(8, rh / 2);
    app.hdrTex = make_color_tex(rw, rh);
    app.hdrFbo = make_fbo(app.hdrTex);
    app.bloomTex[0] = make_color_tex(app.bw, app.bh);
    app.bloomTex[1] = make_color_tex(app.bw, app.bh);
    app.bloomFbo[0] = make_fbo(app.bloomTex[0]);
    app.bloomFbo[1] = make_fbo(app.bloomTex[1]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    app.dirty_fbo = false;
}

void upload_trace(const bh::CameraFrame& cam, int w, int h, float fov, int color_mode, int flat,
                  int show_disk, int show_halo, int step_limit, float coord_step, float time) {
    glUniform2f(app.tr.resolution, static_cast<float>(w), static_cast<float>(h));
    glUniform1f(app.tr.aspect, static_cast<float>(w) / static_cast<float>(h));
    glUniform1f(app.tr.tanHalf, std::tan(fov * 0.5f));
    glUniform3f(app.tr.camPos, static_cast<float>(cam.pos[0]), static_cast<float>(cam.pos[1]),
                static_cast<float>(cam.pos[2]));
    glUniform3f(app.tr.right, static_cast<float>(cam.right[0]), static_cast<float>(cam.right[1]),
                static_cast<float>(cam.right[2]));
    glUniform3f(app.tr.up, static_cast<float>(cam.up[0]), static_cast<float>(cam.up[1]),
                static_cast<float>(cam.up[2]));
    glUniform3f(app.tr.fwd, static_cast<float>(cam.forward[0]), static_cast<float>(cam.forward[1]),
                static_cast<float>(cam.forward[2]));
    glUniform1i(app.tr.flux, 1);
    glUniform1f(app.tr.fluxIn, static_cast<float>(app.flux.r_in));
    glUniform1f(app.tr.fluxOut, static_cast<float>(app.flux.r_out));
    glUniform1f(app.tr.fluxN, static_cast<float>(bh::FluxTable::N));
    glUniform1f(app.tr.diskIn, 6.f);
    glUniform1f(app.tr.diskOut, 36.f);
    glUniform1f(app.tr.time, time);
    glUniform1f(app.tr.coordStep, coord_step);
    glUniform1i(app.tr.stepLimit, step_limit);
    glUniform1i(app.tr.colorMode, color_mode);
    glUniform1i(app.tr.flat, flat);
    glUniform1i(app.tr.showDisk, show_disk);
    glUniform1i(app.tr.showHalo, show_halo);
    glUniform1i(app.tr.showGrid, app.show_grid ? 1 : 0);
    glUniform1f(app.tr.diskGain, app.disk_gain);
    glUniform1f(app.tr.haloGain, app.halo_gain);
    glUniform1f(app.tr.tmax, app.tmax);
    glUniform3fv(app.tr.source0, 1, app.source0);
    glUniform3fv(app.tr.source1, 1, app.source1);
}

void draw_quad() {
    glBindVertexArray(app.quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void blur_pass(int src, int dst, float dx, float dy, float scale) {
    glBindFramebuffer(GL_FRAMEBUFFER, app.bloomFbo[dst]);
    glViewport(0, 0, app.bw, app.bh);
    glUniform1i(app.post.mode, 1);
    glUniform2f(app.post.texel, 1.f / static_cast<float>(app.bw), 1.f / static_cast<float>(app.bh));
    glUniform2f(app.post.direction, dx, dy);
    glUniform1f(app.post.blurScale, scale);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.bloomTex[src]);
    glUniform1i(app.post.tex, 0);
    draw_quad();
}

void render_rays() {
    bh::CameraFrame cam = bh::make_camera(app.radius, app.theta, app.phi);
    glBindFramebuffer(GL_FRAMEBUFFER, app.hdrFbo);
    glViewport(0, 0, app.rw, app.rh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(app.traceProg);
    upload_trace(cam, app.rw, app.rh, app.fov_y, app.color_mode, app.flat ? 1 : 0, app.show_disk ? 1 : 0,
                 app.show_halo ? 1 : 0, app.step_limit, app.coord_step, static_cast<float>(app.sim_time));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.fluxTex);
    draw_quad();

    glUseProgram(app.postProg);
    glBindFramebuffer(GL_FRAMEBUFFER, app.bloomFbo[0]);
    glViewport(0, 0, app.bw, app.bh);
    glUniform1i(app.post.mode, 0);
    glUniform1f(app.post.exposure, app.exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.hdrTex);
    glUniform1i(app.post.tex, 0);
    draw_quad();
    blur_pass(0, 1, 1.f, 0.f, 1.f);
    blur_pass(1, 0, 0.f, 1.f, 1.f);
    blur_pass(0, 1, 1.f, 0.f, 2.2f);
    blur_pass(1, 0, 0.f, 1.f, 2.2f);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, app.fbw, app.fbh);
    glUniform1i(app.post.mode, 2);
    glUniform1f(app.post.exposure, app.exposure);
    glUniform1f(app.post.bloomStrength, app.bloom_strength);
    glUniform1i(app.post.bloomOn, app.bloom ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.hdrTex);
    glUniform1i(app.post.hdr, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.bloomTex[0]);
    glUniform1i(app.post.bloom, 2);
    draw_quad();
}

void step_particles(float dt) {
    const float r0 = 14.6f;
    const float E = std::sqrt(1.f - 2.f / r0);
    const float dtau = app.paused ? 0.f : dt * 5.4f;
    for (Vertex& p : app.particles) {
        float r = p.radius;
        float v2 = E * E - (1.f - 2.f / r);
        r += -std::sqrt(std::max(v2, 0.f)) * dtau;
        if (r < 2.12f) r = r0 - 0.18f;
        p.radius = r;
        float dz = std::sqrt(2.f / std::max(r - 2.f, 1e-3f));
        float nx = -std::cos(p.phi) * dz;
        float ny = -std::sin(p.phi) * dz;
        float nz = 1.f;
        float nn = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= nn;
        ny /= nn;
        nz /= nn;
        p.px = r * std::cos(p.phi) + nx * 0.14f;
        p.py = r * std::sin(p.phi) + ny * 0.14f;
        p.pz = flamm_z(r) + nz * 0.14f;
        p.nx = nx;
        p.ny = ny;
        p.nz = nz;
        p.kind = 2.f;
    }
    glBindBuffer(GL_ARRAY_BUFFER, app.partVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(app.particles.size() * sizeof(Vertex)),
                    app.particles.data());
}

void draw_embedding(int vp_w, int vp_h) {
    float aspect = static_cast<float>(vp_w) / static_cast<float>(std::max(vp_h, 1));
    Vec3 target{0.f, 0.f, flamm_z(kRMin) * 0.42f};
    float yaw = app.embed_yaw;
    float pitch = app.embed_pitch;
    float dist = app.embed_dist;
    Vec3 eye = target + Vec3{dist * std::cos(pitch) * std::cos(yaw), dist * std::cos(pitch) * std::sin(yaw),
                             dist * std::sin(pitch)};
    Mat4 mvp = mul(perspective(38.f * kPi / 180.f, aspect, 0.08f, 220.f), look_at(eye, target, {0.f, 0.f, 1.f}));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(app.embedProg);
    glUniformMatrix4fv(app.emb.mvp, 1, GL_FALSE, mvp.m);
    glUniform3f(app.emb.eye, eye.x, eye.y, eye.z);
    glUniform1f(app.emb.pointSize, std::max(7.f, vp_h / 48.f));
    glBindVertexArray(app.meshVao);
    glDrawElements(GL_TRIANGLES, app.meshCount, GL_UNSIGNED_INT, nullptr);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(app.partVao);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(app.particles.size()));
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
}

void hud_vert(float x, float y, float r, float g, float b, float a) {
    float nx = (x / static_cast<float>(app.fbw)) * 2.f - 1.f;
    float ny = (y / static_cast<float>(app.fbh)) * 2.f - 1.f;
    app.hud.insert(app.hud.end(), {nx, ny, r, g, b, a});
}

void hud_quad(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    hud_vert(x0, y0, r, g, b, a);
    hud_vert(x1, y0, r, g, b, a);
    hud_vert(x1, y1, r, g, b, a);
    hud_vert(x0, y0, r, g, b, a);
    hud_vert(x1, y1, r, g, b, a);
    hud_vert(x0, y1, r, g, b, a);
}

void hud_text(float x, float y, float scale, const char* text, float r, float g, float b) {
    for (const char* p = text; *p; ++p) {
        const uint8_t* glyph = font5x7(*p);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (0x10 >> col)) == 0) continue;
                float px = x + static_cast<float>(col) * scale;
                float py = y + static_cast<float>(6 - row) * scale;
                hud_quad(px, py, px + scale, py + scale, r, g, b, 1.f);
            }
        }
        x += 6.f * scale;
    }
}

void draw_hud() {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    if (!app.show_hud) return;
    app.hud.clear();
    float ui = static_cast<float>(app.fbw) / static_cast<float>(std::max(app.winw, 1));
    float sc = std::max(2.f, 1.65f * ui);
    float line = 9.f * sc;
    float x = 16.f * ui;
    float y0 = static_cast<float>(app.fbh) - 16.f * ui - 7.f * sc;
    hud_quad(8.f * ui, y0 - 3.f * line - 10.f * ui, x + 27.f * 6.f * sc, static_cast<float>(app.fbh) - 8.f * ui, 0.f,
             0.f, 0.f, 0.46f);
    hud_text(x, y0, sc, "SCHWARZSCHILD BLACK HOLE", 0.93f, 0.95f, 0.97f);
    hud_text(x, y0 - line, sc, "M=1   RS=2M   PHOTON 3M   ISCO 6M   B=5.196M", 0.55f, 0.78f, 0.88f);
    hud_text(x, y0 - 2.f * line, sc, "DRAG ORBIT   SCROLL R   1 2 3 TILT", 0.80f, 0.82f, 0.74f);
    hud_text(x, y0 - 3.f * line, sc, "V VIEW   L FLAT   [ ] QUALITY   - + LIGHT", 0.80f, 0.82f, 0.74f);

    const char* banner = nullptr;
    if (app.flat) banner = "FLAT RAYS   NO LIGHT BENDING";
    else if (app.color_mode == 1) banner = "FALSE COLOR   REDSHIFT G";
    else if (app.color_mode == 2) banner = "CAPTURE MASK";
    else if (app.paused) banner = "PAUSED";
    if (banner) {
        float by = y0 - 4.2f * line;
        hud_text(x, by, sc, banner, 1.f, 0.72f, 0.32f);
    }

    if (app.view != 1) {
        float lx = (app.view == 0) ? static_cast<float>(app.inset_x) : 16.f * ui;
        float ly = (app.view == 0) ? static_cast<float>(app.inset_y + app.inset_h) + 12.f * ui : 28.f * ui;
        hud_text(lx, ly + line, sc, "FLAMM EMBEDDING", 0.82f, 0.88f, 0.95f);
        float sy = ly;
        float gap = 6.f * sc;
        hud_quad(lx, sy, lx + 2.4f * sc, sy + 2.4f * sc, 1.f, 0.74f, 0.28f, 1.f);
        hud_text(lx + 3.2f * sc, sy - 0.6f * sc, sc * 0.85f, "HORIZON", 1.f, 0.78f, 0.40f);
        float x2 = lx + 3.2f * sc + 8.f * gap;
        hud_quad(x2, sy, x2 + 2.4f * sc, sy + 2.4f * sc, 0.25f, 0.85f, 1.f, 1.f);
        hud_text(x2 + 3.2f * sc, sy - 0.6f * sc, sc * 0.85f, "PHOTON 3M", 0.45f, 0.88f, 1.f);
        float x3 = x2 + 3.2f * sc + 11.f * gap;
        hud_quad(x3, sy, x3 + 2.4f * sc, sy + 2.4f * sc, 1.f, 0.46f, 0.12f, 1.f);
        hud_text(x3 + 3.2f * sc, sy - 0.6f * sc, sc * 0.85f, "ISCO 6M", 1.f, 0.58f, 0.22f);
        if (app.view == 0) {
            float t = std::max(1.5f, ui);
            float x0 = static_cast<float>(app.inset_x);
            float yb = static_cast<float>(app.inset_y);
            float x1 = x0 + static_cast<float>(app.inset_w);
            float yt = yb + static_cast<float>(app.inset_h);
            hud_quad(x0 - t, yb - t, x1 + t, yb, 0.45f, 0.62f, 0.78f, 0.9f);
            hud_quad(x0 - t, yt, x1 + t, yt + t, 0.45f, 0.62f, 0.78f, 0.9f);
            hud_quad(x0 - t, yb, x0, yt, 0.45f, 0.62f, 0.78f, 0.9f);
            hud_quad(x1, yb, x1 + t, yt, 0.45f, 0.62f, 0.78f, 0.9f);
        }
    }

    if (app.hud.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(app.hudProg);
    glBindVertexArray(app.hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.hudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(app.hud.size() * sizeof(float)), app.hud.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(app.hud.size() / 6));
    glDisable(GL_BLEND);
}

void layout_inset() {
    float ui = static_cast<float>(app.fbw) / static_cast<float>(std::max(app.winw, 1));
    int margin = static_cast<int>(16.f * ui);
    app.inset_w = std::min(app.fbw / 3, static_cast<int>(480.f * ui));
    app.inset_h = static_cast<int>(app.inset_w * 0.76f);
    app.inset_x = margin;
    app.inset_y = margin;
}

void render_frame(float dt) {
    ensure_targets();
    layout_inset();
    if (app.view != 2) app.embed_yaw += dt * 0.28f;
    step_particles(dt);

    if (app.view == 2) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, app.fbw, app.fbh);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.012f, 0.014f, 0.022f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_embedding(app.fbw, app.fbh);
    } else {
        render_rays();
        if (app.view == 0) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(app.inset_x, app.inset_y, app.inset_w, app.inset_h);
            glViewport(app.inset_x, app.inset_y, app.inset_w, app.inset_h);
            glEnable(GL_DEPTH_TEST);
            glClearColor(0.010f, 0.012f, 0.020f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            draw_embedding(app.inset_w, app.inset_h);
            glDisable(GL_SCISSOR_TEST);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, app.fbw, app.fbh);
    glDisable(GL_DEPTH_TEST);
    draw_hud();
}

bool write_ppm(const char* path, int w, int h, const uint8_t* rgb_bottom_up) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) {
        if (std::fwrite(rgb_bottom_up + static_cast<size_t>(y) * w * 3, 1, static_cast<size_t>(w) * 3, f) !=
            static_cast<size_t>(w) * 3) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool shadow_selftest() {
    const int W = 800, H = 800;
    const float fov = 50.f * kPi / 180.f;
    const double rcam = 30.0;
    const double theta = 1.15;
    const double phi = 0.35;
    GLuint tex = make_color_tex(W, H);
    GLuint fbo = make_fbo(tex);
    bh::CameraFrame cam = bh::make_camera(rcam, theta, phi);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, W, H);
    glUseProgram(app.traceProg);
    for (int pass = 0; pass < 2; ++pass) {
        upload_trace(cam, W, H, fov, 2, 0, 0, 0, 276, 0.028f, 0.f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, app.fluxTex);
        draw_quad();
        glFinish();
    }
    std::vector<float> pix(static_cast<size_t>(W) * H * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGB, GL_FLOAT, pix.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);

    auto lum_at = [&](int x, int y) {
        x = std::clamp(x, 0, W - 1);
        y = std::clamp(y, 0, H - 1);
        return pix[(static_cast<size_t>(y) * W + x) * 3];
    };
    const int cx = W / 2;
    const int cy = H / 2;
    auto scan = [&](int step_x, int step_y) {
        int n = 0;
        for (int i = 0; i < W; ++i) {
            int x = cx + step_x * i;
            int y = cy + step_y * i;
            if (x < 0 || y < 0 || x >= W || y >= H) break;
            if (lum_at(x, y) > 0.5f) return i - 0.5f;
            n = i;
        }
        return static_cast<float>(n);
    };
    // Pixel radius of the filled shadow along the four axes. Outlier timeouts
    // must not stretch a bounding box.
    float rpos = scan(1, 0), rneg = scan(-1, 0), up = scan(0, 1), down = scan(0, -1);
    int black = 0;
    for (int i = 0; i < W * H; ++i) {
        if (pix[static_cast<size_t>(i) * 3] < 0.5f) ++black;
    }
    double alpha = 1.0 - 2.0 / rcam;
    double psi_exact = std::asin(bh::B_CRIT * std::sqrt(alpha) / rcam);
    double tan_half = std::tan(fov * 0.5);
    auto to_psi = [&](float pix_r) { return std::atan(pix_r * (2.0 / H) * tan_half); };
    double psi_h = to_psi(0.5f * (rpos + rneg));
    double psi_v = to_psi(0.5f * (up + down));
    double psi = 0.5 * (psi_h + psi_v);
    double rel = std::fabs(psi - psi_exact) / psi_exact;
    double circ = std::fabs(psi_h - psi_v) / std::max(psi, 1e-6);
    double center_off = 0.5 * std::hypot(rpos - rneg, up - down);
    bool ok = rel < 0.03 && circ < 0.04 && center_off < 6.0 && black > 1000 && black < W * H / 2;
    std::printf("GPU shadow  psi_h=%.4f deg  psi_v=%.4f deg  theory=%.4f deg  rel=%.3e  circularity=%.3e\n",
                psi_h * 180.0 / kPi, psi_v * 180.0 / kPi, psi_exact * 180.0 / kPi, rel, circ);
    std::printf("  radii px +x %.1f -x %.1f +y %.1f -y %.1f  black=%d  center=%.2f corner=%.2f offset=%.1f  %s\n",
                rpos, rneg, up, down, black, lum_at(cx, cy), lum_at(0, 0), center_off, ok ? "PASS" : "FAIL");
    std::vector<uint8_t> mask(static_cast<size_t>(W) * H * 3);
    for (int i = 0; i < W * H; ++i) {
        uint8_t b = pix[static_cast<size_t>(i) * 3] > 0.5f ? 255 : 0;
        mask[static_cast<size_t>(i) * 3] = mask[static_cast<size_t>(i) * 3 + 1] = mask[static_cast<size_t>(i) * 3 + 2] = b;
    }
    write_ppm("/tmp/bh-mask.ppm", W, H, mask.data());
    std::fflush(stdout);
    return ok;
}

void on_key(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_RELEASE) return;
    const bool repeatable = key == GLFW_KEY_MINUS || key == GLFW_KEY_EQUAL || key == GLFW_KEY_LEFT_BRACKET ||
                            key == GLFW_KEY_RIGHT_BRACKET;
    if (action == GLFW_REPEAT && !repeatable) return;
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(app.win, GLFW_TRUE); break;
    case GLFW_KEY_V: app.view = (app.view + 1) % 3; break;
    case GLFW_KEY_L: app.flat = !app.flat; break;
    case GLFW_KEY_C: app.color_mode = (app.color_mode + 1) % 3; break;
    case GLFW_KEY_D: app.show_disk = !app.show_disk; break;
    case GLFW_KEY_N: app.show_halo = !app.show_halo; break;
    case GLFW_KEY_B: app.bloom = !app.bloom; break;
    case GLFW_KEY_G: app.show_grid = !app.show_grid; break;
    case GLFW_KEY_H: app.show_hud = !app.show_hud; break;
    case GLFW_KEY_SPACE: app.paused = !app.paused; break;
    case GLFW_KEY_1: app.theta = 0.22; break;
    case GLFW_KEY_2: app.theta = 1.15; break;
    case GLFW_KEY_3: app.theta = 1.47; break;
    case GLFW_KEY_LEFT_BRACKET:
        app.quality = std::max(0, app.quality - 1);
        apply_quality();
        break;
    case GLFW_KEY_RIGHT_BRACKET:
        app.quality = std::min(3, app.quality + 1);
        apply_quality();
        break;
    case GLFW_KEY_MINUS: app.exposure = std::max(0.15f, app.exposure * 0.88f); break;
    case GLFW_KEY_EQUAL: app.exposure = std::min(8.f, app.exposure * 1.14f); break;
    case GLFW_KEY_R:
        app.radius = 70;
        app.theta = 1.15;
        app.phi = 0;
        app.fov_y = 46.f * kPi / 180.f;
        app.exposure = 1.05f;
        app.flat = false;
        app.color_mode = 0;
        app.paused = false;
        place_sources();
        break;
    case GLFW_KEY_F:
        if (!app.fullscreen) {
            glfwGetWindowPos(app.win, &app.restore_x, &app.restore_y);
            glfwGetWindowSize(app.win, &app.restore_w, &app.restore_h);
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(mon);
            if (mode) {
                glfwSetWindowMonitor(app.win, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
                app.fullscreen = true;
            }
        } else {
            glfwSetWindowMonitor(app.win, nullptr, app.restore_x, app.restore_y, app.restore_w, app.restore_h, 0);
            app.fullscreen = false;
        }
        app.dirty_fbo = true;
        break;
    default: break;
    }
}

void on_mouse(GLFWwindow*, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        app.dragging = true;
        glfwGetCursorPos(app.win, &app.last_x, &app.last_y);
    } else if (action == GLFW_RELEASE) {
        app.dragging = false;
    }
}

void on_cursor(GLFWwindow*, double x, double y) {
    if (!app.dragging) return;
    double dx = x - app.last_x;
    double dy = y - app.last_y;
    app.last_x = x;
    app.last_y = y;
    if (app.view == 2) {
        app.embed_yaw += static_cast<float>(dx) * 0.007f;
        app.embed_pitch -= static_cast<float>(dy) * 0.005f;
        app.embed_pitch = std::clamp(app.embed_pitch, -0.15f, 1.15f);
    } else {
        app.phi -= dx * 0.0055;
        app.theta += dy * 0.0042;
        app.theta = std::clamp(app.theta, 0.16, 1.48);
    }
}

void on_scroll(GLFWwindow*, double, double yoff) {
    if (app.view == 2) {
        app.embed_dist = std::clamp(app.embed_dist * static_cast<float>(std::exp(-yoff * 0.08)), 14.f, 90.f);
    } else {
        app.radius = std::clamp(app.radius * std::exp(-yoff * 0.07), 16.0, 150.0);
    }
}

void on_resize(GLFWwindow*, int, int) { app.dirty_fbo = true; }

void build_mesh() {
    const int radial = 90;
    const int seg = 144;
    const int stride = seg + 1;
    std::vector<Vertex> verts(static_cast<size_t>(radial + 1) * stride);
    for (int i = 0; i <= radial; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(radial);
        float r = kRMin + (kRMax - kRMin) * std::pow(t, 1.7f);
        float dz = std::sqrt(2.f / std::max(r - 2.f, 1e-4f));
        for (int j = 0; j <= seg; ++j) {
            float phi = 2.f * kPi * static_cast<float>(j) / static_cast<float>(seg);
            Vertex& v = verts[static_cast<size_t>(i) * stride + j];
            float nx = -std::cos(phi) * dz;
            float ny = -std::sin(phi) * dz;
            float nz = 1.f;
            float nn = std::sqrt(nx * nx + ny * ny + nz * nz);
            v.px = r * std::cos(phi);
            v.py = r * std::sin(phi);
            v.pz = flamm_z(r);
            v.nx = nx / nn;
            v.ny = ny / nn;
            v.nz = nz / nn;
            v.radius = r;
            v.phi = phi;
            v.kind = 0.f;
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(radial) * seg * 6 + seg * 3);
    for (int i = 0; i < radial; ++i) {
        for (int j = 0; j < seg; ++j) {
            uint32_t a = static_cast<uint32_t>(i * stride + j);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(stride);
            uint32_t d = c + 1;
            idx.insert(idx.end(), {a, c, d, a, d, b});
        }
    }
    Vertex cap{};
    cap.pz = flamm_z(kRMin);
    cap.nz = 1.f;
    cap.radius = 2.f;
    cap.kind = 1.f;
    uint32_t center = static_cast<uint32_t>(verts.size());
    verts.push_back(cap);
    for (int j = 0; j < seg; ++j) {
        idx.insert(idx.end(), {center, static_cast<uint32_t>(j), static_cast<uint32_t>(j + 1)});
    }

    glGenVertexArrays(1, &app.meshVao);
    glGenBuffers(1, &app.meshVbo);
    glGenBuffers(1, &app.meshEbo);
    glBindVertexArray(app.meshVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.meshVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.meshEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)), idx.data(),
                 GL_STATIC_DRAW);
    const GLsizei stride_b = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(8 * sizeof(float)));
    app.meshCount = static_cast<GLint>(idx.size());

    app.particles.resize(16);
    for (int i = 0; i < 16; ++i) {
        app.particles[i].phi = 2.f * kPi * static_cast<float>(i) / 16.f;
        app.particles[i].radius = 14.4f - static_cast<float>(i % 6) * 1.8f;
        app.particles[i].kind = 2.f;
    }
    glGenVertexArrays(1, &app.partVao);
    glGenBuffers(1, &app.partVbo);
    glBindVertexArray(app.partVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.partVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(app.particles.size() * sizeof(Vertex)), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride_b, reinterpret_cast<void*>(8 * sizeof(float)));
    step_particles(0.f);
}

void build_quad_and_hud() {
    float q[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    glGenVertexArrays(1, &app.quadVao);
    glGenBuffers(1, &app.quadVbo);
    glBindVertexArray(app.quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));

    glGenVertexArrays(1, &app.hudVao);
    glGenBuffers(1, &app.hudVbo);
    glBindVertexArray(app.hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.hudVbo);
    glBufferData(GL_ARRAY_BUFFER, 64, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
}

void upload_flux() {
    std::vector<float> tex(bh::FluxTable::N);
    for (int i = 0; i < bh::FluxTable::N; ++i) tex[i] = static_cast<float>(app.flux.Fnorm[i]);
    glGenTextures(1, &app.fluxTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.fluxTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, bh::FluxTable::N, 1, 0, GL_RED, GL_FLOAT, tex.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void cache_uniforms() {
    GLuint t = app.traceProg;
    app.tr.resolution = glGetUniformLocation(t, "uResolution");
    app.tr.aspect = glGetUniformLocation(t, "uAspect");
    app.tr.tanHalf = glGetUniformLocation(t, "uTanHalf");
    app.tr.camPos = glGetUniformLocation(t, "uCamPos");
    app.tr.right = glGetUniformLocation(t, "uRight");
    app.tr.up = glGetUniformLocation(t, "uUp");
    app.tr.fwd = glGetUniformLocation(t, "uFwd");
    app.tr.flux = glGetUniformLocation(t, "uFluxTex");
    app.tr.fluxIn = glGetUniformLocation(t, "uFluxIn");
    app.tr.fluxOut = glGetUniformLocation(t, "uFluxOut");
    app.tr.fluxN = glGetUniformLocation(t, "uFluxN");
    app.tr.diskIn = glGetUniformLocation(t, "uDiskIn");
    app.tr.diskOut = glGetUniformLocation(t, "uDiskOut");
    app.tr.time = glGetUniformLocation(t, "uTime");
    app.tr.coordStep = glGetUniformLocation(t, "uCoordStep");
    app.tr.stepLimit = glGetUniformLocation(t, "uStepLimit");
    app.tr.colorMode = glGetUniformLocation(t, "uColorMode");
    app.tr.flat = glGetUniformLocation(t, "uFlat");
    app.tr.showDisk = glGetUniformLocation(t, "uShowDisk");
    app.tr.showHalo = glGetUniformLocation(t, "uShowHalo");
    app.tr.showGrid = glGetUniformLocation(t, "uShowGrid");
    app.tr.diskGain = glGetUniformLocation(t, "uDiskGain");
    app.tr.haloGain = glGetUniformLocation(t, "uHaloGain");
    app.tr.tmax = glGetUniformLocation(t, "uTmax");
    app.tr.source0 = glGetUniformLocation(t, "uSource0");
    app.tr.source1 = glGetUniformLocation(t, "uSource1");
    need_uniform(app.tr.camPos, "uCamPos");
    need_uniform(app.tr.fwd, "uFwd");
    need_uniform(app.tr.flux, "uFluxTex");
    need_uniform(app.tr.showDisk, "uShowDisk");
    need_uniform(app.tr.stepLimit, "uStepLimit");

    GLuint p = app.postProg;
    app.post.tex = glGetUniformLocation(p, "uTex");
    app.post.hdr = glGetUniformLocation(p, "uHdr");
    app.post.bloom = glGetUniformLocation(p, "uBloom");
    app.post.mode = glGetUniformLocation(p, "uMode");
    app.post.exposure = glGetUniformLocation(p, "uExposure");
    app.post.texel = glGetUniformLocation(p, "uTexel");
    app.post.direction = glGetUniformLocation(p, "uDirection");
    app.post.blurScale = glGetUniformLocation(p, "uBlurScale");
    app.post.bloomStrength = glGetUniformLocation(p, "uBloomStrength");
    app.post.bloomOn = glGetUniformLocation(p, "uBloomOn");

    GLuint e = app.embedProg;
    app.emb.mvp = glGetUniformLocation(e, "uMVP");
    app.emb.pointSize = glGetUniformLocation(e, "uPointSize");
    app.emb.eye = glGetUniformLocation(e, "uEye");
    need_uniform(app.emb.mvp, "uMVP");
}

void print_banner() {
    std::printf("Schwarzschild black hole\n");
    std::printf("  G = c = 1,  M = 1,  r_s = 2M\n");
    std::printf("  photon sphere 3M,  ISCO 6M,  critical impact parameter %.6f M\n", bh::B_CRIT);
    std::printf("  Novikov–Thorne flux peaks at r = %.2f M\n", app.flux.r_peak);
    std::printf("  Disk temperature follows F(r)^(1/4), scaled so the peak is %.0f K.\n", app.tmax);
    std::printf("  The g^4 beaming and gravitational redshift are not scaled.\n");
    std::printf("\nControls\n");
    std::printf("  drag            orbit   (embedding view: orbit the funnel)\n");
    std::printf("  scroll          camera radius\n");
    std::printf("  1  2  3         face-on, 66 deg, edge-on\n");
    std::printf("  V               observatory / rays only / spacetime embedding\n");
    std::printf("  L               flat-space comparison, no light bending\n");
    std::printf("  C               realistic / g-factor / capture mask\n");
    std::printf("  D N B G         disk, corona halo, bloom, sky grid\n");
    std::printf("  [ ]             quality        - +  exposure\n");
    std::printf("  space           pause          R reset        F fullscreen\n");
    std::printf("  H               hide labels    esc quit\n\n");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    bool selftest = false;
    bool exit_after = false;
    const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
        else if (std::strcmp(argv[i], "--exit") == 0) exit_after = true;
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("Usage: blackhole [--selftest] [--screenshot file.ppm] [--exit]\n");
            return 0;
        }
    }

    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GL_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_SAMPLES, 0);
    app.win = glfwCreateWindow(1400, 880, "Schwarzschild black hole", nullptr, nullptr);
    if (!app.win) {
        std::fprintf(stderr, "Could not open an OpenGL window\n");
        glfwTerminate();
        return 1;
    }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (const GLFWvidmode* mode = glfwGetVideoMode(mon)) {
        glfwSetWindowPos(app.win, std::max(0, (mode->width - 1400) / 2), std::max(40, (mode->height - 880) / 2));
    }
    glfwSetWindowUserPointer(app.win, &app);
    glfwSetKeyCallback(app.win, on_key);
    glfwSetMouseButtonCallback(app.win, on_mouse);
    glfwSetCursorPosCallback(app.win, on_cursor);
    glfwSetScrollCallback(app.win, on_scroll);
    glfwSetFramebufferSizeCallback(app.win, on_resize);
    glfwMakeContextCurrent(app.win);
    glfwSwapInterval(1);

    std::printf("Compiling shaders...\n");
    std::fflush(stdout);
    const std::string dir = SHADER_DIR;
    const std::string quad = read_file(dir + "/quad.vert");
    app.traceProg = link_program(compile_shader(GL_VERTEX_SHADER, quad, "quad"),
                                 compile_shader(GL_FRAGMENT_SHADER, read_file(dir + "/trace.frag"), "trace"), "trace");
    app.postProg = link_program(compile_shader(GL_VERTEX_SHADER, quad, "quad"),
                                compile_shader(GL_FRAGMENT_SHADER, read_file(dir + "/post.frag"), "post"), "post");
    app.embedProg = link_program(compile_shader(GL_VERTEX_SHADER, read_file(dir + "/embed.vert"), "embed.vert"),
                                 compile_shader(GL_FRAGMENT_SHADER, read_file(dir + "/embed.frag"), "embed.frag"),
                                 "embed");
    app.hudProg = link_program(compile_shader(GL_VERTEX_SHADER, read_file(dir + "/hud.vert"), "hud.vert"),
                               compile_shader(GL_FRAGMENT_SHADER, read_file(dir + "/hud.frag"), "hud.frag"), "hud");
    cache_uniforms();
    glEnable(GL_PROGRAM_POINT_SIZE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    app.flux = bh::build_flux_table();
    upload_flux();
    build_quad_and_hud();
    build_mesh();
    place_sources();
    apply_quality();
    print_banner();

    bool test_ok = true;
    if (selftest) test_ok = shadow_selftest();
    if (shot) app.sim_time = 1.7;

    double last = glfwGetTime();
    double fps_acc = 0;
    int fps_frames = 0;
    int frame = 0;
    while (!glfwWindowShouldClose(app.win)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(std::min(0.05, now - last));
        last = now;
        if (!app.paused) app.sim_time += static_cast<double>(dt) * 7.0;
        glfwPollEvents();
        double t0 = glfwGetTime();
        render_frame(dt);
        if (shot && frame == 1) {
            glFinish();
            std::vector<uint8_t> px(static_cast<size_t>(app.fbw) * app.fbh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, app.fbw, app.fbh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (!write_ppm(shot, app.fbw, app.fbh, px.data())) {
                std::fprintf(stderr, "Could not write %s\n", shot);
                test_ok = false;
            } else {
                std::printf("Wrote %s (%dx%d)  frame %.1f ms\n", shot, app.fbw, app.fbh,
                            (glfwGetTime() - t0) * 1000.0);
            }
            std::fflush(stdout);
            if (exit_after) break;
        }
        glfwSwapBuffers(app.win);
        if (frame > 0) {
            fps_acc += dt;
            ++fps_frames;
            if (fps_acc > 0.35) {
                app.fps = static_cast<float>(fps_frames / fps_acc);
                fps_acc = 0;
                fps_frames = 0;
            }
        }
        char title[192];
        std::snprintf(title, sizeof title, "Schwarzschild black hole  |  %.0f fps  |  r = %.1fM  |  i = %.0f deg  |  Q%d%s%s",
                      app.fps, app.radius, app.theta * 180.0 / kPi, app.quality, app.flat ? "  |  flat" : "",
                      app.paused ? "  |  paused" : "");
        glfwSetWindowTitle(app.win, title);
        ++frame;
        if (exit_after && !shot && frame > 0) break;
    }

    destroy_targets();
    glfwDestroyWindow(app.win);
    glfwTerminate();
    if (selftest && !test_ok) return 1;
    return 0;
}
