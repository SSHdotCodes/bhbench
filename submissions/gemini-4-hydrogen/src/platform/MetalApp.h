#pragma once

#include <string>

namespace bh {

struct LaunchOptions {
    std::string capturePath;   // If non-empty, renders a frame via Metal+MPS and writes PNG, then exits
    int capturePreset = 0;     // Camera preset to use for capture (0..3)
    double captureSpin = 0.90; // Spin a/M for capture
    bool captureGrid = true;
    bool captureDisk = true;
    int width = 1440;
    int height = 900;
};

int runMetalApplication(const LaunchOptions& opts);

} // namespace bh
