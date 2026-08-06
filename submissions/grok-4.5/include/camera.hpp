#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orbit camera looking at the black hole at the origin.
class Camera {
public:
    Camera();

    void orbit(float dYaw, float dPitch);
    void zoom(float delta);
    void setAspect(float aspect);

    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::mat4 view() const;
    glm::mat4 projection() const;
    float distance() const { return distance_; }
    float fovY() const { return fovY_; }

    // Basis for ray generation (right, up, forward)
    void basis(glm::vec3& right, glm::vec3& up, glm::vec3& forward) const;

private:
    float yaw_ = 0.35f;      // radians
    float pitch_ = 0.45f;    // looking somewhat from above
    float distance_ = 28.0f; // geometric units (M=1 => rs=2)
    float fovY_ = glm::radians(50.0f);
    float aspect_ = 16.0f / 9.0f;
    float minDist_ = 8.0f;
    float maxDist_ = 120.0f;
};
