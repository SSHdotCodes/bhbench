#pragma once
#include "common.h"
#include "blackhole.h"

class SpacetimeGrid {
public:
    double gridSize = 20.0;
    double gridSpacing = 1.0;
    double gridHeight = 0.5;

    struct GridVertex {
        Vec3 position;
        Vec3 offset;
    };

    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices;

    SpacetimeGrid();
    void generateGrid();
    Vec3 getDeformedPosition(const Vec3& gridPos, const BlackHole& bh) const;
    Vec3 getGridColor(const Vec3& pos, const BlackHole& bh) const;
    uint32_t vertexCount() const;
    uint32_t indexCount() const;
};
