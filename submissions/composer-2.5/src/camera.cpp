#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

glm::vec3 Camera::position() const {
    const float sinTheta = std::sin(orbitTheta);
    return glm::vec3(
        orbitRadius * sinTheta * std::cos(orbitPhi),
        orbitRadius * std::cos(orbitTheta),
        orbitRadius * sinTheta * std::sin(orbitPhi)
    );
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), target(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, 0.1f, 500.0f);
}

void Camera::rotate(float dPhi, float dTheta) {
    orbitPhi += dPhi;
    orbitTheta = std::clamp(orbitTheta + dTheta, 0.08f, 3.05f);
}

void Camera::zoom(float delta) {
    orbitRadius = std::clamp(orbitRadius + delta, 8.0f, 120.0f);
}