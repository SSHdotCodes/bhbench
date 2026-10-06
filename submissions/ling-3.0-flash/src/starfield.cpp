#include "starfield.h"

Starfield::Starfield() {
    RNG rng;
    for (int i = 0; i < count; i++) {
        double theta = rng() * 2.0 * M_PI;
        double phi = std::acos(2.0 * rng() - 1.0);
        double r = 300.0 + rng() * 500.0;

        stars.emplace_back(
            r * std::sin(phi) * std::cos(theta),
            r * std::sin(phi) * std::sin(theta),
            r * std::cos(phi)
        );

        double temp = rng();
        if (temp < 0.1) {
            colors.emplace_back(1.0, 0.7, 0.4);
        } else if (temp < 0.25) {
            colors.emplace_back(1.0, 0.95, 0.85);
        } else if (temp < 0.5) {
            colors.emplace_back(0.7, 0.85, 1.0);
        } else if (temp < 0.7) {
            colors.emplace_back(0.9, 0.9, 1.0);
        } else {
            colors.emplace_back(1.0, 1.0, 1.0);
        }

        brightnesses.push_back(0.3 + rng() * 0.7);
    }
}