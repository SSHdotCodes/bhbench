#pragma once

#include <vector>
#include <cstddef>

// GPU mesh (VAO/VBO/EBO) for spacetime grid embedding surface & lines.
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void build(const std::vector<float>& interleavedPosNormal,
               const std::vector<unsigned int>& indices,
               bool lines = false);

    void draw() const;

    // Build Flamm paraboloid embedding of the equatorial Schwarzschild spatial slice:
    //   z(r) = ±2 √(rs (r − rs))  for r > rs
    // plus polar coordinate grid lines on that surface ("trapdoor in spacetime").
    static Mesh createFlammSurface(float rs, float rMin, float rMax,
                                   int nRadial, int nAzimuth);
    static Mesh createFlammGridLines(float rs, float rMin, float rMax,
                                     int nRadial, int nAzimuth);

    // Cartesian-like coordinate lattice warped by the Schwarzschild spatial factor
    // √g_rr = 1/√(1−rs/r) along radial direction (visual curvature cage).
    static Mesh createCurvatureCage(float rs, float halfExtent, int divisions);

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    std::size_t indexCount_ = 0;
    bool lines_ = false;

    void destroy();
};
