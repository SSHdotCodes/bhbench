#version 410 core
// ============================================================================
//  Schwarzschild black hole — per-pixel null-geodesic ray tracer.
//
//  Every pixel shoots a light ray backwards from the camera and integrates its
//  path through curved spacetime with RK4. The bending follows the exact
//  Schwarzschild photon orbit equation  d2u/dphi2 + u = 3 M u^2  (u = 1/r),
//  re-expressed as a Cartesian acceleration
//
//        a = -(3/2) rs h^2 r_vec / r^5 ,   h = |r x v|  (conserved).
//
//  Working units: rs = 1  (Schwarzschild radius), hence M = rs/2 = 0.5.
//    photon sphere  r = 1.5 rs ,  shadow impact param b = 3*sqrt(3) M ~ 2.6 rs ,
//    ISCO           r = 3   rs  (= 6 M).
//
//  Outputs LINEAR HDR (can exceed 1.0); the blit pass does bloom + tonemap.
// ============================================================================

out vec4 FragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform vec3  uCamPos;          // camera position, in units of rs
uniform vec3  uCamRight;        // camera basis (orthonormal)
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform int   uSteps;           // max integration steps per ray
uniform float uDiskInner;       // disk inner radius (rs) — default 3.0 (ISCO)
uniform float uDiskOuter;
uniform int   uShowDisk;        // 1 = render accretion disk
uniform int   uBgMode;          // 0 = starfield, 1 = lensing test grid
uniform float uDiskBrightness;

const float RS       = 1.0;
const float M        = 0.5;          // rs / 2
const float ESCAPE_R = 45.0;         // ray escaped to "infinity"
const float PI       = 3.14159265358979;

// ---------------------------------------------------------------------------
//  Hash / noise helpers
// ---------------------------------------------------------------------------
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0,0,0)), n100 = hash31(i + vec3(1,0,0));
    float n010 = hash31(i + vec3(0,1,0)), n110 = hash31(i + vec3(1,1,0));
    float n001 = hash31(i + vec3(0,0,1)), n101 = hash31(i + vec3(1,0,1));
    float n011 = hash31(i + vec3(0,1,1)), n111 = hash31(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { s += a * vnoise(p); p *= 2.02; a *= 0.5; }
    return s;
}

// ---------------------------------------------------------------------------
//  Blackbody color (Tanner Helland approximation), T in Kelvin.
// ---------------------------------------------------------------------------
vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0) / 100.0;
    float r, g, b;
    if (T <= 66.0) r = 255.0;
    else           r = 329.698727446 * pow(T - 60.0, -0.1332047592);
    if (T <= 66.0) g = 99.4708025861 * log(T) - 161.1195681661;
    else           g = 288.1221695283 * pow(T - 60.0, -0.0755148492);
    if (T >= 66.0) b = 255.0;
    else if (T <= 19.0) b = 0.0;
    else b = 138.5177312231 * log(T - 10.0) - 305.0447927307;
    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
//  Geodesic acceleration and one RK4 step over state (pos, vel).
// ---------------------------------------------------------------------------
vec3 accel(vec3 p, vec3 v) {
    vec3 h = cross(p, v);
    float h2 = dot(h, h);
    float r = length(p);
    float r5 = r * r * r * r * r;
    return -1.5 * RS * h2 * p / r5;   // = -(3/2) rs h^2 r / r^5  (M = rs/2)
}
void rk4(inout vec3 p, inout vec3 v, float dt) {
    vec3 k1p = v;                 vec3 k1v = accel(p, v);
    vec3 k2p = v + 0.5*dt*k1v;    vec3 k2v = accel(p + 0.5*dt*k1p, v + 0.5*dt*k1v);
    vec3 k3p = v + 0.5*dt*k2v;    vec3 k3v = accel(p + 0.5*dt*k2p, v + 0.5*dt*k2v);
    vec3 k4p = v + dt*k3v;        vec3 k4v = accel(p + dt*k3p,     v + dt*k3v);
    p += (dt / 6.0) * (k1p + 2.0*k2p + 2.0*k3p + k4p);
    v += (dt / 6.0) * (k1v + 2.0*k2v + 2.0*k3v + k4v);
}

// ---------------------------------------------------------------------------
//  Accretion disk shading at an equatorial-plane crossing.
//   p   = crossing point (y ~ 0)
//   pdir= photon travel direction there (outward along the traced ray)
// ---------------------------------------------------------------------------
vec3 diskColor(vec3 p, vec3 pdir) {
    float r = length(vec2(p.x, p.z));

    // Novikov–Thorne-style thin-disk temperature: zero-torque inner edge.
    float xin = uDiskInner / r;
    float Tprofile = pow(xin, 0.75) * pow(max(1.0 - sqrt(xin), 0.0), 0.25);
    float T_emit = 14000.0 * Tprofile;          // peak emitted temperature (K)

    // Keplerian circular-orbit speed measured locally: v = sqrt(M/(r-2M)).
    float v = sqrt(M / max(r - 2.0 * M, 1e-3));
    v = min(v, 0.96);
    float gamma = 1.0 / sqrt(1.0 - v * v);

    // Orbital velocity direction (tangential, in the equatorial plane).
    vec3 bdir = normalize(vec3(-p.z, 0.0, p.x));
    vec3 beta = v * bdir;

    // Direction from emitter toward observer = reverse of the traced ray.
    vec3 n = -normalize(pdir);

    // Relativistic Doppler * gravitational redshift -> total frequency ratio g.
    float doppler = 1.0 / (gamma * (1.0 - dot(beta, n)));   // > 1 : approaching
    float grav    = sqrt(max(1.0 - RS / r, 0.0));            // gravitational redshift
    float g       = grav * doppler;

    // A Doppler/redshift-shifted blackbody is still a blackbody at T_obs = g*T,
    // and its bolometric surface brightness scales as g^4  ( = (T_obs/T)^4 ).
    float T_obs = T_emit * g;
    vec3  col   = blackbody(T_obs);
    float gg    = g * g;
    float beam  = gg * gg;                                   // g^4 relativistic boost

    // Turbulent, differentially-rotating texture (advect by Keplerian Omega).
    float phi   = atan(p.z, p.x);
    float omega = sqrt(M) / pow(r, 1.5);                     // coordinate ang. velocity
    float swirl = phi - omega * uTime * 6.0;
    float tex   = fbm(vec3(cos(swirl) * r * 0.6, sin(swirl) * r * 0.6, log(r) * 2.0
                           - omega * uTime * 1.5));
    float bands = 0.62 + 0.6 * tex;

    // Soft inner / outer edges.
    float edge = smoothstep(uDiskInner, uDiskInner + 0.25, r) *
                 (1.0 - smoothstep(uDiskOuter - 2.5, uDiskOuter, r));

    float intensity = beam * Tprofile * Tprofile * bands * edge;
    return col * intensity * uDiskBrightness;
}

// ---------------------------------------------------------------------------
//  Backgrounds (sampled when a ray escapes). The lensing warps whatever is here.
// ---------------------------------------------------------------------------
vec3 starLayer(vec3 d, float scale, float thresh, float size, vec3 tint) {
    vec3 c = d * scale;
    vec3 cell = floor(c);
    float h = hash31(cell);
    if (h < thresh) return vec3(0.0);
    vec3 sp = vec3(hash31(cell + 11.5), hash31(cell + 27.1), hash31(cell + 41.7));
    float dist = length(fract(c) - sp);
    float star = smoothstep(size, 0.0, dist);
    star *= star;
    float twinkle = 0.7 + 0.3 * sin(uTime * (1.0 + 5.0 * h) + h * 30.0);
    return tint * star * (0.4 + 0.6 * h) * twinkle;
}
vec3 starfield(vec3 d) {
    vec3 col = vec3(0.0014, 0.0022, 0.005);                 // faint sky
    // Milky-Way-like band with nebulosity.
    float band = exp(-pow(d.y * 3.2, 2.0));
    float neb  = fbm(d * 3.0 + 7.0);
    col += band * mix(vec3(0.010, 0.014, 0.030), vec3(0.05, 0.035, 0.028), neb) * (0.12 + 0.22 * neb);
    // A few star layers, varied color & density.
    col += starLayer(d, 28.0,  0.93, 0.045, vec3(1.0, 0.96, 0.92));
    col += starLayer(d, 55.0,  0.95, 0.040, vec3(0.85, 0.9, 1.0));
    col += starLayer(d, 95.0,  0.965, 0.035, vec3(1.0, 0.85, 0.7)) * 0.8;
    col += starLayer(d, 160.0, 0.978, 0.030, vec3(0.95, 0.95, 1.0)) * 0.6;
    return col;
}
vec3 gridBg(vec3 d) {
    // Lat/long celestial grid — makes gravitational lensing obvious.
    float lon = atan(d.z, d.x);
    float lat = asin(clamp(d.y, -1.0, 1.0));
    vec2 uv = vec2(lon, lat) * (180.0 / PI) / 10.0;          // line every 10 deg
    vec2 g = abs(fract(uv) - 0.5);
    vec2 w = fwidth(uv) * 1.5 + 1e-4;
    vec2 line = 1.0 - smoothstep(vec2(0.0), w, g);
    float l = max(line.x, line.y);
    vec3 base = mix(vec3(0.01, 0.02, 0.04), vec3(0.0, 0.05, 0.08), 0.5 + 0.5 * d.y);
    return base + vec3(0.1, 0.85, 1.0) * l * 0.9;
}
vec3 background(vec3 d) { return uBgMode == 1 ? gridBg(d) : starfield(d); }

// ---------------------------------------------------------------------------
void main() {
    vec2 uv = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;

    vec3 dir = normalize(uCamFwd
                         + uv.x * aspect * uTanHalfFov * uCamRight
                         + uv.y * uTanHalfFov * uCamUp);

    vec3 pos = uCamPos;
    vec3 vel = dir;

    vec3  color   = vec3(0.0);
    bool  done    = false;        // captured or hit the disk
    bool  escaped = false;
    float minR    = 1e9;          // closest approach (for the photon-ring glow)
    vec3  diskAccum = vec3(0.0);  // additive faint disk glow from multiple crossings

    for (int i = 0; i < uSteps; ++i) {
        float r = length(pos);
        minR = min(minR, r);

        if (r < RS * 1.001) { color = vec3(0.0); done = true; break; }  // captured
        if (r > ESCAPE_R)   { escaped = true; break; }

        // Adaptive step: small near the hole, larger far away. Refine near the
        // equatorial plane so the thin disk is never stepped over.
        float dt = clamp(0.16 * (r - RS), 0.012, 0.55);
        if (abs(pos.y) < 1.0 && r < uDiskOuter + 3.0) dt = min(dt, 0.05);

        vec3 prev = pos;
        rk4(pos, vel, dt);

        // Equatorial-plane (y = 0) crossing -> possible disk sample.
        if (uShowDisk == 1 && prev.y * pos.y < 0.0) {
            float t = prev.y / (prev.y - pos.y);
            vec3 hit = mix(prev, pos, t);
            float rad = length(vec2(hit.x, hit.z));
            if (rad >= uDiskInner && rad <= uDiskOuter) {
                vec3 dc = diskColor(hit, vel);
                // First (nearest) crossing is the opaque image we see.
                color = dc;
                done = true;
                break;
            }
        }
    }

    if (!done) {
        vec3 bg = escaped ? background(normalize(vel)) : background(normalize(vel));
        color += bg;
    }
    color += diskAccum;

    // Photon-ring glow: rays that grazed the photon sphere (~1.5 rs) pile up
    // into a thin bright ring at the shadow's edge.
    float ring = exp(-pow((minR - 1.5) * 3.2, 2.0));
    color += vec3(1.0, 0.78, 0.5) * ring * 0.35;

    FragColor = vec4(max(color, 0.0), 1.0);
}
