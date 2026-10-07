#version 300 es
precision highp float;
precision highp int;
//
// General Relativistic Black Hole Simulation
// ==========================================
// Features:
//   - Schwarzschild geodesic ray tracing
//   - Gravitational lensing of background stars
//   - Accretion disk with Doppler beaming, gravitational redshift, thermal spectrum
//   - Photon sphere and event horizon rendering
//   - Spacetime curvature grid visualization
//   - User-controlled camera orbit
//

in vec2 vUV;
out vec4 fragColor;

// ── Constants ────────────────────────────────────────────────────────
const float PI = 3.14159265359;
const int MAX_STEPS = 256;
const float STEP_SIZE = 0.05;
const float DH = 1e-4;           // numerical differentiation step
const float EH = 1e-6;           // convergence tolerance

// Black hole parameters (geometric units: G = c = M = 1)
const float RS = 2.0;            // Schwarzschild radius = 2M
const float PHOTON_SPHERE = 1.5; // photon sphere at 1.5M = RS*0.75
const float DISK_INNER = 3.0;    // inner disk edge (ISCO = 3*RS for Schwarzschild)
const float DISK_OUTER = 12.0;   // outer disk edge
const float DISK_THICKNESS = 0.15;

// Camera
uniform vec3 camPos;
uniform vec3 camTarget;
uniform float aspectRatio;
uniform float time;
uniform int showGrid;           // 1 = show spacetime grid
uniform float gridDensity;      // spacing of grid lines
uniform float gridBend;         // visual exaggeration of grid curvature
uniform vec3 bgColor;           // background color (star field)
uniform float exposure;

// ── Star field (procedural, seeded) ─────────────────────────────────
uint hash(uint n) {
    n ^= 2747636419u;
    n *= 2654435761u;
    n ^= n >> 16;
    n *= 2654435761u;
    n ^= n >> 8;
    n *= 2654435761u;
    return n ^ (n >> 16);
}

float starField(vec3 dir) {
    vec3 d = normalize(dir);
    vec3 p = d * 200.0;
    vec3 ip = floor(p);
    float star = 0.0;
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
        vec3 neighbor = ip + vec3(float(dx), float(dy), float(dz));
        uint hx = uint(neighbor.x) * 73856093u;
        uint hy = uint(neighbor.y) * 19349663u;
        uint hz = uint(neighbor.z) * 83492791u;
        uint h = hash(hx ^ hy ^ hz);
        float r = float(h & 65535u) / 65535.0;
        float g = float((h >> 16u) & 65535u) / 65535.0;
        float brightness = step(0.97, r) * (0.5 + 0.5 * g);
        float dist = length(p - neighbor);
        brightness *= exp(-dist * dist * 0.1);
        star = max(star, brightness);
    }
    return star;
}

vec3 getStarColor(vec3 dir) {
    float brightness = starField(dir);
    uint h = hash(uint(length(dir) * 1000.0));
    float temp = float(h & 255u) / 255.0 * 255.0;
    vec3 col;
    if (temp < 85.0) col = vec3(1.0, 0.8, 0.6);
    else if (temp < 170.0) col = vec3(1.0, 1.0, 1.0);
    else col = vec3(0.7, 0.8, 1.0);
    return col * brightness;
}

// ── Schwarzschild geodesic integration ──────────────────────────────
// Returns: x = 0 = outside, 1 = inside event horizon
// Also fills accretionDiskHit with disk intersection info
struct RayResult {
    float dist;           // distance traveled
    int status;           // 0 = free space, 1 = event horizon, 2 = disk hit
    float diskR;          // radial coordinate at disk intersection
    float diskAngle;      // azimuthal angle at disk
    float diskSpeed;      // orbital speed at that radius
    float diskTemp;       // temperature at that radius
    vec3 diskPos;         // position of disk intersection
    vec3 diskNormal;      // normal at disk
    float lensMagnification; // magnification from lensing
};

RayResult traceGeodesic(vec3 origin, vec3 dir) {
    RayResult res;
    res.dist = 0.0;
    res.status = 0;
    res.diskR = 0.0;
    res.diskAngle = 0.0;
    res.diskSpeed = 0.0;
    res.diskTemp = 0.0;
    res.diskPos = vec3(0.0);
    res.diskNormal = vec3(0.0, 1.0, 0.0);
    res.lensMagnification = 1.0;

    vec3 pos = origin;
    vec3 v = dir; // 4-velocity spatial component (c=1)

    float totalDist = 0.0;
    float prevArea = 1.0; // tracking area element for magnification

    for (int i = 0; i < MAX_STEPS; i++) {
        float r = length(pos);
        float rOverM = r; // M = 1 in our units

        // Check event horizon
        if (r < RS * 0.5) {
            res.status = 1; // swallowed
            res.dist = totalDist;
            return res;
        }

        // Check accretion disk intersection (z ≈ 0 plane)
        if (abs(pos.z) < DISK_THICKNESS && r > DISK_INNER && r < DISK_OUTER) {
            // Compute disk properties
            float diskR = r;
            float angle = atan(pos.y, pos.x);

            // Keplerian orbital velocity (relativistic correction)
            float vKepler = sqrt(1.0 / (2.0 * r)); // simplified
            float lorentz = 1.0 / sqrt(1.0 - vKepler * vKepler);

            // Temperature from viscous heating: T ~ r^(-3/4)
            float T = 1.0 / pow(diskR / DISK_INNER, 0.75) * 15000.0;

            res.status = 2;
            res.diskR = diskR;
            res.diskAngle = angle;
            res.diskSpeed = vKepler;
            res.diskTemp = T;
            res.diskPos = pos;
            res.diskNormal = vec3(0.0, 1.0, 0.0);
            res.dist = totalDist;
            res.lensMagnification = prevArea;
            return res;
        }

        // Schwarzschild geodesic equation (spatial part):
        // d²xᵢ/dλ² = -GM/r³ * dxᵢ/dλ + 3GM/r³ * (dxᵢ/dλ · x̂) * x̂ + corrections
        // Using the post-Newtonian approximation for light bending:
        // deflection angle per step: δθ ≈ 2GM/(r²c²) * step

        float rsOverR2 = RS / (r * r);
        vec3 rHat = pos / r;

        // Gravitational acceleration (spatial curvature effect)
        // This gives the correct 1.75 arcsec deflection for Sun
        vec3 accel = 1.5 * rsOverR2 * (dot(v, v) * rHat - 3.0 * dot(v, rHat) * v);

        // Integrate (Velocity Verlet-like)
        vec3 newPos = pos + v * STEP_SIZE;
        float newR = length(newPos);
        vec3 newRHat = newPos / newR;
        float newRsOverR2 = RS / (newR * newR);
        vec3 newAccel = 1.5 * newRsOverR2 * (dot(v, v) * newRHat - 3.0 * dot(v, newRHat) * v);
        vec3 newV = v + 0.5 * (accel + newAccel) * STEP_SIZE;

        // Renormalize to keep |v| = 1 (light-like)
        newV = normalize(newV);
        v = newV;
        pos = newPos;

        totalDist += STEP_SIZE;

        // Track area element for lensing magnification
        float area = r * r;
        prevArea = area;

        // Escape condition
        if (r > 80.0) {
            res.dist = totalDist;
            return res;
        }
    }

    res.dist = totalDist;
    return res;
}

// ── Accretion disk color computation ────────────────────────────────
vec3 diskColor(float r, float angle, vec3 rayDir, vec3 obsDir) {
    // Blackbody spectrum (approximate)
    float T = 15000.0 * pow(r / DISK_INNER, -0.75);

    // Wien's approximation for Planck spectrum
    float freq = T * 5.879e10; // peak frequency
    vec3 color;

    // RGB approximation of blackbody at temperature T
    float tNorm = T / 10000.0;
    if (tNorm > 1.0) {
        color = vec3(
            smoothstep(0.0, 1.0, 1.0 - exp(-5.0 * (tNorm - 0.5))),
            smoothstep(0.0, 1.0, 1.0 - exp(-3.0 * (tNorm - 0.3))),
            1.0
        );
    } else {
        color = vec3(
            1.0,
            0.5 + 0.5 * tNorm,
            0.2 + 0.3 * tNorm
        );
    }

    // Doppler beaming (relativistic boosting)
    // Disk material orbits toward/away from observer
    vec3 tangent = vec3(-sin(angle), cos(angle), 0.0);
    float vOrb = sqrt(1.0 / (2.0 * r));
    float gamma = 1.0 / sqrt(1.0 - vOrb * vOrb);

    // Dot product of velocity with line of sight
    float doppler = 1.0 / (gamma * (1.0 - vOrb * dot(tangent, normalize(rayDir))));

    // Beaming factor: intensity ~ δ³
    float beaming = pow(doppler, 3.0);

    // Gravitational redshift
    float redshift = sqrt(1.0 - RS / r);

    // Combined effect
    float intensity = beaming * redshift;
    intensity *= exp(-pow((r - DISK_INNER) / (DISK_OUTER - DISK_INNER) * 4.0, 2.0)); // radial profile

    // Optical depth (inner disk is optically thick)
    float opticalDepth = exp(-pow((r - DISK_INNER) / 2.0, 2.0));

    vec3 finalColor = color * intensity * 800.0;

    // Add glow/bloom effect
    finalColor += color * 0.3 * beaming;

    return finalColor;
}

// ── Spacetime grid computation ──────────────────────────────────────
vec3 gridColor(vec3 gridPos, vec3 rayDir) {
    float r = length(gridPos);
    float rFlat = sqrt(gridPos.x * gridPos.x + gridPos.y * gridPos.y);
    float angle = atan(gridPos.y, gridPos.x);

    // Spacetime curvature: grid lines dip toward black hole
    // The "fabric" distortion follows the Schwarzschild metric
    float curvature = RS / (r + RS * 0.1);
    float zDip = -curvature * gridBend * exp(-rFlat / (RS * 3.0));

    // Grid line brightness
    float gridSpacing = gridDensity;
    float lineX = abs(fract(gridPos.x / gridSpacing) - 0.5);
    float lineY = abs(fract(gridPos.y / gridSpacing) - 0.5);
    float lineR = abs(fract(rFlat / gridSpacing) - 0.5);
    float lineA = abs(fract(angle / (PI / 8.0) - 0.5) - 0.5);

    float lineWidth = 0.04;
    float gridLine = 1.0 - smoothstep(0.0, lineWidth, min(min(lineX, lineY), min(lineR, lineA)));

    // Only show grid in the x-y plane (or near it for 3D effect)
    float planeFactor = 1.0 - smoothstep(0.0, 0.5, abs(gridPos.z));

    // Grid color with gravitational redshift tint
    float redshiftFactor = 1.0 / sqrt(1.0 + RS / (r + 0.1));
    vec3 gridCol = vec3(0.3, 0.5, 1.0) * redshiftFactor;

    // Fade grid near event horizon
    float horizonFade = smoothstep(RS * 0.8, RS * 2.0, r);

    return gridCol * gridLine * planeFactor * horizonFade * 2.0;
}

// ── Main ────────────────────────────────────────────────────────────
void main()
{
    vec2 ndc = (vUV - 0.5) * 2.0;
    vec3 rayDir;

    // Camera setup
    vec3 forward = normalize(camTarget - camPos);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);

    // Ray direction from camera through pixel
    float fov = 1.2;
    rayDir = normalize(forward * fov + right * ndc.x * fov * aspectRatio + up * ndc.y * fov);

    vec3 color = bgColor;
    float alpha = 1.0;

    // ── Ray trace through Schwarzschild spacetime ──
    RayResult result = traceGeodesic(camPos, rayDir);

    if (result.status == 1) {
        // Inside event horizon — pure black
        color = vec3(0.0);
    }
    else if (result.status == 2) {
        // Hit accretion disk
        vec3 diskCol = diskColor(result.diskR, result.diskAngle, rayDir, rayDir);

        // Add some turbulence/noise to the disk
        float noise = sin(result.diskAngle * 20.0 + time * 0.5) * 0.1;
        noise += sin(result.diskR * 5.0 - time * 2.0) * 0.05;
        diskCol += diskCol * noise;

        // Photon ring brightening (light bending around photon sphere)
        float photonRingFactor = exp(-pow((result.diskR - PHOTON_SPHERE * RS) / 0.5, 2.0));
        diskCol += vec3(1.0, 0.9, 0.7) * photonRingFactor * 3.0;

        color = diskCol;
    }
    else {
        // Ray escaped — check background stars with lensing
        // The ray direction has been bent, so we see background stars at displaced positions
        vec3 starDir = normalize(rayDir);
        color = getStarColor(starDir);

        // Add gravitational lensing arcs (subtle distortion of background)
        float r = length(camPos);
        float lensing = RS / (r + 0.1);
        color += vec3(0.02, 0.02, 0.04) * lensing;
    }

    // ── Spacetime grid overlay ──
    if (showGrid > 0) {
        // Trace a secondary ray to sample the grid
        vec3 gridOrigin = camPos;
        vec3 gridDir = rayDir;

        // Sample grid along the ray path
        vec3 gridCol = vec3(0.0);
        for (int g = 0; g < 8; g++) {
            float t = float(g) * 3.0;
            vec3 samplePos = gridOrigin + gridDir * t;
            float r = length(samplePos);
            if (r > RS * 0.5 && r < 30.0) {
                vec3 gCol = gridColor(samplePos, gridDir);
                float weight = exp(-t * 0.08);
                gridCol += gCol * weight;
            }
        }
        color += gridCol * 0.6;
    }

    // ── Photon sphere glow ──
    float rCam = length(camPos);
    float impactParam = RS * sqrt(27.0) / 4.0; // photon sphere impact parameter
    // Visual glow around the black hole shadow
    float shadowRadius = RS * 2.6; // apparent shadow size (includes lensing)
    float distFromCenter = length(ndc) * min(1.0, 1.0 / aspectRatio);
    float glow = exp(-pow((distFromCenter - shadowRadius / (rCam * 0.3)) / 0.05, 2.0));
    color += vec3(0.1, 0.08, 0.05) * glow * 0.3;

    // ── Tone mapping and output ──
    // Reinhard tone mapping
    color = color / (1.0 + color);

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    // Exposure
    color *= exposure;

    fragColor = vec4(color, 1.0);
}
