// Spacetime curvature visualisation.
//
// The spatial slice t = const, theta = pi/2 of Schwarzschild is embedded in
// Euclidean 3-space as the Flamm paraboloid
//
//     z(r) = 2 sqrt(r_s (r - r_s)) ,   r >= r_s
//
// which is the exact isometric embedding of the equatorial plane. The throat at
// r = r_s is the "trapdoor": the grid narrows to a circle of circumference
// 2 pi r_s and then simply ends, because there is no spatial geometry beyond it.
#pragma once

#include <vector>

#include "glutil.h"
#include "math3d.h"

class GridScene {
public:
    void init();
    void shutdown();

    // Draws the whole scene. `viewProj` is the camera matrix.
    void draw(const m3::Mat4& viewProj, float time, float intensity, float zScale);

private:
    struct V {
        float x, y, z;
        float r, g, b, a;
    };
    struct Poly {
        std::vector<m3::Vec3> pts;
        float r = 1, g = 1, b = 1;
        float weight = 1.0f;
    };

    void buildStatic();
    void buildDynamic(float time, float zScale);
    void drawBuffer(const std::vector<V>& verts, GLenum mode, bool opaque,
                    bool roundPoints, float intensity, const m3::Mat4& mvp);

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    std::vector<V> lines_;
    std::vector<V> points_;
    std::vector<V> opaque_;

    std::vector<Poly> photonPaths_;
    std::vector<Poly> particlePaths_;
    std::vector<float> particlePhase_;
};
