#pragma once

#include "Math.hpp"
#include "SimulationState.hpp"
#include <algorithm>
#include <cmath>

namespace bh {

class OrbitalCamera {
public:
    float radius = 25.0f;
    float theta = degToRad(65.0f);  // Polar angle from +Z spin axis (25 deg above equatorial disk)
    float phi = degToRad(25.0f);

    float targetRadius = 25.0f;
    float targetTheta = degToRad(65.0f);
    float targetPhi = degToRad(25.0f);

    float fovYDeg = 52.0f;

    void applyPreset(CameraPreset preset) {
        switch (preset) {
            case CameraPreset::GargantuaInclined:
                targetRadius = 25.0f;
                targetTheta = degToRad(65.0f);
                targetPhi = degToRad(25.0f);
                fovYDeg = 52.0f;
                break;
            case CameraPreset::EdgeOnLensing:
                targetRadius = 20.0f;
                targetTheta = degToRad(88.5f);
                targetPhi = degToRad(0.0f);
                fovYDeg = 46.0f;
                break;
            case CameraPreset::PolarTopDown:
                targetRadius = 26.0f;
                targetTheta = degToRad(14.0f);
                targetPhi = degToRad(45.0f);
                fovYDeg = 50.0f;
                break;
            case CameraPreset::ClosePhotonSkim:
                targetRadius = 9.5f;
                targetTheta = degToRad(79.0f);
                targetPhi = degToRad(10.0f);
                fovYDeg = 56.0f;
                break;
        }
    }

    void snapToTarget() {
        radius = targetRadius;
        theta = targetTheta;
        phi = targetPhi;
    }

    void orbit(float deltaAzimuthRad, float deltaPolarRad) {
        targetPhi -= deltaAzimuthRad;
        targetTheta = std::clamp(targetTheta - deltaPolarRad, 0.04f, kPi - 0.04f);
    }

    void zoom(float deltaScroll) {
        float factor = std::exp(-deltaScroll * 0.08f);
        targetRadius = std::clamp(targetRadius * factor, 3.4f, 65.0f);
    }

    void update(float dt) {
        float alpha = 1.0f - std::exp(-std::min(dt, 0.1f) * 12.0f);
        radius += (targetRadius - radius) * alpha;
        theta  += (targetTheta - theta) * alpha;
        phi    += (targetPhi - phi) * alpha;
    }

    Vec3 position() const {
        float sinT = std::sin(theta);
        float cosT = std::cos(theta);
        return {
            radius * sinT * std::cos(phi),
            radius * sinT * std::sin(phi),
            radius * cosT
        };
    }

    void computeBasis(Vec3& outPos, Vec3& outForward, Vec3& outRight, Vec3& outUp) const {
        outPos = position();
        outForward = (-outPos).normalized();
        Vec3 worldUp{0.0f, 0.0f, 1.0f};
        outRight = Vec3::cross(outForward, worldUp).normalized();
        outUp = Vec3::cross(outRight, outForward).normalized();
    }
};

} // namespace bh
