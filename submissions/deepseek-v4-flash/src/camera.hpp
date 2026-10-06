#pragma once
#include <cmath>

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double xx, double yy, double zz) : x(xx), y(yy), z(zz) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(double s, const Vec3& a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(const Vec3& a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { double l = length(a); return l > 0 ? a / l : a; }

struct Camera {
    Vec3 target{0, 0, 0};
    double azim = 3.141592653589793; // 180 deg: camera on -z axis, looking +z
    double elev = 0.30;
    double dist = 25.0;
    double fovY = 60.0 * 3.141592653589793 / 180.0;

    Vec3 pos() const {
        const double ce = std::cos(elev), se = std::sin(elev);
        const double sa = std::sin(azim), ca = std::cos(azim);
        return target + Vec3(dist * ce * sa, dist * se, dist * ce * ca);
    }
    void basis(Vec3& fwd, Vec3& right, Vec3& up) const {
        fwd = normalize(target - pos());
        right = normalize(cross(fwd, Vec3(0, 1, 0)));
        up = cross(right, fwd);
    }
    void orbit(double da, double de) {
        azim += da;
        elev += de;
        const double lim = 1.50;
        if (elev > lim) elev = lim;
        if (elev < -lim) elev = -lim;
    }
    void dolly(double f) {
        dist *= f;
        if (dist < 6.0) dist = 6.0;
        if (dist > 200.0) dist = 200.0;
    }
};
