#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include <fstream>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "physics.hpp"
#include "shaders.hpp"
#include "spacetime_grid.hpp"
#include "hud.hpp"

namespace {

// ============================================================================
// Camera State (Spherical Orbit around the Black Hole)
// ============================================================================
struct OrbitCamera {
    float yaw = 0.35f;          // Azimuthal angle (radians)
    float pitch = 0.085f;       // Elevation above equatorial plane (radians)
    float distance = 24.5f;     // Radial distance in units of M
    float fovDeg = 46.0f;       // Vertical field of view
    glm::vec3 target{0.0f, -1.15f, 0.0f}; // Frames both disk/halos and the spacetime trapdoor funnel

    bool dragging = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool autoOrbit = false;

    glm::vec3 position(float M) const {
        float r = distance * M;
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        float cy = std::cos(yaw);
        float sy = std::sin(yaw);
        // Orbit around the black hole origin (0,0,0) so pitch is true equatorial inclination
        return glm::vec3(r * cp * sy, r * sp, r * cp * cy);
    }

    void basis(float M, glm::vec3& pos, glm::vec3& right, glm::vec3& up, glm::vec3& fwd) const {
        pos = position(M);
        fwd = glm::normalize(target * M - pos);
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        right = glm::normalize(glm::cross(fwd, worldUp));
        up = glm::normalize(glm::cross(right, fwd));
    }
};

struct AppState {
    bh::BlackHoleParams params;
    OrbitCamera cam;
    float renderScale = 0.75f; // Real-time high-FPS scale on Retina displays (toggle with 'B')
    bool meshDirty = true;
    std::string activePresetName = "GARGANTUA + TRAPDOOR HYBRID";
};

AppState gApp;

// ============================================================================
// Shader Compilation Utilities
// ============================================================================
GLuint compileShader(GLenum type, const char* src, const char* debugName) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[Shader Compile Error - " << debugName << "]:\n" << log << std::endl;
    }
    return shader;
}

GLuint createProgram(const char* vertSrc, const char* fragSrc, const char* debugName) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc, debugName);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc, debugName);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[Program Link Error - " << debugName << "]:\n" << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ============================================================================
// Offscreen HDR Framebuffer Setup
// ============================================================================
struct FramebufferHDR {
    GLuint fbo = 0;
    GLuint colorTex = 0;
    int width = 0;
    int height = 0;

    void init(int w, int h) {
        cleanup();
        width = std::max(1, w);
        height = std::max(1, h);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void cleanup() {
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    }
};

// ============================================================================
// Presets & Input Callbacks
// ============================================================================
void applyPreset(int presetId) {
    auto& p = gApp.params;
    auto& c = gApp.cam;
    if (presetId == 6) {
        // Gargantua / Interstellar Near-Equatorial View + Trapdoor
        p.spin = 0.88f;
        p.diskTempKelvin = 5400.0f;
        p.diskIntensity = 1.95f;
        p.dopplerStrength = 1.0f;
        p.haloStrength = 1.35f;
        p.trapdoorDepth = 1.20f;
        p.showSpacetimeGrid = true;
        p.showGeodesicBeams = true;
        p.showRelativisticJets = false;
        p.showAccretionDisk = true;
        c.pitch = 0.16f;
        c.yaw = 0.32f;
        c.distance = 23.0f;
        c.target = glm::vec3(0.0f, -1.25f, 0.0f);
        gApp.activePresetName = "6: INTERSTELLAR GARGANTUA (a*=0.88)";
    } else if (presetId == 7) {
        // Deep "Trapdoor in Spacetime" Curvature & Geodesic View
        p.spin = 0.65f;
        p.diskTempKelvin = 5000.0f;
        p.diskIntensity = 1.65f;
        p.trapdoorDepth = 1.35f;
        p.showSpacetimeGrid = true;
        p.showGeodesicBeams = true;
        p.showRelativisticJets = false;
        p.showAccretionDisk = true;
        c.pitch = 0.28f;
        c.yaw = 0.48f;
        c.distance = 27.5f;
        c.target = glm::vec3(0.0f, -2.8f, 0.0f);
        gApp.activePresetName = "7: TRAPDOOR IN SPACETIME FUNNEL";
    } else if (presetId == 8) {
        // EHT M87* Relativistic Jet & Photon Ring View
        p.spin = 0.94f;
        p.diskTempKelvin = 5800.0f;
        p.diskIntensity = 1.95f;
        p.haloStrength = 1.50f;
        p.trapdoorDepth = 1.15f;
        p.showSpacetimeGrid = true;
        p.showGeodesicBeams = false;
        p.showRelativisticJets = true;
        p.showAccretionDisk = true;
        c.pitch = 0.18f;
        c.yaw = -0.38f;
        c.distance = 25.5f;
        c.target = glm::vec3(0.0f, -0.6f, 0.0f);
        gApp.activePresetName = "8: EHT M87* JET + PHOTON RING (a*=0.94)";
    } else if (presetId == 9) {
        // Exact Schwarzschild (Non-Rotating a*=0)
        p.spin = 0.0f;
        p.diskTempKelvin = 4900.0f;
        p.diskIntensity = 1.85f;
        p.haloStrength = 1.30f;
        p.trapdoorDepth = 1.25f;
        p.showSpacetimeGrid = true;
        p.showGeodesicBeams = true;
        p.showRelativisticJets = false;
        p.showAccretionDisk = true;
        c.pitch = 0.09f;
        c.yaw = 0.20f;
        c.distance = 24.5f;
        c.target = glm::vec3(0.0f, -1.15f, 0.0f);
        gApp.activePresetName = "9: SCHWARZSCHILD EXACT (a*=0.00)";
    } else if (presetId == 0) {
        // Thorne Extreme Kerr Limit (a*=0.995)
        p.spin = 0.995f;
        p.diskTempKelvin = 6200.0f;
        p.diskIntensity = 2.0f;
        p.haloStrength = 1.65f;
        p.trapdoorDepth = 1.35f;
        p.showSpacetimeGrid = true;
        p.showGeodesicBeams = true;
        p.showRelativisticJets = true;
        p.showAccretionDisk = true;
        c.pitch = 0.08f;
        c.yaw = 0.15f;
        c.distance = 20.0f;
        c.target = glm::vec3(0.0f, -0.95f, 0.0f);
        gApp.activePresetName = "0: EXTREME KERR D-SHADOW (a*=0.995)";
    }
    gApp.meshDirty = true;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            gApp.cam.dragging = true;
            glfwGetCursorPos(window, &gApp.cam.lastMouseX, &gApp.cam.lastMouseY);
        } else if (action == GLFW_RELEASE) {
            gApp.cam.dragging = false;
        }
    }
}

void cursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (!gApp.cam.dragging) return;
    double dx = xpos - gApp.cam.lastMouseX;
    double dy = ypos - gApp.cam.lastMouseY;
    gApp.cam.lastMouseX = xpos;
    gApp.cam.lastMouseY = ypos;

    gApp.cam.yaw -= static_cast<float>(dx) * 0.0055f;
    gApp.cam.pitch = std::clamp(gApp.cam.pitch + static_cast<float>(dy) * 0.0055f, -1.48f, 1.48f);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    gApp.cam.distance = std::clamp(gApp.cam.distance * (1.0f - static_cast<float>(yoffset) * 0.08f), 4.2f, 55.0f);
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    auto& p = gApp.params;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_1:
            p.showSpacetimeGrid = !p.showSpacetimeGrid;
            break;
        case GLFW_KEY_2:
            p.showGeodesicBeams = !p.showGeodesicBeams;
            gApp.meshDirty = true;
            break;
        case GLFW_KEY_3:
            p.showLensedGrid = !p.showLensedGrid;
            break;
        case GLFW_KEY_4:
            p.showAccretionDisk = !p.showAccretionDisk;
            break;
        case GLFW_KEY_5:
            p.showRelativisticJets = !p.showRelativisticJets;
            break;
        case GLFW_KEY_6: applyPreset(6); break;
        case GLFW_KEY_7: applyPreset(7); break;
        case GLFW_KEY_8: applyPreset(8); break;
        case GLFW_KEY_9: applyPreset(9); break;
        case GLFW_KEY_0: applyPreset(0); break;
        case GLFW_KEY_SPACE:
            p.animateTime = !p.animateTime;
            break;
        case GLFW_KEY_O:
            gApp.cam.autoOrbit = !gApp.cam.autoOrbit;
            break;
        case GLFW_KEY_H:
            p.showHUD = !p.showHUD;
            break;
        case GLFW_KEY_D:
            if (p.dopplerStrength < 0.5f) p.dopplerStrength = 1.0f;
            else if (p.dopplerStrength < 1.15f) p.dopplerStrength = 1.35f;
            else p.dopplerStrength = 0.0f;
            break;
        case GLFW_KEY_B:
            if (gApp.renderScale < 0.65f) gApp.renderScale = 0.75f;
            else if (gApp.renderScale < 0.90f) gApp.renderScale = 1.00f;
            else gApp.renderScale = 0.55f;
            break;
        case GLFW_KEY_Q:
            p.spin = std::clamp(p.spin - 0.04f, -0.995f, 0.995f);
            gApp.meshDirty = true;
            break;
        case GLFW_KEY_E:
            p.spin = std::clamp(p.spin + 0.04f, -0.995f, 0.995f);
            gApp.meshDirty = true;
            break;
        case GLFW_KEY_T:
            p.diskTempKelvin = std::clamp(p.diskTempKelvin + 400.0f, 2500.0f, 22000.0f);
            break;
        case GLFW_KEY_G:
            p.diskTempKelvin = std::clamp(p.diskTempKelvin - 400.0f, 2500.0f, 22000.0f);
            break;
        case GLFW_KEY_R:
            p.trapdoorDepth = std::clamp(p.trapdoorDepth + 0.10f, 0.20f, 2.50f);
            gApp.meshDirty = true;
            break;
        case GLFW_KEY_F:
            p.trapdoorDepth = std::clamp(p.trapdoorDepth - 0.10f, 0.20f, 2.50f);
            gApp.meshDirty = true;
            break;
        default:
            break;
    }
}

void processContinuousKeys(GLFWwindow* window, float dt) {
    float rotSpeed = 1.15f * dt;
    float zoomSpeed = 12.0f * dt;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        gApp.cam.yaw += rotSpeed;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) gApp.cam.yaw -= rotSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        gApp.cam.pitch = std::clamp(gApp.cam.pitch + rotSpeed * 0.8f, -1.48f, 1.48f);
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        gApp.cam.pitch = std::clamp(gApp.cam.pitch - rotSpeed * 0.8f, -1.48f, 1.48f);
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
        gApp.cam.distance = std::clamp(gApp.cam.distance - zoomSpeed, 4.2f, 55.0f);
    if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
        gApp.cam.distance = std::clamp(gApp.cam.distance + zoomSpeed, 4.2f, 55.0f);
}

std::string fmtFloat(float v, int prec = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

void saveFramebufferPPM(const std::string& path, int w, int h) {
    std::vector<unsigned char> pixels(w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream ofs(path, std::ios::binary);
    ofs << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) {
        ofs.write(reinterpret_cast<const char*>(&pixels[y * w * 3]), w * 3);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string capturePath;
    int maxFrames = -1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::stoi(argv[++i]);
        } else if (arg == "--preset" && i + 1 < argc) {
            applyPreset(std::stoi(argv[++i]));
        }
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

    const int initWinW = 1440;
    const int initWinH = 900;
    GLFWwindow* window = glfwCreateWindow(
        initWinW,
        initWinH,
        "Relativistic 3D Black Hole Simulation (Kerr/Schwarzschild Ray-Tracing + Accretion Halo + Spacetime Trapdoor)",
        nullptr,
        nullptr
    );
    if (!window) {
        std::cerr << "Failed to create OpenGL 4.1 window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // V-Sync for tear-free real-time rendering
    glfwShowWindow(window);
    glfwFocusWindow(window);

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    std::cout << "====================================================================\n";
    std::cout << "  RELATIVISTIC 3D BLACK HOLE SIMULATION (OpenGL 4.1 GPU Accelerated)\n";
    std::cout << "  GPU: " << glGetString(GL_RENDERER) << "\n";
    std::cout << "  OpenGL: " << glGetString(GL_VERSION) << "\n";
    std::cout << "====================================================================\n";

    // Compile GPU Shaders
    GLuint raytraceProg  = createProgram(bh::QUAD_VERT_SHADER, bh::RAYTRACE_FRAG_SHADER, "RayTraceKerr");
    GLuint gridProg      = createProgram(bh::GRID_VERT_SHADER, bh::GRID_FRAG_SHADER, "SpacetimeTrapdoorGrid");
    GLuint bloomProg     = createProgram(bh::QUAD_VERT_SHADER, bh::BLOOM_FRAG_SHADER, "HDRBloom");
    GLuint compositeProg = createProgram(bh::QUAD_VERT_SHADER, bh::COMPOSITE_FRAG_SHADER, "ACESComposite");
    GLuint hudProg       = createProgram(bh::HUD_VERT_SHADER,  bh::HUD_FRAG_SHADER, "ScientificHUD");

    // Fullscreen Quad VAO
    float quadVerts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };
    GLuint quadVAO = 0, quadVBO = 0;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    // 3D Spacetime Trapdoor Grid VAOs (Surface Triangles + Coordinate/Geodesic Lines)
    GLuint gridSurfVAO = 0, gridSurfVBO = 0;
    GLuint gridLineVAO = 0, gridLineVBO = 0;
    glGenVertexArrays(1, &gridSurfVAO);
    glGenBuffers(1, &gridSurfVBO);
    glGenVertexArrays(1, &gridLineVAO);
    glGenBuffers(1, &gridLineVBO);

    auto setupGridVAO = [](GLuint vao, GLuint vbo) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(bh::GridVertex), reinterpret_cast<void*>(offsetof(bh::GridVertex, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(bh::GridVertex), reinterpret_cast<void*>(offsetof(bh::GridVertex, color)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(bh::GridVertex), reinterpret_cast<void*>(offsetof(bh::GridVertex, meta)));
    };
    setupGridVAO(gridSurfVAO, gridSurfVBO);
    setupGridVAO(gridLineVAO, gridLineVBO);

    // 2D HUD VAO
    GLuint hudVAO = 0, hudVBO = 0;
    glGenVertexArrays(1, &hudVAO);
    glGenBuffers(1, &hudVBO);
    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(bh::HudVertex), reinterpret_cast<void*>(offsetof(bh::HudVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(bh::HudVertex), reinterpret_cast<void*>(offsetof(bh::HudVertex, color)));

    bh::SpacetimeMeshData meshData;
    std::vector<bh::HudVertex> hudVerts;
    hudVerts.reserve(16384);

    FramebufferHDR sceneFBO;
    FramebufferHDR bloomPingFBO;
    FramebufferHDR bloomPongFBO;

    int curSceneW = 0, curSceneH = 0;
    float simTime = 0.0f;
    auto prevClock = std::chrono::high_resolution_clock::now();
    float smoothedFPS = 60.0f;
    int frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto nowClock = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(nowClock - prevClock).count();
        prevClock = nowClock;
        dt = std::clamp(dt, 0.0005f, 0.1f);
        smoothedFPS = glm::mix(smoothedFPS, 1.0f / dt, 0.08f);

        processContinuousKeys(window, dt);
        if (gApp.params.animateTime) {
            simTime += dt;
        }
        if (gApp.cam.autoOrbit) {
            gApp.cam.yaw += 0.22f * dt;
        }

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) continue;

        int targetSceneW = std::max(320, static_cast<int>(fbW * gApp.renderScale));
        int targetSceneH = std::max(200, static_cast<int>(fbH * gApp.renderScale));
        if (targetSceneW != curSceneW || targetSceneH != curSceneH) {
            curSceneW = targetSceneW;
            curSceneH = targetSceneH;
            sceneFBO.init(curSceneW, curSceneH);
            bloomPingFBO.init(curSceneW / 2, curSceneH / 2);
            bloomPongFBO.init(curSceneW / 2, curSceneH / 2);
        }

        // Rebuild 3D Spacetime Curvature ("Trapdoor in Spacetime") mesh if parameters changed
        if (gApp.meshDirty) {
            bh::buildSpacetimeTrapdoorMesh(meshData, gApp.params, simTime);
            glBindBuffer(GL_ARRAY_BUFFER, gridSurfVBO);
            glBufferData(GL_ARRAY_BUFFER,
                         meshData.surfaceTris.size() * sizeof(bh::GridVertex),
                         meshData.surfaceTris.data(),
                         GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, gridLineVBO);
            glBufferData(GL_ARRAY_BUFFER,
                         meshData.gridLines.size() * sizeof(bh::GridVertex),
                         meshData.gridLines.data(),
                         GL_DYNAMIC_DRAW);
            gApp.meshDirty = false;
        }

        const auto& p = gApp.params;
        float M = p.mass;
        float rPlus = bh::kerrEventHorizon(M, p.spin);
        float rISCO = bh::kerrISCO(M, p.spin) * p.diskInnerMult;
        float rPhPro = bh::kerrPhotonSpherePrograde(M, p.spin);
        float rPhRetro = bh::kerrPhotonSphereRetrograde(M, p.spin);
        float bCritPro = bh::kerrCriticalImpactParam(M, p.spin, true);
        float bCritRetro = bh::kerrCriticalImpactParam(M, p.spin, false);

        glm::vec3 camPos, camRight, camUp, camFwd;
        gApp.cam.basis(M, camPos, camRight, camUp, camFwd);
        float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
        float tanHalfFov = std::tan(glm::radians(gApp.cam.fovDeg * 0.5f));

        // ====================================================================
        // PASS 1: General Relativistic Kerr-Schild RK4 Ray Tracing into HDR FBO
        // ====================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO.fbo);
        glViewport(0, 0, sceneFBO.width, sceneFBO.height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        glUseProgram(raytraceProg);
        glUniform3fv(glGetUniformLocation(raytraceProg, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniform3fv(glGetUniformLocation(raytraceProg, "uCamRight"), 1, glm::value_ptr(camRight));
        glUniform3fv(glGetUniformLocation(raytraceProg, "uCamUp"), 1, glm::value_ptr(camUp));
        glUniform3fv(glGetUniformLocation(raytraceProg, "uCamFwd"), 1, glm::value_ptr(camFwd));
        glUniform1f(glGetUniformLocation(raytraceProg, "uTanHalfFov"), tanHalfFov);
        glUniform1f(glGetUniformLocation(raytraceProg, "uAspect"), aspect);
        glUniform1f(glGetUniformLocation(raytraceProg, "uTime"), simTime);

        glUniform1f(glGetUniformLocation(raytraceProg, "uMass"), M);
        glUniform1f(glGetUniformLocation(raytraceProg, "uSpin"), p.spin);
        glUniform1f(glGetUniformLocation(raytraceProg, "uHorizonR"), rPlus);
        glUniform1f(glGetUniformLocation(raytraceProg, "uISCO"), rISCO);
        glUniform1f(glGetUniformLocation(raytraceProg, "uPhotonRPro"), rPhPro);
        glUniform1f(glGetUniformLocation(raytraceProg, "uDiskOuter"), p.diskOuterM);
        glUniform1f(glGetUniformLocation(raytraceProg, "uDiskTemp"), p.diskTempKelvin);
        glUniform1f(glGetUniformLocation(raytraceProg, "uDiskIntensity"), p.diskIntensity);
        glUniform1f(glGetUniformLocation(raytraceProg, "uDiskThickness"), p.diskThickness);
        glUniform1f(glGetUniformLocation(raytraceProg, "uDopplerStrength"), p.dopplerStrength);
        glUniform1f(glGetUniformLocation(raytraceProg, "uHaloStrength"), p.haloStrength);
        glUniform1f(glGetUniformLocation(raytraceProg, "uTrapdoorDepth"), p.trapdoorDepth);
        glUniform1f(glGetUniformLocation(raytraceProg, "uGridExtent"), p.gridExtentM);
        glUniform1i(glGetUniformLocation(raytraceProg, "uMaxSteps"), p.maxRaySteps);
        glUniform1i(glGetUniformLocation(raytraceProg, "uShowDisk"), p.showAccretionDisk ? 1 : 0);
        glUniform1i(glGetUniformLocation(raytraceProg, "uShowJets"), p.showRelativisticJets ? 1 : 0);
        glUniform1i(glGetUniformLocation(raytraceProg, "uShowLensedGrid"), p.showLensedGrid ? 1 : 0);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ====================================================================
        // PASS 2: 3D Spacetime Curvature ("Trapdoor in Spacetime") & Geodesics
        //         Rendered into HDR Scene FBO so glowing lines get HDR bloom
        // ====================================================================
        if (p.showSpacetimeGrid) {
            glm::mat4 proj = glm::perspective(glm::radians(gApp.cam.fovDeg), aspect, 0.1f * M, 180.0f * M);
            glm::mat4 view = glm::lookAt(camPos, gApp.cam.target * M, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 viewProj = proj * view;

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);

            glUseProgram(gridProg);
            glUniformMatrix4fv(glGetUniformLocation(gridProg, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform1f(glGetUniformLocation(gridProg, "uTime"), simTime);
            glUniform1f(glGetUniformLocation(gridProg, "uHorizonR"), rPlus);
            glUniform1f(glGetUniformLocation(gridProg, "uMass"), M);
            glUniform3fv(glGetUniformLocation(gridProg, "uCamPos"), 1, glm::value_ptr(camPos));

            if (!meshData.surfaceTris.empty()) {
                glBindVertexArray(gridSurfVAO);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshData.surfaceTris.size()));
            }
            if (!meshData.gridLines.empty()) {
                glBindVertexArray(gridLineVAO);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(meshData.gridLines.size()));
            }
            glDisable(GL_BLEND);
        }

        // ====================================================================
        // PASS 3: HDR Bright-Pass & Separable Gaussian Bloom
        // ====================================================================
        glBindVertexArray(quadVAO);
        glUseProgram(bloomProg);
        glUniform1i(glGetUniformLocation(bloomProg, "uSourceTex"), 0);

        // 3a. Bright-pass extract into bloomPingFBO
        glBindFramebuffer(GL_FRAMEBUFFER, bloomPingFBO.fbo);
        glViewport(0, 0, bloomPingFBO.width, bloomPingFBO.height);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneFBO.colorTex);
        glUniform2f(glGetUniformLocation(bloomProg, "uTexelSize"),
                    1.0f / bloomPingFBO.width, 1.0f / bloomPingFBO.height);
        glUniform2f(glGetUniformLocation(bloomProg, "uDirection"), 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 3b. Two iterations of Horizontal + Vertical Gaussian Blur
        for (int iter = 0; iter < 2; ++iter) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomPongFBO.fbo);
            glBindTexture(GL_TEXTURE_2D, bloomPingFBO.colorTex);
            glUniform2f(glGetUniformLocation(bloomProg, "uDirection"), 1.0f, 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glBindFramebuffer(GL_FRAMEBUFFER, bloomPingFBO.fbo);
            glBindTexture(GL_TEXTURE_2D, bloomPongFBO.colorTex);
            glUniform2f(glGetUniformLocation(bloomProg, "uDirection"), 0.0f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // ====================================================================
        // PASS 4: Final ACES Filmic Tone-Mapped Composite to Default Framebuffer
        // ====================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(compositeProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneFBO.colorTex);
        glUniform1i(glGetUniformLocation(compositeProg, "uSceneTex"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomPingFBO.colorTex);
        glUniform1i(glGetUniformLocation(compositeProg, "uBloomTex"), 1);

        glUniform1f(glGetUniformLocation(compositeProg, "uBloomStrength"), p.bloomStrength);
        glUniform1f(glGetUniformLocation(compositeProg, "uExposure"), p.exposure);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ====================================================================
        // PASS 5: Scientific Telemetry HUD & Interactive Controls Legend
        // ====================================================================
        if (p.showHUD) {
            int winW = 0, winH = 0;
            glfwGetWindowSize(window, &winW, &winH);
            if (winW > 0 && winH > 0) {
                hudVerts.clear();
                float s = 2.0f; // crisp 2x pixel scale in logical window coordinates

                // Left Telemetry Panel Background
                bh::appendRect(hudVerts, 14.0f, 14.0f, 665.0f, 272.0f, glm::vec4(0.02f, 0.04f, 0.09f, 0.78f));
                bh::appendRect(hudVerts, 14.0f, 14.0f, 665.0f, 3.0f,   glm::vec4(0.15f, 0.78f, 1.00f, 0.95f));

                bh::appendText(hudVerts, 26.0f, 26.0f, s,
                    "GENERAL RELATIVISTIC BLACK HOLE SIMULATOR",
                    glm::vec4(0.30f, 0.90f, 1.00f, 1.0f));
                bh::appendText(hudVerts, 26.0f, 46.0f, s,
                    "METRIC: KERR-SCHILD EXACT RK4 NULL GEODESICS",
                    glm::vec4(0.72f, 0.85f, 0.98f, 0.95f));

                std::string lineFPS = "FPS: " + fmtFloat(smoothedFPS, 1) +
                    "  SCALE: " + fmtFloat(gApp.renderScale * 100.0f, 0) + "%  STEPS: " + std::to_string(p.maxRaySteps);
                bh::appendText(hudVerts, 26.0f, 68.0f, s, lineFPS, glm::vec4(0.45f, 1.00f, 0.65f, 1.0f));

                std::string lineSpin = "MASS M: " + fmtFloat(M, 2) +
                    "   KERR SPIN A*: " + fmtFloat(p.spin, 3) +
                    "   RS: " + fmtFloat(2.0f * M, 2) + "M";
                bh::appendText(hudVerts, 26.0f, 90.0f, s, lineSpin, glm::vec4(0.95f, 0.96f, 1.00f, 1.0f));

                std::string lineHor = "EVENT HORIZON R+: " + fmtFloat(rPlus, 3) +
                    "M   ERGOSPHERE: " + fmtFloat(2.0f * M, 2) + "M";
                bh::appendText(hudVerts, 26.0f, 110.0f, s, lineHor, glm::vec4(1.00f, 0.38f, 0.52f, 1.0f));

                std::string linePh = "PHOTON SPHERE R_PH: " + fmtFloat(rPhPro, 2) +
                    "M (PRO) / " + fmtFloat(rPhRetro, 2) + "M (RETRO)";
                bh::appendText(hudVerts, 26.0f, 130.0f, s, linePh, glm::vec4(0.25f, 0.95f, 1.00f, 1.0f));

                std::string lineISCO = "ISCO RADIUS R_ISCO: " + fmtFloat(rISCO, 2) +
                    "M   SHADOW B_CRIT: " + fmtFloat(bCritPro, 2) + "/" + fmtFloat(bCritRetro, 2) + "M";
                bh::appendText(hudVerts, 26.0f, 150.0f, s, lineISCO, glm::vec4(0.30f, 1.00f, 0.62f, 1.0f));

                std::string lineDisk = "DISK T_MAX: " + fmtFloat(p.diskTempKelvin, 0) +
                    "K   DOPPLER g^4: " + fmtFloat(p.dopplerStrength * 100.0f, 0) + "%";
                bh::appendText(hudVerts, 26.0f, 170.0f, s, lineDisk, glm::vec4(1.00f, 0.78f, 0.35f, 1.0f));

                float inclDeg = 90.0f - glm::degrees(gApp.cam.pitch);
                std::string lineCam = "CAM DIST: " + fmtFloat(gApp.cam.distance, 1) +
                    "M   INCLINATION: " + fmtFloat(inclDeg, 1) + " DEG";
                bh::appendText(hudVerts, 26.0f, 190.0f, s, lineCam, glm::vec4(0.82f, 0.88f, 0.96f, 1.0f));

                bh::appendText(hudVerts, 26.0f, 212.0f, s,
                    "TRAPDOOR CURVATURE: Z(R) = -2 SQRT(RS*(R - R+))",
                    glm::vec4(0.75f, 0.62f, 1.00f, 1.0f));

                bh::appendText(hudVerts, 26.0f, 234.0f, s,
                    "PRESET: " + gApp.activePresetName,
                    glm::vec4(1.00f, 0.92f, 0.45f, 1.0f));

                bh::appendText(hudVerts, 26.0f, 258.0f, s,
                    "RINGS: [RED]=HORIZON [CYAN]=PHOTON [GREEN]=ISCO",
                    glm::vec4(0.85f, 0.90f, 0.95f, 0.95f));

                // Bottom-Left Interactive Controls Panel
                float ctrlY = static_cast<float>(winH) - 152.0f;
                bh::appendRect(hudVerts, 14.0f, ctrlY, 825.0f, 138.0f, glm::vec4(0.02f, 0.04f, 0.09f, 0.76f));
                bh::appendRect(hudVerts, 14.0f, ctrlY, 825.0f, 2.5f,   glm::vec4(0.45f, 0.55f, 0.95f, 0.90f));

                bh::appendText(hudVerts, 26.0f, ctrlY + 12.0f, s,
                    "CONTROLS: MOUSE DRAG / WASD = ORBIT   SCROLL / Z,X = ZOOM",
                    glm::vec4(0.92f, 0.95f, 1.00f, 0.95f));
                bh::appendText(hudVerts, 26.0f, ctrlY + 32.0f, s,
                    "[Q/E] SPIN A*   [T/G] DISK TEMP   [R/F] TRAPDOOR DEPTH   [D] DOPPLER",
                    glm::vec4(0.82f, 0.90f, 1.00f, 0.95f));
                bh::appendText(hudVerts, 26.0f, ctrlY + 52.0f, s,
                    "[1] TRAPDOOR GRID  [2] 3D GEODESIC RAYS  [3] LENSED GRID  [4] DISK",
                    glm::vec4(0.82f, 0.90f, 1.00f, 0.95f));
                bh::appendText(hudVerts, 26.0f, ctrlY + 72.0f, s,
                    "[5] POLAR JETS     [B] RES SCALE         [O] AUTO-ORBIT   [H] HUD",
                    glm::vec4(0.82f, 0.90f, 1.00f, 0.95f));
                bh::appendText(hudVerts, 26.0f, ctrlY + 94.0f, s,
                    "PRESETS: [6] GARGANTUA  [7] TRAPDOOR FUNNEL  [8] M87* JET",
                    glm::vec4(1.00f, 0.86f, 0.42f, 0.98f));
                bh::appendText(hudVerts, 26.0f, ctrlY + 114.0f, s,
                    "         [9] SCHWARZSCHILD (A*=0)  [0] EXTREME KERR (A*=0.995)",
                    glm::vec4(1.00f, 0.86f, 0.42f, 0.98f));

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUseProgram(hudProg);
                glUniform2f(glGetUniformLocation(hudProg, "uScreenSize"),
                            static_cast<float>(winW), static_cast<float>(winH));
                glBindVertexArray(hudVAO);
                glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
                glBufferData(GL_ARRAY_BUFFER,
                             hudVerts.size() * sizeof(bh::HudVertex),
                             hudVerts.data(),
                             GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(hudVerts.size()));
                glDisable(GL_BLEND);
            }
        }

        glfwSwapBuffers(window);
        frameCount++;

        if (!capturePath.empty() && frameCount == std::max(8, maxFrames)) {
            saveFramebufferPPM(capturePath, fbW, fbH);
            std::cout << "Captured verification frame to: " << capturePath << "\n";
        }
        if (maxFrames > 0 && frameCount >= maxFrames) {
            break;
        }
    }

    sceneFBO.cleanup();
    bloomPingFBO.cleanup();
    bloomPongFBO.cleanup();
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &gridSurfVAO);
    glDeleteBuffers(1, &gridSurfVBO);
    glDeleteVertexArrays(1, &gridLineVAO);
    glDeleteBuffers(1, &gridLineVBO);
    glDeleteVertexArrays(1, &hudVAO);
    glDeleteBuffers(1, &hudVBO);
    glDeleteProgram(raytraceProg);
    glDeleteProgram(gridProg);
    glDeleteProgram(bloomProg);
    glDeleteProgram(compositeProg);
    glDeleteProgram(hudProg);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
