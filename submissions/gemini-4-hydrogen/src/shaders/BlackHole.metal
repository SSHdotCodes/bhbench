#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

constant float PI = 3.14159265358979323846f;

// ============================================================================
// Procedural Noise & Blackbody Radiation Helpers
// ============================================================================

inline float hash12(float2 p) {
    float3 p3 = fract(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.x + p3.y) * p3.z);
}

inline float hash13(float3 p3) {
    p3 = fract(p3 * 0.1031f);
    p3 += dot(p3, p3.zyx + 31.32f);
    return fract((p3.x + p3.y) * p3.z);
}

inline float2 hash23(float3 p3) {
    p3 = fract(p3 * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.xx + p3.yz) * p3.zy);
}

inline float valueNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = hash12(i + float2(0.0f, 0.0f));
    float b = hash12(i + float2(1.0f, 0.0f));
    float c = hash12(i + float2(0.0f, 1.0f));
    float d = hash12(i + float2(1.0f, 1.0f));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

inline float valueNoise3D(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    float3 u = f * f * (3.0f - 2.0f * f);

    float n000 = hash13(i + float3(0, 0, 0));
    float n100 = hash13(i + float3(1, 0, 0));
    float n010 = hash13(i + float3(0, 1, 0));
    float n110 = hash13(i + float3(1, 1, 0));
    float n001 = hash13(i + float3(0, 0, 1));
    float n101 = hash13(i + float3(1, 0, 1));
    float n011 = hash13(i + float3(0, 1, 1));
    float n111 = hash13(i + float3(1, 1, 1));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

inline float fbm2D(float2 p) {
    float sum = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.8f, 0.6f, -0.6f, 0.8f);
    for (int i = 0; i < 4; ++i) {
        sum += amp * valueNoise2D(p);
        p = rot * p * 2.07f;
        amp *= 0.5f;
    }
    return sum;
}

inline float fbm3D(float3 p) {
    float sum = 0.0f;
    float amp = 0.5f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * valueNoise3D(p);
        p = p * 2.03f + float3(13.1f, 7.7f, 5.3f);
        amp *= 0.5f;
    }
    return sum;
}

// Physically-based Planckian blackbody color (linear sRGB) for Kelvin temperature
inline float3 blackbodyLinearRGB(float tempK) {
    float t = clamp(tempK, 800.0f, 32000.0f) / 100.0f;
    float3 rgb;

    if (t <= 66.0f) {
        rgb.r = 1.0f;
        rgb.g = clamp((99.4708025861f * log(max(t, 1.0f)) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        if (t <= 19.0f) {
            rgb.b = 0.0f;
        } else {
            rgb.b = clamp((138.5177312231f * log(max(t - 10.0f, 1.0f)) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
        }
    } else {
        rgb.r = clamp((329.698727446f * pow(t - 60.0f, -0.1332047592f)) / 255.0f, 0.0f, 1.0f);
        rgb.g = clamp((288.1221695283f * pow(t - 60.0f, -0.0755148492f)) / 255.0f, 0.0f, 1.0f);
        rgb.b = 1.0f;
    }

    if (tempK < 1700.0f) {
        float warmFactor = clamp((tempK - 800.0f) / 900.0f, 0.0f, 1.0f);
        rgb = mix(float3(0.85f, 0.06f, 0.008f), rgb, warmFactor);
    }

    return pow(max(rgb, float3(0.0f)), float3(2.2f));
}

// ============================================================================
// Gravitational Lensing Sky Background
// ============================================================================

inline float3 sampleStarLayer(float3 dir, float scale, float density, float brightness) {
    float3 p = dir * scale;
    float3 cell = floor(p);
    float3 local = fract(p) - 0.5f;

    float3 accum = float3(0.0f);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                float3 nCell = cell + float3(dx, dy, dz);
                float rnd = hash13(nCell);
                if (rnd < density) {
                    float2 jit = hash23(nCell * 1.73f) - 0.5f;
                    float3 starOffset = float3(dx, dy, dz) + float3(jit.x, jit.y, hash13(nCell + 9.2f) - 0.5f) * 0.72f;
                    float dist = length(local - starOffset);

                    float size = mix(0.020f, 0.055f, fract(rnd * 91.7f));
                    float core = exp(-dist * dist / (size * size * 0.16f));
                    float halo = exp(-dist / (size * 0.85f)) * 0.18f;

                    float temp = mix(2800.0f, 17000.0f, pow(fract(rnd * 43.1f), 1.6f));
                    float lum = brightness * mix(0.25f, 2.4f, pow(fract(rnd * 17.3f), 3.0f));
                    accum += blackbodyLinearRGB(temp) * (core + halo) * lum;
                }
            }
        }
    }
    return accum;
}

inline float3 sampleDeepSpaceSky(float3 dir, float backgroundMode) {
    dir = normalize(dir);

    if (backgroundMode > 0.5f) {
        // Mode 1: Celestial Calibration Grid + Einstein Ring Reference Markers
        float theta = acos(clamp(dir.z, -1.0f, 1.0f));
        float phi = atan2(dir.y, dir.x);

        float latLines = abs(fract(theta / (PI / 12.0f) + 0.5f) - 0.5f);
        float lonLines = abs(fract(phi / (PI / 12.0f) + 0.5f) - 0.5f);
        float majorLat = abs(fract(theta / (PI / 4.0f) + 0.5f) - 0.5f);
        float majorLon = abs(fract(phi / (PI / 4.0f) + 0.5f) - 0.5f);

        float minor = max(smoothstep(0.045f, 0.01f, latLines),
                          smoothstep(0.045f, 0.01f, lonLines) * sin(theta));
        float major = max(smoothstep(0.03f, 0.008f, majorLat),
                          smoothstep(0.03f, 0.008f, majorLon) * sin(theta));

        float3 quadColor = float3(0.04f, 0.09f, 0.18f);
        if (dir.x > 0.0f && dir.z > 0.0f) quadColor = float3(0.14f, 0.06f, 0.04f);
        if (dir.x < 0.0f && dir.z > 0.0f) quadColor = float3(0.04f, 0.12f, 0.15f);
        if (dir.x < 0.0f && dir.z < 0.0f) quadColor = float3(0.10f, 0.05f, 0.14f);

        float3 col = quadColor * 0.35f;
        col += float3(0.18f, 0.42f, 0.72f) * minor * 0.45f;
        col += float3(0.65f, 0.85f, 1.0f) * major * 0.75f;
        col += sampleStarLayer(dir, 55.0f, 0.06f, 0.9f);
        return col;
    }

    // Mode 0: Deep Space + Tilted Milky Way + Dust Lanes + Multi-Temperature Stars
    float c = cos(0.52f), s = sin(0.52f);
    float3 gDir = float3(dir.x, dir.y * c - dir.z * s, dir.y * s + dir.z * c);

    float galLat = gDir.z;
    float galBand = exp(-galLat * galLat * 20.0f);
    float galCore = exp(-galLat * galLat * 8.5f) * pow(max(0.0f, -gDir.x * 0.5f + 0.5f), 2.8f);

    float nebula1 = fbm3D(gDir * 4.4f + float3(1.2f, -0.8f, 2.4f));
    float nebula2 = fbm3D(gDir * 9.0f - float3(3.1f, 1.9f, 0.6f));
    float dustLane = smoothstep(0.42f, 0.68f, fbm3D(gDir * 6.5f + float3(0.0f, 4.0f, 1.0f)));

    float3 spaceBase = float3(0.0012f, 0.0020f, 0.0045f);
    float3 milkBlue = float3(0.032f, 0.058f, 0.135f) * galBand * smoothstep(0.36f, 0.82f, nebula1);
    float3 milkWarm = float3(0.125f, 0.072f, 0.038f) * galCore * smoothstep(0.32f, 0.85f, nebula2);
    float3 milkMagenta = float3(0.055f, 0.018f, 0.075f) * galBand * smoothstep(0.56f, 0.88f, nebula2);

    float3 milkyWay = (milkBlue + milkWarm + milkMagenta) * (1.0f - 0.72f * dustLane * exp(-galLat * galLat * 48.0f));

    float3 stars = float3(0.0f);
    stars += sampleStarLayer(dir, 98.0f, 0.14f, 0.45f);
    stars += sampleStarLayer(dir, 48.0f, 0.09f, 1.05f);
    stars += sampleStarLayer(dir, 22.0f, 0.045f, 1.85f);

    return spaceBase + milkyWay + stars;
}

// ============================================================================
// Relativistic Accretion Disk Radiative Evaluation
// ============================================================================

inline float4 evaluateDiskCrossing(
    float r, float phi, float a, float rISCO, float rOut,
    float E_obs, float E, float Lz,
    float simTime, float dopplerEnabled, float diskBrightness)
{
    if (r < rISCO * 0.84f || r > rOut) {
        return float4(0.0f);
    }

    float innerFade = smoothstep(rISCO * 0.84f, rISCO * 1.06f, r);
    float outerFade = 1.0f - smoothstep(rOut * 0.58f, rOut, r);

    // Novikov-Thorne temperature profile T(r) ~ r^(-3/4) * (1 - sqrt(rISCO / r))^(1/4)
    float rEff = max(r, rISCO * 1.005f);
    float ntFactor = max(0.025f, 1.0f - sqrt(rISCO / rEff));
    float tempNorm = pow(rISCO / rEff, 0.75f) * pow(ntFactor, 0.25f);
    float T_em = 5400.0f * (tempNorm / 0.52f);
    if (r < rISCO) {
        T_em *= mix(0.50f, 1.0f, smoothstep(rISCO * 0.84f, rISCO, r));
    }

    // Keplerian prograde angular velocity Omega_K = 1 / (r^(3/2) + a)
    float omegaK = 1.0f / (pow(r, 1.5f) + a);

    // Differentially sheared MRI spiral turbulence
    float logR = log(max(r, 0.5f));
    float shearedPhi = phi - omegaK * simTime * 1.5f - 2.9f * logR;

    float2 ringCoord = float2(cos(shearedPhi), sin(shearedPhi)) * (2.4f + 1.2f * logR);
    float n1 = fbm2D(ringCoord + float2(logR * 6.0f, logR * 3.4f));
    float n2 = fbm2D(float2(cos(shearedPhi * 2.0f), sin(shearedPhi * 2.0f)) * 5.2f + float2(logR * 10.5f, -simTime * 0.08f));

    float ringWave = 0.5f + 0.5f * sin(logR * 24.0f + n1 * 4.8f);
    float turb = smoothstep(0.22f, 0.88f, n1 * 0.62f + n2 * 0.38f);
    float density = innerFade * outerFade * mix(0.18f, 1.35f, turb) * mix(0.60f, 1.25f, ringWave);

    // Innermost ISCO bright rim
    float iscoGlow = exp(-pow((r - rISCO * 1.14f) / (0.16f * rISCO), 2.0f)) * 0.50f;
    density += iscoGlow * outerFade;

    // Exact relativistic gravitational redshift + Doppler beaming factor g
    float g = 1.0f;
    if (dopplerEnabled > 0.5f) {
        float gttTerm = 1.0f - (2.0f / r) * pow(1.0f - a * omegaK, 2.0f) - (r * r + a * a) * omegaK * omegaK;
        float ut = 1.0f / sqrt(max(0.05f, gttTerm));
        float denom = max(0.08f, ut * (E - omegaK * Lz));
        g = clamp(E_obs / denom, 0.24f, 2.45f);
    }

    float T_obs = T_em * g;
    float3 col = blackbodyLinearRGB(T_obs);

    float beaming = pow(g, 3.4f);
    float emissivity = diskBrightness * beaming * density * (0.35f + 1.45f * pow(rISCO / rEff, 1.35f));

    float alpha = clamp(density * 0.85f, 0.0f, 0.94f);
    return float4(col * emissivity, alpha);
}

// ============================================================================
// Pass 1: Singularity-Free Kerr Geodesic Raytracing Compute Kernel
// ============================================================================

struct KerrConstants {
    float a;
    float E;
    float Lz;
    float Q;
    float K;
};

struct KerrState {
    float u;
    float vu;
    float3 q;
    float3 vq;
    float phiFD;
};

inline KerrState kerrRhs(KerrState s, KerrConstants c) {
    float u = s.u;
    float u2 = u * u;
    float a = c.a;
    float a2 = a * a;

    float term = c.E + a * (a * c.E - c.Lz) * u2;
    float ddu = 2.0f * a * (a * c.E - c.Lz) * u * term
              - c.K * (u - 3.0f * u2 + 2.0f * a2 * u2 * u);

    float vq2 = dot(s.vq, s.vq);
    float a2E2 = a2 * c.E * c.E;
    float qz = s.q.z;
    float radialAccel = -(vq2 + a2E2 * qz * qz);
    float3 ddq = radialAccel * s.q + float3(0.0f, 0.0f, a2E2 * qz);

    float deltaU = max(1e-5f, 1.0f - 2.0f * u + a2 * u2);
    float dphiFD = -(a * ((1.0f + a2 * u2) * c.E - a * c.Lz * u2) / deltaU - a * c.E);

    KerrState out;
    out.u = s.vu;
    out.vu = ddu;
    out.q = s.vq;
    out.vq = ddq;
    out.phiFD = dphiFD;
    return out;
}

inline KerrState stepAdd(KerrState a, KerrState b, float scale) {
    KerrState o;
    o.u = a.u + b.u * scale;
    o.vu = a.vu + b.vu * scale;
    o.q = a.q + b.q * scale;
    o.vq = a.vq + b.vq * scale;
    o.phiFD = a.phiFD + b.phiFD * scale;
    return o;
}

inline float3 rotateZ(float3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

kernel void raytraceBlackHole(
    texture2d<float, access::write> outColor [[texture(0)]],
    texture2d<float, access::write> outDepth [[texture(1)]],
    constant SimulationUniforms& ubo [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint width = uint(ubo.screenParams.x);
    uint height = uint(ubo.screenParams.y);
    if (gid.x >= width || gid.y >= height) return;

    float2 uv = (float2(gid) + 0.5f) * ubo.screenParams.zw;
    float sx = (2.0f * uv.x - 1.0f) * ubo.camRightAspect.w * ubo.camForwardFov.w;
    float sy = (1.0f - 2.0f * uv.y) * ubo.camForwardFov.w;

    float3 rayDirWorld = normalize(ubo.camForwardFov.xyz + sx * ubo.camRightAspect.xyz + sy * ubo.camUpDummy.xyz);

    float r0 = ubo.camBL.x;
    float theta0 = ubo.camBL.y;
    float phi0 = ubo.camBL.z;
    float cosT = ubo.camBL.w;
    float sinT = sin(theta0);
    float cosP = cos(phi0);
    float sinP = sin(phi0);

    float3 e_r     = float3(sinT * cosP, sinT * sinP, cosT);
    float3 e_theta = float3(cosT * cosP, cosT * sinP, -sinT);
    float3 e_phi   = float3(-sinP, cosP, 0.0f);

    float nr     = dot(rayDirWorld, e_r);
    float ntheta = dot(rayDirWorld, e_theta);
    float nphi   = dot(rayDirWorld, e_phi);

    float a = ubo.bhParams.x;
    float rp = ubo.bhParams.y;
    float rISCO = ubo.bhParams.z;
    float rOut = ubo.bhParams.w;

    float r2 = r0 * r0;
    float a2 = a * a;
    float sin2 = max(1e-8f, sinT * sinT);
    float sigma = r2 + a2 * cosT * cosT;
    float delta = r2 - 2.0f * r0 + a2;
    float A = (r2 + a2) * (r2 + a2) - a2 * delta * sin2;

    float varpi = sqrt(A * sin2 / sigma);
    float omega = (2.0f * a * r0) / A;
    float alphaLapse = sqrt(sigma * delta / A);

    KerrConstants c;
    c.a  = a;
    c.E  = alphaLapse - omega * varpi * nphi;
    c.Lz = -varpi * nphi;
    c.Q  = sigma * ntheta * ntheta + cosT * cosT * ((c.Lz * c.Lz) / sin2 - a2 * c.E * c.E);
    c.K  = c.Q + (c.Lz - a * c.E) * (c.Lz - a * c.E);

    KerrState s;
    s.u = 1.0f / r0;
    s.vu = -s.u * s.u * sqrt(max(0.0f, sigma * delta)) * nr;
    s.q = e_r;
    float vTheta = sqrt(sigma) * ntheta;
    float vPhiSphere = (varpi / max(1e-6f, sinT)) * nphi;
    s.vq = vTheta * e_theta + vPhiSphere * e_phi;
    s.phiFD = 0.0f;

    float uHorizon = 1.0f / (rp + 0.004f);
    float omega0 = sqrt(max(1e-4f, c.K + c.E * c.E));

    float3 accumColor = float3(0.0f);
    float transmittance = 1.0f;
    float hitDist = 1e6f;
    bool captured = false;

    float diskEnabled = ubo.diskParams.x;
    float dopplerEnabled = ubo.diskParams.y;
    float diskBrightness = ubo.diskParams.z;
    float haloIntensity = ubo.diskParams.w;
    float simTime = ubo.camPosTime.w;

    constexpr int MAX_STEPS = 560;
    for (int step = 0; step < MAX_STEPS; ++step) {
        float deltaU = max(1e-4f, 1.0f - 2.0f * s.u + a2 * s.u * s.u);
        float baseStep = (s.u < 0.06f) ? 0.11f : ((s.u < 0.16f) ? 0.055f : 0.032f);
        float h = (baseStep / (1.0f + 2.2f * s.u + 0.08f / deltaU)) / omega0;

        KerrState prev = s;

        KerrState k1 = kerrRhs(s, c);
        KerrState k2 = kerrRhs(stepAdd(s, k1, 0.5f * h), c);
        KerrState k3 = kerrRhs(stepAdd(s, k2, 0.5f * h), c);
        KerrState k4 = kerrRhs(stepAdd(s, k3, h), c);

        s.u     += (h / 6.0f) * (k1.u     + 2.0f * k2.u     + 2.0f * k3.u     + k4.u);
        s.vu    += (h / 6.0f) * (k1.vu    + 2.0f * k2.vu    + 2.0f * k3.vu    + k4.vu);
        s.q     += (h / 6.0f) * (k1.q     + 2.0f * k2.q     + 2.0f * k3.q     + k4.q);
        s.vq    += (h / 6.0f) * (k1.vq    + 2.0f * k2.vq    + 2.0f * k3.vq    + k4.vq);
        s.phiFD += (h / 6.0f) * (k1.phiFD + 2.0f * k2.phiFD + 2.0f * k3.phiFD + k4.phiFD);

        // Enforce unit sphere & orthogonality to eliminate numerical drift
        s.q = normalize(s.q);
        s.vq -= dot(s.q, s.vq) * s.q;

        float rCurr = (s.u > 1e-5f) ? (1.0f / s.u) : 1e5f;

        // Subtle grazing photon-sphere & equatorial plasma corona halo
        // Zero glow inside directly-plunging rays (keeps event horizon shadow pitch black!)
        if (haloIntensity > 0.01f && rCurr < rOut && rCurr > rp * 1.06f) {
            float dsApprox = min(0.75f, length(s.q - prev.q) * rCurr + abs(s.u - prev.u) / max(1e-4f, s.u * max(s.u, prev.u)));
            float grazing = 1.0f - clamp(abs(s.vu) / max(1e-3f, omega0 * s.u * s.u + c.E), 0.0f, 1.0f);
            float radialTangential = 1.0f - clamp(abs(s.vu) / max(0.25f, c.E), 0.0f, 1.0f);
            grazing = max(grazing, radialTangential);

            float rHaloCenter = rp * 1.48f;
            float photonGlow = exp(-pow((rCurr - rHaloCenter) / (0.38f * rp), 2.0f)) * pow(grazing, 3.0f);

            float zHeight = rCurr * abs(s.q.z);
            float scaleH = 0.065f * rCurr + 0.08f;
            float diskHalo = (diskEnabled > 0.5f)
                ? exp(-0.5f * (zHeight * zHeight) / (scaleH * scaleH)) * smoothstep(rISCO * 0.9f, rISCO * 1.25f, rCurr) * pow(max(0.0f, rISCO / rCurr), 2.2f)
                : 0.0f;

            float coronaBeam = (dopplerEnabled > 0.5f) ? clamp(1.0f + 0.45f * (c.Lz / max(1.5f, rCurr)), 0.45f, 1.85f) : 1.0f;
            float3 photonCol = mix(float3(1.0f, 0.50f, 0.16f), float3(0.65f, 0.84f, 1.0f), clamp((coronaBeam - 0.7f) * 0.8f, 0.0f, 1.0f));
            float3 coronaCol = float3(1.0f, 0.62f, 0.28f) * coronaBeam;

            float3 haloEmit = (photonCol * photonGlow * 0.032f + coronaCol * diskHalo * 0.014f) * haloIntensity * dsApprox;
            accumColor += transmittance * haloEmit;
        }

        // Check equatorial accretion disk crossing (q.z changes sign)
        if (diskEnabled > 0.5f && ((prev.q.z > 0.0f && s.q.z <= 0.0f) || (prev.q.z < 0.0f && s.q.z >= 0.0f))) {
            float denom = abs(s.q.z - prev.q.z);
            float frac = (denom > 1e-7f) ? clamp(abs(prev.q.z) / denom, 0.0f, 1.0f) : 0.5f;
            float uHit = mix(prev.u, s.u, frac);
            float phiFDHit = mix(prev.phiFD, s.phiFD, frac);
            float3 qHit = normalize(mix(prev.q, s.q, frac));
            float3 worldHitDir = rotateZ(qHit, phiFDHit);
            float phiHit = atan2(worldHitDir.y, worldHitDir.x);

            if (uHit > 1e-4f) {
                float rHit = 1.0f / uHit;
                float4 diskSample = evaluateDiskCrossing(
                    rHit, phiHit, a, rISCO, rOut,
                    c.E, c.E, c.Lz,
                    simTime, dopplerEnabled, diskBrightness);
                if (diskSample.a > 0.001f) {
                    accumColor += transmittance * diskSample.rgb * diskSample.a;
                    transmittance *= (1.0f - diskSample.a);

                    if (hitDist > 1e5f && transmittance < 0.65f) {
                        float3 diskWorld = float3(rHit * cos(phiHit), rHit * sin(phiHit), 0.0f);
                        hitDist = length(diskWorld - ubo.camPosTime.xyz);
                    }
                    if (transmittance < 0.015f) {
                        break;
                    }
                }
            }
        }

        // Event horizon capture (pitch-black shadow)
        if (s.u >= uHorizon) {
            captured = true;
            if (hitDist > 1e5f) {
                hitDist = max(0.2f, r0 - 0.3f * rp);
            }
            break;
        }

        // Escaped to asymptotic infinity (u crosses 0)
        if (s.u <= 0.0f && s.vu < 0.0f) {
            float frac = clamp(prev.u / max(1e-8f, prev.u - s.u), 0.0f, 1.0f);
            float3 qInf = normalize(mix(prev.q, s.q, frac));
            float phiFDInf = mix(prev.phiFD, s.phiFD, frac);
            float3 skyDir = rotateZ(qInf, phiFDInf);
            accumColor += transmittance * sampleDeepSpaceSky(skyDir, ubo.visualParams.x);
            transmittance = 0.0f;
            break;
        }
    }

    if (!captured && transmittance > 0.01f) {
        if (s.u > 0.22f) {
            if (hitDist > 1e5f) {
                hitDist = max(0.2f, r0 - 0.3f * rp);
            }
        } else {
            float3 skyDir = rotateZ(s.q, s.phiFD);
            accumColor += transmittance * sampleDeepSpaceSky(skyDir, ubo.visualParams.x);
        }
    }

    outColor.write(float4(accumColor, 1.0f), gid);
    outDepth.write(float4(hitDist, 0.0f, 0.0f, 0.0f), gid);
}

// ============================================================================
// Pass 2: 3D Spacetime Curvature ("Trapdoor in Spacetime") Grid
// ============================================================================

struct GridVaryings {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float r;
    float twist;
    float camDist;
};

vertex GridVaryings vertexSpacetimeGrid(
    uint vertexId [[vertex_id]],
    constant GridVertex* vertices [[buffer(0)]],
    constant SimulationUniforms& ubo [[buffer(1)]])
{
    GridVertex v = vertices[vertexId];
    float3 worldPos = v.posAndInfo.xyz;
    worldPos.z += ubo.gridParams.y;

    GridVaryings out;
    out.position = ubo.viewProj * float4(worldPos, 1.0f);
    out.worldPos = worldPos;
    out.normal = v.normalAndTwist.xyz;
    out.r = v.posAndInfo.w;
    out.twist = v.normalAndTwist.w;
    out.camDist = length(worldPos - ubo.camPosTime.xyz);
    return out;
}

fragment float4 fragmentSpacetimeGrid(
    GridVaryings in [[stage_in]],
    texture2d<float, access::read> depthTex [[texture(0)]],
    constant SimulationUniforms& ubo [[buffer(0)]])
{
    if (ubo.gridParams.x < 0.5f) {
        discard_fragment();
    }

    uint texW = depthTex.get_width();
    uint texH = depthTex.get_height();
    float4 clip = ubo.viewProj * float4(in.worldPos, 1.0f);
    float2 ndc = clip.xy / clip.w;
    float2 texUV = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    uint2 dCoord = uint2(clamp(int2(texUV * float2(texW, texH)), int2(0), int2(int(texW) - 1, int(texH) - 1)));
    float sceneHitDist = depthTex.read(dCoord).r;

    float occlusion = 1.0f - smoothstep(sceneHitDist - 0.35f, sceneHitDist + 0.35f, in.camDist);
    if (occlusion < 0.01f) {
        discard_fragment();
    }

    float rp = ubo.bhParams.y;
    float rISCO = ubo.bhParams.z;
    float rOuter = ubo.gridParams.z;
    float simTime = ubo.camPosTime.w;

    float phi = atan2(in.worldPos.y, in.worldPos.x);
    float twistedPhi = phi - in.twist;
    float2 gridUV = float2(in.r * cos(twistedPhi), in.r * sin(twistedPhi));

    // Anti-aliased Cartesian square grid lines using screen-space derivatives
    float gridSpacing = 2.0f;
    float2 coord = gridUV / gridSpacing;
    float2 gridDeriv = fwidth(coord);
    float2 gridDist = abs(fract(coord - 0.5f) - 0.5f) / max(gridDeriv, float2(1e-4f));
    float cartesianLine = 1.0f - min(min(gridDist.x, gridDist.y), 1.0f);

    // Anti-aliased concentric radial curvature rings
    float rCoord = in.r / 2.0f;
    float rDist = abs(fract(rCoord - 0.5f) - 0.5f) / max(fwidth(rCoord), 1e-4f);
    float radialRing = 1.0f - min(rDist, 1.0f);

    // Physical GR radius markers on the trapdoor surface
    float horizonRing = exp(-pow((in.r - rp) / 0.09f, 2.0f));
    float iscoRing    = exp(-pow((in.r - rISCO) / 0.08f, 2.0f));
    float ergoRing    = (ubo.bhParams.x > 0.05f) ? exp(-pow((in.r - 2.0f) / 0.08f, 2.0f)) : 0.0f;

    float curvatureFactor = clamp(pow(rp / max(in.r, rp * 0.85f), 0.72f), 0.0f, 1.0f);
    float3 outerCol  = float3(0.10f, 0.62f, 1.00f);
    float3 midCol    = float3(0.95f, 0.52f, 0.14f);
    float3 throatCol = float3(1.00f, 0.18f, 0.05f);
    float3 baseLineCol = (curvatureFactor < 0.5f)
        ? mix(outerCol, midCol, curvatureFactor * 2.0f)
        : mix(midCol, throatCol, (curvatureFactor - 0.5f) * 2.0f);

    float infallPulse = 0.0f;
    if (ubo.visualParams.z > 0.5f && in.r > rp * 0.72f) {
        float wavePhase = fract(in.r * 0.28f + simTime * 0.65f);
        float pulse = exp(-pow((wavePhase - 0.5f) / 0.065f, 2.0f));
        infallPulse = pulse * 0.35f * smoothstep(rOuter * 0.85f, rp * 1.1f, in.r);
    }

    float edgeFade = 1.0f - smoothstep(rOuter * 0.62f, rOuter * 0.96f, in.r);
    float horizonInteriorFade = smoothstep(rp * 0.68f, rp * 0.94f, in.r);

    float lineStrength = max(cartesianLine * 0.90f, radialRing * 0.34f);
    float3 color = baseLineCol * lineStrength * (0.65f + 1.05f * curvatureFactor);
    color += baseLineCol * (0.018f + 0.075f * curvatureFactor);

    color += float3(1.0f, 0.28f, 0.08f) * horizonRing * 2.5f;
    color += float3(0.22f, 0.95f, 0.88f) * iscoRing * 0.75f;
    color += float3(0.80f, 0.42f, 1.00f) * ergoRing * 0.55f;
    color += mix(float3(0.28f, 0.82f, 1.0f), float3(1.0f, 0.52f, 0.18f), curvatureFactor) * infallPulse;

    float alpha = (lineStrength * 0.86f + 0.025f + horizonRing * 0.85f + iscoRing * 0.45f + infallPulse * 0.45f)
                * edgeFade * horizonInteriorFade * ubo.gridParams.w * occlusion;

    return float4(color * alpha, clamp(alpha, 0.0f, 1.0f));
}

// ============================================================================
// Pass 3: MPS Bloom Extraction & ACES Filmic Composite
// ============================================================================

kernel void extractBloomBright(
    texture2d<float, access::read> inHDR [[texture(0)]],
    texture2d<float, access::write> outBloom [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint outW = outBloom.get_width();
    uint outH = outBloom.get_height();
    if (gid.x >= outW || gid.y >= outH) return;

    uint inW = inHDR.get_width();
    uint inH = inHDR.get_height();
    float2 uv = (float2(gid) + 0.5f) / float2(outW, outH);
    uint2 src = uint2(clamp(int2(uv * float2(inW, inH)), int2(0), int2(int(inW) - 1, int(inH) - 1)));

    float3 c0 = inHDR.read(src).rgb;
    float3 c1 = inHDR.read(min(src + uint2(1, 0), uint2(inW - 1, inH - 1))).rgb;
    float3 c2 = inHDR.read(min(src + uint2(0, 1), uint2(inW - 1, inH - 1))).rgb;
    float3 c3 = inHDR.read(min(src + uint2(1, 1), uint2(inW - 1, inH - 1))).rgb;
    float3 col = 0.25f * (c0 + c1 + c2 + c3);

    float lum = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
    float threshold = 1.05f;
    float softKnee = 0.45f;
    float rq = clamp(lum - threshold + softKnee, 0.0f, 2.0f * softKnee);
    float weight = max(rq * rq / (4.0f * softKnee + 1e-5f), max(0.0f, lum - threshold)) / max(lum, 1e-5f);

    outBloom.write(float4(col * clamp(weight, 0.0f, 3.5f), 1.0f), gid);
}

struct QuadVaryings {
    float4 position [[position]];
    float2 uv;
};

vertex QuadVaryings vertexFullscreenQuad(uint vid [[vertex_id]]) {
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f)
    };
    float2 uvs[3] = {
        float2(0.0f, 1.0f),
        float2(2.0f, 1.0f),
        float2(0.0f, -1.0f)
    };
    QuadVaryings out;
    out.position = float4(positions[vid], 0.0f, 1.0f);
    out.uv = uvs[vid];
    return out;
}

inline float3 acesFilmic(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

fragment float4 fragmentCompositeFinal(
    QuadVaryings in [[stage_in]],
    texture2d<float, access::sample> sceneHDR [[texture(0)]],
    texture2d<float, access::sample> bloomTight [[texture(1)]],
    texture2d<float, access::sample> bloomWide [[texture(2)]],
    constant SimulationUniforms& ubo [[buffer(0)]])
{
    constexpr sampler linSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);

    float3 hdr = sceneHDR.sample(linSampler, in.uv).rgb;
    float3 b1  = bloomTight.sample(linSampler, in.uv).rgb;
    float3 b2  = bloomWide.sample(linSampler, in.uv).rgb;

    float bloomAmt = ubo.visualParams.y;
    float3 combined = hdr + bloomAmt * (0.60f * b1 + 0.45f * b2);

    float2 centered = in.uv - 0.5f;
    float vignette = 1.0f - dot(centered, centered) * 0.25f;
    combined *= vignette * ubo.visualParams.w;

    float3 mapped = acesFilmic(combined);
    float3 srgb = pow(mapped, float3(1.0f / 2.2f));
    return float4(srgb, 1.0f);
}
