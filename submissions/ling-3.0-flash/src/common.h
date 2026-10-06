#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { double l = length(); return l > 1e-30 ? Vec3(x / l, y / l, z / l) : Vec3(); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline Vec3 reflect(const Vec3& I, const Vec3& N) { return I - N * (2.0 * I.dot(N)); }
inline Vec3 refract(const Vec3& I, const Vec3& N, double eta) {
    double dotNI = I.dot(N);
    double k = 1.0 - eta * eta * (1.0 - dotNI * dotNI);
    if (k < 0) return Vec3(0, 0, 0);
    return I * eta - N * (eta * dotNI + std::sqrt(k));
}

struct Ray {
    Vec3 origin, dir;
    Ray() = default;
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d.normalized()) {}
};

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    RGBA() = default;
    RGBA(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
};

inline uint32_t rgbaToUint32(const RGBA& c) {
    return (uint32_t(c.a) << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
}

inline Vec3 colorToVec3(const RGBA& c) {
    return {c.r / 255.0, c.g / 255.0, c.b / 255.0};
}

inline RGBA vec3ToColor(const Vec3& v) {
    return RGBA(
        (uint8_t)std::clamp(v.x * 255.0, 0.0, 255.0),
        (uint8_t)std::clamp(v.y * 255.0, 0.0, 255.0),
        (uint8_t)std::clamp(v.z * 255.0, 0.0, 255.0)
    );
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }
inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double smoothstep(double lo, double hi, double t) {
    t = clamp((t - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Camera {
    Vec3 pos, lookAt, up;
    double fov = 60.0 * M_PI / 180.0;
    double aspect = 16.0 / 9.0;
    double lensRadius = 0.0;
    double focusDist = 100.0;

    Ray getRay(double u, double v) const {
        Vec3 w = (pos - lookAt).normalized();
        Vec3 right = Vec3(0, 1, 0).cross(w).normalized();
        Vec3 up = w.cross(right);

        Vec3 target = lookAt;
        Vec3 fwd = (target - pos).normalized();
        double halfH = std::tan(fov * 0.5);
        double halfW = aspect * halfH;

        Vec3 horizontal = right * halfW * 2.0;
        Vec3 vertical = up * halfH * 2.0;

        Vec3 lowerLeft = pos - horizontal * 0.5 - vertical * 0.5 + fwd;

        Vec3 dir = (lowerLeft + horizontal * u + vertical * v - pos).normalized();

        if (lensRadius > 1e-6) {
            Vec3 rd = Vec3(
                (rand() / (double)RAND_MAX - 0.5) * 2.0,
                (rand() / (double)RAND_MAX - 0.5) * 2.0,
                (rand() / (double)RAND_MAX - 0.5) * 2.0
            ).normalized() * lensRadius;
            Vec3 offset = rd.cross((target - pos).normalized()) * (rd.dot((target - pos).normalized()))
                        + rd * (target - pos).normalized().dot(rd) * 0.0;
            Vec3 newOrigin = pos + rd;
            Vec3 newTarget = target + offset;
            dir = (newTarget - newOrigin).normalized();
            return Ray(newOrigin, dir);
        }

        return Ray(pos, dir);
    }
};

class RNG {
public:
    std::mt19937 gen;
    std::uniform_real_distribution<double> dist;
    RNG() : gen(std::random_device{}()), dist(0.0, 1.0) {}
    double operator()() { return dist(gen); }
};

extern RNG g_rng;