#pragma once
#include "common.h"

class BlackHole {
public:
    Vec3 position;
    double mass;
    double schwarzschildRadius;
    double ISCO;
    double photonSphere;
    double accretionInnerRadius;
    double accretionOuterRadius;

    BlackHole(const Vec3& pos = {0, 0, 0}, double mass = 10.0)
        : position(pos), mass(mass) {
        double G = 1.0;
        double c = 1.0;
        schwarzschildRadius = 2.0 * G * mass / (c * c);
        ISCO = 3.0 * schwarzschildRadius;
        photonSphere = 1.5 * schwarzschildRadius;
        accretionInnerRadius = ISCO;
        accretionOuterRadius = ISCO * 6.0;
    }

    Vec3 gravityAcceleration(const Vec3& pt) const {
        Vec3 r = pt - position;
        double dist = r.length();
        if (dist < schwarzschildRadius * 1.01) return Vec3(0, 0, 0);
        double rSq = dist * dist;
        double force = mass / rSq;
        return r.normalized() * force;
    }

    double gravitationalTimeDilation(const Vec3& pt) const {
        Vec3 r = pt - position;
        double dist = r.length();
        if (dist <= schwarzschildRadius) return 0.0;
        return std::sqrt(1.0 - schwarzschildRadius / dist);
    }

    bool isInsideEventHorizon(const Vec3& pt) const {
        return (pt - position).length() < schwarzschildRadius * 1.001;
    }

    double deflectionAngle(const Vec3& rayPos, const Vec3& rayDir) const {
        Vec3 r = rayPos - position;
        double dist = r.length();
        if (dist < schwarzschildRadius) return 0.0;
        double b = std::abs(r.cross(rayDir).length());
        if (b < schwarzschildRadius * 0.5) return M_PI;
        return 4.0 * mass / b;
    }
};