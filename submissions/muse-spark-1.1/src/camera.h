#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    float distance = 20.0f;   // distance from target
    float yaw = 0.0f;         // azimuth, radians
    float pitch = 0.3f;       // elevation, radians
    float fov = 60.0f;        // degrees
    float aspect = 16.0f/9.0f;

    glm::vec3 position;
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
    glm::vec3 worldUp = glm::vec3(0,1,0);

    void update() {
        // Clamp pitch to avoid gimbal lock
        const float pitchLimit = glm::radians(85.0f);
        if (pitch > pitchLimit) pitch = pitchLimit;
        if (pitch < -pitchLimit) pitch = -pitchLimit;

        position.x = target.x + distance * cos(pitch) * sin(yaw);
        position.y = target.y + distance * sin(pitch);
        position.z = target.z + distance * cos(pitch) * cos(yaw);

        forward = glm::normalize(target - position);
        right = glm::normalize(glm::cross(forward, worldUp));
        up = glm::cross(right, forward); // already normalized

        // Handle looking straight up/down where cross becomes degenerate
        if (glm::length(right) < 0.001f) {
            right = glm::vec3(1,0,0);
            up = glm::vec3(0,0,1);
        }
    }

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(fov), aspect, 0.1f, 1000.0f);
    }
};
