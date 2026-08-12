#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    float distance = 44.0f;
    float yaw = 1.22f;
    float pitch = 0.36f;
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float fovDeg = 48.0f;
    float nearP = 0.05f;
    float farP = 400.0f;

    glm::vec3 position() const {
        const float cp = std::cos(pitch);
        return target + glm::vec3(
            distance * cp * std::cos(yaw),
            distance * std::sin(pitch),
            distance * cp * std::sin(yaw)
        );
    }

    glm::vec3 forward() const { return glm::normalize(target - position()); }

    glm::vec3 right() const {
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 r = glm::cross(forward(), worldUp);
        const float L = glm::length(r);
        if (L < 1e-5f) {
            return glm::vec3(1.0f, 0.0f, 0.0f);
        }
        return r / L;
    }

    glm::vec3 up() const { return glm::normalize(glm::cross(right(), forward())); }

    glm::mat4 view() const {
        return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::mat4 proj(float aspect) const {
        return glm::perspective(glm::radians(fovDeg), aspect, nearP, farP);
    }

    void orbit(float dx, float dy) {
        yaw += dx;
        pitch = std::clamp(pitch + dy, -1.15f, 1.25f);
    }

    void zoom(float factor) {
        distance = std::clamp(distance * factor, 6.5f, 120.0f);
    }

    void pan(float dx, float dy) {
        const float s = distance * 0.0022f;
        target += right() * dx * s + up() * dy * s;
    }

    void reset() {
        distance = 44.0f;
        yaw = 1.22f;
        pitch = 0.36f;
        target = glm::vec3(0.0f, 0.0f, 0.0f);
    }
};
