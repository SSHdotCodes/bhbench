// Orbit camera shared by both views (units of M, Y-up, disk in XZ plane).
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct OrbitCamera {
    float yaw_deg = 35.0f;    // azimuth around Y
    float pitch_deg = 24.0f;  // elevation above disk plane
    float dist = 17.0f;       // distance from hole (M)
    float fov_deg = 55.0f;

    glm::vec3 pos() const {
        const float yaw = glm::radians(yaw_deg);
        const float p = glm::radians(pitch_deg);
        const float ch = std::cos(p) * dist;
        return glm::vec3(ch * std::cos(yaw), std::sin(p) * dist,
                         ch * std::sin(yaw));
    }
    glm::vec3 fwd() const { return glm::normalize(-pos()); }
    glm::vec3 right() const {
        return glm::normalize(glm::cross(fwd(), glm::vec3(0, 1, 0)));
    }
    glm::vec3 up() const { return glm::cross(right(), fwd()); }

    glm::mat4 view() const { return glm::lookAt(pos(), glm::vec3(0), up()); }
    glm::mat4 proj(float aspect) const {
        return glm::perspective(glm::radians(fov_deg), aspect, 0.1f, 800.0f);
    }

    void orbit(float dx, float dy) {
        yaw_deg += dx * 0.35f;
        pitch_deg += dy * 0.30f;
        if (pitch_deg < 1.5f) pitch_deg = 1.5f;
        if (pitch_deg > 89.0f) pitch_deg = 89.0f;
    }
    void zoom(float steps) {
        dist *= std::pow(1.12f, -steps);
        if (dist < 4.0f) dist = 4.0f;
        if (dist > 60.0f) dist = 60.0f;
    }
};
