#pragma once
#include <simd/simd.h>
#include <cmath>

struct Camera {
    simd::float3 position;
    simd::float3 target;
    simd::float3 up;
    
    float distance;
    float azimuth;    // Orbit angle in horizontal plane (radians)
    float elevation;  // Orbit angle in vertical plane (radians)
    float fovY;        // Field of view in radians
    
    Camera() {
        distance = 18.0f;
        azimuth = 0.35f;
        elevation = 0.22f; // Slight tilt to view disk and gravitational lensing arcs
        target = simd::make_float3(0.0f, 0.0f, 0.0f);
        up = simd::make_float3(0.0f, 1.0f, 0.0f);
        fovY = 55.0f * (M_PI / 180.0f);
        updatePosition();
    }
    
    void updatePosition() {
        // Clamp elevation to avoid gimbal lock at exact poles
        if (elevation > 1.52f) elevation = 1.52f;
        if (elevation < -1.52f) elevation = -1.52f;
        
        position.x = target.x + distance * std::cos(elevation) * std::sin(azimuth);
        position.y = target.y + distance * std::sin(elevation);
        position.z = target.z + distance * std::cos(elevation) * std::cos(azimuth);
    }
    
    void orbit(float deltaAzimuth, float deltaElevation) {
        azimuth += deltaAzimuth;
        elevation += deltaElevation;
        updatePosition();
    }
    
    void zoom(float deltaDistance) {
        distance += deltaDistance;
        if (distance < 2.5f) distance = 2.5f;
        if (distance > 120.0f) distance = 120.0f;
        updatePosition();
    }
    
    void pan(float deltaX, float deltaY) {
        simd::float3 forward = simd::normalize(target - position);
        simd::float3 right = simd::normalize(simd::cross(forward, up));
        simd::float3 camUp = simd::cross(right, forward);
        
        target += right * deltaX * (distance * 0.05f) + camUp * deltaY * (distance * 0.05f);
        updatePosition();
    }
};
