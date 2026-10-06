#pragma once
#include "common.h"

class Starfield {
public:
    std::vector<Vec3> stars;
    std::vector<Vec3> colors;
    std::vector<double> brightnesses;
    int count = 5000;

    Starfield();
};
