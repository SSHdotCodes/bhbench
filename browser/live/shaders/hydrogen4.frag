#version 300 es
precision highp float;
precision highp int;
uniform vec4 camPosTime, camForwardFov, camRightAspect, camUpDummy, camBL;
uniform vec4 bhParams, diskParams, gridParams, visualParams, screenParams;
uniform mat4 viewProj;
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outDepth;
const float PI = 3.14159265358979323846;

// ============================================================================
// Procedural Noise & Blackbody Radiation Helpers
// ============================================================================

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash23(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float valueNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i + vec2(0.0, 0.0));
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float valueNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0, 0, 0));
    float n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0));
    float n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1));
    float n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1));
    float n111 = hash13(i + vec3(1, 1, 1));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

float fbm2D(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 4; ++i) {
        sum += amp * valueNoise2D(p);
        p = rot * p * 2.07;
        amp *= 0.5;
    }
    return sum;
}

float fbm3D(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amp * valueNoise3D(p);
        p = p * 2.03 + vec3(13.1, 7.7, 5.3);
        amp *= 0.5;
    }
    return sum;
}

// Physically-based Planckian blackbody color (linear sRGB) for Kelvin temperature
vec3 blackbodyLinearRGB(float tempK) {
    float t = clamp(tempK, 800.0, 32000.0) / 100.0;
    vec3 rgb;

    if (t <= 66.0) {
        rgb.r = 1.0;
        rgb.g = clamp((99.4708025861 * log(max(t, 1.0)) - 161.1195681661) / 255.0, 0.0, 1.0);
        if (t <= 19.0) {
            rgb.b = 0.0;
        } else {
            rgb.b = clamp((138.5177312231 * log(max(t - 10.0, 1.0)) - 305.0447927307) / 255.0, 0.0, 1.0);
        }
    } else {
        rgb.r = clamp((329.698727446 * pow(t - 60.0, -0.1332047592)) / 255.0, 0.0, 1.0);
        rgb.g = clamp((288.1221695283 * pow(t - 60.0, -0.0755148492)) / 255.0, 0.0, 1.0);
        rgb.b = 1.0;
    }

    if (tempK < 1700.0) {
        float warmFactor = clamp((tempK - 800.0) / 900.0, 0.0, 1.0);
        rgb = mix(vec3(0.85, 0.06, 0.008), rgb, warmFactor);
    }

    return pow(max(rgb, vec3(0.0)), vec3(2.2));
}

// ============================================================================
// Gravitational Lensing Sky Background
// ============================================================================

vec3 sampleStarLayer(vec3 dir, float scale, float density, float brightness) {
    vec3 p = dir * scale;
    vec3 cell = floor(p);
    vec3 local = fract(p) - 0.5;

    vec3 accum = vec3(0.0);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                vec3 nCell = cell + vec3(dx, dy, dz);
                float rnd = hash13(nCell);
                if (rnd < density) {
                    vec2 jit = hash23(nCell * 1.73) - 0.5;
                    vec3 starOffset = vec3(dx, dy, dz) + vec3(jit.x, jit.y, hash13(nCell + 9.2) - 0.5) * 0.72;
                    float dist = length(local - starOffset);

                    float size = mix(0.020, 0.055, fract(rnd * 91.7));
                    float core = exp(-dist * dist / (size * size * 0.16));
                    float halo = exp(-dist / (size * 0.85)) * 0.18;

                    float temp = mix(2800.0, 17000.0, pow(fract(rnd * 43.1), 1.6));
                    float lum = brightness * mix(0.25, 2.4, pow(fract(rnd * 17.3), 3.0));
                    accum += blackbodyLinearRGB(temp) * (core + halo) * lum;
                }
            }
        }
    }
    return accum;
}

vec3 sampleDeepSpaceSky(vec3 dir, float backgroundMode) {
    dir = normalize(dir);

    if (backgroundMode > 0.5) {
        // Mode 1: Celestial Calibration Grid + Einstein Ring Reference Markers
        float theta = acos(clamp(dir.z, -1.0, 1.0));
        float phi = atan(dir.y, dir.x);

        float latLines = abs(fract(theta / (PI / 12.0) + 0.5) - 0.5);
        float lonLines = abs(fract(phi / (PI / 12.0) + 0.5) - 0.5);
        float majorLat = abs(fract(theta / (PI / 4.0) + 0.5) - 0.5);
        float majorLon = abs(fract(phi / (PI / 4.0) + 0.5) - 0.5);

        float minor = max(smoothstep(0.045, 0.01, latLines),
                          smoothstep(0.045, 0.01, lonLines) * sin(theta));
        float major = max(smoothstep(0.03, 0.008, majorLat),
                          smoothstep(0.03, 0.008, majorLon) * sin(theta));

        vec3 quadColor = vec3(0.04, 0.09, 0.18);
        if (dir.x > 0.0 && dir.z > 0.0) quadColor = vec3(0.14, 0.06, 0.04);
        if (dir.x < 0.0 && dir.z > 0.0) quadColor = vec3(0.04, 0.12, 0.15);
        if (dir.x < 0.0 && dir.z < 0.0) quadColor = vec3(0.10, 0.05, 0.14);

        vec3 col = quadColor * 0.35;
        col += vec3(0.18, 0.42, 0.72) * minor * 0.45;
        col += vec3(0.65, 0.85, 1.0) * major * 0.75;
        col += sampleStarLayer(dir, 55.0, 0.06, 0.9);
        return col;
    }

    // Mode 0: Deep Space + Tilted Milky Way + Dust Lanes + Multi-Temperature Stars
    float c = cos(0.52), s = sin(0.52);
    vec3 gDir = vec3(dir.x, dir.y * c - dir.z * s, dir.y * s + dir.z * c);

    float galLat = gDir.z;
    float galBand = exp(-galLat * galLat * 20.0);
    float galCore = exp(-galLat * galLat * 8.5) * pow(max(0.0, -gDir.x * 0.5 + 0.5), 2.8);

    float nebula1 = fbm3D(gDir * 4.4 + vec3(1.2, -0.8, 2.4));
    float nebula2 = fbm3D(gDir * 9.0 - vec3(3.1, 1.9, 0.6));
    float dustLane = smoothstep(0.42, 0.68, fbm3D(gDir * 6.5 + vec3(0.0, 4.0, 1.0)));

    vec3 spaceBase = vec3(0.0012, 0.0020, 0.0045);
    vec3 milkBlue = vec3(0.032, 0.058, 0.135) * galBand * smoothstep(0.36, 0.82, nebula1);
    vec3 milkWarm = vec3(0.125, 0.072, 0.038) * galCore * smoothstep(0.32, 0.85, nebula2);
    vec3 milkMagenta = vec3(0.055, 0.018, 0.075) * galBand * smoothstep(0.56, 0.88, nebula2);

    vec3 milkyWay = (milkBlue + milkWarm + milkMagenta) * (1.0 - 0.72 * dustLane * exp(-galLat * galLat * 48.0));

    vec3 stars = vec3(0.0);
    stars += sampleStarLayer(dir, 98.0, 0.14, 0.45);
    stars += sampleStarLayer(dir, 48.0, 0.09, 1.05);
    stars += sampleStarLayer(dir, 22.0, 0.045, 1.85);

    return spaceBase + milkyWay + stars;
}

// ============================================================================
// Relativistic Accretion Disk Radiative Evaluation
// ============================================================================

vec4 evaluateDiskCrossing(
    float r, float phi, float a, float rISCO, float rOut,
    float E_obs, float E, float Lz,
    float simTime, float dopplerEnabled, float diskBrightness)
{
    if (r < rISCO * 0.84 || r > rOut) {
        return vec4(0.0);
    }

    float innerFade = smoothstep(rISCO * 0.84, rISCO * 1.06, r);
    float outerFade = 1.0 - smoothstep(rOut * 0.58, rOut, r);

    // Novikov-Thorne temperature profile T(r) ~ r^(-3/4) * (1 - sqrt(rISCO / r))^(1/4)
    float rEff = max(r, rISCO * 1.005);
    float ntFactor = max(0.025, 1.0 - sqrt(rISCO / rEff));
    float tempNorm = pow(rISCO / rEff, 0.75) * pow(ntFactor, 0.25);
    float T_em = 5400.0 * (tempNorm / 0.52);
    if (r < rISCO) {
        T_em *= mix(0.50, 1.0, smoothstep(rISCO * 0.84, rISCO, r));
    }

    // Keplerian prograde angular velocity Omega_K = 1 / (r^(3/2) + a)
    float omegaK = 1.0 / (pow(r, 1.5) + a);

    // Differentially sheared MRI spiral turbulence
    float logR = log(max(r, 0.5));
    float shearedPhi = phi - omegaK * simTime * 1.5 - 2.9 * logR;

    vec2 ringCoord = vec2(cos(shearedPhi), sin(shearedPhi)) * (2.4 + 1.2 * logR);
    float n1 = fbm2D(ringCoord + vec2(logR * 6.0, logR * 3.4));
    float n2 = fbm2D(vec2(cos(shearedPhi * 2.0), sin(shearedPhi * 2.0)) * 5.2 + vec2(logR * 10.5, -simTime * 0.08));

    float ringWave = 0.5 + 0.5 * sin(logR * 24.0 + n1 * 4.8);
    float turb = smoothstep(0.22, 0.88, n1 * 0.62 + n2 * 0.38);
    float density = innerFade * outerFade * mix(0.18, 1.35, turb) * mix(0.60, 1.25, ringWave);

    // Innermost ISCO bright rim
    float iscoGlow = exp(-pow((r - rISCO * 1.14) / (0.16 * rISCO), 2.0)) * 0.50;
    density += iscoGlow * outerFade;

    // Exact relativistic gravitational redshift + Doppler beaming factor g
    float g = 1.0;
    if (dopplerEnabled > 0.5) {
        float gttTerm = 1.0 - (2.0 / r) * pow(1.0 - a * omegaK, 2.0) - (r * r + a * a) * omegaK * omegaK;
        float ut = 1.0 / sqrt(max(0.05, gttTerm));
        float denom = max(0.08, ut * (E - omegaK * Lz));
        g = clamp(E_obs / denom, 0.24, 2.45);
    }

    float T_obs = T_em * g;
    vec3 col = blackbodyLinearRGB(T_obs);

    float beaming = pow(g, 3.4);
    float emissivity = diskBrightness * beaming * density * (0.35 + 1.45 * pow(rISCO / rEff, 1.35));

    float alpha = clamp(density * 0.85, 0.0, 0.94);
    return vec4(col * emissivity, alpha);
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
    vec3 q;
    vec3 vq;
    float phiFD;
};

KerrState kerrRhs(KerrState s, KerrConstants c) {
    float u = s.u;
    float u2 = u * u;
    float a = c.a;
    float a2 = a * a;

    float term = c.E + a * (a * c.E - c.Lz) * u2;
    float ddu = 2.0 * a * (a * c.E - c.Lz) * u * term
              - c.K * (u - 3.0 * u2 + 2.0 * a2 * u2 * u);

    float vq2 = dot(s.vq, s.vq);
    float a2E2 = a2 * c.E * c.E;
    float qz = s.q.z;
    float radialAccel = -(vq2 + a2E2 * qz * qz);
    vec3 ddq = radialAccel * s.q + vec3(0.0, 0.0, a2E2 * qz);

    float deltaU = max(1e-5, 1.0 - 2.0 * u + a2 * u2);
    float dphiFD = -(a * ((1.0 + a2 * u2) * c.E - a * c.Lz * u2) / deltaU - a * c.E);

    KerrState result;
    result.u = s.vu;
    result.vu = ddu;
    result.q = s.vq;
    result.vq = ddq;
    result.phiFD = dphiFD;
    return result;
}

KerrState stepAdd(KerrState a, KerrState b, float scale) {
    KerrState o;
    o.u = a.u + b.u * scale;
    o.vu = a.vu + b.vu * scale;
    o.q = a.q + b.q * scale;
    o.vq = a.vq + b.vq * scale;
    o.phiFD = a.phiFD + b.phiFD * scale;
    return o;
}

vec3 rotateZ(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

void main(){
    vec2 uv = vec2(gl_FragCoord.x, screenParams.y-gl_FragCoord.y) * screenParams.zw;
    float sx = (2.0 * uv.x - 1.0) * camRightAspect.w * camForwardFov.w;
    float sy = (1.0 - 2.0 * uv.y) * camForwardFov.w;

    vec3 rayDirWorld = normalize(camForwardFov.xyz + sx * camRightAspect.xyz + sy * camUpDummy.xyz);

    float r0 = camBL.x;
    float theta0 = camBL.y;
    float phi0 = camBL.z;
    float cosT = camBL.w;
    float sinT = sin(theta0);
    float cosP = cos(phi0);
    float sinP = sin(phi0);

    vec3 e_r     = vec3(sinT * cosP, sinT * sinP, cosT);
    vec3 e_theta = vec3(cosT * cosP, cosT * sinP, -sinT);
    vec3 e_phi   = vec3(-sinP, cosP, 0.0);

    float nr     = dot(rayDirWorld, e_r);
    float ntheta = dot(rayDirWorld, e_theta);
    float nphi   = dot(rayDirWorld, e_phi);

    float a = bhParams.x;
    float rp = bhParams.y;
    float rISCO = bhParams.z;
    float rOut = bhParams.w;

    float r2 = r0 * r0;
    float a2 = a * a;
    float sin2 = max(1e-8, sinT * sinT);
    float sigma = r2 + a2 * cosT * cosT;
    float delta = r2 - 2.0 * r0 + a2;
    float A = (r2 + a2) * (r2 + a2) - a2 * delta * sin2;

    float varpi = sqrt(A * sin2 / sigma);
    float omega = (2.0 * a * r0) / A;
    float alphaLapse = sqrt(sigma * delta / A);

    KerrConstants c;
    c.a  = a;
    c.E  = alphaLapse - omega * varpi * nphi;
    c.Lz = -varpi * nphi;
    c.Q  = sigma * ntheta * ntheta + cosT * cosT * ((c.Lz * c.Lz) / sin2 - a2 * c.E * c.E);
    c.K  = c.Q + (c.Lz - a * c.E) * (c.Lz - a * c.E);

    KerrState s;
    s.u = 1.0 / r0;
    s.vu = -s.u * s.u * sqrt(max(0.0, sigma * delta)) * nr;
    s.q = e_r;
    float vTheta = sqrt(sigma) * ntheta;
    float vPhiSphere = (varpi / max(1e-6, sinT)) * nphi;
    s.vq = vTheta * e_theta + vPhiSphere * e_phi;
    s.phiFD = 0.0;

    float uHorizon = 1.0 / (rp + 0.004);
    float omega0 = sqrt(max(1e-4, c.K + c.E * c.E));

    vec3 accumColor = vec3(0.0);
    float transmittance = 1.0;
    float hitDist = 1e6;
    bool captured = false;

    float diskEnabled = diskParams.x;
    float dopplerEnabled = diskParams.y;
    float diskBrightness = diskParams.z;
    float haloIntensity = diskParams.w;
    float simTime = camPosTime.w;

    const int MAX_STEPS = 560;
    for (int step = 0; step < MAX_STEPS; ++step) {
        float deltaU = max(1e-4, 1.0 - 2.0 * s.u + a2 * s.u * s.u);
        float baseStep = (s.u < 0.06) ? 0.11 : ((s.u < 0.16) ? 0.055 : 0.032);
        float h = (baseStep / (1.0 + 2.2 * s.u + 0.08 / deltaU)) / omega0;

        KerrState prev = s;

        KerrState k1 = kerrRhs(s, c);
        KerrState k2 = kerrRhs(stepAdd(s, k1, 0.5 * h), c);
        KerrState k3 = kerrRhs(stepAdd(s, k2, 0.5 * h), c);
        KerrState k4 = kerrRhs(stepAdd(s, k3, h), c);

        s.u     += (h / 6.0) * (k1.u     + 2.0 * k2.u     + 2.0 * k3.u     + k4.u);
        s.vu    += (h / 6.0) * (k1.vu    + 2.0 * k2.vu    + 2.0 * k3.vu    + k4.vu);
        s.q     += (h / 6.0) * (k1.q     + 2.0 * k2.q     + 2.0 * k3.q     + k4.q);
        s.vq    += (h / 6.0) * (k1.vq    + 2.0 * k2.vq    + 2.0 * k3.vq    + k4.vq);
        s.phiFD += (h / 6.0) * (k1.phiFD + 2.0 * k2.phiFD + 2.0 * k3.phiFD + k4.phiFD);

        // Enforce unit sphere & orthogonality to eliminate numerical drift
        s.q = normalize(s.q);
        s.vq -= dot(s.q, s.vq) * s.q;

        float rCurr = (s.u > 1e-5) ? (1.0 / s.u) : 1e5;

        // Subtle grazing photon-sphere & equatorial plasma corona halo
        // Zero glow inside directly-plunging rays (keeps event horizon shadow pitch black!)
        if (haloIntensity > 0.01 && rCurr < rOut && rCurr > rp * 1.06) {
            float dsApprox = min(0.75, length(s.q - prev.q) * rCurr + abs(s.u - prev.u) / max(1e-4, s.u * max(s.u, prev.u)));
            float grazing = 1.0 - clamp(abs(s.vu) / max(1e-3, omega0 * s.u * s.u + c.E), 0.0, 1.0);
            float radialTangential = 1.0 - clamp(abs(s.vu) / max(0.25, c.E), 0.0, 1.0);
            grazing = max(grazing, radialTangential);

            float rHaloCenter = rp * 1.48;
            float photonGlow = exp(-pow((rCurr - rHaloCenter) / (0.38 * rp), 2.0)) * pow(grazing, 3.0);

            float zHeight = rCurr * abs(s.q.z);
            float scaleH = 0.065 * rCurr + 0.08;
            float diskHalo = (diskEnabled > 0.5)
                ? exp(-0.5 * (zHeight * zHeight) / (scaleH * scaleH)) * smoothstep(rISCO * 0.9, rISCO * 1.25, rCurr) * pow(max(0.0, rISCO / rCurr), 2.2)
                : 0.0;

            float coronaBeam = (dopplerEnabled > 0.5) ? clamp(1.0 + 0.45 * (c.Lz / max(1.5, rCurr)), 0.45, 1.85) : 1.0;
            vec3 photonCol = mix(vec3(1.0, 0.50, 0.16), vec3(0.65, 0.84, 1.0), clamp((coronaBeam - 0.7) * 0.8, 0.0, 1.0));
            vec3 coronaCol = vec3(1.0, 0.62, 0.28) * coronaBeam;

            vec3 haloEmit = (photonCol * photonGlow * 0.032 + coronaCol * diskHalo * 0.014) * haloIntensity * dsApprox;
            accumColor += transmittance * haloEmit;
        }

        // Check equatorial accretion disk crossing (q.z changes sign)
        if (diskEnabled > 0.5 && ((prev.q.z > 0.0 && s.q.z <= 0.0) || (prev.q.z < 0.0 && s.q.z >= 0.0))) {
            float denom = abs(s.q.z - prev.q.z);
            float frac = (denom > 1e-7) ? clamp(abs(prev.q.z) / denom, 0.0, 1.0) : 0.5;
            float uHit = mix(prev.u, s.u, frac);
            float phiFDHit = mix(prev.phiFD, s.phiFD, frac);
            vec3 qHit = normalize(mix(prev.q, s.q, frac));
            vec3 worldHitDir = rotateZ(qHit, phiFDHit);
            float phiHit = atan(worldHitDir.y, worldHitDir.x);

            if (uHit > 1e-4) {
                float rHit = 1.0 / uHit;
                vec4 diskSample = evaluateDiskCrossing(
                    rHit, phiHit, a, rISCO, rOut,
                    c.E, c.E, c.Lz,
                    simTime, dopplerEnabled, diskBrightness);
                if (diskSample.a > 0.001) {
                    accumColor += transmittance * diskSample.rgb * diskSample.a;
                    transmittance *= (1.0 - diskSample.a);

                    if (hitDist > 1e5 && transmittance < 0.65) {
                        vec3 diskWorld = vec3(rHit * cos(phiHit), rHit * sin(phiHit), 0.0);
                        hitDist = length(diskWorld - camPosTime.xyz);
                    }
                    if (transmittance < 0.015) {
                        break;
                    }
                }
            }
        }

        // Event horizon capture (pitch-black shadow)
        if (s.u >= uHorizon) {
            captured = true;
            if (hitDist > 1e5) {
                hitDist = max(0.2, r0 - 0.3 * rp);
            }
            break;
        }

        // Escaped to asymptotic infinity (u crosses 0)
        if (s.u <= 0.0 && s.vu < 0.0) {
            float frac = clamp(prev.u / max(1e-8, prev.u - s.u), 0.0, 1.0);
            vec3 qInf = normalize(mix(prev.q, s.q, frac));
            float phiFDInf = mix(prev.phiFD, s.phiFD, frac);
            vec3 skyDir = rotateZ(qInf, phiFDInf);
            accumColor += transmittance * sampleDeepSpaceSky(skyDir, visualParams.x);
            transmittance = 0.0;
            break;
        }
    }

    if (!captured && transmittance > 0.01) {
        if (s.u > 0.22) {
            if (hitDist > 1e5) {
                hitDist = max(0.2, r0 - 0.3 * rp);
            }
        } else {
            vec3 skyDir = rotateZ(s.q, s.phiFD);
            accumColor += transmittance * sampleDeepSpaceSky(skyDir, visualParams.x);
        }
    }

    outColor = vec4(accumColor, 1.0);
    outDepth = vec4(hitDist, 0.0, 0.0, 0.0);
}
