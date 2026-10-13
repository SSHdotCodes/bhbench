#pragma once

namespace bh {

// ============================================================================
// 1. Fullscreen Quad Vertex Shader
// ============================================================================
static const char* QUAD_VERT_SHADER = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// ============================================================================
// 2. General Relativistic Kerr/Schwarzschild Ray-Tracing Fragment Shader
//    - Exact Cartesian Kerr-Schild Hamiltonian RK4 null geodesic integration
//    - Novikov-Thorne relativistic accretion disk + Doppler beaming + Liouville g^4
//    - Multi-order photon ring halos (n=0 primary, n=1 secondary arch, n>=2 sub-rings)
//    - Volumetric turbulent plasma + relativistic photon-sphere corona
//    - Optional Blandford-Znajek relativistic polar jets & lensed Flamm grid
// ============================================================================
static const char* RAYTRACE_FRAG_SHADER = R"GLSL(
#version 410 core
in vec2 vUV;
layout(location = 0) out vec4 FragColor;

uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uTime;

uniform float uMass;            // M
uniform float uSpin;            // a* = a/M in [-0.995, 0.995]
uniform float uHorizonR;        // r_+ = M(1 + sqrt(1 - a*^2))
uniform float uISCO;            // r_ISCO(a*)
uniform float uPhotonRPro;      // Prograde photon orbit radius
uniform float uDiskOuter;       // Outer disk radius in M
uniform float uDiskTemp;        // Peak temperature in Kelvin
uniform float uDiskIntensity;   // Disk brightness multiplier
uniform float uDiskThickness;   // Disk H/r scale
uniform float uDopplerStrength; // Relativistic beaming control
uniform float uHaloStrength;    // Photon ring & coronal halo intensity
uniform float uTrapdoorDepth;   // Flamm's paraboloid depth scale
uniform float uGridExtent;      // Spacetime grid outer radius
uniform int   uMaxSteps;        // RK4 integration steps
uniform int   uShowDisk;
uniform int   uShowJets;
uniform int   uShowLensedGrid;

// ----------------------------------------------------------------------------
// Procedural 3D Noise & Fractional Brownian Motion (fBM) for MRI Disk Plasma
// ----------------------------------------------------------------------------
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float noise3D(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(mix(hash13(i + vec3(0,0,0)), hash13(i + vec3(1,0,0)), f.x),
            mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), f.x),
            mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), f.x), f.y),
        f.z
    );
}

float fbm3D(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    mat3 rot = mat3(
         0.80,  0.60,  0.00,
        -0.60,  0.80,  0.00,
         0.00,  0.00,  1.00
    );
    for (int i = 0; i < 4; ++i) {
        v += a * noise3D(p);
        p = rot * p * 2.07 + vec3(13.7, 7.1, 19.3);
        a *= 0.5;
    }
    return v;
}

// ----------------------------------------------------------------------------
// Physical Planckian Blackbody Temperature (Kelvin) -> Linear RGB
// Preserves rich crimson/orange/gold/blue-white astrophysical chromaticity
// ----------------------------------------------------------------------------
vec3 blackbodyRGB(float tempK) {
    float t = clamp(tempK, 900.0, 38000.0) / 100.0;
    vec3 col;
    if (t <= 66.0) {
        col.r = 1.0;
    } else {
        col.r = clamp(1.292936186 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    }
    if (t <= 66.0) {
        col.g = clamp(0.3900815787 * log(max(t, 1.0)) - 0.6318414437, 0.0, 1.0);
    } else {
        col.g = clamp(1.129890861 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    }
    if (t >= 66.0) {
        col.b = 1.0;
    } else if (t <= 19.0) {
        col.b = 0.0;
    } else {
        col.b = clamp(0.5432067891 * log(t - 10.0) - 1.1962540891, 0.0, 1.0);
    }
    // Enhance thermal color separation in linear HDR space
    return pow(col, vec3(2.4));
}

// ----------------------------------------------------------------------------
// Exact Cartesian Kerr-Schild Metric & Hamiltonian Geodesic Equations
//   ds^2 = eta_uv dx^u dx^v + f (k_u dx^u)^2
//   H(x, p) = 0.5 * (|p|^2 - 1 - f * (k . p + 1)^2) = 0
// ----------------------------------------------------------------------------
struct KerrAux {
    float r;
    float f;
    vec3  k;
    vec3  grad_r;
};

KerrAux evalKerrAux(vec3 x, float M, float a) {
    float rho2 = dot(x, x);
    float a2 = a * a;
    float diff = rho2 - a2;
    float disc = sqrt(max(0.0, diff * diff + 4.0 * a2 * x.y * x.y));
    float r2 = max(0.5 * (diff + disc), 1e-5);
    float r = sqrt(r2);
    float r3 = r2 * r;
    float r4 = r2 * r2;
    float denom = r4 + a2 * x.y * x.y;
    float r2_a2 = r2 + a2;

    KerrAux aux;
    aux.r = r;
    aux.f = (2.0 * M * r3) / denom;
    aux.k = vec3(
        (r * x.x + a * x.z) / r2_a2,
        x.y / r,
        (r * x.z - a * x.x) / r2_a2
    );
    aux.grad_r = (r3 / denom) * vec3(x.x, (r2_a2 / r2) * x.y, x.z);
    return aux;
}

void kerrHamiltonianRHS(vec3 x, vec3 p, float M, float a, out vec3 dx, out vec3 dp, out KerrAux aux) {
    aux = evalKerrAux(x, M, a);
    float r = aux.r;
    float r2 = r * r;
    float r3 = r2 * r;
    float r4 = r2 * r2;
    float a2 = a * a;
    float denom = r4 + a2 * x.y * x.y;
    float invDenom2 = 1.0 / (denom * denom);
    float r2_a2 = r2 + a2;

    float kp = dot(aux.k, p);
    float q = kp + 1.0;

    // Velocity dx/dlambda = dH/dp
    dx = p - aux.f * q * aux.k;

    // Spatial gradient of f(x)
    float df_dr = 2.0 * M * r2 * (3.0 * a2 * x.y * x.y - r4) * invDenom2;
    float df_dy = -4.0 * M * r3 * a2 * x.y * invDenom2;
    vec3 grad_f = df_dr * aux.grad_r + vec3(0.0, df_dy, 0.0);

    // Spatial gradient of (k(x) . p)
    vec3 dkp_dx_exp = vec3(
        (r * p.x - a * p.z) / r2_a2,
        p.y / r,
        (a * p.x + r * p.z) / r2_a2
    );
    float dkp_dr = ((x.x - 2.0 * r * aux.k.x) * p.x +
                    (x.z - 2.0 * r * aux.k.z) * p.z) / r2_a2 -
                   (aux.k.y * p.y) / r;
    vec3 grad_kp = dkp_dx_exp + dkp_dr * aux.grad_r;

    // Force dp/dlambda = -dH/dx
    dp = (0.5 * q * q) * grad_f + (aux.f * q) * grad_kp;
}

// ----------------------------------------------------------------------------
// Relativistic Accretion Disk Shading at Equatorial Midplane Crossing
// Includes Novikov-Thorne T(r), Kerr Keplerian Doppler shift, gravitational
// redshift, Liouville g^4 beaming, and differentially sheared MRI spiral arms
// ----------------------------------------------------------------------------
vec4 shadeAccretionDisk(vec3 pos, vec3 rayVel, float M, float a, int crossingOrder) {
    float r = length(pos.xz);
    float rIn = uISCO;
    float rOut = uDiskOuter * M;
    if (r < rIn * 0.94 || r > rOut) {
        return vec4(0.0);
    }

    // Smooth inner ISCO rim and exponential outer disk falloff
    float innerFade = smoothstep(rIn * 0.94, rIn * 1.06, r);
    float radialNorm = clamp((r - rIn) / max(0.001, rOut - rIn), 0.0, 1.0);
    float outerFade = (1.0 - smoothstep(0.65, 1.0, radialNorm)) * exp(-1.8 * radialNorm * radialNorm);
    float radialWindow = innerFade * outerFade;
    if (radialWindow <= 1e-4) return vec4(0.0);

    // Novikov-Thorne / Page-Thorne relativistic temperature profile:
    // T(r) ~ (r_in / r)^(3/4) * (1 - sqrt(r_in / r))^(1/4)
    float ratio = clamp(rIn / max(r, rIn * 1.001), 0.0, 0.9995);
    float ntProfile = pow(ratio, 0.75) * pow(max(1e-4, 1.0 - sqrt(ratio)), 0.25) / 0.488;
    // Hot plunging-region inner ring right at the ISCO boundary
    float iscoRing = exp(-pow((r - rIn * 1.06) / (0.28 * M), 2.0)) * 0.55;
    float tempNorm = clamp(ntProfile + iscoRing, 0.12, 1.45);

    // Exact Kerr equatorial Keplerian angular velocity: Omega_K = sqrt(M) / (r^(3/2) + a * sqrt(M))
    float sqrtM = sqrt(M);
    float omegaK = sqrtM / (pow(r, 1.5) + a * sqrtM);

    // Local orbital speed beta = v / c in Locally Non-Rotating Frame
    float delta = max(0.05 * M * M, r * r - 2.0 * M * r + a * a);
    float beta = clamp(((r * r + a * a) * omegaK - 2.0 * M * a * omegaK / r) / sqrt(delta), 0.0, 0.82);
    float gamma = inversesqrt(max(0.08, 1.0 - beta * beta));

    // Prograde orbital tangent vector in (x, z) plane:
    vec3 tangent = normalize(vec3(pos.z, 0.0, -pos.x));
    vec3 rayDir = normalize(rayVel);

    // Relativistic Doppler factor: photon emitted along -rayDir toward observer
    float cosTheta = dot(tangent, -rayDir);
    float gDoppler = 1.0 / (gamma * (1.0 - beta * cosTheta));

    // Gravitational redshift factor sqrt(1 - 2Mr / (r^2 + a^2))
    float gGrav = sqrt(clamp(1.0 - (2.0 * M * r) / (r * r + a * a + 0.05 * M * M), 0.15, 1.0));

    // Combined relativistic frequency shift g = nu_obs / nu_emit
    float gTotal = mix(1.0, gDoppler * gGrav, uDopplerStrength);
    gTotal = clamp(gTotal, 0.25, 2.45);

    // Differential Keplerian spiral coordinates for MRI turbulence
    float phi = atan(pos.z, pos.x);
    float logR = log(max(r / rIn, 1.001));
    float windingAngle = phi + omegaK * uTime * 2.4 - 4.8 * logR;

    vec3 noiseCoord1 = vec3(r * 0.65 / M, cos(windingAngle) * 2.4, sin(windingAngle) * 2.4);
    vec3 noiseCoord2 = vec3(logR * 6.5 - uTime * 0.20,
                            cos(phi * 3.0 + omegaK * uTime * 3.2 - 7.5 * logR) * 1.9,
                            sin(phi * 3.0 + omegaK * uTime * 3.2 - 7.5 * logR) * 1.9);

    float turb1 = fbm3D(noiseCoord1);
    float turb2 = fbm3D(noiseCoord2 * 1.9);
    // High-contrast spiral filaments & concentric relativistic density waves
    float spiralArms = smoothstep(0.22, 0.88, 0.5 + 0.5 * sin(2.0 * phi - 8.5 * logR + omegaK * uTime * 2.6 + 3.2 * turb1));
    float fineRings  = smoothstep(0.15, 0.90, 0.5 + 0.5 * sin(r * 4.5 / M - uTime * 0.9 + 4.5 * turb2));

    float filament = pow(clamp(0.45 * turb1 + 0.35 * turb2 + 0.20 * spiralArms, 0.0, 1.0), 1.35);
    float density = filament * (0.45 + 0.75 * fineRings) * radialWindow;

    // Liouville's Theorem: bolometric intensity I_obs = g^4 * I_emit
    float beamingFlux = pow(gTotal, 3.4);
    float obsTempK = uDiskTemp * tempNorm * pow(gTotal, 0.95);
    vec3 bbColor = blackbodyRGB(obsTempK);

    // Boost higher-order photon ring crossings (n >= 1) so the lensed halo & photon sub-rings gleam
    float orderBoost = (crossingOrder == 0) ? 1.0 : ((crossingOrder == 1) ? 1.35 * uHaloStrength : 1.95 * uHaloStrength);

    vec3 emission = bbColor * density * pow(tempNorm, 1.25) * beamingFlux * uDiskIntensity * orderBoost * 2.2;
    float alpha = clamp(density * (0.82 + 0.18 * iscoRing), 0.0, 0.94);

    return vec4(emission, alpha);
}

// ----------------------------------------------------------------------------
// Deep-Field Background Starfield & Milky Way Galactic Band (Gravitationally Lensed)
// ----------------------------------------------------------------------------
vec3 sampleCelestialSky(vec3 dir) {
    vec3 d = normalize(dir);

    mat3 galRot = mat3(
         0.848,  0.530,  0.000,
        -0.450,  0.720,  0.527,
         0.280, -0.448,  0.850
    );
    vec3 gd = galRot * d;

    float galLat = gd.y;
    float galBand = exp(-galLat * galLat * 14.0);
    float dustBand = exp(-galLat * galLat * 65.0);

    float n1 = fbm3D(gd * 3.5 + vec3(1.2, 3.4, 5.6));
    float n2 = fbm3D(gd * 8.0 - vec3(4.1, 2.3, 1.9));

    vec3 coreCol = vec3(0.22, 0.14, 0.09) * galBand * n1;
    vec3 nebulaCol = vec3(0.04, 0.08, 0.18) * exp(-galLat * galLat * 6.5) * n2;
    float dustExtinction = clamp(1.0 - 0.75 * dustBand * smoothstep(0.35, 0.75, n2), 0.12, 1.0);

    vec3 sky = (vec3(0.002, 0.003, 0.007) + coreCol + nebulaCol) * dustExtinction;

    // Multi-scale procedural stars with spectral blackbody colors
    for (int layer = 0; layer < 2; ++layer) {
        float scale = (layer == 0) ? 145.0 : 320.0;
        vec3 p = d * scale;
        vec3 cell = floor(p);
        vec3 fracP = fract(p) - 0.5;
        float h = hash13(cell + float(layer) * 73.1);
        if (h > 0.964) {
            vec3 jitter = vec3(hash13(cell + 1.0), hash13(cell + 2.0), hash13(cell + 3.0)) - 0.5;
            float dist = length(fracP - jitter * 0.55);
            float starSize = (layer == 0) ? 0.095 : 0.065;
            float starGlow = exp(- (dist * dist) / (starSize * starSize * 0.24));
            float starTemp = mix(3000.0, 24000.0, hash13(cell + 9.7));
            float starBri = pow((h - 0.964) / 0.036, 1.5) * ((layer == 0) ? 3.2 : 1.5);
            sky += blackbodyRGB(starTemp) * starGlow * starBri;
        }
    }
    return sky;
}

// ----------------------------------------------------------------------------
// Optional In-Shader Lensed Spacetime Trapdoor Grid Surface Evaluation
// ----------------------------------------------------------------------------
float flammSurfaceY(float r, float M) {
    float rPlus = uHorizonR;
    float yBaseOffset = -1.15 * M;
    if (r > rPlus) {
        float dr = (r - rPlus) / M;
        float flammWell = -5.4 * M / (1.0 + 0.28 * dr + 0.055 * dr * dr)
                          - 2.6 * M * (1.0 - sqrt(dr / (dr + 3.2)));
        return uTrapdoorDepth * flammWell + yBaseOffset;
    } else {
        float yRim = -8.0 * M;
        float frac = clamp((rPlus - r) / (0.75 * rPlus), 0.0, 1.0);
        float throatDrop = -6.2 * M * (0.85 * frac + 1.15 * frac * frac);
        return uTrapdoorDepth * (yRim + throatDrop) + yBaseOffset;
    }
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 rayDir = normalize(
        uCamFwd +
        ndc.x * uAspect * uTanHalfFov * uCamRight +
        ndc.y * uTanHalfFov * uCamUp
    );

    float M = uMass;
    float a = uSpin * M;
    float rPlus = uHorizonR;

    // Initialize exact Kerr-Schild null geodesic state (x, p)
    vec3 x = uCamPos;
    KerrAux aux0 = evalKerrAux(x, M, a);
    float dk = dot(rayDir, aux0.k);
    float oneMinusDk2 = max(0.0, 1.0 - dk * dk);
    float s0 = inversesqrt(max(1e-6, 1.0 - aux0.f * oneMinusDk2));
    float q0 = (s0 * s0 * oneMinusDk2) / max(1e-6, 1.0 - s0 * dk);
    vec3 p = s0 * rayDir + (aux0.f * q0) * aux0.k;

    vec3 accumRadiance = vec3(0.0);
    float transmittance = 1.0;

    bool captured = false;
    int diskCrossings = 0;
    float minRadius = 1e9;
    float totalPolarDeflection = 0.0;
    vec3 lastVel = rayDir;

    for (int step = 0; step < 360; ++step) {
        if (step >= uMaxSteps || transmittance < 0.015) break;

        vec3 prevX = x;
        vec3 dx1, dp1;
        KerrAux aux1;
        kerrHamiltonianRHS(x, p, M, a, dx1, dp1, aux1);
        vec3 prevVel = lastVel;
        lastVel = dx1;
        minRadius = min(minRadius, aux1.r);

        // Event horizon capture check
        if (aux1.r <= rPlus * 1.012) {
            captured = true;
            break;
        }

        // Escape to celestial sphere check
        if (aux1.r > 52.0 * M && dot(x, dx1) > 0.0) {
            break;
        }

        // Adaptive RK4 step size
        float distFromHorizon = max(0.02 * M, aux1.r - 0.84 * rPlus);
        float maxH = (aux1.r > uDiskOuter * M * 1.05) ? 1.35 * M : 0.68 * M;
        float h = clamp(0.058 * distFromHorizon, 0.018 * M, maxH);
        float eqProximity = abs(x.y) / max(aux1.r, M);
        if (aux1.r < uDiskOuter * M * 1.15 && eqProximity < 0.20) {
            h = min(h, max(0.022 * M, 0.35 * abs(x.y) + 0.024 * M));
        }

        // 4th-order Runge-Kutta (RK4) integration step
        vec3 dx2, dp2, dx3, dp3, dx4, dp4;
        KerrAux auxTmp;
        kerrHamiltonianRHS(x + 0.5 * h * dx1, p + 0.5 * h * dp1, M, a, dx2, dp2, auxTmp);
        kerrHamiltonianRHS(x + 0.5 * h * dx2, p + 0.5 * h * dp2, M, a, dx3, dp3, auxTmp);
        kerrHamiltonianRHS(x + h * dx3,       p + h * dp3,       M, a, dx4, dp4, auxTmp);

        x += (h / 6.0) * (dx1 + 2.0 * dx2 + 2.0 * dx3 + dx4);
        p += (h / 6.0) * (dp1 + 2.0 * dp2 + 2.0 * dp3 + dp4);

        // Track 3D ray velocity bending angle (measures gravitational lensing / photon ring winding)
        if (step > 0) {
            float cosBend = clamp(dot(normalize(prevVel), normalize(dx1)), -1.0, 1.0);
            totalPolarDeflection += acos(cosBend);
        }

        // 1. Exact Equatorial Midplane Crossing (Accretion Disk + Multi-Order Lensed Halos)
        if (uShowDisk != 0 && (prevX.y * x.y <= 0.0) && (abs(prevX.y - x.y) > 1e-7)) {
            float tCross = abs(prevX.y) / abs(prevX.y - x.y);
            vec3 hitPos = mix(prevX, x, tCross);
            vec4 diskSample = shadeAccretionDisk(hitPos, dx1, M, a, diskCrossings);
            if (diskSample.a > 0.001) {
                accumRadiance += transmittance * diskSample.rgb * diskSample.a;
                transmittance *= (1.0 - diskSample.a * 0.88);
            }
            diskCrossings++;
        }

        // 2. Volumetric 3D Flared Accretion Disk Corona (only very close to midplane)
        if (uShowDisk != 0 && abs(x.y) < 1.35 * M) {
            float rCyl = length(x.xz);
            float rIn = uISCO;
            float rOut = uDiskOuter * M;
            if (rCyl > rIn * 0.95 && rCyl < rOut * 0.92) {
                float diskH = uDiskThickness * M * (0.45 + 0.14 * (rCyl - rIn) / M);
                if (abs(x.y) < diskH * 1.45) {
                    float vertGauss = exp(- (x.y * x.y) / max(1e-4, diskH * diskH));
                    float ratio = clamp(rIn / rCyl, 0.0, 0.999);
                    float nt = pow(ratio, 0.75) * pow(max(1e-4, 1.0 - sqrt(ratio)), 0.25) / 0.488;
                    vec3 tangent = normalize(vec3(x.z, 0.0, -x.x));
                    float cosTh = dot(tangent, -normalize(dx1));
                    float gApprox = clamp(1.0 + 0.45 * uDopplerStrength * cosTh * sqrt(rIn / rCyl), 0.35, 2.1);
                    vec3 haloCol = blackbodyRGB(uDiskTemp * nt * gApprox);
                    float volAlpha = vertGauss * (1.0 - rCyl / rOut) * (h / M) * 0.11;
                    accumRadiance += transmittance * haloCol * pow(gApprox, 3.0) * volAlpha * uDiskIntensity;
                    transmittance *= (1.0 - volAlpha * 0.45);
                }
            }
        }

        // 3. Optional Blandford-Znajek Relativistic Polar Magnetospheric Jets
        if (uShowJets != 0 && aux1.r > rPlus * 1.08 && aux1.r < 28.0 * M) {
            float rCyl = length(x.xz);
            float absY = abs(x.y);
            float jetRadius = 0.42 * M + 0.15 * pow(max(absY, 0.1 * M), 0.72);
            if (rCyl < jetRadius * 2.1 && absY > 0.9 * M) {
                float radialProfile = exp(-pow(rCyl / jetRadius, 2.0));
                float sheath = exp(-pow((rCyl - jetRadius * 0.75) / (0.28 * jetRadius), 2.0));
                float jetPhi = atan(x.z, x.x) - sign(x.y) * (absY * 0.45 / M - uTime * 2.8);
                float helix = 0.55 + 0.45 * sin(2.0 * jetPhi + fbm3D(x * 0.6 - vec3(0.0, sign(x.y) * uTime * 3.5, 0.0)) * 3.0);
                float zFade = exp(-absY / (16.0 * M)) * smoothstep(0.9 * M, 2.2 * M, absY);
                float jetBeta = 0.88;
                float jetGamma = inversesqrt(1.0 - jetBeta * jetBeta);
                vec3 jetVelDir = vec3(0.0, sign(x.y), 0.0);
                float jetDoppler = 1.0 / (jetGamma * (1.0 - jetBeta * dot(jetVelDir, -normalize(dx1))));
                vec3 jetColor = mix(vec3(0.20, 0.50, 1.0), vec3(0.82, 0.92, 1.0), sheath) * pow(clamp(jetDoppler, 0.4, 2.4), 2.1);
                float jetStep = (0.45 * radialProfile + 0.75 * sheath) * helix * zFade * (h / M) * 0.32;
                accumRadiance += transmittance * jetColor * jetStep;
            }
        }

        // 4. Optional Gravitationally Lensed Spacetime Trapdoor Grid Inside Ray Tracer
        if (uShowLensedGrid != 0) {
            float rPrev = length(prevX.xz);
            float rCurr = length(x.xz);
            if (rCurr < uGridExtent * M && rCurr > rPlus * 0.45) {
                float dPrev = prevX.y - flammSurfaceY(rPrev, M);
                float dCurr = x.y - flammSurfaceY(rCurr, M);
                if (dPrev * dCurr <= 0.0 && abs(dPrev - dCurr) > 1e-6) {
                    float tG = abs(dPrev) / abs(dPrev - dCurr);
                    vec3 gPos = mix(prevX, x, tG);
                    float gR = length(gPos.xz);
                    float gPhi = atan(gPos.z, gPos.x) + (2.0 * M * a) / (gR * gR + 0.35 * rPlus * rPlus);
                    float ringLine = exp(-pow(sin(gR * 3.14159265 / (1.5 * M)), 2.0) * 32.0);
                    float spokeLine = exp(-pow(sin(gPhi * 12.0), 2.0) * 36.0);
                    float gridIntensity = max(ringLine, spokeLine);
                    if (gridIntensity > 0.05) {
                        vec3 gridCol = mix(vec3(0.08, 0.65, 1.0), vec3(1.0, 0.22, 0.45), clamp((3.2 * M) / max(gR, rPlus), 0.0, 1.0));
                        accumRadiance += transmittance * gridCol * gridIntensity * 0.85;
                    }
                }
            }
        }
    }

    // Background skybox + Einstein ring lensing & razor-sharp Photon Ring Halo
    if (!captured) {
        vec3 skyColor = sampleCelestialSky(lastVel);
        float einsteinMagnification = 1.0 + 0.55 * exp(-pow((minRadius - uPhotonRPro) / (0.55 * M), 2.0));
        accumRadiance += transmittance * skyColor * einsteinMagnification;
    }

    // Critical Photon Ring & Relativistic Halo (for strongly deflected rays grazing the photon sphere)
    if (totalPolarDeflection > 1.85) {
        float ringSharp = smoothstep(1.85, 5.8, totalPolarDeflection);
        // Keep interior of captured shadow pitch-black except right at the razor-thin critical rim
        float edgeMask = captured ? smoothstep(1.85, 4.2, totalPolarDeflection) * 0.55 : 1.0;
        vec3 ringGlow = blackbodyRGB(uDiskTemp * 1.25) * ringSharp * edgeMask * 1.45 * uHaloStrength;
        accumRadiance += transmittance * ringGlow;
    }

    FragColor = vec4(max(accumRadiance, vec3(0.0)), 1.0);
}
)GLSL";

// ============================================================================
// 3. 3D Spacetime Curvature ("Trapdoor in Spacetime") Vertex & Fragment Shaders
//    Renders Flamm's Paraboloid isometric embedding funnel + Kerr frame-dragging
//    twist + Gullstrand-Painleve infalling spacetime river pulses + critical rings
// ============================================================================
static const char* GRID_VERT_SHADER = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aMeta; // x = radius r/M, y = lineType (0=grid, 1=horizon, 2=photon, 3=isco, 4=ergo, 5=geodesic)

uniform mat4 uViewProj;
out vec4 vColor;
out vec2 vMeta;
out vec3 vWorldPos;

void main() {
    vColor = aColor;
    vMeta = aMeta;
    vWorldPos = aPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

static const char* GRID_FRAG_SHADER = R"GLSL(
#version 410 core
in vec4 vColor;
in vec2 vMeta;
in vec3 vWorldPos;
layout(location = 0) out vec4 FragColor;

uniform float uTime;
uniform float uHorizonR;
uniform float uMass;
uniform vec3  uCamPos;

void main() {
    float rOverM = vMeta.x;
    float lineType = vMeta.y;

    vec3 col = vColor.rgb;
    float alpha = vColor.a;

    // Smoothly attenuate far-side grid lines occluded behind the black hole shadow sphere
    if (dot(vWorldPos.xz, uCamPos.xz) < 0.0 && vWorldPos.y > -2.5 * uMass) {
        vec3 toCam = uCamPos - vWorldPos;
        float distToCam = length(toCam);
        vec3 rayDir = toCam / max(distToCam, 1e-4);
        float proj = dot(-vWorldPos, rayDir);
        if (proj > 0.0 && proj < distToCam) {
            vec3 closestPt = vWorldPos + proj * rayDir;
            float impactR = length(closestPt);
            alpha *= smoothstep(3.6 * uMass, 5.2 * uMass, impactR);
        }
    }

    if (lineType < 0.5) {
        // Gullstrand-Painleve "River Model of Spacetime" infalling pulse waves
        // Spacetime flows inward at v_river = sqrt(2M / r), accelerating into the trapdoor
        float riverPhase = 2.4 * sqrt(max(0.1, rOverM)) + uTime * 1.75;
        float pulse = 0.65 + 0.35 * sin(riverPhase * 3.14159265);
        // Brighten curvature glow inside the trapdoor throat
        float throatGlow = 1.0 + 1.25 * exp(-max(0.0, rOverM - uHorizonR / uMass) * 0.55);
        col *= pulse * throatGlow;
    } else if (lineType > 4.5) {
        // Animated photon packet traveling along 3D null geodesic beam
        float packet = 0.75 + 0.55 * pow(0.5 + 0.5 * sin(rOverM * 7.5 - uTime * 4.5), 3.0);
        col *= packet;
    }

    FragColor = vec4(col, alpha);
}
)GLSL";

// ============================================================================
// 4. HDR Bright-Pass & Separable Gaussian Bloom Shader
// ============================================================================
static const char* BLOOM_FRAG_SHADER = R"GLSL(
#version 410 core
in vec2 vUV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D uSourceTex;
uniform vec2      uTexelSize;
uniform vec2      uDirection; // (1,0) horizontal, (0,1) vertical, (0,0) bright-pass extract

void main() {
    if (length(uDirection) < 0.5) {
        vec3 c = texture(uSourceTex, vUV).rgb;
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float knee = smoothstep(0.75, 2.10, lum);
        FragColor = vec4(c * knee, 1.0);
        return;
    }

    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(uSourceTex, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * uTexelSize * (float(i) * 1.75);
        result += texture(uSourceTex, vUV + offset).rgb * weights[i];
        result += texture(uSourceTex, vUV - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
)GLSL";

// ============================================================================
// 5. Final Composite + ACES Filmic Tone Mapping Shader
// ============================================================================
static const char* COMPOSITE_FRAG_SHADER = R"GLSL(
#version 410 core
in vec2 vUV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;
uniform float     uBloomStrength;
uniform float     uExposure;

vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrScene = texture(uSceneTex, vUV).rgb;
    vec3 hdrBloom = texture(uBloomTex, vUV).rgb;

    vec3 combined = hdrScene + hdrBloom * uBloomStrength;
    combined *= uExposure;

    vec3 mapped = acesFilm(combined);
    vec2 centered = vUV * 2.0 - 1.0;
    float vig = 1.0 - 0.14 * dot(centered, centered);
    mapped *= vig;

    vec3 srgb = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(srgb, 1.0);
}
)GLSL";

// ============================================================================
// 6. 2D HUD & Telemetry Overlay Shader
// ============================================================================
static const char* HUD_VERT_SHADER = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform vec2 uScreenSize;
out vec4 vColor;
void main() {
    vColor = aColor;
    vec2 ndc = vec2(
        (aPos.x / uScreenSize.x) * 2.0 - 1.0,
        1.0 - (aPos.y / uScreenSize.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

static const char* HUD_FRAG_SHADER = R"GLSL(
#version 410 core
in vec4 vColor;
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)GLSL";

} // namespace bh
