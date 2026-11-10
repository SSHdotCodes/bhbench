// Orbit camera around the black hole. The disk lies in the z = 0 plane, so the
// camera orbits about the z axis with `pitch` as the elevation above the disk.
#pragma once

#include "math3d.h"

struct OrbitCamera {
    float yaw = 0.55f;       // radians about +z
    float pitch = 0.20f;     // elevation above the equatorial plane
    float distance = 48.0f;  // in units of M
    float fovDeg = 50.0f;
    m3::Vec3 target{0.0f, 0.0f, 0.0f};

    m3::Vec3 position() const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        return target + m3::Vec3(distance * cp * std::cos(yaw),
                                 distance * cp * std::sin(yaw),
                                 distance * sp);
    }

    void basis(m3::Vec3& right, m3::Vec3& up, m3::Vec3& fwd) const {
        m3::Vec3 pos = position();
        fwd = m3::normalize(target - pos);
        m3::Vec3 worldUp(0.0f, 0.0f, 1.0f);
        if (std::fabs(m3::dot(fwd, worldUp)) > 0.999f) worldUp = m3::Vec3(0, 1, 0);
        right = m3::normalize(m3::cross(fwd, worldUp));
        up = m3::cross(right, fwd);
    }

    void orbit(float dYaw, float dPitch) {
        yaw += dYaw;
        pitch = m3::clampf(pitch + dPitch, -1.45f, 1.45f);
    }

    void zoom(float factor) {
        distance = m3::clampf(distance * factor, 8.0f, 400.0f);
    }
};
