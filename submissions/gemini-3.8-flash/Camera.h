#ifndef CAMERA_H
#define CAMERA_H

#include <simd/simd.h>
#include <cmath>

class Camera {
public:
    Camera()
        : azimuth(0.0f), elevation(0.26f), distance(16.0f),
          center(simd_make_float3(0.0f, 0.0f, 0.0f)),
          fov(55.0f * (M_PI / 180.0f)),
          minDistance(2.5f), maxDistance(45.0f) {
        updateVectors();
    }

    void orbit(float deltaAzimuth, float deltaElevation) {
        azimuth += deltaAzimuth;
        elevation += deltaElevation;

        // Clamp elevation to avoid pole flip
        const float maxElev = 1.55f;
        if (elevation > maxElev) elevation = maxElev;
        if (elevation < -maxElev) elevation = -maxElev;

        updateVectors();
    }

    void zoom(float deltaDistance) {
        distance += deltaDistance;
        if (distance < minDistance) distance = minDistance;
        if (distance > maxDistance) distance = maxDistance;
        updateVectors();
    }

    void pan(float deltaX, float deltaY) {
        center += right * deltaX + up * deltaY;
        updateVectors();
    }

    simd_float3 getPosition() const { return position; }
    simd_float3 getForward() const { return forward; }
    simd_float3 getUp() const { return up; }
    simd_float3 getRight() const { return right; }
    float getFov() const { return fov; }
    float getDistance() const { return distance; }
    float getAzimuth() const { return azimuth; }
    float getElevation() const { return elevation; }

    void setDistance(float d) { distance = d; updateVectors(); }
    void setElevation(float el) { elevation = el; updateVectors(); }
    void setAzimuth(float az) { azimuth = az; updateVectors(); }

private:
    void updateVectors() {
        // Spherical coordinates
        float cosEl = std::cos(elevation);
        float sinEl = std::sin(elevation);
        float cosAz = std::cos(azimuth);
        float sinAz = std::sin(azimuth);

        simd_float3 offset = simd_make_float3(
            distance * cosEl * sinAz,
            distance * sinEl,
            distance * cosEl * cosAz
        );

        position = center + offset;
        forward = simd_normalize(center - position);

        simd_float3 worldUp = simd_make_float3(0.0f, 1.0f, 0.0f);
        right = simd_normalize(simd_cross(forward, worldUp));
        up = simd_normalize(simd_cross(right, forward));
    }

    float azimuth;
    float elevation;
    float distance;
    simd_float3 center;
    float fov;
    float minDistance;
    float maxDistance;

    simd_float3 position;
    simd_float3 forward;
    simd_float3 up;
    simd_float3 right;
};

#endif // CAMERA_H
