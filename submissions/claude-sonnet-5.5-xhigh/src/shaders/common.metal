// common.metal -- noise, blackbody colour, disk look-ups shared by the ray tracer and the funnel view.
// (kerr_shared.h and shared_types.h are prepended by the build.)

constant float PI_F = 3.14159265358979f;

// ---------------------------------------------------------------------------------------------
// Hash / value noise
// ---------------------------------------------------------------------------------------------
inline uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
inline uint hash3u(uint3 p) { return pcg(p.x + pcg(p.y + pcg(p.z))); }
inline float u01(uint h) { return float(h) * (1.0f / 4294967296.0f); }

inline float vnoise(float3 x)
{
    float3 i = floor(x);
    float3 f = fract(x);
    f = f * f * (3.0f - 2.0f * f);
    int3 ii = int3(i);
    float n000 = u01(hash3u(uint3(ii + int3(0, 0, 0))));
    float n100 = u01(hash3u(uint3(ii + int3(1, 0, 0))));
    float n010 = u01(hash3u(uint3(ii + int3(0, 1, 0))));
    float n110 = u01(hash3u(uint3(ii + int3(1, 1, 0))));
    float n001 = u01(hash3u(uint3(ii + int3(0, 0, 1))));
    float n101 = u01(hash3u(uint3(ii + int3(1, 0, 1))));
    float n011 = u01(hash3u(uint3(ii + int3(0, 1, 1))));
    float n111 = u01(hash3u(uint3(ii + int3(1, 1, 1))));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

inline float fbm3(float3 p, int octaves)
{
    float s = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        s += amp * vnoise(p);
        p = p * 2.03f + float3(17.1f, 3.7f, 9.2f);
        amp *= 0.5f;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Blackbody look-up: table rows are (r, g, b) chroma with luminance 1, and log2 luminance relative to 6504 K.
// ---------------------------------------------------------------------------------------------
constant float BB_TMIN = 300.0f;
constant float BB_TMAX = 400000.0f;
constant int BB_N = 1024;

inline float4 bb_row(device const float4* lut, float T)
{
    float x = log(max(T, BB_TMIN)) - log(BB_TMIN);
    x = x / log(BB_TMAX / BB_TMIN) * float(BB_N - 1);
    x = clamp(x, 0.0f, float(BB_N) - 1.001f);
    int i = int(x);
    float f = x - float(i);
    return mix(lut[i], lut[i + 1], f);
}
inline float3 bb_radiance(device const float4* lut, float T)   // linear sRGB, 1.0 = visible luminance of a 6504 K body
{
    float4 r = bb_row(lut, T);
    return r.rgb * exp2(r.a);
}
inline float3 bb_chroma(device const float4* lut, float T) { return bb_row(lut, T).rgb; }

// ---------------------------------------------------------------------------------------------
// Novikov-Thorne temperature profile T(r)/T_peak, tabulated on [r_isco, r_out]
// ---------------------------------------------------------------------------------------------
constant int DISK_N = 1024;
inline float disk_temp_ratio(device const float* tab, float r, float r_isco, float r_out)
{
    float x = (r - r_isco) / (r_out - r_isco) * float(DISK_N - 1);
    x = clamp(x, 0.0f, float(DISK_N) - 1.001f);
    int i = int(x);
    float f = x - float(i);
    return mix(tab[i], tab[i + 1], f);
}

// ---------------------------------------------------------------------------------------------
// Turbulent surface brightness of the disk (dimensionless, mean ~1).  The pattern is advected by the
// Keplerian shear Omega(r); two staggered epochs are cross-faded so the shear never winds up forever.
// ---------------------------------------------------------------------------------------------
inline float disk_epoch_pattern(float r, float phi, float t_age, float epoch, float a)
{
    const float Om = bh::omega_kepler<float>(r, a);
    const float ph = phi - Om * t_age;
    const float ell = 0.55f + 0.09f * r;               // structure size grows with radius
    float2 xy = r * float2(cos(ph), sin(ph)) / ell;
    float3 p = float3(xy, epoch * 7.31f);
    float n = fbm3(p, 4);                                 // 0..1 blobs, sheared into spirals by Omega(r)
    float rad = vnoise(float3(r * 2.3f / ell * 1.6f, epoch * 3.7f, 11.0f));   // fine radial banding
    return 0.25f + 1.55f * n * (0.8f + 0.4f * rad);
}
inline float disk_pattern(float r, float phi, float t, float a)
{
    const float Tc = 46.0f;
    float x = t / Tc;
    float eA = floor(x);
    float ageA = (x - eA) * Tc;
    float y = x + 0.5f;
    float eB = floor(y);
    float ageB = (y - eB) * Tc;
    float wA = sin(PI_F * (x - eA));
    wA *= wA;
    float wB = 1.0f - wA;
    return wA * disk_epoch_pattern(r, phi, ageA, eA, a) + wB * disk_epoch_pattern(r, phi, ageB, eB + 101.0f, a);
}

// ---------------------------------------------------------------------------------------------
// Tone mapping helpers
// ---------------------------------------------------------------------------------------------
inline float3 aces_fitted(float3 x)
{
    // Stephen Hill's fitted ACES: sRGB->AP1 input, RRT+ODT, AP1->sRGB output
    const float3x3 M_in = float3x3(float3(0.59719f, 0.07600f, 0.02840f),
                                   float3(0.35458f, 0.90834f, 0.13383f),
                                   float3(0.04823f, 0.01566f, 0.83777f));
    const float3x3 M_out = float3x3(float3(1.60475f, -0.10208f, -0.00327f),
                                    float3(-0.53108f, 1.10813f, -0.07276f),
                                    float3(-0.07367f, -0.00605f, 1.07602f));
    float3 v = M_in * x;
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return clamp(M_out * (a / b), 0.0f, 1.0f);
}
