// Schwarzschild black hole: real-time general-relativistic ray tracer.
//
//   ./blackhole                     open the interactive window
//   ./blackhole --still out.png     render one high-quality frame and exit
//
// Options: --width N --height N --mode 0|1|2 --time T --samples N --scale F
//          --nohud --yaw F --pitch F --dist F --temp K --exposure F

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "camera.h"
#include "params.h"
#include "png.h"
#include "renderer.h"

namespace {

struct Options {
    bool still = false;
    std::string outPath = "frame.png";
    int width = 1600;
    int height = 900;
    int mode = 0;
    float time = 0.0f;
    int samples = 1;
    float scale = 0.85f;
    bool hud = true;
    bool hasYaw = false, hasPitch = false, hasDist = false, hasFov = false;
    float yaw = 0.0f, pitch = 0.0f, dist = 0.0f, fov = 0.0f;
    float temp = 0.0f;
    float exposure = 0.0f;
    bool noStars = false, noBloom = false, noTex = false;
    bool physical = false;
    int bench = 0;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](float def) -> float {
            if (i + 1 < argc) return float(atof(argv[++i]));
            return def;
        };
        if (a == "--still" && i + 1 < argc) { o.still = true; o.outPath = argv[++i]; }
        else if (a == "--width") o.width = int(next(1600));
        else if (a == "--height") o.height = int(next(900));
        else if (a == "--mode") o.mode = int(next(0));
        else if (a == "--time") o.time = next(0);
        else if (a == "--samples") o.samples = int(next(1));
        else if (a == "--scale") o.scale = next(0.85f);
        else if (a == "--nohud") o.hud = false;
        else if (a == "--yaw") { o.yaw = next(0); o.hasYaw = true; }
        else if (a == "--pitch") { o.pitch = next(0); o.hasPitch = true; }
        else if (a == "--dist") { o.dist = next(0); o.hasDist = true; }
        else if (a == "--fov") { o.fov = next(0); o.hasFov = true; }
        else if (a == "--temp") o.temp = next(0);
        else if (a == "--exposure") o.exposure = next(0);
        else if (a == "--nostars") o.noStars = true;
        else if (a == "--nobloom") o.noBloom = true;
        else if (a == "--notex") o.noTex = true;
        else if (a == "--physical") o.physical = true;
        else if (a == "--bench") o.bench = int(next(120));
    }
    return o;
}

struct App {
    Params p;
    Renderer renderer;
    OrbitCamera cam;
    float simTime = 0.0f;
    double lastTime = 0.0;
    float fps = 0.0f;
    bool dragging = false;
    double lastX = 0, lastY = 0;
    bool cameraDirty = true;
    int frameIndex = 0;
    bool autoOrbit = false;
    float fpsAccum = 0.0f;
    int fpsFrames = 0;
    float frameMs = 16.0f;
};

App* g_app = nullptr;

void applyCamera(App& a) {
    a.p.yaw = a.cam.yaw;
    a.p.pitch = a.cam.pitch;
    a.p.distance = a.cam.distance;
    a.p.fovDeg = a.cam.fovDeg;
}

void onKey(GLFWwindow* w, int key, int, int action, int mods) {
    App& a = *g_app;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    (void)mods;

    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_1: a.p.mode = 0; a.cameraDirty = true; break;
        case GLFW_KEY_2: a.p.mode = 1; a.cameraDirty = true; break;
        case GLFW_KEY_3: a.p.mode = 2; a.cameraDirty = true; break;
        case GLFW_KEY_SPACE: a.p.paused = !a.p.paused; break;
        case GLFW_KEY_G: a.p.showGrid = !a.p.showGrid; a.cameraDirty = true; break;
        case GLFW_KEY_N: a.p.diskTexture = !a.p.diskTexture; a.cameraDirty = true; break;
        case GLFW_KEY_V: a.p.physical = !a.p.physical; a.cameraDirty = true; break;
        case GLFW_KEY_B: a.p.bloom = (a.p.bloom > 0.01f) ? 0.0f : 0.55f; a.cameraDirty = true; break;
        case GLFW_KEY_H: a.p.hud = !a.p.hud; break;
        case GLFW_KEY_O: a.autoOrbit = !a.autoOrbit; break;
        case GLFW_KEY_R:
            a.cam = OrbitCamera();
            a.p.gridYaw = 0.95f; a.p.gridPitch = 0.46f; a.p.gridDistance = 44.0f;
            a.p.exposure = 1.0f; a.p.gamma = 1.0f;
            a.cameraDirty = true;
            break;
        case GLFW_KEY_F: {
            static bool full = false;
            full = !full;
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            if (full) glfwSetWindowMonitor(w, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            else glfwSetWindowMonitor(w, nullptr, 80, 80, 1600, 900, 0);
            break;
        }
        case GLFW_KEY_LEFT_BRACKET:
            a.p.exposure = std::max(0.02f, a.p.exposure * 0.85f);
            a.cameraDirty = true; break;
        case GLFW_KEY_RIGHT_BRACKET:
            a.p.exposure = std::min(50.0f, a.p.exposure * 1.18f);
            a.cameraDirty = true; break;
        case GLFW_KEY_MINUS:
            a.p.diskTempPeak = std::max(1500.0f, a.p.diskTempPeak * 0.88f);
            a.cameraDirty = true; break;
        case GLFW_KEY_EQUAL:
            a.p.diskTempPeak = std::min(60000.0f, a.p.diskTempPeak * 1.14f);
            a.cameraDirty = true; break;
        case GLFW_KEY_COMMA:
            a.p.diskOuter = std::max(a.p.diskInner * 1.5f, a.p.diskOuter * 0.9f);
            a.cameraDirty = true; break;
        case GLFW_KEY_PERIOD:
            a.p.diskOuter = std::min(120.0f, a.p.diskOuter * 1.1f);
            a.cameraDirty = true; break;
        case GLFW_KEY_T:
            a.p.gamma = (a.p.gamma > 1.5f) ? 1.0f : 3.0f;
            a.cameraDirty = true; break;
        default: break;
    }
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    App& a = *g_app;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        a.dragging = true;
        glfwGetCursorPos(w, &a.lastX, &a.lastY);
    } else {
        a.dragging = false;
    }
}

void onCursor(GLFWwindow* w, double x, double y) {
    App& a = *g_app;
    if (!a.dragging) return;
    double dx = x - a.lastX;
    double dy = y - a.lastY;
    a.lastX = x;
    a.lastY = y;

    if (a.p.mode == 1) {
        a.p.gridYaw -= float(dx) * 0.006f;
        a.p.gridPitch = m3::clampf(a.p.gridPitch + float(dy) * 0.005f, -1.4f, 1.4f);
    } else {
        a.cam.orbit(-float(dx) * 0.006f, float(dy) * 0.005f);
        applyCamera(a);
    }
    a.cameraDirty = true;
}

void onScroll(GLFWwindow* w, double, double dy) {
    App& a = *g_app;
    if (a.p.mode == 1) {
        a.p.gridDistance = m3::clampf(a.p.gridDistance * float(std::pow(0.9, dy)), 10.0f, 300.0f);
    } else {
        a.cam.zoom(float(std::pow(0.9, dy)));
        applyCamera(a);
    }
    a.cameraDirty = true;
}

void onResize(GLFWwindow*, int w, int h) {
    App& a = *g_app;
    a.renderer.resize(w, h, a.p.renderScale);
    a.cameraDirty = true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);

    if (!glfwInit()) {
        fprintf(stderr, "failed to initialise GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_VISIBLE, opt.still ? GLFW_FALSE : GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(opt.width, opt.height,
                                          "Schwarzschild Black Hole", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "failed to create window (OpenGL 4.1 core required)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(opt.still ? 0 : 1);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(err));
        return 1;
    }
    glGetError();  // GLEW on core profiles raises a benign INVALID_ENUM

    printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    App app;
    g_app = &app;

    app.p.mode = opt.mode;
    app.p.renderScale = opt.scale;
    app.p.hud = opt.hud;
    if (opt.hasYaw) app.cam.yaw = opt.yaw;
    if (opt.hasPitch) app.cam.pitch = opt.pitch;
    if (opt.hasDist) app.cam.distance = opt.dist;
    if (opt.hasFov) app.cam.fovDeg = opt.fov;
    if (opt.temp > 0) app.p.diskTempPeak = opt.temp;
    if (opt.exposure > 0) app.p.exposure = opt.exposure;
    if (opt.noStars) app.p.showStars = false;
    if (opt.noBloom) app.p.bloom = 0.0f;
    if (opt.noTex) app.p.diskTexture = false;
    if (opt.physical) app.p.physical = true;
    applyCamera(app);

    try {
        app.renderer.init();
    } catch (const std::exception& e) {
        fprintf(stderr, "renderer init failed: %s\n", e.what());
        return 1;
    }

    int fbw = opt.width, fbh = opt.height;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    app.renderer.resize(fbw, fbh, app.p.renderScale);

    // ------------------------------------------------------------------
    // Offscreen still render
    // ------------------------------------------------------------------
    if (opt.still) {
        app.simTime = opt.time;
        int samples = std::max(1, opt.samples);
        for (int i = 0; i < samples; ++i) {
            app.renderer.renderScene(app.p, app.simTime, i == 0, i);
        }
        if (app.p.hud) app.renderer.drawHud(app.p, app.simTime, 0.0f, fbw, fbh);

        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (!app.renderer.readFinal(rgb, w, h)) {
            fprintf(stderr, "failed to read framebuffer\n");
            return 1;
        }
        if (!png::write(opt.outPath.c_str(), w, h, 3, rgb.data())) {
            fprintf(stderr, "failed to write %s\n", opt.outPath.c_str());
            return 1;
        }
        printf("wrote %s (%dx%d, %d samples)\n", opt.outPath.c_str(), w, h, samples);
        app.renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    // ------------------------------------------------------------------
    // Benchmark: render N frames offscreen and report throughput
    // ------------------------------------------------------------------
    if (opt.bench > 0) {
        for (int i = 0; i < 5; ++i) app.renderer.renderScene(app.p, app.simTime, true, i);
        glFinish();
        double t0 = glfwGetTime();
        for (int i = 0; i < opt.bench; ++i) {
            app.simTime += 1.0f / 60.0f;
            app.renderer.renderScene(app.p, app.simTime, true, i);
        }
        glFinish();
        double t1 = glfwGetTime();
        double fps = opt.bench / (t1 - t0);
        printf("bench: %d frames in %.3f s -> %.1f fps (%.2f ms/frame) at %dx%d internal, mode %d\n",
               opt.bench, t1 - t0, fps, 1000.0 * (t1 - t0) / opt.bench,
               app.renderer.renderWidth(), app.renderer.renderHeight(), app.p.mode);
        app.renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    // ------------------------------------------------------------------
    // Interactive loop
    // ------------------------------------------------------------------
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursor);
    glfwSetScrollCallback(window, onScroll);
    glfwSetFramebufferSizeCallback(window, onResize);

    app.lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float dt = float(now - app.lastTime);
        app.lastTime = now;
        if (dt > 0.25f) dt = 0.25f;

        if (!app.p.paused) app.simTime += dt * app.p.timeScale;
        if (app.autoOrbit) {
            app.cam.yaw += dt * 0.12f;
            applyCamera(app);
            app.cameraDirty = true;
        }

        glfwGetFramebufferSize(window, &fbw, &fbh);
        app.renderer.resize(fbw, fbh, app.p.renderScale);

        bool reset = app.cameraDirty;
        app.renderer.renderScene(app.p, app.simTime, reset, app.frameIndex);
        app.cameraDirty = false;
        app.frameIndex++;

        if (app.p.hud) app.renderer.drawHud(app.p, app.simTime, app.fps, fbw, fbh);
        app.renderer.present();
        glfwSwapBuffers(window);

        // FPS + adaptive resolution.
        app.fpsAccum += dt;
        app.fpsFrames++;
        if (app.fpsAccum > 0.4f) {
            app.fps = float(app.fpsFrames) / app.fpsAccum;
            app.frameMs = 1000.0f * app.fpsAccum / float(app.fpsFrames);
            app.fpsAccum = 0.0f;
            app.fpsFrames = 0;

            if (app.p.adaptive && app.p.mode != 1) {
                if (app.fps < 45.0f && app.p.renderScale > 0.45f) {
                    app.p.renderScale = std::max(0.45f, app.p.renderScale - 0.06f);
                    app.renderer.resize(fbw, fbh, app.p.renderScale);
                    app.cameraDirty = true;
                } else if (app.fps > 58.0f && app.p.renderScale < 1.0f) {
                    app.p.renderScale = std::min(1.0f, app.p.renderScale + 0.03f);
                    app.renderer.resize(fbw, fbh, app.p.renderScale);
                    app.cameraDirty = true;
                }
            }
        }
    }

    app.renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
