// Shading shared by the Metal kernel and the CPU reference renderer.
//
// Disk emission: Novikov-Thorne thin disk. The bolometric surface flux F(r) is normalised by its
// maximum and read from a table. An observed photon from radius r has redshift factor
// g = E_obs / E_emit; the specific intensity transforms as I_obs = g^4 I_emit and the blackbody
// temperature as T_obs = g T_emit, so the colour comes from the Planck curve at g * T_emit.
//
// Colour: blackbody chromaticity uses the Tanner Helland polynomial fit (about 1000..40000 K),
// which is an sRGB approximation of the Planck spectrum. Brightness is physical up to the display
// gain and exposure.
#ifndef BH_SHADING_H
#define BH_SHADING_H

#include "shared_types.h"
#include "kerr_core.h"

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hash_unit(uint h) { return float(h & 0xFFFFFFu) * (1.0f / 16777216.0f); }

// Linear-sRGB colour of a blackbody at temperature Tk, normalised to unit luminance.
inline V3<float> blackbody_rgb(float Tk) {
    float t = clampf(Tk, 1000.0f, 40000.0f) / 100.0f;
    float r = 255.0f;
    float g;
    float b;
    if (t > 66.0f) {
        r = 329.698727446f * pow(t - 60.0f, -0.1332047592f);
        g = 288.1221695283f * pow(t - 60.0f, -0.0755148492f);
        b = 255.0f;
    } else {
        g = 99.4708025861f * log(t) - 161.1195681661f;
        b = t <= 19.0f ? 0.0f : 138.5177312231f * log(t - 10.0f) - 305.0447927307f;
    }
    r = clampf(r, 0.0f, 255.0f) / 255.0f;
    g = clampf(g, 0.0f, 255.0f) / 255.0f;
    b = clampf(b, 0.0f, 255.0f) / 255.0f;
    // sRGB -> linear (gamma 2.2 approximation), then unit luminance.
    r = pow(r, 2.2f);
    g = pow(g, 2.2f);
    b = pow(b, 2.2f);
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    V3<float> c = v3(r / lum, g / lum, b / lum);
    return c;
}

// Background sky for an escaped photon whose asymptotic direction is d (unit vector).
// Stars are placed on a cube-map grid of cells; the Milky Way is a broad band around a galactic plane.
// The optional grid is a latitude/longitude grid on the sky sphere, which the lensing visibly bends.
inline V3<float> background(V3<float> d, int mode) {
    V3<float> col = v3(0.0f, 0.0f, 0.0f);

    if (mode == 0 || mode == 2) {
        // Stars on latitude/longitude bins of 0.6 degrees. A bin holds a star with probability
        // proportional to sin(theta), so the density is uniform on the sky (no pole crowding).
        const float step = 0.0104719755f;   // 0.6 degrees
        float theta = acos(clampf(d.z, -1.0f, 1.0f));
        float phi = atan2(d.y, d.x);
        int bt = int(floor(theta / step));
        int bp = int(floor(phi / step));
        for (int dt = -1; dt <= 1; ++dt) {
            for (int dp = -1; dp <= 1; ++dp) {
                int it = bt + dt;
                int ip = bp + dp;
                uint seed = hash_u32(uint(it) * 0x9E3779B1u ^ hash_u32(uint(ip) * 0x85EBCA77u + 0x632BE5ABu));
                uint h1 = hash_u32(seed + 1u);
                uint h2 = hash_u32(seed + 2u);
                uint h3 = hash_u32(seed + 3u);
                uint h4 = hash_u32(seed + 4u);
                uint h5 = hash_u32(seed + 5u);
                float thc = (float(it) + 0.5f) * step;
                if (hash_unit(h1) > 0.035f * sin(clampf(thc, 0.0f, 3.14159f))) {
                    continue;
                }
                // Star position inside its bin.
                float ts = (float(it) + hash_unit(h2)) * step;
                float ps = (float(ip) + hash_unit(h3)) * step;
                V3<float> s = v3(sin(ts) * cos(ps), sin(ts) * sin(ps), cos(ts));
                float omc = 1.0f - (s.x * d.x + s.y * d.y + s.z * d.z);   // about theta^2/2
                const float sigma2 = 0.0000045f;                          // (0.12 deg)^2
                float mag = 0.12f + 2.6f * pow(hash_unit(h4), 10.0f);
                float tStar = 3300.0f + 9000.0f * hash_unit(h5);
                V3<float> c = blackbody_rgb(tStar);
                float w = exp(-omc / sigma2);
                col = col + (mag * w) * c;
            }
        }
        // Milky Way band: broad Gaussian in the angle to a galactic plane.
        V3<float> ng = norm3(v3(-0.42f, 0.31f, 0.85f));
        float sinb = d.x * ng.x + d.y * ng.y + d.z * ng.z;
        float band = exp(-(sinb * sinb) / 0.012f);
        // Smooth mottling along the band (no cell structure).
        float mottle = 0.65f + 0.35f * sin(5.0f * d.x + 1.7f) * cos(4.0f * d.y - 0.6f) * sin(3.5f * d.z + 2.2f);
        col = col + (0.045f * band * mottle) * v3(0.85f, 0.9f, 1.0f);
    }

    if (mode == 1 || mode == 2) {
        // Latitude/longitude grid every 15 degrees on the celestial sphere.
        float theta = acos(clampf(d.z, -1.0f, 1.0f));
        float phi = atan2(d.y, d.x);
        const float step = 0.2617993878f;   // 15 degrees
        float ft = theta / step;
        float dt = fabs(ft - floor(ft + 0.5f)) * step;
        float fp = phi / step;
        float dp = fabs(fp - floor(fp + 0.5f)) * step * clampf(sin(theta), 0.05f, 1.0f);
        float lineT = exp(-(dt * dt) / (0.0045f * 0.0045f));
        float lineP = exp(-(dp * dp) / (0.0045f * 0.0045f));
        float lines = clampf(lineT + lineP, 0.0f, 1.0f);
        col = col + (0.12f * lines) * v3(0.35f, 0.8f, 1.0f);
    }
    return col;
}

// Radiance of the disk at emission radius r with redshift g.
template <typename P> inline V3<float> disk_radiance(const TraceUniforms& U, P lut, float r, float g) {
    float F = disk_flux_norm(lut, U.lut_n, r, U.r_isco, U.r_out);   // F / F_max
    float Tem = U.T_peak * pow(F, 0.25f);                            // emitted temperature
    float Tobs = g * Tem;                                            // observed temperature
    V3<float> col = blackbody_rgb(Tobs);
    float g2 = g * g;
    float I = U.disk_gain * g2 * g2 * F;                             // I_obs = g^4 I_emit
    return I * col;
}

// Final colour of a traced pixel before tone mapping.
template <typename T, typename P> inline V3<float> shade_hit(const TraceUniforms& U, P lut, const Hit<T>& h) {
    V3<float> c = v3(0.0f, 0.0f, 0.0f);
    if (h.outcome == OUT_ESCAPED) {
        V3<float> d = v3(float(h.dir.x), float(h.dir.y), float(h.dir.z));
        c = background(d, U.bg_mode);
    } else if (h.outcome == OUT_DISK && U.disk_on != 0) {
        c = disk_radiance(U, lut, float(h.r), float(h.g));
    }
    return c;
}

#endif
