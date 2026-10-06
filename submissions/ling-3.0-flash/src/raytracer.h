#pragma once
#include "common.h"
#include "blackhole.h"

struct HitRecord {
    Vec3 point;
    Vec3 normal;
    double t;
    bool hit;
    Vec3 color;
    double emission;
};

class RayTracer {
public:
    BlackHole* bh;
    std::vector<Vec3> stars;
    std::vector<Vec3> starColors;
    double starScale = 200.0;
    int maxBounces = 8;
    int maxSteps = 500;
    double stepSize = 0.1;
    double criticalAngle = 0.0;

    RayTracer(BlackHole* blackhole);
    void generateStars();
    Vec3 trace(const Ray& ray, int depth = 0);
    bool traceGravityLensing(const Ray& ray, int steps, double bhDist);
    Vec3 traceWithLensing(const Ray& ray);
    Vec3 sampleAccretionDisk(const Vec3& pos, const Vec3& dir, double dist);
};