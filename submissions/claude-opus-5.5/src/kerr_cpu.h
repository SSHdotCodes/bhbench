// kerr_cpu.h — compiles the shared GPU geodesic core (shaders/kerr_core.h) for the CPU, once in single
// precision (bit-for-bit the same algorithm the Metal shader runs) and once in double precision.
#pragma once
#include <algorithm>
#include <cmath>

template <typename T>
struct V3 {
    T x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3 operator*(T s) const { return {x * s, y * s, z * s}; }
    V3 operator/(T s) const { return {x / s, y / s, z / s}; }
};
template <typename T> inline V3<T> operator*(T s, const V3<T>& v) { return v * s; }
template <typename T> inline T dot(const V3<T>& a, const V3<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <typename T> inline T length(const V3<T>& a) { return std::sqrt(dot(a, a)); }
template <typename T> inline V3<T> normalize(const V3<T>& a) { return a / length(a); }
template <typename T> inline V3<T> cross(const V3<T>& a, const V3<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

#include "shaders/bh_shared.h"   // BH_TRACE_* codes

#define BH_THREAD

namespace kerr64 {
typedef double real;
typedef V3<double> vec3;
using std::abs; using std::acos; using std::cos; using std::max; using std::min; using std::pow; using std::sin; using std::sqrt;
#include "shaders/kerr_core.h"
}  // namespace kerr64

namespace kerr32 {
typedef float real;
typedef V3<float> vec3;
using std::abs; using std::acos; using std::cos; using std::max; using std::min; using std::pow; using std::sin; using std::sqrt;
#include "shaders/kerr_core.h"
}  // namespace kerr32
