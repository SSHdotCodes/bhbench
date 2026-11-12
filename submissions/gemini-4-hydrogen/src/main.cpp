#include "platform/MetalApp.h"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bh::LaunchOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--capture" && i + 1 < argc) {
            opts.capturePath = argv[++i];
        } else if (arg == "--preset" && i + 1 < argc) {
            opts.capturePreset = std::atoi(argv[++i]);
        } else if (arg == "--spin" && i + 1 < argc) {
            opts.captureSpin = std::atof(argv[++i]);
        } else if (arg == "--no-grid") {
            opts.captureGrid = false;
        } else if (arg == "--no-disk") {
            opts.captureDisk = false;
        } else if (arg == "--width" && i + 1 < argc) {
            opts.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            opts.height = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Kerr Black Hole Simulation (C++20 + Metal + MPS)\n"
                      << "Usage:\n"
                      << "  black_hole_sim                       Launch realtime interactive window\n"
                      << "  black_hole_sim --capture out.png     Render a frame via Metal+MPS and save PNG\n"
                      << "    --preset <0..3>                    Camera preset (0=Inclined,1=Edge,2=Polar,3=Skim)\n"
                      << "    --spin <0.0..0.998>                Dimensionless Kerr spin a/M\n"
                      << "    --no-grid / --no-disk              Disable spacetime grid or accretion disk\n"
                      << "    --width <px> --height <px>         Output resolution for capture\n";
            return 0;
        }
    }

    return bh::runMetalApplication(opts);
}
