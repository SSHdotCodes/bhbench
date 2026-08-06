#pragma once

#include <glm/glm.hpp>

class Camera {
public:
    float orbitRadius = 30.0f;
    float orbitTheta = 0.55f;   // polar angle from +Y
    float orbitPhi = 0.0f;      // azimuthal angle
    float fov = 55.0f;
    float lookAtY = 0.0f;

    glm::vec3 position() const;
    glm::vec3 target() const { return glm::vec3(0.0f, lookAtY, 0.0f); }
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;

    void rotate(float dPhi, float dTheta);
    void zoom(float delta);
};