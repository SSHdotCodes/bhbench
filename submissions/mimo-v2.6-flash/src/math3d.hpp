// Minimal 3D / 4x4 matrix math for camera and scene rendering.
// Matrices are stored column-major as required by OpenGL.
#pragma once

#include <cmath>
#include <array>

namespace bh {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return (l > 1e-20f) ? Vec3(a.x / l, a.y / l, a.z / l) : Vec3(0, 0, 1);
}

using Mat4 = std::array<float, 16>;

inline Mat4 identity4() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.f;
    return m;
}

// Column-major multiply: out = a * b
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 o{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = s;
        }
    return o;
}

inline Mat4 perspective(float fovyRad, float aspect, float znear, float zfar) {
    float f = 1.f / std::tan(fovyRad * 0.5f);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.f;
    m[14] = (2.f * zfar * znear) / (znear - zfar);
    return m;
}

inline Mat4 ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 m{};
    m[0] = 2.f / (r - l);
    m[5] = 2.f / (t - b);
    m[10] = -2.f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.f;
    return m;
}

inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 m = identity4();
    m[0] = s.x;  m[4] = s.y;  m[8]  = s.z;
    m[1] = u.x;  m[5] = u.y;  m[9]  = u.z;
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z;
    m[12] = -dot(s, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
    return m;
}

}  // namespace bh
