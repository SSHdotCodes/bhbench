#include "Simulation.h"
#include <iostream>

int main() {
    try {
        Simulation sim;
        std::cout << "Starting Black Hole Simulation..." << std::endl;
        std::cout << "Features: Gravitational lensing (ray tracing), Accretion Disk with thermal emission," << std::endl;
        std::cout << "         Spacetime curvature visualization (Schwarzschild metric)," << std::endl;
        std::cout << "         Real-time OpenGL rendering with MPS acceleration support" << std::endl;
        sim.run();
    } catch (const std::exception& e) {
        std::cerr << "Simulation error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
