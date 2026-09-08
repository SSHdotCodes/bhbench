// black-hole-cpp-musemax — realtime GPU black-hole simulation.
// Realtime Schwarzschild geodesic ray tracer + Flamm-paraboloid spacetime view.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GLEW_STATIC
#include <GL/glew.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "camera.hpp"
#include "overlay.hpp"
#include "physics.hpp"
#include "shaders.hpp"
#include "spacetime.hpp"

namespace {

enum class ViewMode { LENSED = 0, SPACETIME = 1, SPLIT = 2 };

struct Quality {
    const char* name;
    int steps, octaves, bg;
};
const Quality QUALITIES[] = {
    {"LOW", 160, 2, 0}, {"MED", 288, 3, 1}, {"HIGH", 448, 4, 1}};

struct App {
    GLFWwindow* win = nullptr;
    Shader ray;
    bool want_shot = false;
    SpacetimeScene scene;
    Overlay overlay;
    unsigned empty_vao = 0;
    OrbitCamera cam;
    ViewMode mode = ViewMode::LENSED;
    int quality = 1;
    double spin = 0.0;
    double sim_time = 0.0;
    bool paused = false;
    bool autorotate = true;
    bool show_overlay = true;
    double fps_ema = 60.0;
    int shot_count = 0;
    bool dragging = false;
    double last_x = 0, last_y = 0;
    int win_x = 0, win_y = 0, win_w = 1280, win_h = 800;
    bool fullscreen = false;

    // Ray-shader uniform locations.
    int u_campos = -1, u_right = -1, u_up = -1, u_fwd = -1, u_tanf = -1,
        u_aspect = -1, u_spin = -1, u_din = -1, u_dout = -1, u_time = -1,
        u_steps = -1, u_oct = -1, u_exp = -1, u_bg = -1;
};

void print_help() {
    std::puts(
        "black-hole-cpp-musemax — controls\n"
        "  mouse drag .... orbit camera        wheel ........ zoom\n"
        "  1/2/3 ......... lensed / spacetime-grid / split view\n"
        "  Q ............. ray-tracing quality (LOW/MED/HIGH)\n"
        "  + / - ......... disk spin a/M (ISCO + Doppler; bending exact)\n"
        "  X ............. spin back to 0 (fully self-consistent GR)\n"
        "  R ............. toggle auto-orbit      SPACE ... pause time\n"
        "  O ............. toggle overlay         F ..... fullscreen\n"
        "  S ............. save screenshot (PPM)  H ........ this help\n"
        "  ESC ........... quit\n"
        "Physics: Schwarzschild null geodesics (exact, M = 1), Page-Thorne-ish\n"
        "thin disk with gravitational + Doppler shift, Flamm paraboloid grid.");
}

bool save_ppm(const char* path, int w, int h,
              const std::vector<unsigned char>& rgb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // PPM rows go top-first; GL reads bottom-first: flip.
    std::vector<unsigned char> row(w * 3);
    for (int y = h - 1; y >= 0; --y) {
        std::memcpy(row.data(), &rgb[y * w * 3], w * 3);
        if (std::fwrite(row.data(), 1, w * 3, f) != (size_t)(w * 3)) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

std::vector<unsigned char> read_pixels(int w, int h) {
    std::vector<unsigned char> px(w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    return px;
}

void framebuffer_size_cb(GLFWwindow*, int, int) {}  // sizes polled per frame

void mouse_button_cb(GLFWwindow* w, int button, int action, int) {
    App* a = (App*)glfwGetWindowUserPointer(w);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        a->dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &a->last_x, &a->last_y);
    }
}

void cursor_pos_cb(GLFWwindow* w, double x, double y) {
    App* a = (App*)glfwGetWindowUserPointer(w);
    if (a->dragging) {
        a->cam.orbit((float)(x - a->last_x), (float)(a->last_y - y));
        a->last_x = x;
        a->last_y = y;
    }
}

void scroll_cb(GLFWwindow* w, double, double dy) {
    App* a = (App*)glfwGetWindowUserPointer(w);
    a->cam.zoom((float)dy);
}

void toggle_fullscreen(App* a) {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (!a->fullscreen) {
        glfwGetWindowPos(a->win, &a->win_x, &a->win_y);
        glfwGetWindowSize(a->win, &a->win_w, &a->win_h);
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(a->win, mon, 0, 0, vm->width, vm->height,
                             vm->refreshRate);
    } else {
        glfwSetWindowMonitor(a->win, nullptr, a->win_x, a->win_y, a->win_w,
                             a->win_h, 0);
    }
    a->fullscreen = !a->fullscreen;
}

void key_cb(GLFWwindow* w, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    App* a = (App*)glfwGetWindowUserPointer(w);
    const double spin_step = (mods & GLFW_MOD_SHIFT) ? 0.2 : 0.05;
    switch (key) {
        case GLFW_KEY_1: a->mode = ViewMode::LENSED; break;
        case GLFW_KEY_2: a->mode = ViewMode::SPACETIME; break;
        case GLFW_KEY_3: a->mode = ViewMode::SPLIT; break;
        case GLFW_KEY_Q: a->quality = (a->quality + 1) % 3; break;
        case GLFW_KEY_EQUAL:  // '+' / '=' key
        case GLFW_KEY_KP_ADD:
            a->spin += spin_step;
            if (a->spin > 0.95) a->spin = 0.95;
            a->scene.set_spin(a->spin);
            break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            a->spin -= spin_step;
            if (a->spin < -0.95) a->spin = -0.95;
            a->scene.set_spin(a->spin);
            break;
        case GLFW_KEY_X:
            a->spin = 0.0;
            a->scene.set_spin(a->spin);
            break;
        case GLFW_KEY_R: a->autorotate = !a->autorotate; break;
        case GLFW_KEY_SPACE: a->paused = !a->paused; break;
        case GLFW_KEY_O: a->show_overlay = !a->show_overlay; break;
        case GLFW_KEY_F: toggle_fullscreen(a); break;
        case GLFW_KEY_H: print_help(); break;
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_S:
            // Deferred: the frame is captured pre-swap in the main loop,
            // since post-swap back-buffer contents are undefined.
            a->want_shot = true;
            break;
    }
}

bool init_ray_shader(App* a) {
    if (!a->ray.load(find_shader("raytrace.vert"),
                     find_shader("raytrace.frag")))
        return false;
    a->u_campos = a->ray.uniform("uCamPos");
    a->u_right = a->ray.uniform("uCamRight");
    a->u_up = a->ray.uniform("uCamUp");
    a->u_fwd = a->ray.uniform("uCamFwd");
    a->u_tanf = a->ray.uniform("uTanHalfFov");
    a->u_aspect = a->ray.uniform("uAspect");
    a->u_spin = a->ray.uniform("uSpin");
    a->u_din = a->ray.uniform("uDiskInner");
    a->u_dout = a->ray.uniform("uDiskOuter");
    a->u_time = a->ray.uniform("uTime");
    a->u_steps = a->ray.uniform("uSteps");
    a->u_oct = a->ray.uniform("uOctaves");
    a->u_exp = a->ray.uniform("uExposure");
    a->u_bg = a->ray.uniform("uBgDetail");
    glGenVertexArrays(1, &a->empty_vao);
    return true;
}

void render_raytrace(App* a, int x, int y, int w, int h) {
    glViewport(x, y, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    a->ray.use();
    glm::vec3 p = a->cam.pos(), r = a->cam.right(), u = a->cam.up(),
              f = a->cam.fwd();
    glUniform3f(a->u_campos, p.x, p.y, p.z);
    glUniform3f(a->u_right, r.x, r.y, r.z);
    glUniform3f(a->u_up, u.x, u.y, u.z);
    glUniform3f(a->u_fwd, f.x, f.y, f.z);
    glUniform1f(a->u_tanf, std::tan(glm::radians(a->cam.fov_deg * 0.5f)));
    glUniform1f(a->u_aspect, (float)w / (float)h);
    glUniform1f(a->u_spin, (float)a->spin);
    glUniform1f(a->u_din, (float)bh::disk_inner(a->spin));
    glUniform1f(a->u_dout, (float)bh::DISK_OUTER);
    glUniform1f(a->u_time, (float)a->sim_time);
    glUniform1i(a->u_steps, QUALITIES[a->quality].steps);
    glUniform1i(a->u_oct, QUALITIES[a->quality].octaves);
    glUniform1f(a->u_exp, 1.15f);
    glUniform1i(a->u_bg, QUALITIES[a->quality].bg);
    glBindVertexArray(a->empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void render_grid(App* a, int x, int y, int w, int h) {
    glViewport(x, y, w, h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glm::vec3 p = a->cam.pos() * 1.25f;
    if (glm::length(p) > 60.0f) p = glm::normalize(p) * 60.0f;
    glm::mat4 view = glm::lookAt(p, glm::vec3(0, -2.2f, 0),
                                 glm::vec3(0, 1, 0));
    glm::mat4 proj = a->cam.proj((float)w / (float)h);
    a->scene.render(view, proj, p);
}

void draw_overlay(App* a, int fb_w, int fb_h) {
    const Rgba white{1, 1, 1, 0.95f}, dim{0.62f, 0.72f, 0.85f, 0.95f},
        amber{1.0f, 0.75f, 0.35f, 0.95f}, panel{0.02f, 0.04f, 0.08f, 0.62f};
    const char* mode_name = a->mode == ViewMode::LENSED
                                ? "GEODESIC RAY TRACING (SCHWARZSCHILD)"
                                : (a->mode == ViewMode::SPACETIME
                                       ? "SPACETIME GRID (FLAMM PARABOLOID)"
                                       : "LENSED VIEW + SPACETIME GRID");
    char l1[96], l2[96], l3[96], l4[96];
    std::snprintf(
        l1, sizeof(l1), "FPS %.0f  QUAL %s  STEPS %d", a->fps_ema,
        QUALITIES[a->quality].name, QUALITIES[a->quality].steps);
    std::snprintf(l2, sizeof(l2), "SPIN %+.2f  ISCO %.2FM  DISK %.2F-%.0FM",
                  a->spin, bh::isco_kerr(a->spin), bh::disk_inner(a->spin),
                  bh::DISK_OUTER);
    std::snprintf(l3, sizeof(l3), "CAM R %.1FM  EL %.0FDEG  AZ %.0FDEG",
                  a->cam.dist, a->cam.pitch_deg, a->cam.yaw_deg);
    std::snprintf(l4, sizeof(l4), "T+%.1FS  ROT %s  %s", a->sim_time,
                  a->autorotate ? "ON" : "OFF", a->paused ? "HELD" : "LIVE");

    a->overlay.begin(fb_w, fb_h);
    const float sc = 2.0f, lh = Overlay::line_height(sc) + 5.0f;
    float pw = Overlay::line_width(mode_name, sc);
    for (auto* l : {l1, l2, l3, l4})
        pw = std::max(pw, Overlay::line_width(l, sc));
    pw += 20;
    const float ph = lh * 5 + 22;
    a->overlay.panel(10, 10, pw, ph, panel);
    float ty = 20.0f;
    a->overlay.text(20, ty, sc, mode_name, amber);
    ty += lh;
    a->overlay.text(20, ty, sc, l1, white);
    ty += lh;
    a->overlay.text(20, ty, sc, l2, white);
    ty += lh;
    a->overlay.text(20, ty, sc, l3, dim);
    ty += lh;
    a->overlay.text(20, ty, sc, l4, dim);

    if (a->mode != ViewMode::LENSED) {
        const char* leg[] = {"W = 2 SQRT(RS(R-RS)): FLAT FAR, TRAPDOOR NEAR",
                             "AMBER RING R=3M (PHOTONS) ORANGE = ISCO",
                             "CYAN ARCS LIGHT, RED ARCS CAPTURED",
                             "GREEN PRECESSING, EMBER PLUNGING"};
        float lx = (a->mode == ViewMode::SPLIT) ? fb_w / 2 + 10 : 10.0f;
        float ly = (a->mode == ViewMode::SPLIT) ? 10.0f : 10 + ph + 12;
        float lw = 0;
        for (auto* s : leg) lw = std::max(lw, Overlay::line_width(s, 1.0f));
        lw += 16;
        float lh2 = Overlay::line_height(1.0f) + 4.0f;
        a->overlay.panel(lx, ly, lw, lh2 * 4 + 14, panel);
        float y2 = ly + 8;
        for (auto* s : leg) {
            a->overlay.text(lx + 8, y2, 1.0f, s, dim);
            y2 += lh2;
        }
    }
    const char* help =
        "[1/2/3] VIEW [Q] QUAL [+-] SPIN [R] ROT [SPACE] HOLD [S] PIC";
    float hw = Overlay::line_width(help, 1.0f) + 16;
    a->overlay.panel(fb_w / 2 - hw / 2, fb_h - 30, hw, 22, panel);
    a->overlay.text(fb_w / 2 - hw / 2 + 8, fb_h - 25, 1.0f, help, dim);
    a->overlay.end();
}

// ---- headless verification helpers (screenshot mode) ----
double luminance(const unsigned char* p) {
    return (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
}

// Radius (px) of the contiguous dark run through the image center row.
double measure_shadow_radius(const std::vector<unsigned char>& px, int w,
                             int h) {
    const int cy = h / 2, cx = w / 2;
    int left = cx, right = cx;
    while (left > 0 && luminance(&px[(cy * w + left - 1) * 3]) < 0.031) --left;
    while (right < w - 1 && luminance(&px[(cy * w + right + 1) * 3]) < 0.031)
        ++right;
    return 0.5 * (right - left);
}

double box_mean(const std::vector<unsigned char>& px, int w, int h, int x0,
                int x1, int y0, int y1) {
    double s = 0;
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            s += luminance(&px[(y * w + x) * 3]);
            ++n;
        }
    return s / n;
}

int run_screenshot(const char* path, int W, int H) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 2;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(W, H, "verify", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "hidden window creation failed\n");
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(win);
#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    glewInit();
#endif
    App a;
    a.win = win;
    a.quality = 2;  // HIGH for verification
    a.spin = 0.0;
    if (!init_ray_shader(&a) || !a.scene.init(a.spin) || !a.overlay.init()) {
        std::fprintf(stderr, "GL init failed\n");
        return 2;
    }
    int fails = 0;
    // 1) Beauty shot: inclined view + Doppler asymmetry check.
    a.cam.yaw_deg = 35;
    a.cam.pitch_deg = 24;
    a.cam.dist = 17;
    a.sim_time = 3.0;
    a.scene.update(a.sim_time);
    for (int i = 0; i < 3; ++i) render_raytrace(&a, 0, 0, W, H);
    auto px = read_pixels(W, H);  // pre-swap: back buffer is valid here
    glfwSwapBuffers(win);
    if (!save_ppm(path, W, H, px)) return 2;
    std::printf("saved %s (%dx%d)\n", path, W, H);
    {
        const int cx = W / 2, cy = H / 2;
        const double dl = box_mean(px, W, H, cx - 330, cx - 200, cy - 40,
                                   cy + 40);
        const double dr = box_mean(px, W, H, cx + 200, cx + 330, cy - 40,
                                   cy + 40);
        const double ratio = std::max(dl, dr) / std::max(std::min(dl, dr),
                                                         1e-6);
        const bool ok = ratio > 1.25;
        if (!ok) ++fails;
        std::printf("[%s] disk Doppler asymmetry L=%.3f R=%.3f ratio=%.2f\n",
                    ok ? "PASS" : "FAIL", dl, dr, ratio);
    }
    // 2) Face-on shadow radius vs analytic prediction.
    a.cam.yaw_deg = 0;
    a.cam.pitch_deg = 89;
    a.cam.dist = 16;
    for (int i = 0; i < 3; ++i) render_raytrace(&a, 0, 0, W, H);
    px = read_pixels(W, H);
    glfwSwapBuffers(win);
    {
        const double meas = measure_shadow_radius(px, W, H);
        const double alpha = bh::shadow_angle(16.0);
        const double pred =
            std::tan(alpha) / std::tan(glm::radians(a.cam.fov_deg * 0.5f)) *
            (H / 2);
        const double err = std::fabs(meas - pred) / pred;
        const bool ok = err < 0.20;
        if (!ok) ++fails;
        std::printf(
            "[%s] shadow radius face-on: measured %.1fpx predicted %.1fpx "
            "(err %.1f%%)\n",
            ok ? "PASS" : "FAIL", meas, pred, err * 100);
    }
    glfwTerminate();
    if (fails == 0)
        std::puts("render verification: ALL CHECKS PASSED");
    else
        std::printf("render verification: %d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* shot_path = nullptr;
    int shot_w = 960, shot_h = 600;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--selftest")) return bh::run_self_tests();
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            print_help();
            std::puts(
                "  --selftest ........ run physics unit tests (no window)\n"
                "  --screenshot F [W H] render + verify headlessly to PPM");
            return 0;
        }
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            shot_path = argv[++i];
            if (i + 2 < argc && argv[i + 1][0] != '-') {
                shot_w = std::atoi(argv[++i]);
                shot_h = std::atoi(argv[++i]);
            }
        }
    }
    if (shot_path) return run_screenshot(shot_path, shot_w, shot_h);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 2;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    App a;
    a.win = glfwCreateWindow(1280, 800, "Black Hole — musemax", nullptr,
                             nullptr);
    if (!a.win) {
        std::fprintf(stderr, "window creation failed\n");
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(a.win);
    glfwSwapInterval(1);
#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed\n");
        return 2;
    }
#endif
    glfwSetWindowUserPointer(a.win, &a);
    glfwSetKeyCallback(a.win, key_cb);
    glfwSetMouseButtonCallback(a.win, mouse_button_cb);
    glfwSetCursorPosCallback(a.win, cursor_pos_cb);
    glfwSetScrollCallback(a.win, scroll_cb);
    glfwSetFramebufferSizeCallback(a.win, framebuffer_size_cb);

    if (!init_ray_shader(&a)) return 2;
    if (!a.scene.init(a.spin)) return 2;
    if (!a.overlay.init()) return 2;
    print_help();

    double prev = glfwGetTime(), title_at = 0;
    while (!glfwWindowShouldClose(a.win)) {
        const double now = glfwGetTime();
        double dt = now - prev;
        prev = now;
        if (dt > 0.25) dt = 0.25;
        a.fps_ema += (1.0 / std::max(dt, 1e-4) - a.fps_ema) * 0.05;

        if (!a.paused) a.sim_time += dt;
        if (a.autorotate && !a.paused) a.cam.yaw_deg += (float)(dt * 4.0);
        a.scene.update(a.sim_time);

        int fb_w, fb_h;
        glfwGetFramebufferSize(a.win, &fb_w, &fb_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (a.mode == ViewMode::LENSED) {
            render_raytrace(&a, 0, 0, fb_w, fb_h);
        } else if (a.mode == ViewMode::SPACETIME) {
            render_grid(&a, 0, 0, fb_w, fb_h);
        } else {
            render_raytrace(&a, 0, 0, fb_w / 2, fb_h);
            render_grid(&a, fb_w / 2, 0, fb_w - fb_w / 2, fb_h);
        }
        glViewport(0, 0, fb_w, fb_h);
        if (a.show_overlay) draw_overlay(&a, fb_w, fb_h);
        if (a.want_shot) {
            a.want_shot = false;
            char name[64];
            std::snprintf(name, sizeof(name), "screenshot_%03d.ppm",
                          a.shot_count++);
            if (save_ppm(name, fb_w, fb_h, read_pixels(fb_w, fb_h)))
                std::printf("saved %s (%dx%d)\n", name, fb_w, fb_h);
        }
        glfwSwapBuffers(a.win);
        glfwPollEvents();

        if (now - title_at > 0.5) {
            title_at = now;
            char t[160];
            const char* m = a.mode == ViewMode::LENSED
                                ? "lensed"
                                : (a.mode == ViewMode::SPACETIME ? "spacetime"
                                                                : "split");
            std::snprintf(t, sizeof(t),
                          "Black Hole (%s) — %.0f FPS — spin %+.2f — %s", m,
                          a.fps_ema, a.spin, QUALITIES[a.quality].name);
            glfwSetWindowTitle(a.win, t);
        }
    }
    glfwTerminate();
    return 0;
}
