#version 300 es
precision highp float;
precision highp int;
#define saturate(x) clamp(x,0.0,1.0)
struct URT {
    float cam_x, cam_y, cam_z, cam_r;
    float fwd_x, fwd_y, fwd_z, tan_fov;
    float rgt_x, rgt_y, rgt_z, aspect;
    float up_x,  up_y,  up_z,  time;
};

struct UMesh {
    mat4 proj;
    mat4 view;
    float time, pad0, pad1, pad2;
};

// ----------------------------------------------------------------- hashing
uint hashU(uvec3 p) {
    p = (p << 13u) ^ (p >> 7u);
    p = p + (p << 15u);
    p = p ^ (p >> 12u);
    p = p + (p << 13u);
    return p.x ^ (p.y * 1973u) ^ (p.z * 9277u);
}

float hashF(uvec3 p) {
    return float(hashU(p)) * (1.0 / 4294967296.0);
}

float noise3(vec3 p) {
    uvec3 i = uvec3(floor(p));
    vec3 f = p - vec3(i);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float c00 = hashF(i);
    float c10 = hashF(i + uvec3(1, 0, 0));
    float c01 = hashF(i + uvec3(0, 1, 0));
    float c11 = hashF(i + uvec3(1, 1, 0));
    float c02 = hashF(i + uvec3(0, 0, 1));
    float c12 = hashF(i + uvec3(1, 0, 1));
    float c03 = hashF(i + uvec3(0, 1, 1));
    float c13 = hashF(i + uvec3(1, 1, 1));
    float x0 = mix(c00, c10, u.x);
    float x1 = mix(c01, c11, u.x);
    float x2 = mix(c02, c12, u.x);
    float x3 = mix(c03, c13, u.x);
    return mix(mix(x0, x1, u.y), mix(x2, x3, u.y), u.z);
}

float fbm3(vec3 p) {
    return 0.55 * noise3(p) + 0.30 * noise3(p * 2.13 + 13.7)
         + 0.15 * noise3(p * 4.41 + 41.2);
}

// ----------------------------------------------------------------- color
vec3 blackbody(float tempK) {
    float t = clamp(tempK * 0.01, 1.9, 400.0);
    float r, g, b;
    r = (t <= 66.0) ? 255.0 : 329.698727446 * pow(t - 60.0, -0.1332047592);
    g = (t <= 66.0) ? 99.4708025861 * log(max(t, 2.0)) - 161.1195681661
                     : 288.1221695283 * pow(t - 60.0, -0.0755148492);
    b = (t >= 66.0) ? 255.0
                     : ((t <= 19.0) ? 0.0
                                     : 138.5177312231 * log(t - 10.0) - 305.0447927307);
    return clamp(vec3(r, g, b), 0.0, 255.0) / 255.0;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

// ------------------------------------------------------- background sky
vec3 starLayer(vec3 n, float scale, float t, float dens, float gain) {
    vec3 p = n * scale;
    uvec3 id = uvec3(floor(p));
    vec3 f = p - vec3(id);
    float h0 = hashF(id);
    if (h0 > dens) return vec3(0.0);
    vec3 sp = vec3(hashF(id + uvec3(11, 0, 0)),
                       hashF(id + uvec3(22, 0, 0)),
                       hashF(id + uvec3(33, 0, 0))) - 0.5;
    float d = length(f - (0.5 + sp * 0.62));
    float m = saturate(1.0 - d * 3.6);
    m = m * m * m;
    float tw = 0.82 + 0.18 * sin(t * 2.5 + h0 * 311.0);
    vec3 tint = mix(vec3(1.0, 0.82, 0.64),
                      vec3(0.70, 0.84, 1.0),
                      hashF(id + uvec3(44, 0, 0)));
    return tint * m * gain * tw;
}

vec3 background(vec3 n, float t) {
    vec3 c = vec3(0.003, 0.004, 0.009);
    vec3 bn = normalize(vec3(0.37, 1.0, 0.28));
    float band = exp(-dot(n, bn) * dot(n, bn) / 0.042);
    float neb = fbm3(n * 2.6 + 5.1);
    c += band * (0.045 + 0.14 * neb) * vec3(0.72, 0.78, 1.0);
    c += band * band * (0.5 + 0.5 * noise3(n * 6.0)) * vec3(0.16, 0.07, 0.14);
    c += starLayer(n, 210.0, t, 0.10, 0.55);
    c += starLayer(n, 72.0, t, 0.045, 1.7);
    return c;
}

// ------------------------------------- Schwarzschild null geodesics
// State: position (r, th, ph), velocity d/dl (dr, dth, dph) in an affine
// parameter l. E = conserved energy (per unit affine length), Lz / Lth the
// conserved angular momentum components.
struct GS { float r, th, ph, dr, dth, dph; };
struct GOut { float vr, vt, vp, ar, at, ap; };

GOut gderiv(GS s, float E) {
    GOut o;
    float f  = max(1.0 - 2.0 / s.r, 0.03);
    float sn = max(sin(s.th), 1e-5);
    float cs = cos(s.th);
    float s2 = sn * sn;
    float invf3 = 1.0 / (f * f * f);
    // r-ddot = M E^2/(f^3 r^2) - M rdot^2/(f^3 r^2) - (r/f)(thdot^2 + sin^2 th phdot^2)
    o.ar = invf3 * (E * E - s.dr * s.dr) / (s.r * s.r)
           - (s.r / f) * (s.dth * s.dth + s2 * s.dph * s.dph);
    o.at = (s.dr * s.dth) / s.r - sn * cs * s.dph * s.dph;
    o.ap = (s.dr * s.dph) / s.r + (cs / sn) * s.dth * s.dph;
    o.vr = s.dr; o.vt = s.dth; o.vp = s.dph;
    return o;
}

void gstep(inout GS s, float h, float E) {
    GOut a = gderiv(s, E);
    GS s2 = s;
    s2.r += 0.5 * h * a.vr; s2.th += 0.5 * h * a.vt; s2.ph += 0.5 * h * a.vp;
    s2.dr += 0.5 * h * a.ar; s2.dth += 0.5 * h * a.at; s2.dph += 0.5 * h * a.ap;
    GOut b = gderiv(s2, E);
    GS s3 = s;
    s3.r += 0.5 * h * b.vr; s3.th += 0.5 * h * b.vt; s3.ph += 0.5 * h * b.vp;
    s3.dr += 0.5 * h * b.ar; s3.dth += 0.5 * h * b.at; s3.dph += 0.5 * h * b.ap;
    GOut c = gderiv(s3, E);
    GS s4 = s;
    s4.r += h * c.vr; s4.th += h * c.vt; s4.ph += h * c.vp;
    s4.dr += h * c.ar; s4.dth += h * c.at; s4.dph += h * c.ap;
    GOut d = gderiv(s4, E);
    s.r  += (h / 6.0) * (a.vr + 2.0 * b.vr + 2.0 * c.vr + d.vr);
    s.th += (h / 6.0) * (a.vt + 2.0 * b.vt + 2.0 * c.vt + d.vt);
    s.ph += (h / 6.0) * (a.vp + 2.0 * b.vp + 2.0 * c.vp + d.vp);
    s.dr += (h / 6.0) * (a.ar + 2.0 * b.ar + 2.0 * c.ar + d.ar);
    s.dth+= (h / 6.0) * (a.at + 2.0 * b.at + 2.0 * c.at + d.at);
    s.dph+= (h / 6.0) * (a.ap + 2.0 * b.ap + 2.0 * c.ap + d.ap);
}

// ------------------------------------------------- accretion disk light
// Thin disk in the equatorial plane, r in [6, ~34].
//   tempK(r)  = 1.5e7 K * (6/r)^(3/4)      (Shakura-Sunyaev inner-peak profile)
//   g     = sqrt(1-2M/r) / (gamma (1 - v n_phi))
//           with v = sqrt(M/(r-2M)) the Keplerian speed measured by a local
//           observer; g multiplies the temperature (Doppler shift)
//           and I ~ g^4 (relativistic beaming + redshifted blackbody).
vec3 diskEmit(GS s, float rc, float E, float Lz, float time) {
    float fc = max(1.0 - 2.0 / rc, 0.05);
    // photon phi-direction in the local orthonormal frame
    float nph = (Lz / rc) * sqrt(fc) / E;
    float v   = sqrt(1.0 / max(rc - 2.0, 0.1));
    float gam = 1.0 / sqrt(max(1.0 - v * v, 1e-4));
    float g   = sqrt(fc) / (gam * max(1.0 - v * nph, 0.05));

    float prof = pow(6.0 / rc, 1.5);
    prof *= smoothstep(5.95, 6.6, rc);
    prof *= exp(-max(rc - 24.0, 0.0) / 4.0);

    float spiral = 2.2 * s.ph - 0.45 * rc - 3.4 * time * pow(rc, -1.5);
    float turb   = 0.82 + 0.18 * sin(spiral);
    float fine   = 0.94 + 0.06 * noise3(vec3(rc * 0.7, s.ph * 30.0, 0.0));

    // path-length boost when the ray grazes the disk plane
    float nz    = max(abs(s.dr) * sqrt(fc) / E, 0.18);
    float boost = min(1.0 / nz, 3.5);

    float I  = 5.0 * prof * turb * fine * boost * pow(g, 4.0);
    float Tv = clamp(1.5e7 * pow(6.0 / rc, 0.75) * 1.05e-3 * g, 900.0, 40000.0);
    return blackbody(Tv) * I;
}

// ------------------------------------------------------------------- ray trace

uniform URT U;
in vec2 vUV; out vec4 fragColor;

void main(){
vec2 nd = vUV * 2.0 - 1.0;
    

    vec3 cp  = vec3(U.cam_x, U.cam_y, U.cam_z);
    vec3 fwd = normalize(vec3(U.fwd_x, U.fwd_y, U.fwd_z));
    vec3 rgt = normalize(vec3(U.rgt_x, U.rgt_y, U.rgt_z));
    vec3 upv = cross(rgt, fwd);
    vec3 dir = normalize(fwd + U.tan_fov * (nd.x * U.aspect * rgt + nd.y * upv));

    // camera in spherical coords
    float rC  = U.cam_r;
    vec3 rh = cp / rC;
    float thC = acos(clamp(rh.z, -1.0, 1.0));
    float phC = atan(rh.y, rh.x);
    vec3 phat = cross(vec3(0.0, 0.0, 1.0), rh);
    phat = (length(phat) < 0.5) ? vec3(1.0, 0.0, 0.0) : normalize(phat);
    vec3 that = cross(phat, rh);

    // orthonormal ray direction at the camera
    float nr  = dot(dir, rh);
    float ntt = dot(dir, that);
    float np  = dot(dir, phat);
    float fC   = max(1.0 - 2.0 / rC, 0.5);
    float E    = sqrt(fC);
    float Lth  = rC * ntt;
    float Lz   = rC * sin(thC) * np;
    float sinThC = max(sin(thC), 1e-4);

    GS s;
    s.r  = rC;  s.th = thC;  s.ph = phC;
    s.dr = nr * E; s.dth = ntt / rC; s.dph = np / (rC * sinThC);

    vec3 col = vec3(0.0, 0.0, 0.0);
    bool done = false;
    float rPrev = s.r;
    for (int i = 0; i < 1000 && !done; ++i) {
        float Ltot = sqrt(Lth * Lth + Lz * Lz);
        float h = clamp(0.022 * s.r * s.r / max(Ltot, 0.6), 0.008, 0.4);
        float sPrev = s.th - 1.5707963;
        rPrev = s.r;
        gstep(s, h, E);
        float sNow = s.th - 1.5707963;

        if (sPrev * sNow < 0.0) {
            float u  = sPrev / (sPrev - sNow);
            float rc = rPrev + u * (s.r - rPrev);
            if (rc > 5.95 && rc < 34.0) {
                GS sc = s;
                sc.r = rc;
                sc.th = 1.5707963;
                col = diskEmit(sc, rc, E, Lz, U.time);
                done = true;
                break;
            }
        }
        if (s.r <= 2.0) { done = true; break; }            // captured: black
        if (s.r > 85.0 && s.dr > 0.0) {
            vec3 n = normalize(vec3(s.dr, s.r * s.dth,
                                        s.r * sin(s.th) * s.dph));
            col = background(n, U.time);                    // escaped: lensed sky
            done = true;
            break;
        }
    }
    if (!done) col = vec3(1.7, 2.0, 2.6);              // photon ring
    fragColor = vec4(vec3(col), 0.0);

}
