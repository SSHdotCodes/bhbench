#pragma once

// Procedural polar wireframe grid used by the "spacetime curvature" view.
// CPU side only stores flat (x, 0, z) positions; the vertical displacement
// implementing Flamm's paraboloid embedding is computed in the vertex
// shader from the radius, so the same mesh works for any rs.
class GridMesh {
public:
    GridMesh() = default;
    ~GridMesh();

    GridMesh(const GridMesh&) = delete;
    GridMesh& operator=(const GridMesh&) = delete;

    // innerRadius/outerRadius are in scene units (multiples of rs).
    void init(float innerRadius, float outerRadius, int rings, int spokes);
    void render() const;

private:
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int indexCount = 0;
};
