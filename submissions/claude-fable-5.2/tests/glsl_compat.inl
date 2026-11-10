// =============================================================================
//  glsl_compat.inl — just enough of GLSL's vector types and built-ins to
//  compile shaders/kerr_geodesic.glsl as C++.
//
//  Included inside a namespace, after `real` has been defined as the scalar
//  type (double, or the F32 wrapper that forces single-precision arithmetic)
//  together with  double toDouble(real).  No standard headers in here.
// =============================================================================

struct vec2 {
    real x, y;
    vec2() : x(0.0), y(0.0) {}
    vec2(real a, real b) : x(a), y(b) {}
    explicit vec2(real a) : x(a), y(a) {}
};

struct vec3 {
    real x, y, z;
    vec3() : x(0.0), y(0.0), z(0.0) {}
    vec3(real a, real b, real c) : x(a), y(b), z(c) {}
    explicit vec3(real a) : x(a), y(a), z(a) {}
};

struct vec4 {
    real x, y, z, w;
    vec4() : x(0.0), y(0.0), z(0.0), w(0.0) {}
    vec4(real a, real b, real c, real d) : x(a), y(b), z(c), w(d) {}
    explicit vec4(real a) : x(a), y(a), z(a), w(a) {}
};

inline vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 operator-(vec3 a, vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 operator*(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec3 operator*(vec3 a, real s) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator*(real s, vec3 a) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator/(vec3 a, real s) { return vec3(a.x / s, a.y / s, a.z / s); }

inline vec4 operator+(vec4 a, vec4 b) { return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline vec4 operator-(vec4 a, vec4 b) { return vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline vec4 operator*(vec4 a, real s) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline vec4 operator*(real s, vec4 a) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }

inline real sqrt(real v) { return real(std::sqrt(toDouble(v))); }
inline real inversesqrt(real v) { return real(1.0) / sqrt(v); }
inline real abs(real v) { return real(std::fabs(toDouble(v))); }
inline real min(real a, real b) { return (b < a) ? b : a; }
inline real max(real a, real b) { return (a < b) ? b : a; }
inline real clamp(real v, real lo, real hi) { return min(max(v, lo), hi); }

inline real dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline real length(vec3 a) { return sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a) { return a / length(a); }
inline vec3 cross(vec3 a, vec3 b) {
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
