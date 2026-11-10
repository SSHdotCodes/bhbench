#version 410 core
// ---------------------------------------------------------------------------
// General-relativistic ray tracer for a Schwarzschild black hole.
//
// Units: G = c = 1, Schwarzschild radius RS = 2M = 1  (so M = 0.5).
//   photon sphere   r = 1.5 RS
//   ISCO            r = 3   RS
//   shadow radius   b_c = (3*sqrt(3)/2) RS ~= 2.598 RS
//
// Photon trajectories are the EXACT spatial paths of Schwarzschild null
// geodesics. From the orbit (Binet) equation  u'' + u = (3/2) RS u^2
// (u = 1/r), the equivalent central acceleration in areal-radius Cartesian
// coordinates is  a = -(3/2) RS h^2 x / r^5  with conserved h = |x cross v|.
// Integrated with RK4 and adaptive step size.
//
// Disk: optically thick, geometrically thin Shakura-Sunyaev disk on circular
// Keplerian orbits. Frequency shift between the comoving emitter and a
// static observer at infinity is computed in the local static orthonormal
// frame:   g = sqrt(1 - RS/r) / (gamma * (1 - beta . n_hat))
// with beta = sqrt(M/(r-RS)) the Keplerian speed measured by a static
// observer. Bolometric intensity scales as g^4 (Liouville / I_nu^3
// invariance); the observed spectrum is a blackbody at T_obs = g * T_emit.
// ---------------------------------------------------------------------------
in vec2 vUV;
out vec4 fragColor;

uniform vec2  uResolution;
uniform vec3  uCamPos, uCamRight, uCamUp, uCamFwd;
uniform float uTanHalfFov, uAspect;
uniform mat4  uPV;
uniform float uTime;
uniform float uStepScale;
uniform int   uMaxSteps;
uniform float uDiskInner, uDiskOuter, uDiskTemp;
uniform float uShowDisk, uShowStars;

const float RS = 1.0;
const float M  = 0.5;
const float ESCAPE_R = 90.0;

// ---------------- hashes / noise ----------------
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(
        mix(mix(hash13(i), hash13(i+vec3(1,0,0)), f.x),
            mix(hash13(i+vec3(0,1,0)), hash13(i+vec3(1,1,0)), f.x), f.y),
        mix(mix(hash13(i+vec3(0,0,1)), hash13(i+vec3(1,0,1)), f.x),
            mix(hash13(i+vec3(0,1,1)), hash13(i+vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
    return 0.55*vnoise(p) + 0.28*vnoise(p*2.13+7.7) + 0.17*vnoise(p*4.41+3.1);
}

// ---------------- blackbody color (Tanner Hellard fit) ----------------
vec3 blackbody(float T) {
    float t = clamp(T, 1000.0, 40000.0) / 100.0;
    float r, g, b;
    r = (t <= 66.0) ? 255.0 : 329.698727446 * pow(t - 60.0, -0.1332047592);
    g = (t <= 66.0) ? 99.4708025861 * log(t) - 161.1195681661
                    : 288.1221695283 * pow(t - 60.0, -0.0755148492);
    b = (t >= 66.0) ? 255.0
        : ((t <= 19.0) ? 0.0 : 138.5177312231 * log(t - 10.0) - 305.0447927307);
    vec3 c = clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
    return pow(c, vec3(2.2)); // to linear light
}

// ---------------- background sky (lensed by the geodesic integration) ------
vec3 sky(vec3 d) {
    if (uShowStars < 0.5) return vec3(0.0);
    vec3 col = vec3(0.0012, 0.0015, 0.0022);
    // faint Milky-Way-like band (procedural, for lensing visualization)
    vec3 nb = normalize(vec3(0.42, 0.78, 0.46));
    float q = dot(d, nb) / 0.16;
    float band = exp(-q * q);   // note: pow(x<0, y) is undefined in GLSL
    float n = fbm(d * 5.0);
    float dust = 0.55 + 0.45 * fbm(d * 9.0 + 3.7);
    col += band * (0.35 + 0.65 * n) * dust * vec3(0.30, 0.33, 0.44) * 0.085;
    col += band * band * vec3(0.10, 0.075, 0.05) * 0.05 * n;
    // star layer 1: bright, sparse
    vec3 cell = floor(d * 220.0);
    vec3 rnd = hash33(cell);
    if (rnd.x > 0.996) {
        vec3 sp = normalize((cell + 0.15 + 0.7 * rnd) / 220.0);
        float ang = length(d - sp);
        float rad = mix(0.0007, 0.0021, pow(rnd.y, 3.0));
        float s = 1.0 - smoothstep(rad * 0.25, rad, ang);
        col += s * blackbody(3200.0 + 9500.0 * rnd.z) * (2.0 + 7.0 * pow(rnd.y, 6.0));
    }
    // star layer 2: faint, dense
    cell = floor(d * 340.0);
    rnd = hash33(cell + 17.0);
    if (rnd.x > 0.988) {
        vec3 sp = normalize((cell + 0.15 + 0.7 * rnd) / 340.0);
        float ang = length(d - sp);
        float s = 1.0 - smoothstep(0.0001, 0.0006, ang);
        col += s * blackbody(3000.0 + 8000.0 * rnd.z) * (0.25 + 0.8 * rnd.y);
    }
    return col;
}

// ---------------- accretion disk emission ----------------
vec3 diskEmission(vec3 hp, vec3 v, out float depthOut) {
    float r = length(hp.xz);
    float f = 1.0 - RS / r;                       // lapse^2
    vec3 rhat = vec3(hp.x, 0.0, hp.z) / r;
    // photon direction in the local STATIC orthonormal frame:
    // radial component is blueshifted by 1/sqrt(f) relative to coordinate basis
    float vr = dot(v, rhat);
    vec3 vt = v - vr * rhat;
    vec3 nhat = normalize(vt + (vr / sqrt(f)) * rhat);
    // Keplerian circular orbit: physical speed vs static observer
    float beta = sqrt(M / (r - RS));
    vec3 phat = normalize(cross(vec3(0.0, 1.0, 0.0), rhat));
    float gamma = 1.0 / sqrt(1.0 - beta * beta);
    float g = sqrt(f) / (gamma * (1.0 - dot(beta * phat, nhat)));  // nu_obs/nu_emit

    // Shakura-Sunyaev zero-torque temperature profile, peak-normalized
    float x = uDiskInner / r;
    float prof = pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25) / 0.4909;
    float Temit = uDiskTemp * prof;

    // differential Keplerian rotation of emitting structure
    float omega = sqrt(M / (r * r * r));
    float phi = atan(hp.z, hp.x) - omega * uTime;
    float lr = log(r);
    float pattern = 1.0 + 0.18 * sin(6.0 * phi + 12.0 * lr)
                        + 0.12 * sin(11.0 * phi - 5.0 * lr + 1.7)
                        + 0.07 * sin(23.0 * phi + 3.0 * lr + 4.2);
    float edge = 1.0 - smoothstep(uDiskOuter - 0.9, uDiskOuter, r);

    float Tobs = g * Temit;
    // bolometric intensity ~ T_obs^4  =>  includes g^4 relativistic beaming
    float I = pow(Tobs / uDiskTemp, 4.0) * pattern * edge;
    depthOut = 0.0;
    return blackbody(Tobs) * I * 1.15;
}

// ---------------- geodesic acceleration ----------------
vec3 accel(vec3 p, float h2) {
    float r2 = dot(p, p);
    float r5 = r2 * r2 * sqrt(r2);
    return p * (-1.5 * RS * h2 / r5);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 rd = normalize(uCamFwd + uCamRight * (ndc.x * uTanHalfFov * uAspect)
                                + uCamUp    * (ndc.y * uTanHalfFov));
    vec3 p = uCamPos;
    vec3 v = rd;
    vec3 hx = cross(p, v);
    float h2 = dot(hx, hx);           // conserved angular momentum squared

    vec3 col = vec3(0.0);
    float depth = 1.0;
    bool done = false;

    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(p);
        float dl = clamp(0.08 * r, 0.02, 0.6) * uStepScale;

        // RK4 step of  (p' = v, v' = a(p))
        vec3 k1p = v;               vec3 k1v = accel(p, h2);
        vec3 k2p = v + 0.5*dl*k1v;  vec3 k2v = accel(p + 0.5*dl*k1p, h2);
        vec3 k3p = v + 0.5*dl*k2v;  vec3 k3v = accel(p + 0.5*dl*k2p, h2);
        vec3 k4p = v + dl*k3v;      vec3 k4v = accel(p + dl*k3p, h2);
        vec3 pn = p + (dl/6.0)*(k1p + 2.0*k2p + 2.0*k3p + k4p);
        vec3 vn = v + (dl/6.0)*(k1v + 2.0*k2v + 2.0*k3v + k4v);

        float rn = length(pn);

        // optically thick disk: first equatorial-plane crossing inside annulus
        if (uShowDisk > 0.5 && p.y * pn.y < 0.0) {
            float t = p.y / (p.y - pn.y);
            vec3 hp = mix(p, pn, t);
            float rh = length(hp.xz);
            if (rh > uDiskInner && rh < uDiskOuter) {
                float dHit;
                col = diskEmission(hp, normalize(mix(v, vn, t)), dHit);
                vec4 clip = uPV * vec4(hp, 1.0);
                depth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
                done = true;
                break;
            }
        }
        p = pn; v = vn;

        if (rn < 1.02) {            // crossed the horizon: captured
            vec4 clip = uPV * vec4(p, 1.0);
            // slight bias toward camera so the embedding grid's throat does
            // not poke through the shadow (curved-ray depth is approximate)
            depth = clamp((clip.z / clip.w * 0.5 + 0.5) * 0.96, 0.0, 1.0);
            col = vec3(0.0);
            done = true;
            break;
        }
        if (rn > ESCAPE_R && dot(p, v) > 0.0) {   // escaped to infinity
            col = sky(normalize(v));
            done = true;
            break;
        }
    }
    if (!done) {
        // ran out of steps (rare, near-critical rays): treat as captured
        col = vec3(0.0);
        vec4 clip = uPV * vec4(p, 1.0);
        depth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    }
    fragColor = vec4(col, 1.0);
    gl_FragDepth = depth;
}
