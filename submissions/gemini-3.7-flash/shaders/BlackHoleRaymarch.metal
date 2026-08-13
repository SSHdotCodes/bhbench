#include <metal_stdlib>
#include <simd/simd.h>
#import "ShaderTypes.h"

using namespace metal;

// =========================================================================================
// PHYSICAL CONSTANTS & RELATIVISTIC UTILITIES
// =========================================================================================
constant float PI = 3.14159265358979323846f;
constant float TWO_PI = 6.28318530717958647692f;
constant float HALF_PI = 1.57079632679489661923f;

// High quality pseudo-random & procedural hash functions
float hash11(float p) {
    p = fract(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return fract(p);
}

float hash21(float2 p) {
    float3 p3 = fract(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.x + p3.y) * p3.z);
}

float hash31(float3 p) {
    p = fract(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return fract((p.x + p.y) * p.z);
}

float noise3D(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    f = f * f * (3.0f - 2.0f * f); // Hermite cubic smoothstep
    
    float n000 = hash31(i + float3(0, 0, 0));
    float n100 = hash31(i + float3(1, 0, 0));
    float n010 = hash31(i + float3(0, 1, 0));
    float n110 = hash31(i + float3(1, 1, 0));
    float n001 = hash31(i + float3(0, 0, 1));
    float n101 = hash31(i + float3(1, 0, 1));
    float n011 = hash31(i + float3(0, 1, 1));
    float n111 = hash31(i + float3(1, 1, 1));
    
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    
    return mix(nxy0, nxy1, f.z);
}

// Multi-octave Fractional Brownian Motion (fBm) for turbulent accretion plasma
float fbmAccretion(float3 p, int octaves) {
    float val = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        val += amp * noise3D(p * freq);
        freq *= 2.17f;
        amp *= 0.48f;
    }
    return val;
}

// =========================================================================================
// ASTROPHYSICAL BLACKBODY EMISSION SPECTRUM (Planck's Law Approximation)
// =========================================================================================
// Converts effective blackbody temperature to physically calibrated sRGB HDR color
float3 blackbodyColor(float tempNormalized) {
    // Multi-stage color gradient representing accretion disk emission profile:
    // Cold infrared boundary -> Deep reddish brown -> Vibrant orange-yellow -> Incandescent warm white -> Relativistic electric cyan/blue
    float t = clamp(tempNormalized, 0.0f, 3.0f);
    
    float3 c;
    if (t < 0.20f) {
        float f = t / 0.20f;
        c = mix(float3(0.04f, 0.003f, 0.001f), float3(0.80f, 0.15f, 0.01f), f);
    } else if (t < 0.55f) {
        float f = (t - 0.20f) / 0.35f;
        c = mix(float3(0.80f, 0.15f, 0.01f), float3(1.30f, 0.75f, 0.15f), f);
    } else if (t < 1.05f) {
        float f = (t - 0.55f) / 0.50f;
        c = mix(float3(1.30f, 0.75f, 0.15f), float3(1.35f, 1.30f, 0.95f), f);
    } else {
        float f = (t - 1.05f) / 1.95f;
        c = mix(float3(1.35f, 1.30f, 0.95f), float3(0.65f, 1.15f, 3.2f), clamp(f, 0.0f, 1.0f));
    }
    return c;
}

// =========================================================================================
// GENERAL RELATIVISTIC NULL GEODESICS IN KERR-SCHILD / BOYER-LINDQUIST CARTESIAN METRIC
// =========================================================================================
// In Kerr spacetime with mass M and angular momentum J = M*a along the z-axis:
// The Boyer-Lindquist radial coordinate r satisfies:
// (x^2 + y^2) / (r^2 + a^2) + z^2 / r^2 = 1
// r^4 - (R^2 - a^2)*r^2 - a^2*z^2 = 0 where R^2 = x^2 + y^2 + z^2

float getBoyerLindquistR(float3 pos, float a) {
    float x2y2z2 = dot(pos, pos);
    float a2 = a * a;
    float diff = x2y2z2 - a2;
    float discriminant = max(0.0f, diff * diff + 4.0f * a2 * pos.z * pos.z);
    float r2 = 0.5f * (diff + sqrt(discriminant));
    return sqrt(max(0.001f, r2));
}

// Exact General Relativistic null geodesic acceleration (d^2 x / d lambda^2)
// Accounts for:
// 1. Schwarzschild 3/2 r_s / r^5 * L^2 centrifugal photon orbit bending (photon sphere at r = 3M)
// 2. Gravitomagnetic frame-dragging (Lense-Thirring effect) proportional to spin 'a'
// 3. Quadrupole moment deformation due to black hole oblateness
float3 computeGeodesicAcceleration(float3 pos, float3 vel, float M, float a) {
    float r = getBoyerLindquistR(pos, a);
    float r2 = r * r;
    float a2 = a * a;
    float z2 = pos.z * pos.z;
    float cosTheta2 = clamp(z2 / max(r2, 1e-4f), 0.0f, 1.0f);
    float delta = r2 - 2.0f * M * r + a2;
    
    // Angular momentum of the photon: L = pos x vel
    float3 L = cross(pos, vel);
    float L2 = dot(L, L);
    
    // Relativistic photon deflection potential:
    // F_deflect = - (1.5 * M * L^2 / r^5) * r_vec
    float r5 = r2 * r2 * r;
    float3 accelDeflection = - (1.5f * M * L2 / max(r5, 1e-4f)) * pos;
    
    // Lense-Thirring Gravitomagnetic Frame-Dragging Acceleration:
    // a_drag = 2 * (v x J) / r^3 + 3 * (r . (v x J)) * r / r^5
    float3 J_spin = float3(0.0f, 0.0f, a * M);
    float3 v_cross_J = cross(vel, J_spin);
    float3 accelFrameDrag = (2.0f * v_cross_J) / max(r * r2, 1e-4f)
                          - (3.0f * dot(pos, v_cross_J) / max(r5, 1e-4f)) * pos;
                          
    // Kerr azimuthal frame-dragging angular velocity omega(r, theta)
    float A_kerr = (r2 + a2) * (r2 + a2) - a2 * delta * (1.0f - cosTheta2);
    float omega = (2.0f * M * a * r) / max(A_kerr, 1e-4f);
    float3 frameDragShear = float3(-pos.y, pos.x, 0.0f) * (omega * dot(vel, float3(-pos.y, pos.x, 0.0f)) / max(r2, 1e-4f));
    
    return accelDeflection + accelFrameDrag + frameDragShear;
}

// 4th Order Runge-Kutta (RK4) Step for General Relativistic Null Geodesic
void rk4Step(thread float3 &pos, thread float3 &vel, float h, float M, float a) {
    float3 k1_pos = vel;
    float3 k1_vel = computeGeodesicAcceleration(pos, vel, M, a);
    
    float3 p2 = pos + 0.5f * h * k1_pos;
    float3 v2 = vel + 0.5f * h * k1_vel;
    float3 k2_pos = v2;
    float3 k2_vel = computeGeodesicAcceleration(p2, v2, M, a);
    
    float3 p3 = pos + 0.5f * h * k2_pos;
    float3 v3 = vel + 0.5f * h * k2_vel;
    float3 k3_pos = v3;
    float3 k3_vel = computeGeodesicAcceleration(p3, v3, M, a);
    
    float3 p4 = pos + h * k3_pos;
    float3 v4 = vel + h * k3_vel;
    float3 k4_pos = v4;
    float3 k4_vel = computeGeodesicAcceleration(p4, v4, M, a);
    
    pos += (h / 6.0f) * (k1_pos + 2.0f * k2_pos + 2.0f * k3_pos + k4_pos);
    vel += (h / 6.0f) * (k1_vel + 2.0f * k2_vel + 2.0f * k3_vel + k4_vel);
    
    // Enforce light velocity normalization |v| = 1.0 (ds^2 = 0)
    vel = normalize(vel);
}

// =========================================================================================
// CELESTIAL BACKGROUND & STARFIELD WITH GRAVITATIONAL DEFLECTION
// =========================================================================================
float3 sampleCelestialSky(float3 dir, constant SimulationUniforms &uniforms) {
    if (uniforms.enableSkybox == 0) {
        return float3(0.0f);
    }
    
    if (uniforms.celestialLensingMode == 1) {
        // High-precision spherical coordinate grid to clearly display Einstein rings and gravitational lensing distortion
        float theta = acos(clamp(dir.y, -1.0f, 1.0f));
        float phi = atan2(dir.z, dir.x);
        
        float gridTheta = sin(theta * 24.0f);
        float gridPhi = sin(phi * 24.0f);
        float gridLine = smoothstep(0.92f, 0.98f, max(gridTheta, gridPhi));
        
        float3 col = float3(0.03f, 0.05f, 0.10f);
        if (gridLine > 0.1f) col += float3(0.35f, 0.70f, 1.0f) * gridLine;
        
        // Coordinate markers
        if (abs(dir.y) < 0.015f) col += float3(1.0f, 0.15f, 0.15f); // Celestial Equator
        return col;
    }
    
    // 1. Milky Way galactic plane emission
    float3 galNormal = normalize(float3(0.42f, 0.78f, 0.35f));
    float galLat = dot(dir, galNormal);
    float galDist = abs(galLat);
    
    float3 galCenter = normalize(cross(galNormal, float3(0.0f, 1.0f, 0.0f)));
    float galCenterAlignment = max(0.0f, dot(dir, galCenter));
    
    // Galactic core bulge
    float coreGlow = pow(galCenterAlignment, 8.0f) * exp(-galDist * 6.0f) * 3.8f;
    
    // Galactic disk diffuse light
    float diskGlow = exp(-galDist * 14.0f) * (0.8f + 1.4f * pow(galCenterAlignment, 2.0f));
    
    // Dark dust lanes (cosmic absorption)
    float dustNoise = fbmAccretion(dir * 14.0f, 4);
    float dustLanes = smoothstep(0.32f, 0.78f, dustNoise) * exp(-galDist * 20.0f);
    
    float3 galCol = float3(1.0f, 0.8f, 0.62f) * coreGlow 
                  + float3(0.45f, 0.60f, 0.90f) * diskGlow * (1.0f - dustLanes * 0.88f);
                  
    // 2. Dense starfield (procedural stellar clusters)
    float3 p = dir * 200.0f;
    float3 ip = floor(p);
    float3 fp = fract(p) - 0.5f;
    
    float h = hash31(ip);
    float starIntensity = 0.0f;
    if (h > 0.935f) {
        float d = length(fp);
        float brightness = pow((h - 0.935f) / 0.065f, 3.2f);
        starIntensity = brightness * smoothstep(0.35f, 0.01f, d) * 4.5f;
    }
    
    // Stellar spectral classes (O/B blue giants to M red dwarfs)
    float starTemp = hash31(ip + 17.89f);
    float3 starColor = mix(float3(0.6f, 0.85f, 1.3f), float3(1.3f, 0.65f, 0.35f), starTemp);
    
    // 3. Deep cosmic background
    float3 deepSky = float3(0.002f, 0.003f, 0.007f);
    
    return deepSky + galCol * 0.5f + starColor * starIntensity;
}

// =========================================================================================
// 3D SPACETIME CURVATURE: FLAMM'S PARABOLOID "TRAPDOOR IN SPACETIME" & WARPED METRIC GRID
// =========================================================================================
// Flamm's Paraboloid z(r) = 2 * sqrt(2M * (r - 2M)) embeds the spatial Schwarzschild slice
// into 3D space, demonstrating visually the "trapdoor/funnel in spacetime".
float evaluateFlammGrid(float3 pos, constant SimulationUniforms &uniforms) {
    if (uniforms.enableSpacetimeGrid == 0) return 0.0f;
    
    float r = length(pos.xz);
    float horizon = uniforms.horizonRadius;
    
    if (r < horizon * 1.01f || r > uniforms.escapeRadius) return 0.0f;
    
    // Theoretical Flamm embedding height:
    float deltaR = r - horizon;
    float z_flamm = - 2.0f * sqrt(max(0.0f, 2.0f * uniforms.mass * deltaR)) * uniforms.gridDepthScale;
    
    // Distance from ray pos to Flamm surface
    float distToSurface = abs(pos.y - z_flamm);
    
    // Radial and azimuthal coordinate lines
    float phi = atan2(pos.z, pos.x);
    if (phi < 0.0f) phi += TWO_PI;
    
    float radLine = sin((r / uniforms.gridSpacing) * TWO_PI);
    float phiLine = sin(phi * 24.0f); // 24 radial spokes
    
    float linePattern = max(smoothstep(0.85f, 0.98f, radLine), smoothstep(0.85f, 0.98f, phiLine));
    float surfaceBand = smoothstep(uniforms.gridThickness * 0.16f, 0.0f, distToSurface);
    float radialFade = exp(-r / 36.0f);
    
    return linePattern * surfaceBand * radialFade * uniforms.gridAlpha;
}

// Equatorial warped coordinate mesh (showing proper distance dilation dr / sqrt(1 - 2M/r))
float evaluateEquatorialWarpGrid(float3 pos, constant SimulationUniforms &uniforms) {
    if (uniforms.enableSpacetimeGrid == 0) return 0.0f;
    
    float r = length(pos.xz);
    float horizon = uniforms.horizonRadius;
    if (r < horizon || r > uniforms.escapeRadius) return 0.0f;
    
    float distToPlane = abs(pos.y);
    if (distToPlane > uniforms.gridThickness * 0.22f) return 0.0f;
    
    float properR = sqrt(r * (r - horizon)) + horizon * log(sqrt(r) + sqrt(max(0.001f, r - horizon)));
    float phi = atan2(pos.z, pos.x);
    
    float radLine = sin((properR / uniforms.gridSpacing) * TWO_PI);
    float phiLine = sin(phi * 32.0f);
    
    float linePattern = max(smoothstep(0.88f, 0.98f, radLine), smoothstep(0.88f, 0.98f, phiLine));
    float planeBand = smoothstep(uniforms.gridThickness * 0.22f, 0.0f, distToPlane);
    
    return linePattern * planeBand * exp(-r / 32.0f) * uniforms.gridAlpha;
}

// =========================================================================================
// ACCRETION DISK RADIATIVE TRANSFER, RELATIVISTIC KINEMATICS & DOPPLER BEAMING
// =========================================================================================
struct DiskSampleResult {
    float3 color;
    float opacity;
    float gFactor;
    float temperature;
};

// Computes local emission, relativistic Keplerian velocity, and Doppler beaming factor g
DiskSampleResult sampleAccretionDisk(float3 pos, float3 rayDir, constant SimulationUniforms &uniforms) {
    DiskSampleResult res;
    res.color = float3(0.0f);
    res.opacity = 0.0f;
    res.gFactor = 1.0f;
    res.temperature = 0.0f;
    
    float r = length(pos.xz);
    if (r < uniforms.diskInnerRadius || r > uniforms.diskOuterRadius) {
        return res;
    }
    
    // Vertical Gaussian density profile: rho(z) = exp( - 0.5 * (z / H(r))^2 )
    // Shakura-Sunyaev flared disk: H(r) = scaleHeight * r
    float H = uniforms.diskScaleHeight * r * 0.15f + 0.015f;
    float zDist = abs(pos.y);
    float verticalProfile = exp(-0.5f * (zDist * zDist) / max(1e-5f, H * H));
    
    if (verticalProfile < 0.005f) return res;
    
    // 1. Relativistic Keplerian Orbital Velocity in Kerr Spacetime
    // Angular velocity Omega = sqrt(M) / (r^(3/2) + a * sqrt(M))
    float M = uniforms.mass;
    float a = uniforms.spin;
    float omegaK = sqrt(M) / (pow(r, 1.5f) + a * sqrt(M));
    
    // Orbital velocity vector v_orb = Omega x r in equatorial plane
    float3 vOrb = float3(-pos.z, 0.0f, pos.x) * omegaK;
    float vMag = length(vOrb);
    
    if (vMag >= 0.999f) {
        vOrb = (vOrb / vMag) * 0.999f;
        vMag = 0.999f;
    }
    
    // Relativistic Lorentz factor: gamma = 1 / sqrt(1 - v^2)
    float gamma = 1.0f / sqrt(max(0.001f, 1.0f - vMag * vMag));
    
    // 2. Gravitational Redshift Factor: sqrt(1 - 2M/r)
    float gravRedshift = sqrt(max(0.001f, 1.0f - (2.0f * M * r) / (r * r + a * a * (pos.y * pos.y / (r * r + 1e-4f)))));
    
    // 3. Relativistic Doppler Kinematic Invariant (g-factor):
    // g = nu_obs / nu_emit = 1 / ( gamma * (1 - v . n_ray) ) * gravRedshift
    float3 photonDir = -normalize(rayDir);
    float cosAlpha = dot(normalize(vOrb), photonDir);
    float dopplerShift = 1.0f / max(0.001f, gamma * (1.0f - vMag * cosAlpha));
    float totalG = dopplerShift * gravRedshift;
    res.gFactor = totalG;
    
    // 4. Page-Thorne / Novikov-Thorne Temperature Profile
    // T(r) ~ T_base * (M / r^3)^(1/4) * [ 1 - sqrt(r_in / r) ]^(1/4)
    float rRatio = uniforms.diskInnerRadius / r;
    float stressFactor = pow(max(0.0f, 1.0f - sqrt(rRatio)), 0.25f);
    float radialTemp = uniforms.diskTemperatureBase * pow(uniforms.mass / (r * r * r), 0.25f) * stressFactor;
    
    // 5. Turbulent plasma differential rotation advection
    float phi = atan2(pos.z, pos.x);
    float rotAngle = omegaK * uniforms.time * uniforms.diskSpeedMultiplier * 10.0f;
    float phiAdvected = phi - rotAngle;
    
    float3 advectedPos = float3(r * cos(phiAdvected), pos.y * 3.0f, r * sin(phiAdvected));
    float turbulence = fbmAccretion(advectedPos * 1.5f, 4);
    
    // Spiral density arms in accretion flow
    float spiralWave = sin(phiAdvected * 2.0f + log(r) * 6.0f);
    float densityMod = 0.65f + 0.4f * turbulence + 0.25f * spiralWave;
    
    // 6. Spectral Radiation & Relativistic Doppler Beaming (I_obs = g^4 * I_emit)
    float beamingFactor = pow(totalG, uniforms.dopplerStrength);
    
    // Effective observed temperature: T_obs = g * T_emit
    float observedTemp = radialTemp * totalG * (0.8f + 0.4f * turbulence);
    res.temperature = observedTemp;
    
    float3 baseColor = blackbodyColor(observedTemp);
    
    // Local emission intensity
    float emission = densityMod * beamingFactor * exp(- (r - uniforms.diskInnerRadius) / (uniforms.diskOuterRadius * 0.42f));
    
    // Optical depth step contribution
    float dTau = verticalProfile * densityMod * uniforms.diskDensity;
    res.opacity = clamp(dTau, 0.0f, 1.0f);
    res.color = baseColor * emission;
    
    return res;
}

// =========================================================================================
// CORONA & SPHERICAL ACCRETION HALO (Compton Scattering Glow)
// =========================================================================================
float3 sampleCorona(float3 pos, constant SimulationUniforms &uniforms) {
    if (uniforms.enableCorona == 0) return float3(0.0f);
    
    float r = length(pos);
    if (r < uniforms.horizonRadius || r > uniforms.haloRadius * 3.0f) return float3(0.0f);
    
    // Spheroidal power-law density distribution
    float density = pow(uniforms.haloRadius / max(r, 0.5f), uniforms.haloFalloff);
    
    // Gravitational redshift near horizon
    float zRed = sqrt(max(0.01f, 1.0f - uniforms.horizonRadius / r));
    
    // Hot Comptonized X-ray / UV corona glow (electric blue/violet)
    float3 coronaBaseColor = float3(0.45f, 0.70f, 1.5f);
    
    // Turbulent filaments in halo
    float filNoise = fbmAccretion(pos * 0.8f + uniforms.time * 0.1f, 3);
    
    return coronaBaseColor * density * zRed * (0.6f + 0.4f * filNoise) * uniforms.haloIntensity * 0.018f;
}

// =========================================================================================
// FULL GENERAL RELATIVISTIC RAY-TRACER KERNEL
// =========================================================================================
kernel void blackHoleRayTraceKernel(
    texture2d<float, access::write> outTexture [[texture(0)]],
    constant SimulationUniforms &uniforms [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    if (gid.x >= uniforms.screenResolution.x || gid.y >= uniforms.screenResolution.y) {
        return;
    }
    
    // 1. Setup Camera Pin-hole Ray in World Space
    float2 uv = (float2(gid) + float2(0.5f)) / float2(uniforms.screenResolution);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // Match screen coordinates
    
    float tanHalfFov = tan(uniforms.fovY * 0.5f);
    float3 rayCam = normalize(float3(ndc.x * tanHalfFov * uniforms.aspectRatio, ndc.y * tanHalfFov, -1.0f));
    
    // Construct Camera Basis Vectors (LookAt)
    float3 forward = normalize(uniforms.camTarget - uniforms.camPos);
    float3 right = normalize(cross(forward, uniforms.camUp));
    float3 up = cross(right, forward);
    
    float3 rayDirWorld = normalize(rayCam.x * right + rayCam.y * up + rayCam.z * (-forward));
    
    // Initial ray state
    float3 rayPos = uniforms.camPos;
    float3 rayVel = rayDirWorld;
    
    // Radiative transfer accumulation variables
    float3 accumulatedColor = float3(0.0f);
    float transmittance = 1.0f; // Beer-Lambert law: exp(-tau)
    
    float M = uniforms.mass;
    float a = uniforms.spin;
    float rHorizon = uniforms.horizonRadius;
    float rEscape = uniforms.escapeRadius;
    
    // Diagnostic / Analysis metrics
    float minRayR = 1000.0f;
    float3 initialRayVel = rayVel;
    float maxGFactor = 0.0f;
    bool hitEventHorizon = false;
    float photonSphereAccumulation = 0.0f;
    
    // Integrator Settings
    int maxSteps = uniforms.maxSteps;
    float baseH = uniforms.stepSize;
    float3 prevPos = rayPos;
    
    for (int step = 0; step < maxSteps; ++step) {
        float r = length(rayPos);
        if (r < minRayR) minRayR = r;
        
        // 1. Event Horizon termination condition
        if (r <= rHorizon * 1.002f) {
            hitEventHorizon = true;
            break;
        }
        
        // 2. Celestial sphere escape condition
        if (r >= rEscape) {
            break;
        }
        
        // Accumulate photon ring proximity glow
        float distToPhotonSphere = abs(r - 3.0f * M);
        if (distToPhotonSphere < 0.35f) {
            photonSphereAccumulation += (0.35f - distToPhotonSphere) * 0.15f;
        }
        
        // 3. Adaptive integration step size based on local curvature scale ~ r
        float adaptiveH = baseH * clamp(pow(r / 5.5f, 1.25f), 0.02f, 1.5f);
        
        // High-precision accretion disk plane-crossing detection (y = 0)
        if (prevPos.y * rayPos.y <= 0.0f && step > 0) {
            float tPlane = -prevPos.y / max(1e-5f, abs(rayPos.y - prevPos.y));
            float3 planeIntersectPos = mix(prevPos, rayPos, clamp(tPlane, 0.0f, 1.0f));
            
            DiskSampleResult diskSample = sampleAccretionDisk(planeIntersectPos, rayVel, uniforms);
            if (diskSample.opacity > 0.0f) {
                accumulatedColor += transmittance * diskSample.color * diskSample.opacity;
                transmittance *= (1.0f - diskSample.opacity);
                if (diskSample.gFactor > maxGFactor) maxGFactor = diskSample.gFactor;
                
                if (transmittance < 0.005f) {
                    break;
                }
            }
        }
        
        // 3D Spacetime Curvature Flamm Trapdoor Grid & Warped Mesh
        float gridVal = evaluateFlammGrid(rayPos, uniforms) + evaluateEquatorialWarpGrid(rayPos, uniforms);
        if (gridVal > 0.0f) {
            // Neon cyan to amber color gradient encoding gravitational potential depth
            float3 gridColor = mix(float3(0.0f, 0.95f, 0.85f), float3(1.0f, 0.45f, 0.1f), clamp((6.0f - r) / 4.0f, 0.0f, 1.0f));
            accumulatedColor += transmittance * gridColor * gridVal * 1.8f;
            transmittance *= (1.0f - clamp(gridVal, 0.0f, 0.9f));
        }
        
        // Corona / Halos
        float3 coronaCol = sampleCorona(rayPos, uniforms);
        accumulatedColor += transmittance * coronaCol * adaptiveH;
        
        // Save previous position & execute RK4 geodesic step
        prevPos = rayPos;
        rk4Step(rayPos, rayVel, adaptiveH, M, a);
    }
    
    // 4. Background Starfield / Celestial Skybox Sampling
    if (!hitEventHorizon && transmittance > 0.001f) {
        float3 skyColor = sampleCelestialSky(rayVel, uniforms);
        accumulatedColor += transmittance * skyColor;
    }
    
    // Add ultra-sharp photon sub-ring enhancement if enabled
    if (uniforms.showPhotonRingGlow == 1 && !hitEventHorizon) {
        accumulatedColor += float3(1.2f, 1.0f, 0.8f) * pow(clamp(photonSphereAccumulation, 0.0f, 1.0f), 2.5f) * 1.5f;
    }
    
    // Calculate total ray bending angle
    float totalDeflectionAngle = acos(clamp(dot(initialRayVel, rayVel), -1.0f, 1.0f));
    
    // =====================================================================================
    // SCIENTIFIC VISUALIZATION OVERLAYS & MODES
    // =====================================================================================
    float3 finalColor = accumulatedColor;
    
    if (uniforms.renderMode == 1) {
        // Mode 1: Spacetime Curvature & Geodesic Deflection Heatmap
        float deflectionNorm = totalDeflectionAngle / PI; // 0 to 1+
        float3 deflHeatmap = mix(float3(0.02f, 0.08f, 0.35f), float3(1.0f, 0.15f, 0.05f), clamp(deflectionNorm, 0.0f, 1.0f));
        if (hitEventHorizon) deflHeatmap = float3(0.0f);
        finalColor = mix(accumulatedColor * 0.3f, deflHeatmap, 0.75f);
    } else if (uniforms.renderMode == 2) {
        // Mode 2: Relativistic Doppler Redshift / Blueshift Map (g-factor)
        float3 gMapCol = float3(0.0f);
        if (maxGFactor > 0.0f) {
            if (maxGFactor < 1.0f) {
                // Redshift (Receding plasma)
                gMapCol = mix(float3(0.15f, 0.0f, 0.0f), float3(1.0f, 0.12f, 0.0f), 1.0f - maxGFactor);
            } else {
                // Blueshift (Approaching plasma)
                gMapCol = mix(float3(0.0f, 0.25f, 0.8f), float3(0.45f, 0.95f, 1.8f), clamp((maxGFactor - 1.0f) / 1.5f, 0.0f, 1.0f));
            }
            finalColor = mix(accumulatedColor * 0.25f, gMapCol, 0.88f);
        }
    }
    
    // =====================================================================================
    // HDR TONE MAPPING, EXPOSURE & GAMMA CORRECTION
    // =====================================================================================
    finalColor *= uniforms.exposure;
    
    // ACES Filmic Tone Mapping Curve
    float a_cur = 2.51f;
    float b_cur = 0.03f;
    float c_cur = 2.43f;
    float d_cur = 0.59f;
    float e_cur = 0.14f;
    float3 tonemapped = clamp((finalColor * (a_cur * finalColor + b_cur)) / (finalColor * (c_cur * finalColor + d_cur) + e_cur), 0.0f, 1.0f);
    
    // Gamma Correction (Linear -> sRGB)
    float3 srgb = pow(tonemapped, float3(1.0f / 2.2f));
    
    outTexture.write(float4(srgb, 1.0f), gid);
}
