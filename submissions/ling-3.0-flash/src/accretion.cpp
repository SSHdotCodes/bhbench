#include "accretion.h"

#include "accretion.h"

AccretionDisk::AccretionDisk(BlackHole* blackhole) : bh(blackhole) {
    RNG rng;
    for (int i = 0; i < particleCount; i++) {
        double normalizedR = 0.1 + rng() * 0.9;
        double r = bh->accretionInnerRadius + normalizedR *
            (bh->accretionOuterRadius - bh->accretionInnerRadius);
        double theta = rng() * 2.0 * M_PI;
        double h = (rng() - 0.5) * 0.15 * std::max(0.1, 1.0 - normalizedR);

        particles.emplace_back(
            r * std::cos(theta),
            h,
            r * std::sin(theta)
        );

        double temp = 1.0 - normalizedR;
        double brightness = temp * temp;
        Vec3 diskColor;
        if (temp > 0.85) {
            diskColor = Vec3(1.0, 0.95, 0.8) * brightness;
        } else if (temp > 0.6) {
            diskColor = Vec3(1.0, 0.6, 0.1) * brightness;
        } else if (temp > 0.35) {
            diskColor = Vec3(1.0, 0.3, 0.0) * brightness;
        } else {
            diskColor = Vec3(0.8, 0.1, 0.0) * brightness;
        }
        colors.push_back(diskColor);
        radii.push_back(r);
        angles.push_back(theta);
        heights.push_back(h);
        speeds.push_back(std::sqrt(bh->mass / r) * 0.3);
    }
}

void AccretionDisk::update(double dt) {
    for (int i = 0; i < particleCount; i++) {
        angles[i] += speeds[i] * dt;
        double r = radii[i];
        double theta = angles[i];
        double h = heights[i];

        h += std::sin(angles[i] * 3.0 + i * 0.1) * dt * 0.002;

        particles[i] = Vec3(r * std::cos(theta), h, r * std::sin(theta));
    }
}