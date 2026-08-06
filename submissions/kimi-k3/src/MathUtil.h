// MathUtil.h — minimal float4x4 helpers (lookAt, perspective, TRS).
#pragma once
#include <simd/simd.h>
#include <cmath>

static inline simd_float4x4 matIdentity() {
    return (simd_float4x4){ simd_float4{1,0,0,0}, simd_float4{0,1,0,0},
                            simd_float4{0,0,1,0}, simd_float4{0,0,0,1} };
}

static inline simd_float4x4 matMul(simd_float4x4 a, simd_float4x4 b) {
    return simd_mul(a, b);
}

// right-handed lookAt, column-major (simd)
static inline simd_float4x4 matLookAt(simd_float3 eye, simd_float3 center, simd_float3 upHint) {
    simd_float3 f = simd_normalize(center - eye);
    simd_float3 s = simd_normalize(simd_cross(f, upHint));
    simd_float3 u = simd_cross(s, f);
    return (simd_float4x4){
        simd_float4{ s.x, u.x, -f.x, 0.0f },
        simd_float4{ s.y, u.y, -f.y, 0.0f },
        simd_float4{ s.z, u.z, -f.z, 0.0f },
        simd_float4{ -simd_dot(s, eye), -simd_dot(u, eye), simd_dot(f, eye), 1.0f }
    };
}

// perspective, Metal clip space (z in [0,1]), right-handed
static inline simd_float4x4 matPerspective(float fovY, float aspect, float zn, float zf) {
    float t = 1.0f / tanf(fovY * 0.5f);
    return (simd_float4x4){
        simd_float4{ t / aspect, 0, 0, 0 },
        simd_float4{ 0, t, 0, 0 },
        simd_float4{ 0, 0, zf / (zn - zf), -1.0f },
        simd_float4{ 0, 0, zn * zf / (zn - zf), 0.0f }
    };
}

static inline simd_float4x4 matTranslate(simd_float3 t) {
    simd_float4x4 m = matIdentity();
    m.columns[3] = simd_float4{ t.x, t.y, t.z, 1.0f };
    return m;
}

static inline simd_float4x4 matScale(float s) {
    return (simd_float4x4){ simd_float4{s,0,0,0}, simd_float4{0,s,0,0},
                            simd_float4{0,0,s,0}, simd_float4{0,0,0,1} };
}
