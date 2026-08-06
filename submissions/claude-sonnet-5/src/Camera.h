#pragma once

#include <glm/glm.hpp>

// Spherical orbit camera, always looking at the origin (the black hole's
// center). Distance/yaw/pitch are expressed in scene units, where distance
// is measured in multiples of the Schwarzschild radius (rs = 1.0 by default).
class Camera {
public:
    float distance = 18.0f;
    float yaw = 0.7f;    // radians, around world Y
    float pitch = 0.32f; // radians, elevation above the equatorial plane
    float fovDeg = 50.0f;

    float minDistance = 3.0f;
    float maxDistance = 200.0f;

    void orbit(float dYawRad, float dPitchRad);
    void zoom(float factor); // multiplicative: >1 zooms out, <1 zooms in

    glm::vec3 position() const;
    glm::vec3 forward() const; // unit vector pointing from camera toward origin
    glm::vec3 right() const;
    glm::vec3 up() const;

    glm::mat4 view() const;
    glm::mat4 proj(float aspect, float nearZ, float farZ) const;
};
