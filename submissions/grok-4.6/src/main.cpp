#include "camera.hpp"
#include "gl_compat.hpp"
#include "shader.hpp"
#include "spacetime_grid.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

#ifndef SHADER_DIR
#define SHADER_DIR "shaders"
#endif

namespace {

struct FBO {
    GLuint id = 0;
    GLuint color = 0;
    int w = 0;
    int h = 0;

    void destroy() {
        if (color) glDeleteTextures(1, &color);
        if (id) glDeleteFramebuffers(1, &id);
        id = color = 0;
        w = h = 0;
    }
};

FBO makeHdrFbo(int w, int h) {
    FBO f;
    f.w = std::max(w, 1);
    f.h = std::max(h, 1);
    glGenFramebuffers(1, &f.id);
    glBindFramebuffer(GL_FRAMEBUFFER, f.id);
    glGenTextures(1, &f.color);
    glBindTexture(GL_TEXTURE_2D, f.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, f.w, f.h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Incomplete HDR framebuffer\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return f;
}

struct App {
    GLFWwindow* window = nullptr;
    Camera cam;
    Shader geodesic, grid, extract, blur, composite;
    LineMesh fabric;
    GLuint emptyVao = 0;

    FBO scene, bright, ping, pong;

    int fbW = 1, fbH = 1;
    int sceneW = 1, sceneH = 1;
    float renderScale = 0.62f;

    bool showGrid = true;
    bool showDisk = true;
    bool showHalo = true;
    bool showStars = true;
    bool showBloom = true;
    bool paused = false;
    bool mouseDown = false;
    bool rightDown = false;
    double lastX = 0, lastY = 0;

    float time = 0.0f;
    float mass = 1.0f;
    int maxSteps = 140;
    float exposure = 0.95f;
    float bloomStrength = 0.48f;
    float diskOuter = 12.5f;

    bool keys[512]{};
};

App* gApp = nullptr;

std::string shaderPath(const char* name) {
    return std::string(SHADER_DIR) + "/" + name;
}

void resizeFbos(App& a, int fbW, int fbH) {
    a.fbW = std::max(fbW, 1);
    a.fbH = std::max(fbH, 1);
    a.sceneW = std::max(1, static_cast<int>(std::round(a.fbW * a.renderScale)));
    a.sceneH = std::max(1, static_cast<int>(std::round(a.fbH * a.renderScale)));
    a.scene.destroy();
    a.bright.destroy();
    a.ping.destroy();
    a.pong.destroy();
    a.scene = makeHdrFbo(a.sceneW, a.sceneH);
    const int bw = std::max(1, a.sceneW / 2);
    const int bh = std::max(1, a.sceneH / 2);
    a.bright = makeHdrFbo(bw, bh);
    a.ping = makeHdrFbo(bw, bh);
    a.pong = makeHdrFbo(bw, bh);
}

void framebufferSizeCallback(GLFWwindow* w, int, int) {
    if (!gApp) return;
    int ww = 0, hh = 0;
    glfwGetWindowSize(w, &ww, &hh);
    if (ww > 0 && hh > 0) {
        resizeFbos(*gApp, ww, hh);
    }
}

void mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
    if (!gApp) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gApp->mouseDown = action == GLFW_PRESS;
        glfwGetCursorPos(w, &gApp->lastX, &gApp->lastY);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        gApp->rightDown = action == GLFW_PRESS;
        glfwGetCursorPos(w, &gApp->lastX, &gApp->lastY);
    }
}

void cursorCallback(GLFWwindow*, double x, double y) {
    if (!gApp) return;
    const float dx = static_cast<float>(x - gApp->lastX);
    const float dy = static_cast<float>(y - gApp->lastY);
    gApp->lastX = x;
    gApp->lastY = y;
    if (gApp->mouseDown) {
        gApp->cam.orbit(-dx * 0.0055f, dy * 0.0055f);
    }
    if (gApp->rightDown) {
        gApp->cam.pan(-dx, dy);
    }
}

void scrollCallback(GLFWwindow*, double, double yoff) {
    if (!gApp) return;
    gApp->cam.zoom(yoff > 0.0 ? 0.90f : 1.11f);
}

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (!gApp) return;
    if (key >= 0 && key < 512) {
        gApp->keys[key] = action != GLFW_RELEASE;
    }
    if (action != GLFW_PRESS) return;

    auto& a = *gApp;
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(w, 1);
        break;
    case GLFW_KEY_G:
        a.showGrid = !a.showGrid;
        break;
    case GLFW_KEY_D:
        a.showDisk = !a.showDisk;
        break;
    case GLFW_KEY_H:
        a.showHalo = !a.showHalo;
        break;
    case GLFW_KEY_T:
        a.showStars = !a.showStars;
        break;
    case GLFW_KEY_B:
        a.showBloom = !a.showBloom;
        break;
    case GLFW_KEY_SPACE:
        a.paused = !a.paused;
        break;
    case GLFW_KEY_R:
        a.cam.reset();
        break;
    case GLFW_KEY_1:
        a.renderScale = 0.45f;
        a.maxSteps = 110;
        resizeFbos(a, a.fbW, a.fbH);
        break;
    case GLFW_KEY_2:
        a.renderScale = 0.62f;
        a.maxSteps = 140;
        resizeFbos(a, a.fbW, a.fbH);
        break;
    case GLFW_KEY_3:
        a.renderScale = 0.85f;
        a.maxSteps = 210;
        resizeFbos(a, a.fbW, a.fbH);
        break;
    case GLFW_KEY_4:
        a.renderScale = 1.00f;
        a.maxSteps = 240;
        resizeFbos(a, a.fbW, a.fbH);
        break;
    default:
        break;
    }
}

void updateTitle(App& a, double fps) {
    const glm::vec3 p = a.cam.position();
    const float r = glm::length(p);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "Schwarzschild  |  r = %.2f M   fps %.0f   steps %d   scale %.2f   %s%s%s",
        r / a.mass, fps, a.maxSteps, a.renderScale,
        a.showDisk ? "disk " : "",
        a.showHalo ? "halo " : "",
        a.showGrid ? "grid" : "");
    glfwSetWindowTitle(a.window, buf);
}

} // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    App app;
    gApp = &app;
    app.window = glfwCreateWindow(1280, 800, "Schwarzschild black hole", nullptr, nullptr);
    if (!app.window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(mon)) {
            glfwSetWindowPos(app.window, (mode->width - 1280) / 2, (mode->height - 800) / 2);
        }
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(app.window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(app.window, mouseButtonCallback);
    glfwSetCursorPosCallback(app.window, cursorCallback);
    glfwSetScrollCallback(app.window, scrollCallback);
    glfwSetKeyCallback(app.window, keyCallback);

    std::printf(
        "\n"
        "  Schwarzschild geodesic ray tracer\n"
        "  ---------------------------------\n"
        "  LMB drag     orbit          RMB drag     pan\n"
        "  scroll       zoom           WASD/QE      move\n"
        "  G grid       D disk         H halo       T stars\n"
        "  B bloom      Space pause    R reset      1-4 quality\n"
        "  Esc quit\n"
        "  Horizon 2M, photon sphere 3M, ISCO 6M, b_crit = 3√3 M\n\n");

    glGenVertexArrays(1, &app.emptyVao);

    app.geodesic = Shader::fromFiles(shaderPath("fullscreen.vert"), shaderPath("geodesic.frag"));
    app.grid = Shader::fromFiles(shaderPath("grid.vert"), shaderPath("grid.frag"));
    app.extract = Shader::fromFiles(shaderPath("fullscreen.vert"), shaderPath("bloom_extract.frag"));
    app.blur = Shader::fromFiles(shaderPath("fullscreen.vert"), shaderPath("bloom_blur.frag"));
    app.composite = Shader::fromFiles(shaderPath("fullscreen.vert"), shaderPath("composite.frag"));
    if (!app.geodesic.id || !app.grid.id || !app.extract.id || !app.blur.id || !app.composite.id) {
        std::cerr << "Shader load failed\n";
        return 1;
    }

    app.fabric = buildSpacetimeFabric(app.mass);

    int w = 0, h = 0;
    glfwGetWindowSize(app.window, &w, &h);
    resizeFbos(app, w, h);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    double last = glfwGetTime();
    double fpsSmooth = 60.0;
    double titleTimer = 0.0;

    while (!glfwWindowShouldClose(app.window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::min(now - last, 0.05));
        last = now;
        fpsSmooth = 0.9 * fpsSmooth + 0.1 / std::max(now - last + dt, 1e-4);
        titleTimer += dt;
        if (titleTimer > 0.35) {
            updateTitle(app, 1.0 / std::max(static_cast<double>(dt), 1e-4));
            titleTimer = 0.0;
        }
        if (!app.paused) {
            app.time += dt;
        }

        const float move = app.cam.distance * dt * 0.55f;
        if (app.keys[GLFW_KEY_W]) app.cam.target += app.cam.forward() * move;
        if (app.keys[GLFW_KEY_S]) app.cam.target -= app.cam.forward() * move;
        if (app.keys[GLFW_KEY_A] || app.keys[GLFW_KEY_LEFT]) app.cam.target -= app.cam.right() * move;
        if (app.keys[GLFW_KEY_F] || app.keys[GLFW_KEY_RIGHT]) app.cam.target += app.cam.right() * move;
        if (app.keys[GLFW_KEY_Q] || app.keys[GLFW_KEY_DOWN]) app.cam.target -= app.cam.up() * move;
        if (app.keys[GLFW_KEY_E] || app.keys[GLFW_KEY_UP]) app.cam.target += app.cam.up() * move;
        if (app.keys[GLFW_KEY_Z]) app.cam.zoom(1.0f - 0.8f * dt);
        if (app.keys[GLFW_KEY_X]) app.cam.zoom(1.0f + 0.8f * dt);
        if (app.keys[GLFW_KEY_EQUAL] || app.keys[GLFW_KEY_KP_ADD]) {
            app.exposure = std::min(app.exposure + dt * 0.6f, 4.0f);
        }
        if (app.keys[GLFW_KEY_MINUS] || app.keys[GLFW_KEY_KP_SUBTRACT]) {
            app.exposure = std::max(app.exposure - dt * 0.6f, 0.15f);
        }

        // Strafe with J/L so D remains a toggle.
        if (app.keys[GLFW_KEY_J]) app.cam.target -= app.cam.right() * move;
        if (app.keys[GLFW_KEY_L]) app.cam.target += app.cam.right() * move;

        const glm::vec3 camPos = app.cam.position();
        const float tanHalf = std::tan(0.5f * glm::radians(app.cam.fovDeg));
        const float aspect = static_cast<float>(app.sceneW) / static_cast<float>(app.sceneH);
        const glm::mat4 view = app.cam.view();
        const glm::mat4 vp = app.cam.proj(aspect) * view;
        // Rows of the view rotation are the camera axes (GLM column-major).
        const glm::vec3 camR = glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
        const glm::vec3 camU = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));
        const glm::vec3 camF = glm::normalize(glm::vec3(-view[0][2], -view[1][2], -view[2][2]));

        // 1. Geodesic pass.
        glBindFramebuffer(GL_FRAMEBUFFER, app.scene.id);
        glViewport(0, 0, app.sceneW, app.sceneH);
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        app.geodesic.use();
        app.geodesic.setVec2("uResolution", static_cast<float>(app.sceneW),
                             static_cast<float>(app.sceneH));
        app.geodesic.setVec3("uCamPos", camPos.x, camPos.y, camPos.z);
        app.geodesic.setVec3("uCamRight", camR.x, camR.y, camR.z);
        app.geodesic.setVec3("uCamUp", camU.x, camU.y, camU.z);
        app.geodesic.setVec3("uCamForward", camF.x, camF.y, camF.z);
        app.geodesic.setFloat("uTanHalfFov", tanHalf);
        app.geodesic.setFloat("uTime", app.time);
        app.geodesic.setFloat("uMass", app.mass);
        app.geodesic.setInt("uEnableDisk", app.showDisk ? 1 : 0);
        app.geodesic.setInt("uEnableHalo", app.showHalo ? 1 : 0);
        app.geodesic.setInt("uEnableStars", app.showStars ? 1 : 0);
        app.geodesic.setInt("uMaxSteps", app.maxSteps);
        app.geodesic.setFloat("uDiskOuter", app.diskOuter * app.mass);
        glBindVertexArray(app.emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 2. Spacetime fabric + sample light rays.
        if (app.showGrid && app.fabric.count > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            app.grid.use();
            app.grid.setMat4("uViewProj", glm::value_ptr(vp));
            app.grid.setVec3("uCamPos", camPos.x, camPos.y, camPos.z);
            app.grid.setFloat("uMass", app.mass);
            glBindVertexArray(app.fabric.vao);
            glDrawArrays(GL_LINES, 0, app.fabric.count);
            glDisable(GL_BLEND);
        }

        // 3. Bloom extract + separable blur.
        if (app.showBloom) {
            glBindFramebuffer(GL_FRAMEBUFFER, app.bright.id);
            glViewport(0, 0, app.bright.w, app.bright.h);
            app.extract.use();
            app.extract.setInt("uScene", 0);
            app.extract.setFloat("uThreshold", 1.55f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, app.scene.color);
            glBindVertexArray(app.emptyVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            GLuint src = app.bright.color;
            for (int i = 0; i < 3; ++i) {
                glBindFramebuffer(GL_FRAMEBUFFER, app.ping.id);
                glViewport(0, 0, app.ping.w, app.ping.h);
                app.blur.use();
                app.blur.setInt("uImage", 0);
                app.blur.setVec2("uDirection", 1.0f, 0.0f);
                glBindTexture(GL_TEXTURE_2D, src);
                glDrawArrays(GL_TRIANGLES, 0, 3);

                glBindFramebuffer(GL_FRAMEBUFFER, app.pong.id);
                app.blur.setVec2("uDirection", 0.0f, 1.0f);
                glBindTexture(GL_TEXTURE_2D, app.ping.color);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                src = app.pong.color;
            }
        }

        // 4. Tonemap to the window. Use the window size, not the retina
        // drawable — on this macOS/GLFW combo the 2x backbuffer is not
        // scaled, so a framebuffer-sized viewport would show only the
        // lower-left quadrant.
        int winW = 0, winH = 0;
        glfwGetWindowSize(app.window, &winW, &winH);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, std::max(winW, 1), std::max(winH, 1));
        app.composite.use();
        app.composite.setInt("uScene", 0);
        app.composite.setInt("uBloom", 1);
        app.composite.setFloat("uBloomStrength", app.showBloom ? app.bloomStrength : 0.0f);
        app.composite.setFloat("uExposure", app.exposure);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app.scene.color);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, app.showBloom ? app.pong.color : app.scene.color);
        glBindVertexArray(app.emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(app.window);
        glfwPollEvents();
    }

    app.fabric.destroy();
    app.scene.destroy();
    app.bright.destroy();
    app.ping.destroy();
    app.pong.destroy();
    app.geodesic.destroy();
    app.grid.destroy();
    app.extract.destroy();
    app.blur.destroy();
    app.composite.destroy();
    if (app.emptyVao) glDeleteVertexArrays(1, &app.emptyVao);
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
