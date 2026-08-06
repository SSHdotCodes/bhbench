#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPitchLimit = 1.55f; // just under PI/2, avoids the up-vector singularity
}

void Camera::orbit(float dYawRad, float dPitchRad) {
    yaw += dYawRad;
    pitch = std::clamp(pitch + dPitchRad, -kPitchLimit, kPitchLimit);
    // Keep pitch off exactly zero: at pitch==0 the camera, the black hole
    // and the disk plane all coincide, which degenerates the raytracer's
    // plane/disk-crossing test (see blackhole.frag). The nudge is far
    // below visible precision.
    if (std::abs(pitch) < 1e-4f) pitch = pitch < 0.0f ? -1e-4f : 1e-4f;
}

void Camera::zoom(float factor) {
    distance = std::clamp(distance * factor, minDistance, maxDistance);
}

glm::vec3 Camera::position() const {
    return glm::vec3(
        distance * std::cos(pitch) * std::sin(yaw),
        distance * std::sin(pitch),
        distance * std::cos(pitch) * std::cos(yaw));
}

glm::vec3 Camera::forward() const {
    return glm::normalize(-position());
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera::up() const {
    return glm::cross(right(), forward());
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position(), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::proj(float aspect, float nearZ, float farZ) const {
    return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
}
