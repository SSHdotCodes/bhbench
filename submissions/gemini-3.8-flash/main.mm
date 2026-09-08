#define GLFW_INCLUDE_NONE
#import <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cmath>

#include "ShaderTypes.h"
#include "MetalRenderer.h"
#include "Camera.h"

// -----------------------------------------------------------------------------
// Global Application State & Controls
// -----------------------------------------------------------------------------
struct AppState {
    Camera camera;
    BlackHoleParams params;
    
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    bool showHUD = true;
    bool autoRotate = true;
    float rotateSpeed = 0.15f;

    double fps = 60.0;
    int frameCount = 0;
    double lastFpsTime = 0.0;
} gApp;

// -----------------------------------------------------------------------------
// Calculate ISCO & Horizon for Kerr Metric
// -----------------------------------------------------------------------------
void updateKerrParameters(BlackHoleParams& p) {
    float M = p.mass;
    float a = std::min(std::max(p.spin, 0.0f), 0.998f);
    p.spin = a;
    
    // Event horizon radius r_+ = M + sqrt(M^2 - a^2)
    p.rH = M + std::sqrt(std::max(M * M - a * a, 0.0f));

    // Innermost Stable Circular Orbit (ISCO) for prograde equatorial orbits:
    // Z1 = 1 + (1 - a^2/M^2)^(1/3) * ((1 + a/M)^(1/3) + (1 - a/M)^(1/3))
    // Z2 = sqrt(3 * a^2/M^2 + Z1^2)
    // r_ISCO = M * (3 + Z2 - sqrt((3 - Z1) * (3 + Z1 + 2 * Z2)))
    float a_over_M = a / M;
    float z1 = 1.0f + std::cbrt(1.0f - a_over_M * a_over_M) * (std::cbrt(1.0f + a_over_M) + std::cbrt(1.0f - a_over_M));
    float z2 = std::sqrt(3.0f * a_over_M * a_over_M + z1 * z1);
    float isco = M * (3.0f + z2 - std::sqrt(std::max((3.0f - z1) * (3.0f + z1 + 2.0f * z2), 0.0f)));
    
    p.rISCO = isco;
    p.diskInner = isco;
}

// -----------------------------------------------------------------------------
// GLFW Callbacks
// -----------------------------------------------------------------------------
static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    MetalRenderer* renderer = (MetalRenderer*)glfwGetWindowUserPointer(window);
    if (renderer) {
        renderer->resize(width, height);
    }
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    double dx = xpos - gApp.lastMouseX;
    double dy = ypos - gApp.lastMouseY;
    gApp.lastMouseX = xpos;
    gApp.lastMouseY = ypos;

    if (gApp.leftMouseDown) {
        gApp.camera.orbit(-dx * 0.005f, -dy * 0.005f);
        gApp.autoRotate = false; // pause auto rotation when user interacts
    } else if (gApp.rightMouseDown) {
        gApp.camera.pan(-dx * 0.02f, dy * 0.02f);
    }
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gApp.leftMouseDown = (action == GLFW_PRESS);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        gApp.rightMouseDown = (action == GLFW_PRESS);
    }
}

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    gApp.camera.zoom(-yoffset * 0.8f);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_H:
            if (action == GLFW_PRESS) gApp.showHUD = !gApp.showHUD;
            break;
        case GLFW_KEY_G:
            if (action == GLFW_PRESS) gApp.params.renderGrid = !gApp.params.renderGrid;
            break;
        case GLFW_KEY_D:
            if (action == GLFW_PRESS) gApp.params.renderDisk = !gApp.params.renderDisk;
            break;
        case GLFW_KEY_L:
            if (action == GLFW_PRESS) gApp.params.renderLensing = !gApp.params.renderLensing;
            break;
        case GLFW_KEY_SPACE:
            if (action == GLFW_PRESS) gApp.autoRotate = !gApp.autoRotate;
            break;
        case GLFW_KEY_C:
            if (action == GLFW_PRESS) {
                gApp.params.colorMode = (gApp.params.colorMode + 1) % 3;
            }
            break;
        case GLFW_KEY_UP:
            gApp.params.spin = std::min(gApp.params.spin + 0.05f, 0.998f);
            updateKerrParameters(gApp.params);
            break;
        case GLFW_KEY_DOWN:
            gApp.params.spin = std::max(gApp.params.spin - 0.05f, 0.0f);
            updateKerrParameters(gApp.params);
            break;
        case GLFW_KEY_LEFT:
            gApp.params.diskOuter = std::max(gApp.params.diskOuter - 1.0f, 6.0f);
            break;
        case GLFW_KEY_RIGHT:
            gApp.params.diskOuter = std::min(gApp.params.diskOuter + 1.0f, 25.0f);
            break;
        case GLFW_KEY_EQUAL: // '+' key
            gApp.params.gridHeightOffset += 0.2f;
            break;
        case GLFW_KEY_MINUS:
            gApp.params.gridHeightOffset -= 0.2f;
            break;
        case GLFW_KEY_R:
            // Reset camera
            gApp.camera.setDistance(16.0f);
            gApp.camera.setElevation(0.26f);
            gApp.camera.setAzimuth(0.0f);
            gApp.params.spin = 0.90f;
            gApp.params.renderGrid = 1;
            gApp.params.renderDisk = 1;
            gApp.params.renderLensing = 1;
            gApp.params.gridHeightOffset = -1.2f;
            updateKerrParameters(gApp.params);
            break;
        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// Format HUD text
// -----------------------------------------------------------------------------
std::string buildHUDString(const AppState& app) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "FPS: " << app.fps << " (Realtime Metal Compute)\n\n";
    ss << "METRIC: Kerr Black Hole\n";
    ss << "Mass (M):        " << app.params.mass << " M_sol\n";
    ss << "Kerr Spin (a):   " << app.params.spin << " (J/M^2)\n";
    ss << "Event Hor. r+:   " << app.params.rH << " M\n";
    ss << "ISCO Radius:     " << app.params.rISCO << " M\n";
    ss << "Cam Distance:    " << app.camera.getDistance() << " M\n\n";
    
    ss << "COMPONENTS & PHYSICS:\n";
    ss << "[L] Grav Lensing:     " << (app.params.renderLensing ? "ON (GR Geodesics)" : "OFF (Flat Space)") << "\n";
    ss << "[G] Spacetime Grid:   " << (app.params.renderGrid ? "ON (Flamm Trapdoor)" : "OFF") << "\n";
    ss << "[D] Accretion Disk:   " << (app.params.renderDisk ? "ON (Relativistic)" : "OFF") << "\n";
    
    std::string modeStr = "Blackbody + Doppler";
    if (app.params.colorMode == 1) modeStr = "Temperature Map";
    else if (app.params.colorMode == 2) modeStr = "g-Factor Red/Blue Shift";
    ss << "[C] Disk Spectrum:    " << modeStr << "\n";
    ss << "[SPACE] Orbit Cam:    " << (app.autoRotate ? "ON" : "OFF") << "\n\n";

    ss << "KEYBOARD SHORTCUTS:\n";
    ss << "UP / DOWN:    Adjust Kerr Spin a\n";
    ss << "LEFT / RIGHT: Accretion Disk Outer Radius\n";
    ss << "+ / - :       Spacetime Grid Height Offset\n";
    ss << "LMB Drag:     Orbit Camera / Look\n";
    ss << "Scroll Wheel: Zoom In / Out\n";
    ss << "R: Reset  |  H: Toggle HUD";

    return ss.str();
}

// -----------------------------------------------------------------------------
// Main Application Entry
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Check for headless or test flag
    bool testRun = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--test") {
            testRun = true;
        }
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    int windowWidth = 1280;
    int windowHeight = 720;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, 
        "General Relativity Black Hole Simulation (Realtime Metal GR Ray-Tracing)", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    // Set up Cocoa Metal layer
    NSWindow* nswin = glfwGetCocoaWindow(window);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        std::cerr << "Metal is not supported on this device\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = YES;
    nswin.contentView.layer = metalLayer;
    nswin.contentView.wantsLayer = YES;

    // Initialize renderer
    MetalRenderer renderer;
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    
    if (!renderer.init(metalLayer, fbWidth, fbHeight)) {
        std::cerr << "Failed to initialize Metal renderer\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    // Initial Physics Parameters
    gApp.params.mass = 1.0f;
    gApp.params.spin = 0.90f; // Rapidly spinning Kerr black hole
    gApp.params.diskOuter = 14.0f;
    gApp.params.diskAlpha = 0.85f;
    gApp.params.diskTempScale = 1.0f;
    gApp.params.renderGrid = 1;
    gApp.params.renderDisk = 1;
    gApp.params.renderLensing = 1;
    gApp.params.gridHeightOffset = -1.2f;
    gApp.params.stepSizeScale = 1.0f;
    gApp.params.maxSteps = 350.0f;
    gApp.params.colorMode = 0;
    gApp.params.time = 0.0f;

    updateKerrParameters(gApp.params);

    std::cout << "========================================================\n";
    std::cout << " General Relativity Black Hole Realtime Metal Simulator  \n";
    std::cout << "========================================================\n";
    std::cout << "Device: " << [[device name] UTF8String] << "\n";
    std::cout << "Initial Spin a: " << gApp.params.spin << " M\n";
    std::cout << "Horizon r+: " << gApp.params.rH << " M, ISCO: " << gApp.params.rISCO << " M\n";
    std::cout << "Resolution: " << fbWidth << "x" << fbHeight << "\n";
    std::cout << "Controls:\n";
    std::cout << "  - Mouse Drag: Orbit Camera\n";
    std::cout << "  - Mouse Wheel: Zoom\n";
    std::cout << "  - Up/Down: Adjust Kerr Spin\n";
    std::cout << "  - L: Toggle Gravitational Lensing\n";
    std::cout << "  - G: Toggle Spacetime Curvature Grid (Flamm Paraboloid Trapdoor)\n";
    std::cout << "  - D: Toggle Accretion Disk\n";
    std::cout << "  - C: Cycle Color Spectrum Mode\n";
    std::cout << "  - Space: Toggle Auto-Orbit\n";
    std::cout << "  - H: Toggle HUD Overlay\n";
    std::cout << "========================================================\n";

    auto lastTime = std::chrono::high_resolution_clock::now();
    gApp.lastFpsTime = glfwGetTime();

    int testFrames = 0;

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Auto-orbit camera slowly
        if (gApp.autoRotate) {
            gApp.camera.orbit(gApp.rotateSpeed * deltaTime, 0.0f);
        }

        gApp.params.time += deltaTime;

        // Update camera matrices in params
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        gApp.params.resolution = simd_make_float2(fbWidth, fbHeight);
        gApp.params.fov = gApp.camera.getFov();
        gApp.params.camPos = gApp.camera.getPosition();
        gApp.params.camForward = gApp.camera.getForward();
        gApp.params.camUp = gApp.camera.getUp();
        gApp.params.camRight = gApp.camera.getRight();

        // FPS tracking
        gApp.frameCount++;
        double curTime = glfwGetTime();
        if (curTime - gApp.lastFpsTime >= 0.5) {
            gApp.fps = gApp.frameCount / (curTime - gApp.lastFpsTime);
            gApp.frameCount = 0;
            gApp.lastFpsTime = curTime;
        }

        // Update HUD
        std::string hudText = buildHUDString(gApp);
        renderer.updateHUD(hudText, gApp.showHUD);

        // Render frame
        renderer.render(gApp.params);

        if (testRun) {
            testFrames++;
            if (testFrames > 60) {
                std::cout << "Test completed successfully: rendered 60 frames in realtime at " << gApp.fps << " FPS.\n";
                break;
            }
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
