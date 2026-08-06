#version 410 core

in vec2 vUV;
out vec4 FragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform float uRs;           // Schwarzschild radius (2M in geometric units)
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uFovTan;
uniform int   uSteps;
uniform float uStepSize;
uniform bool  uShowGridOverlay;

// ── Utility ──────────────────────────────────────────────────────────────────

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.xxy + p.yzz) * p.zyx);
}

// Approximate blackbody color from temperature (Kelvin, log-scaled).
vec3 blackbodyColor(float tempK) {
    float t = clamp(tempK / 1e4, 0.05, 3.0);
    vec3 col;
    col.r = 1.0;
    col.g = clamp(0.4 + 0.6 * log(1.0 + t), 0.0, 1.0);
    col.b = clamp(0.1 + 0.9 * log(1.0 + t * 0.5), 0.0, 1.0);
    return col * (1.0 / (1.0 + t * 0.3));
}

// Star field on celestial sphere (direction d).
vec3 sampleStarfield(vec3 d) {
    vec3 base = vec3(0.01, 0.015, 0.03);
    vec2 uv = vec2(atan(d.z, d.x), asin(clamp(d.y, -1.0, 1.0)));
    for (int i = 0; i < 3; i++) {
        float h = hash21(uv * (120.0 + float(i) * 77.0));
        if (h > 0.997) {
            float b = pow(hash21(uv * 200.0 + float(i)), 4.0) * 3.0;
            vec3 starCol = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.7, 0.4), hash21(uv * 50.0));
            base += starCol * b;
        }
    }
    return base;
}

// Cartesian ↔ spherical conversions.
vec3 toCartesian(float r, float theta, float phi) {
    float st = sin(theta);
    return vec3(r * st * cos(phi), r * cos(theta), r * st * sin(phi));
}

void toSpherical(vec3 p, out float r, out float theta, out float phi) {
    r = length(p);
    theta = acos(clamp(p.y / max(r, 1e-6), -1.0, 1.0));
    phi = atan(p.z, p.x);
}

// Convert spherical velocity back to Cartesian tangent vector.
vec3 sphVelToCart(vec3 pos, float dr, float dth, float dphi) {
    float r, theta, phi;
    toSpherical(pos, r, theta, phi);
    float st = sin(theta), ct = cos(theta);
    float cp = cos(phi), sp = sin(phi);
    float dx = dr * st * cp + r * dth * ct * cp - r * dphi * st * sp;
    float dy = dr * ct - r * dth * st;
    float dz = dr * st * sp + r * dth * ct * sp + r * dphi * st * cp;
    return normalize(vec3(dx, dy, dz));
}

// Convert Cartesian direction to spherical velocity components (∂/∂λ).
vec3 cartDirToSphVel(vec3 pos, vec3 dir) {
    float r, theta, phi;
    toSpherical(pos, r, theta, phi);
    float st = max(sin(theta), 1e-4);
    float ct = cos(theta);
    float cp = cos(phi);
    float sp = sin(phi);

    float dr   =  st * cp * dir.x + st * sp * dir.z + ct * dir.y;
    float dth  = (ct * cp * dir.x + ct * sp * dir.z - st * dir.y) / max(r, 1e-4);
    float dphi = (-sp * dir.x + cp * dir.z) / max(r * st, 1e-4);
    return vec3(dr, dth, dphi);
}

// Enforce null geodesic condition: g_μν k^μ k^ν = 0.
// Returns dt/dλ given (r,θ, dr, dθ, dφ).
float enforceNull(float r, float theta, vec3 vel) {
    float f = 1.0 - uRs / max(r, uRs + 1e-4);
    float g_rr = 1.0 / max(f, 1e-6);
    float g_tt = -f;
    float g_thth = r * r;
    float g_phph = r * r * sin(theta) * sin(theta);

    float spatial = g_rr * vel.x * vel.x
                  + g_thth * vel.y * vel.y
                  + g_phph * vel.z * vel.z;
    return sqrt(max(spatial / max(-g_tt, 1e-6), 0.0));
}

// Schwarzschild Christoffel-symbol geodesic acceleration for (r, θ, φ).
vec3 geodesicAccel(float r, float theta, vec3 vel, float dt) {
    float f = 1.0 - uRs / max(r, uRs + 1e-4);
    float dr = vel.x, dth = vel.y, dphi = vel.z;
    float st = sin(theta), ct = cos(theta);

    // Γ^r components
    float acc_r = 0.0;
    acc_r -= 0.5 * uRs / (r * r) * f * dt * dt;                    // Γ^r_tt
    acc_r -= uRs / (2.0 * r * r * max(f, 1e-6)) * dr * dr;         // Γ^r_rr
    acc_r -= (r - uRs) * dth * dth;                                 // Γ^r_θθ
    acc_r -= (r - uRs) * st * st * dphi * dphi;                     // Γ^r_φφ

    // Γ^θ components
    float acc_th = 0.0;
    acc_th -= (2.0 / r) * dr * dth;                                 // Γ^θ_rθ + Γ^θ_θr
    acc_th -= st * ct * dphi * dphi;                                // Γ^θ_φφ

    // Γ^φ components
    float acc_phi = 0.0;
    acc_phi -= (2.0 / r) * dr * dphi;                               // Γ^φ_rφ + Γ^φ_φr
    acc_phi -= 2.0 * ct / max(st, 1e-4) * dth * dphi;              // Γ^φ_θφ + Γ^φ_φθ

    return vec3(acc_r, acc_th, acc_phi);
}

// RK4 integration step for null geodesic in Schwarzschild spacetime.
// State: position (r,θ,φ) and velocity (dr,dθ,dφ); dt reconstructed from null condition.
void rk4Step(inout float r, inout float theta, inout float phi,
             inout float dr, inout float dth, inout float dphi, inout float dt,
             float dLambda) {
    vec3 pos0 = vec3(r, theta, phi);
    vec3 vel0 = vec3(dr, dth, dphi);

    vec3 a1 = geodesicAccel(pos0.x, pos0.y, vel0, dt);
    vec3 k1_pos = vel0;
    vec3 k1_vel = a1;

    vec3 pos1 = pos0 + 0.5 * dLambda * k1_pos;
    vec3 vel1 = vel0 + 0.5 * dLambda * k1_vel;
    float dt1 = enforceNull(pos1.x, pos1.y, vel1);
    vec3 a2 = geodesicAccel(pos1.x, pos1.y, vel1, dt1);
    vec3 k2_pos = vel1;
    vec3 k2_vel = a2;

    vec3 pos2 = pos0 + 0.5 * dLambda * k2_pos;
    vec3 vel2 = vel0 + 0.5 * dLambda * k2_vel;
    float dt2 = enforceNull(pos2.x, pos2.y, vel2);
    vec3 a3 = geodesicAccel(pos2.x, pos2.y, vel2, dt2);
    vec3 k3_pos = vel2;
    vec3 k3_vel = a3;

    vec3 pos3 = pos0 + dLambda * k3_pos;
    vec3 vel3 = vel0 + dLambda * k3_vel;
    float dt3 = enforceNull(pos3.x, pos3.y, vel3);
    vec3 a4 = geodesicAccel(pos3.x, pos3.y, vel3, dt3);
    vec3 k4_pos = vel3;
    vec3 k4_vel = a4;

    r     = pos0.x + dLambda * (k1_pos.x + 2.0*k2_pos.x + 2.0*k3_pos.x + k4_pos.x) / 6.0;
    theta = pos0.y + dLambda * (k1_pos.y + 2.0*k2_pos.y + 2.0*k3_pos.y + k4_pos.y) / 6.0;
    phi   = pos0.z + dLambda * (k1_pos.z + 2.0*k2_pos.z + 2.0*k3_pos.z + k4_pos.z) / 6.0;
    dr    = vel0.x + dLambda * (k1_vel.x + 2.0*k2_vel.x + 2.0*k3_vel.x + k4_vel.x) / 6.0;
    dth   = vel0.y + dLambda * (k1_vel.y + 2.0*k2_vel.y + 2.0*k3_vel.y + k4_vel.y) / 6.0;
    dphi  = vel0.z + dLambda * (k1_vel.z + 2.0*k2_vel.z + 2.0*k3_vel.z + k4_vel.z) / 6.0;

    theta = clamp(theta, 0.001, 3.140);
    dt = enforceNull(r, theta, vec3(dr, dth, dphi));
}

// Novikov-Thorne thin disk emissivity profile (Schwarzschild ISCO at 3 r_s).
float diskEmissivity(float r) {
    float rIsco = 3.0 * uRs;
    float rOut  = 20.0 * uRs;
    if (r < rIsco || r > rOut) return 0.0;
    float x = sqrt(r / rIsco);
    return pow(r / rIsco, -3.0) * (1.0 - 1.0 / x) * (1.0 - 1.0 / x);
}

// Corona/halo emission above and below disk plane.
float haloEmissivity(float r, float height) {
    float rIsco = 3.0 * uRs;
    float rOut  = 15.0 * uRs;
    if (r < rIsco || r > rOut) return 0.0;
    float diskE = diskEmissivity(r);
    float scaleH = 0.08 * r;
    return diskE * 0.35 * exp(-abs(height) / scaleH);
}

// Relativistic Doppler beaming for Keplerian disk (prograde orbit).
float dopplerFactor(float r, float phi, vec3 rayDir) {
    float M = uRs * 0.5;
    float vOrb = sqrt(M / max(r, uRs));
    vec3 vel = vec3(-sin(phi) * vOrb, 0.0, cos(phi) * vOrb);
    float cosA = dot(normalize(vel), rayDir);
    float beta = vOrb;
    float gamma = 1.0 / sqrt(max(1.0 - beta * beta, 1e-6));
    float g = 1.0 / (gamma * (1.0 - beta * cosA));
    return pow(clamp(g, 0.05, 8.0), 3.0);
}

// ── Main ray tracer ──────────────────────────────────────────────────────────

vec3 traceRay(vec3 ro, vec3 rd) {
    float r, theta, phi;
    toSpherical(ro, r, theta, phi);

    vec3 sphVel = cartDirToSphVel(ro, rd);
    float velScale = 1.0;
    float dr = sphVel.x * velScale;
    float dth = sphVel.y * velScale;
    float dphi = sphVel.z * velScale;
    float dt = enforceNull(r, theta, vec3(dr, dth, dphi));

    vec3 color = vec3(0.0);
    bool captured = false;
    bool hitDisk = false;
    float prevThetaSign = theta - 1.5707963;

    float dLambda = uStepSize;

    for (int i = 0; i < 256; i++) {
        if (i >= uSteps) break;

        if (r <= uRs) {
            captured = true;
            break;
        }
        if (r > 200.0) {
            vec3 escapeDir = sphVelToCart(toCartesian(r, theta, phi), dr, dth, dphi);
            color = sampleStarfield(escapeDir);
            break;
        }

        // Check equatorial plane crossing (accretion disk).
        float thetaSign = theta - 1.5707963;
        if (!hitDisk && prevThetaSign * thetaSign < 0.0) {
            float rCross = r;
            float phiCross = phi;
            float diskE = diskEmissivity(rCross);
            if (diskE > 0.0) {
                float tempK = 8000.0 * pow(rCross / (3.0 * uRs), -0.75);
                vec3 diskCol = blackbodyColor(tempK);
                float dop = dopplerFactor(rCross, phiCross, rd);
                color += diskCol * diskE * 2.5 * dop;
                hitDisk = true;
            }
        }
        prevThetaSign = thetaSign;

        // Corona/halo sampling near equatorial plane.
        vec3 pos3 = toCartesian(r, theta, phi);
        float height = pos3.y;
        float haloE = haloEmissivity(r, height);
        if (haloE > 0.0) {
            float tempK = 15000.0 * pow(r / (3.0 * uRs), -0.5);
            color += blackbodyColor(tempK) * haloE * 0.6;
        }

        rk4Step(r, theta, phi, dr, dth, dphi, dt, dLambda);
    }

    if (captured) {
        return vec3(0.0);
    }
    if (length(color) < 1e-4) {
        color = sampleStarfield(rd);
    }
    return color;
}

void main() {
    vec2 uv = (vUV * 2.0 - 1.0);
    uv.x *= uResolution.x / uResolution.y;

    vec3 rd = normalize(uCamForward + uv.x * uFovTan * uCamRight + uv.y * uFovTan * uCamUp);
    vec3 col = traceRay(uCamPos, rd);

    // Einstein ring / photon sphere glow (analytic hint layered on traced image).
    float rCam = length(uCamPos);
    float b = rCam * sin(acos(clamp(dot(normalize(uCamPos), rd), -1.0, 1.0)));
    float photonSphere = 1.5 * uRs;
    float ringDist = abs(b - photonSphere);
    float ringGlow = exp(-ringDist * ringDist / (0.15 * uRs));
    col += vec3(1.0, 0.75, 0.35) * ringGlow * 0.08;

    // Tone mapping + gamma.
    col = col / (col + vec3(1.0));
    col = pow(col, vec3(1.0 / 2.2));

    FragColor = vec4(col, 1.0);
}