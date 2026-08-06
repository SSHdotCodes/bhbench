// Params.h — shared simulation parameters (C++ side).
// Layout MUST match `struct Params` in Shaders.metal exactly (128 bytes).
#pragma once
#include <simd/simd.h>

enum ParamFlags : unsigned int {
    F_DISK      = 1u << 0,
    F_SKY       = 1u << 1,
    F_BEAMING   = 1u << 2,
    F_REDSHIFT  = 1u << 3,
};

struct Params {
    simd_float3 camPos;       float aspect;
    simd_float3 camRight;     float tanHalfFov;
    simd_float3 camUp;        float time;
    simd_float3 camFwd;       float exposure;
    simd_float3 diskNormal;   float diskIn;
    simd_float3 skyNormal;    float diskOut;
    unsigned int maxSteps;    unsigned int flags;   unsigned int width;  unsigned int height;
    float rs;                 float diskBrightness; float tempScale;     float farR;
};

// simd_float3 and Metal float3 are both 16-byte aligned/size-16, so the
// C++ and Metal layouts agree field-by-field (each float3+float pair = 32 B).
static_assert(sizeof(Params) == 224, "Params layout must match Metal struct");
