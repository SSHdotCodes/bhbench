#pragma once
#include "common.h"
#include "blackhole.h"

class AccretionDisk {
public:
    BlackHole* bh;
    int particleCount = 15000;
    std::vector<Vec3> particles;
    std::vector<Vec3> colors;
    std::vector<double> radii;
    std::vector<double> angles;
    std::vector<double> heights;
    std::vector<double> speeds;

    AccretionDisk() : bh(nullptr), particleCount(0) {}
    AccretionDisk(BlackHole* blackhole);
    void update(double dt);
};
