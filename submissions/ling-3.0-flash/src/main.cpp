#include "common.h"
#include "renderer.h"

int main() {
    Renderer renderer;

    if (!renderer.init()) {
        fprintf(stderr, "Failed to initialize renderer\n");
        return -1;
    }

    renderer.spacetime.gridSize = 40.0;
    renderer.spacetime.gridSpacing = 2.0;
    renderer.spacetime.gridHeight = 0.5;

    renderer.accretion.particleCount = 15000;

    double lastTime = glfwGetTime();
    double accumulator = 0.0;
    double targetDelta = 1.0 / 60.0;

    while (!renderer.shouldClose()) {
        double currentTime = glfwGetTime();
        double frameTime = currentTime - lastTime;
        lastTime = currentTime;

        accumulator += frameTime;
        if (accumulator > 0.2) accumulator = 0.2;

        while (accumulator >= targetDelta) {
            renderer.render();
            accumulator -= targetDelta;
        }

        renderer.updateCamera();
        renderer.renderFrame();
    }

    glfwTerminate();
    return 0;
}