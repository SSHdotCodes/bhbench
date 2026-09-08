#pragma once
// Minimal linear algebra for camera and mesh transforms.
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }

struct Mat4 {
    // column-major, like OpenGL
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static Mat4 identity() { return Mat4{}; }
    static Mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
        Mat4 r; for (float& v : r.m) v = 0;
        float f = 1.0f / std::tan(fovyRad * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
        r.m[14] = 2 * zf * zn / (zn - zf);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 f = normalize(at - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int ro = 0; ro < 4; ++ro) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + ro] * o.m[c * 4 + k];
                r.m[c * 4 + ro] = s;
            }
        return r;
    }
};
