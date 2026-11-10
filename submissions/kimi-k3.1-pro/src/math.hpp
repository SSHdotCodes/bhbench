#pragma once

#include <algorithm>
#include <cmath>

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(const Vec3& a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline Vec3 operator/(const Vec3& a, float s) {
    return {a.x / s, a.y / s, a.z / s};
}
inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalize(const Vec3& a) {
    const float n = length(a);
    return n > 0.0f ? a / n : Vec3{};
}

struct Mat4 {
    float v[16] = {0.0f};
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                out.v[column * 4 + row] += a.v[k * 4 + row] * b.v[column * 4 + k];
            }
        }
    }
    return out;
}

inline Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
    Mat4 m;
    const float f = 1.0f / std::tan(0.5f * fovYRadians);
    m.v[0] = f / aspect;
    m.v[5] = f;
    m.v[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    m.v[11] = -1.0f;
    m.v[14] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
    return m;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 m;
    m.v[0] = s.x;
    m.v[4] = s.y;
    m.v[8] = s.z;
    m.v[12] = -dot(s, eye);
    m.v[1] = u.x;
    m.v[5] = u.y;
    m.v[9] = u.z;
    m.v[13] = -dot(u, eye);
    m.v[2] = -f.x;
    m.v[6] = -f.y;
    m.v[10] = -f.z;
    m.v[14] = dot(f, eye);
    m.v[15] = 1.0f;
    return m;
}
