#version 410 core
// =============================================================================
//  sky_gen.frag — renders one face of the background cube map, once at start.
//
//  A procedural sky: stars with Euclidean number counts (N(>S) ~ S^-3/2) and
//  blackbody colours, plus a diffuse galactic band with dust lanes.  Baked
//  into a mip-mapped cube map so that the strongly minified, lensed images
//  near the photon ring are filtered correctly.
//  The sky is scenery: its brightness relative to the disk is a free choice.
// =============================================================================
in vec2 vUV;
out vec4 oColor;

uniform int   uFace;
uniform float uTexel;        // angular size of a texel at the face centre (rad)
uniform sampler2D uBlackbody;
uniform vec2  uLogT;
uniform float uLutN;
uniform int   uFirstLayer;  // star layers below this are left to the ray tracer

const float PI = 3.14159265358979;

vec3 faceDir(int face, vec2 uv) {
    if (face == 0) return vec3( 1.0, -uv.y, -uv.x);
    if (face == 1) return vec3(-1.0, -uv.y,  uv.x);
    if (face == 2) return vec3( uv.x,  1.0,  uv.y);
    if (face == 3) return vec3( uv.x, -1.0, -uv.y);
    if (face == 4) return vec3( uv.x, -uv.y,  1.0);
    return vec3(-uv.x, -uv.y, -1.0);
}

vec3 bbChroma(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    return texture(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5)).rgb;
}

#include "stars.glsl"

float hash3(ivec3 p) { return u01(pcg(uint(p.x) + pcg(uint(p.y) + pcg(uint(p.z))))); }
float valueNoise3(vec3 p) {
    vec3 i = floor(p), f = p - i;
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 c = ivec3(i);
    float n000 = hash3(c), n100 = hash3(c + ivec3(1, 0, 0));
    float n010 = hash3(c + ivec3(0, 1, 0)), n110 = hash3(c + ivec3(1, 1, 0));
    float n001 = hash3(c + ivec3(0, 0, 1)), n101 = hash3(c + ivec3(1, 0, 1));
    float n011 = hash3(c + ivec3(0, 1, 1)), n111 = hash3(c + ivec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
float fbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int o = 0; o < 6; ++o) {
        s += a * valueNoise3(p);
        p = p * 2.03 + vec3(11.7, 3.1, 7.3);
        a *= 0.5;
    }
    return s / 0.984;
}

// Faint stars (layers >= uFirstLayer) baked as small blobs.  The brighter
// layers are drawn by the ray tracer as true point sources.
vec3 stars(vec3 d, float density) {
    vec3 col = vec3(0.0);
    for (int L = uFirstLayer; L < 4; ++L) {
        vec3 sd;
        float flux, T;
        if (!starInCell(d, L, density, sd, flux, T)) continue;
        float sigma = max(STAR_SIGMA0, uTexel * 0.6);
        vec3 dd = d - sd;
        // same total flux whatever the texel size
        col += bbChroma(T) * flux * (STAR_SIGMA0 * STAR_SIGMA0 / (sigma * sigma)) * exp(-dot(dd, dd) / (2.0 * sigma * sigma));
    }
    return col;
}

void main() {
    vec3 d = normalize(faceDir(uFace, vUV * 2.0 - 1.0));

    float lat, lon;
    float gal = galacticDensity(d, lat, lon);

    // Diffuse band: unresolved starlight, modulated by clouds and dust lanes.
    float clouds = fbm3(d * 5.0 + 3.7);
    float fine   = fbm3(d * 23.0 - 9.1);
    float dustN  = fbm3(d * 9.0 + vec3(31.0, 5.0, 17.0));
    float lane   = exp(-lat * lat / (2.0 * 0.05 * 0.05));
    float dust   = 1.0 - 0.85 * lane * smoothstep(0.38, 0.62, dustN);
    float glow   = gal * (0.35 + 1.1 * clouds * clouds) * (0.7 + 0.6 * fine) * dust;

    float bulge = exp(-(lon * lon / (2.0 * 0.30 * 0.30) + lat * lat / (2.0 * 0.17 * 0.17)));
    vec3 tint = mix(vec3(0.72, 0.82, 1.0), vec3(1.0, 0.80, 0.58), clamp(bulge * 1.4 + 0.15, 0.0, 1.0));
    vec3 col = tint * glow * 0.050;

    // Faint reddish emission nebulosity along the plane.
    float neb = smoothstep(0.55, 0.85, fbm3(d * 3.1 + 40.0)) * lane;
    col += vec3(0.9, 0.22, 0.25) * neb * 0.02 * dust;

    // Resolved stars, denser towards the galactic plane.
    float starDensity = clamp(0.30 + 0.70 * min(gal, 1.0), 0.0, 1.0) * (0.35 + 0.65 * dust);
    col += stars(d, starDensity);

    oColor = vec4(col, 1.0);
}
