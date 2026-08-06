#include "camera.hpp"

#include <algorithm>
#include <cmath>

Camera::Camera() = default;

void Camera::orbit(float dYaw, float dPitch) {
    yaw_ += dYaw;
    pitch_ += dPitch;
    const float limit = glm::radians(89.0f);
    pitch_ = std::clamp(pitch_, -limit, limit);
}

void Camera::zoom(float delta) {
    distance_ = std::clamp(distance_ * (1.0f - delta * 0.1f), minDist_, maxDist_);
}

void Camera::setAspect(float aspect) {
    aspect_ = std::max(aspect, 0.05f);
}

glm::vec3 Camera::position() const {
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    return glm::vec3(
        distance_ * cp * sy,
        distance_ * sp,
        distance_ * cp * cy
    );
}

glm::vec3 Camera::forward() const {
    return glm::normalize(-position());
}

glm::vec3 Camera::right() const {
    glm::vec3 f = forward();
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 r = glm::cross(f, worldUp);
    if (glm::dot(r, r) < 1e-10f) {
        worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        r = glm::cross(f, worldUp);
    }
    return glm::normalize(r);
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

void Camera::basis(glm::vec3& r, glm::vec3& u, glm::vec3& f) const {
    f = forward();
    r = right();
    u = glm::normalize(glm::cross(r, f));
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position(), glm::vec3(0.0f), up());
}

glm::mat4 Camera::projection() const {
    return glm::perspective(fovY_, aspect_, 0.05f, 500.0f);
}
