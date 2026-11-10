// Mat4.h — minimal column-major 4x4 matrix (perspective RH, lookAt, multiply).
// Only what the grid/horizon pass needs; GL clip space z in [-1, 1].
#pragma once
#include <cmath>
#include <cstring>

namespace bhlib {

struct Mat4 {
    float m[16]; // column-major

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) {
        Mat4 r{};
        float f = 1.0f / std::tan(fovYRad * 0.5f);
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return r;
    }

    // view = lookAt(eye, center, up) — pure rotation+translation, no matrices from glm.
    static Mat4 lookAt(const float eye[3], const float center[3], const float upIn[3]) {
        float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
        float flen = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        f[0] /= flen; f[1] /= flen; f[2] /= flen;

        float s[3] = {f[1] * upIn[2] - f[2] * upIn[1],
                      f[2] * upIn[0] - f[0] * upIn[2],
                      f[0] * upIn[1] - f[1] * upIn[0]};
        float slen = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        s[0] /= slen; s[1] /= slen; s[2] /= slen;

        float u[3] = {s[1] * f[2] - s[2] * f[1],
                      s[2] * f[0] - s[0] * f[2],
                      s[0] * f[1] - s[1] * f[0]};

        Mat4 r = identity();
        r.m[0] = s[0];  r.m[1] = u[0];  r.m[2] = -f[0];
        r.m[4] = s[1];  r.m[5] = u[1];  r.m[6] = -f[1];
        r.m[8] = s[2];  r.m[9] = u[2];  r.m[10] = -f[2];
        r.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
        r.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
        r.m[14] =  (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
        return r;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += a.m[k * 4 + row] * b.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        return r;
    }
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { float l = length(); return (l > 0.0f) ? Vec3(x / l, y / l, z / l) : Vec3(0, 0, 0); }
};

// Orbit camera around the BH origin. Distances in units of M (GM/c^2).
struct OrbitCamera {
    float azimuth   = -0.62f;  // radians, around +y
    float elevation =  0.30f;  // radians above the disk plane
    float distance  = 38.0f;   // in M
    float fovDeg    = 55.0f;

    static constexpr float kMinElev = -1.52f;
    static constexpr float kMaxElev =  1.52f;
    static constexpr float kMinDist = 4.6f;    // keep the camera outside the ISCO comfortably
    static constexpr float kMaxDist = 120.0f;

    void reset() { *this = OrbitCamera{}; }

    Vec3 position() const {
        float ce = std::cos(elevation);
        return Vec3(distance * ce * std::cos(azimuth),
                    distance * std::sin(elevation),
                    distance * ce * std::sin(azimuth));
    }
    Vec3 fwd() const { return (position() * -1.0f).normalized(); }
    Vec3 right() const {
        Vec3 up(0.0f, 1.0f, 0.0f);
        return fwd().cross(up).normalized();
    }
    Vec3 up() const { return right().cross(fwd()).normalized(); }
    float tanHalfFov() const {
        const float kDeg2Rad = 3.14159265f / 180.0f;
        return std::tan(fovDeg * kDeg2Rad * 0.5f);
    }
    void orbit(float dAzimuth, float dElevation) {
        azimuth += dAzimuth;
        elevation += dElevation;
        if (elevation > kMaxElev) elevation = kMaxElev;
        if (elevation < kMinElev) elevation = kMinElev;
    }
    void dolly(float wheelDelta) { // exponential zoom
        distance *= std::exp(-wheelDelta * 0.0011f);
        if (distance < kMinDist) distance = kMinDist;
        if (distance > kMaxDist) distance = kMaxDist;
    }
};

} // namespace bhlib
