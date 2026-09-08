#version 410 core
// Schwarzschild null-geodesic ray tracer (runs per-pixel on the GPU).
//
// Physics (geometric units M = 1, rs = 2):
//  * Each photon is confined to its orbital plane (spherical symmetry).
//  * Plane basis:  e1 = radial dir at camera, e2 = tangential dir.
//  * Initial data from the camera ray (unit spatial direction d):
//      pr = (d.rhat) sqrt(1-rs/r0),  L = r0 |d - (d.rhat) rhat|,
//      E^2 = pr^2 + (1-rs/r0) L^2/r0^2        (null condition)
//  * Integrated with velocity Verlet on d^2r/dλ^2 = (L^2/r^3)(1-3M/r)
//    and dφ/dλ = L/r^2. Capture (r < rs) => shadow; escape (r > 50M)
//    => background sample along the bent ray. Disk-plane crossings
//    accumulate Novikov-Thorne-like emission with full GR redshift:
//      g = (k.u_obs)/(k.u_emit),  T_obs = g T_emit,  I ~ g^3..4.
out vec4 FragColor;
in vec2 vNDC;

uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uSpin;        // a/M, disk model only (bending is Schwarzschild)
uniform float uDiskInner;   // disk inner edge (M)
uniform float uDiskOuter;   // disk outer edge (M)
uniform float uTime;        // scene time (disk advection), arbitrary units
uniform int   uSteps;       // geodesic steps per pixel
uniform int   uOctaves;     // noise octaves (quality)
uniform float uExposure;
uniform int   uBgDetail;    // 0 = cheap background, 1 = full

const float RS = 2.0;
const float B_CRIT = 5.1961524;      // 3*sqrt(3)
const float ESCAPE_R = 50.0;
const float TIME_RATE = 6.0;         // disk pattern speed (illustrative)
const vec3  BAND_N = vec3(0.316228, 0.903738, 0.288675); // ~normalize(0.35,1,0.3)

// ---------------------------------------------------------------- utils
float hash13(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.17, 0.13));
    p *= 19.19;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float fbm(vec3 p, int oct) {
    float amp = 0.5, sum = 0.0, norm = 0.0;
    for (int o = 0; o < 5; ++o) {
        if (o >= oct) break;
        sum += amp * vnoise(p);
        norm += amp;
        p = p * 2.03 + vec3(1.7, 9.2, 4.1);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-6);
}

// Blackbody-ish ramp: cool ember -> orange -> white -> blue-white.
vec3 blackbody(float x) {
    x = clamp(x, 0.0, 1.0);
    vec3 c1 = vec3(0.42, 0.04, 0.0);
    vec3 c2 = vec3(1.00, 0.42, 0.07);
    vec3 c3 = vec3(1.00, 0.90, 0.74);
    vec3 c4 = vec3(0.62, 0.78, 1.00);
    vec3 col = mix(c1, c2, smoothstep(0.0, 0.38, x));
    col = mix(col, c3, smoothstep(0.38, 0.72, x));
    col = mix(col, c4, smoothstep(0.72, 1.0, x));
    return col;
}

// ------------------------------------------------------- background sky
vec3 star_layer(vec3 d, float scale, float thresh) {
    vec3 p = d * scale;
    vec3 cell = floor(p);
    float id = hash13(cell);
    if (id < thresh) return vec3(0.0);
    vec3 f = fract(p) - 0.5;
    vec3 spos = vec3(hash13(cell + 1.7), hash13(cell + 9.2),
                     hash13(cell + 4.5)) - 0.5;
    float dist = length(f - spos * 0.7);
    float aa = fwidth(dist) + 1e-4;
    float m = 1.0 - smoothstep(0.0, 0.10 + aa * 2.0, dist);
    float bright = (id - thresh) / (1.0 - thresh);   // 0..1 magnitude draw
    bright = 0.25 + 3.5 * bright * bright;
    vec3 tint = mix(vec3(0.70, 0.82, 1.0), vec3(1.0, 0.86, 0.70),
                    hash13(cell + 3.3));
    return tint * (m * bright);
}

vec3 background(vec3 d) {
    vec3 col = vec3(0.004, 0.005, 0.010);            // deep-sky floor
    col += star_layer(d, 36.0, 0.978);
    if (uBgDetail == 1) {
        col += star_layer(d, 80.0, 0.988) * 0.8;
    }
    // Milky-way-like band with mottled dust.
    float band = exp(-pow(dot(d, BAND_N) * 2.6, 2.0));
    float mott = fbm(d * 6.0, uBgDetail == 1 ? 4 : 2);
    float dust = fbm(d * 13.0 + 7.3, 3);
    vec3 band_col = mix(vec3(0.05, 0.06, 0.11), vec3(0.50, 0.47, 0.52),
                        smoothstep(0.30, 0.75, mott));
    band_col += vec3(0.42, 0.26, 0.14) * smoothstep(0.55, 0.90, dust) * 0.7;
    col += band * band_col * 0.85;
    // Faint large-scale nebulosity.
    col += vec3(0.10, 0.05, 0.16) * pow(fbm(d * 3.0 + 3.1, 3), 2.0);
    col += vec3(0.02, 0.10, 0.12) * pow(fbm(d * 4.0 + 9.7, 3), 2.0);
    return col;
}

// ------------------------------------------------------- geodesic core
float geo_accel(float r, float L) {
    return (L * L / (r * r * r)) * (1.0 - 3.0 / r);  // M = 1
}

float geo_step(float r) { return clamp(0.06 * r, 0.02, 0.5); }

vec3 perpendicular(vec3 v) {
    vec3 a = abs(v.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(v, a));
}

mat2 rot2(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c);
}

// Disk emission at crossing point pc (|pc| = rc). kphys is the photon's
// physical spatial momentum (static-tetrad components, world frame).
vec3 disk_shade(vec3 pc, float rc, vec3 kphys, float E, float rObs,
                out float alpha) {
    float inner = uDiskInner;
    // Novikov-Thorne-ish flux shape, normalized to its peak.
    float rp = inner * 49.0 / 36.0;
    float fpeak = (1.0 / (rp * rp * rp)) * (1.0 - sqrt(inner / rp));
    float f = (1.0 / (rc * rc * rc)) * (1.0 - sqrt(inner / rc));
    float Tn = pow(clamp(f / fpeak, 0.0, 1.5), 0.25);

    // Relativistic redshift: gravity + Doppler from Keplerian motion.
    float v = min(sqrt(1.0 / max(rc - RS, 0.05)), 0.95);
    float gamma = inversesqrt(max(1.0 - v * v, 1e-4));
    float phid = atan(pc.z, pc.x);
    vec3 phihat = vec3(-sin(phid), 0.0, cos(phid));
    // Backward tracing: the ray's spatial direction is the REVERSE of the
    // physical photon's (disk -> camera), so the Doppler term takes -kdp.
    float kdp = -dot(kphys, phihat);
    float num = -E / sqrt(max(1.0 - RS / rObs, 1e-5));
    float den = gamma * (-E / sqrt(max(1.0 - RS / rc, 1e-5)) + v * kdp);
    float g = clamp(num / den, 0.05, 4.0);
    float Tobs = g * Tn;

    // Advected turbulent pattern (Kerr equatorial Omega for the spin).
    float omega = 1.0 / (pow(rc, 1.5) + uSpin);
    vec2 xz = rot2(-omega * uTime * TIME_RATE) * pc.xz;
    vec3 q = vec3(xz.x * 0.55, rc * 0.35, xz.y * 0.55);
    float n = fbm(q + vec3(0.0, 3.7, 1.3), uOctaves);
    float streak = 0.55 + 0.90 * n;

    float edge_in = smoothstep(inner, inner * 1.05, rc);
    float edge_out = 1.0 - smoothstep(uDiskOuter * 0.80, uDiskOuter, rc);

    vec3 col = blackbody(pow(clamp(Tobs / 1.25, 0.0, 1.0), 0.65));
    // Steep T^4-style falloff: white-hot beamed rim, amber outer disk.
    float I = 2.2 * pow(max(Tobs, 0.0), 4.0) * streak;
    alpha = clamp(0.85 * (0.55 + 0.70 * n), 0.0, 1.0)
          * edge_in * edge_out;
    return col * I * edge_in * edge_out;
}

// ------------------------------------------------------------------ main
void main() {
    vec3 ro = uCamPos;
    vec3 rd = normalize(uCamFwd
                        + vNDC.x * uTanHalfFov * uAspect * uCamRight
                        + vNDC.y * uTanHalfFov * uCamUp);

    float r0 = length(ro);
    vec3 rhat0 = ro / r0;
    float dr0 = dot(rd, rhat0);
    vec3 tv = rd - dr0 * rhat0;
    float dt0 = length(tv);
    vec3 e1 = rhat0;
    vec3 e2 = dt0 > 1e-6 ? tv / dt0 : perpendicular(e1);

    float sq0 = sqrt(max(1.0 - RS / r0, 1e-6));
    float pr = dr0 * sq0;
    float L = r0 * dt0;
    float E = sqrt(pr * pr + (1.0 - RS / r0) * L * L / (r0 * r0));
    float b = L / max(E, 1e-9);   // impact parameter (halo diagnostic)

    float r = r0, phi = 0.0;
    vec3 pp = ro;
    vec3 col = vec3(0.0);
    float acc = 0.0;
    int crossings = 0;

    for (int i = 0; i < 1024; ++i) {
        if (i >= uSteps) break;
        float a0 = geo_accel(r, L);
        float h = geo_step(r);
        float rN = r + pr * h + 0.5 * a0 * h * h;
        if (rN < RS) { acc = 1.0; break; }          // captured: shadow
        float a1 = geo_accel(rN, L);
        float rmid = 0.5 * (r + rN);
        phi += L * h / (rmid * rmid);
        pr += 0.5 * (a0 + a1) * h;
        r = rN;

        float cp = cos(phi), sp = sin(phi);
        vec3 rhat = cp * e1 + sp * e2;              // plane radial dir
        vec3 that = -sp * e1 + cp * e2;             // plane tangent dir
        vec3 pn = r * rhat;

        // Accretion-disk plane crossing (disk: y = 0 annulus).
        if (pp.y * pn.y < 0.0 && crossings < 5) {
            float f = pp.y / (pp.y - pn.y);
            vec3 pc = mix(pp, pn, f);
            float rc = length(pc);
            if (rc > uDiskInner && rc < uDiskOuter) {
                float sq = sqrt(max(1.0 - RS / r, 1e-5));
                vec3 kphys = (pr / sq) * rhat + (L / r) * that;
                float alpha = 0.0;
                vec3 dem = disk_shade(pc, rc, kphys, E, r0, alpha);
                col += (1.0 - acc) * dem * alpha;
                acc += (1.0 - acc) * alpha;
                crossings++;
                if (acc > 0.995) break;
            }
        }
        pp = pn;

        if (r > ESCAPE_R) {                          // escaped: lens image
            vec3 vel = pr * rhat + (L / r) * that;
            col += (1.0 - acc) * background(normalize(vel));
            acc = 1.0;
            break;
        }
    }
    if (acc < 1.0) {  // step budget exhausted on a near-bound orbit
        float cp = cos(phi), sp = sin(phi);
        vec3 vel = pr * (cp * e1 + sp * e2) + (L / r) * (-sp * e1 + cp * e2);
        col += (1.0 - acc) * background(normalize(vel)) * 0.5;
        acc = 1.0;
    }

    // Photon-ring halo: rays near the critical curve (b ~ 3sqrt(3)M) pile
    // up higher-order images; the narrow term glows, the wide term hazes.
    float db = (b - B_CRIT);
    col += vec3(1.0, 0.70, 0.42) * exp(-db * db / (0.28 * 0.28)) * 0.45
           * (1.0 - acc * 0.75);
    col += vec3(0.45, 0.32, 0.22) * exp(-db * db / (2.2 * 2.2)) * 0.05;

    // Filmic-ish tonemap + gamma + dither.
    col = vec3(1.0) - exp(-col * uExposure);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    col += (hash13(vec3(vNDC * 513.7, 1.7)) - 0.5) * (1.5 / 255.0);
    FragColor = vec4(col, 1.0);
}
