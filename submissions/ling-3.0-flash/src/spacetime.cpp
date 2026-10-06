#include "spacetime.h"

SpacetimeGrid::SpacetimeGrid() {
    generateGrid();
}

void SpacetimeGrid::generateGrid() {
    vertices.clear();
    indices.clear();

    int divisions = (int)(gridSize / gridSpacing);

    for (int ix = -divisions / 2; ix <= divisions / 2; ix++) {
        for (int iz = -divisions / 2; iz <= divisions / 2; iz++) {
            double x = ix * gridSpacing;
            double z = iz * gridSpacing;
            Vec3 gridPos(x, 0, z);
            vertices.push_back(gridPos);
        }
    }

    int halfDiv = divisions / 2 + 1;
    for (int ix = 0; ix < divisions; ix++) {
        for (int iz = 0; iz < divisions; iz++) {
            int i0 = ix * halfDiv + iz;
            int i1 = i0 + 1;
            int i2 = i0 + halfDiv;
            int i3 = i2 + 1;
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }
}

Vec3 SpacetimeGrid::getDeformedPosition(const Vec3& gridPos, const BlackHole& bh) const {
    Vec3 rel = gridPos;
    double dist = rel.length();

    if (dist < 0.5) return rel;

    double deformation = bh.mass / (dist * dist + 0.5);
    deformation = std::min(deformation, 3.0);

    double falloff = std::exp(-dist * 0.2);
    deformation *= falloff;

    Vec3 deformed = rel;
    deformed.y -= deformation * falloff * gridHeight;

    return deformed;
}

Vec3 SpacetimeGrid::getGridColor(const Vec3& pos, const BlackHole& bh) const {
    Vec3 rel = pos;
    double dist = rel.length();

    double alpha = std::exp(-dist * 0.15) * 0.6;
    alpha = std::clamp(alpha, 0.05, 0.7);

    if (dist < bh.schwarzschildRadius * 1.5) {
        return Vec3(0.0, 0.0, 0.0);
    }

    double t = dist / gridSize;
    Vec3 color = lerp(Vec3(0.1, 0.1, 0.3), Vec3(0.02, 0.02, 0.08), t);

    double gridLineFade = 0.3 + 0.7 * (std::abs(std::sin(pos.x * 2.0)) + std::abs(std::cos(pos.z * 2.0))) / 2.0;
    color = color * gridLineFade;

    return color;
}