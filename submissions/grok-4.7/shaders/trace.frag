#version 410 core
// Backward null geodesics in the Schwarzschild metric (G = c = M = 1, r_s = 2).
// q_μ is past-directed, so the ray walks from the camera into the scene.
// Redshift uses g = ω_camera / ω_emitter with the orbiting-disk 4-velocity.

in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform vec2 uResolution;
uniform float uAspect;
uniform float uTanHalf;
uniform vec3 uCamPos;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFwd;
uniform sampler2D uFluxTex;
uniform float uFluxIn;
uniform float uFluxOut;
uniform float uFluxN;
uniform float uDiskIn;
uniform float uDiskOut;
uniform float uTime;
uniform float uCoordStep;
uniform int uStepLimit;
uniform int uColorMode;   // 0 physical, 1 g-factor, 2 capture mask
uniform int uFlat;
uniform int uShowDisk;
uniform int uShowHalo;
uniform int uShowGrid;
uniform float uDiskGain;
uniform float uHaloGain;
uniform float uTmax;
uniform vec3 uSource0;
uniform vec3 uSource1;

const float RS = 2.0;
const float PI = 3.141592653589793;
const int MAX_STEPS = 280;

struct State {
    vec3 x;
    vec3 p;
    float pt;
};

struct Deriv {
    vec3 x;
    vec3 p;
};

float hash13(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 blackbody_lin(float kelvin) {
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float r, g, b;
    if (t <= 66.0) {
        r = 1.0;
        g = clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0);
    } else {
        r = clamp(1.29293618606 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
        g = clamp(1.12989086090 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    }
    if (t >= 66.0) b = 1.0;
    else if (t <= 19.0) b = 0.0;
    else b = clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0);
    return pow(max(vec3(r, g, b), vec3(0.0)), vec3(2.2));
}

float flux_at(float r) {
    if (r <= uFluxIn || r >= uFluxOut) return 0.0;
    float u = (r - uFluxIn) / (uFluxOut - uFluxIn);
    u = (u * (uFluxN - 1.0) + 0.5) / uFluxN;
    return texture(uFluxTex, vec2(clamp(u, 0.0, 1.0), 0.5)).r;
}

float disk_pattern(float rc, vec3 x) {
    float omega = pow(max(rc, 6.0), -1.5);
    float phi = atan(x.y, x.x) - uTime * omega;
    float lnr = log(max(rc, 1.0));
    float s = 1.0
        + 0.28 * sin(3.0 * phi + 1.8 * lnr)
        + 0.16 * sin(8.0 * phi - 3.4 * lnr)
        + 0.08 * sin(14.0 * phi + 5.0 * sin(lnr));
    return clamp(s, 0.28, 1.7);
}

// g = ω_cam / ω_em for a circular equatorial emitter. ω_cam = 1 by construction.
float g_factor(vec3 x, vec3 p, float pt) {
    float rad = max(length(x), 6.05);
    float denom = sqrt(max(1.0 - 3.0 / rad, 1e-3));
    float ut = 1.0 / denom;
    float uphi = inversesqrt(rad * rad * rad) / denom;
    float ux = -uphi * x.y;
    float uy = uphi * x.x;
    float omega = pt * ut + p.x * ux + p.y * uy;
    return clamp(1.0 / max(omega, 1e-3), 0.02, 8.0);
}

vec3 disk_radiance(float rc, float g, vec3 x) {
    float Fn = flux_at(rc);
    if (Fn <= 1e-4) return vec3(0.0);
    float T = uTmax * pow(Fn, 0.25) * g;
    vec3 chroma = blackbody_lin(T);
    float gg = clamp(g, 0.0, 8.0);
    float g4 = gg * gg;
    g4 *= g4;
    // Physical weight is g^4 * F. The exponential compresses that for an SDR
    // display so the blackbody hue survives instead of clipping to white.
    float Iphys = pow(Fn, 0.55) * g4 * disk_pattern(rc, x);
    float shown = 1.0 - exp(-0.55 * Iphys);
    return chroma * shown * uDiskGain;
}

vec3 false_g(float g, float Fn) {
    vec3 c = mix(vec3(0.06, 0.16, 0.75), vec3(0.95, 0.92, 0.78), smoothstep(0.35, 1.05, g));
    c = mix(c, vec3(1.0, 0.22, 0.05), smoothstep(1.05, 2.5, g));
    return c * (0.30 + 0.70 * clamp(Fn, 0.0, 1.0));
}

vec3 sky_color(vec3 dir) {
    dir = normalize(dir);
    vec3 col = vec3(0.004, 0.005, 0.010);

    vec3 galN = normalize(vec3(0.22, 0.62, 0.75));
    float band = exp(-pow(dot(dir, galN) * 5.2, 2.0));
    float mottling = 0.65 + 0.35 * sin(dot(dir, vec3(18.0, 7.0, 11.0)) + 2.0 * sin(dot(dir, vec3(4.0, 13.0, 3.0))));
    col += band * mottling * vec3(0.11, 0.09, 0.16);

    float scale = 86.0;
    vec3 id = floor(dir * scale);
    vec3 f = fract(dir * scale) - 0.5;
    float h = hash13(id);
    if (h > 0.982) {
        vec3 jitter = vec3(hash13(id + 11.0), hash13(id + 29.0), hash13(id + 47.0)) - 0.5;
        float d2 = dot(f - jitter * 0.5, f - jitter * 0.5);
        float core = exp(-d2 * 160.0);
        float mag = pow(fract(h * 17.13), 18.0);
        float tint = hash13(id + 3.1);
        vec3 star = mix(vec3(1.0, 0.72, 0.48), vec3(0.72, 0.84, 1.0), tint);
        col += star * core * (1.1 + 16.0 * mag);
    }

    if (uShowGrid == 1) {
        float lon = atan(dir.y, dir.x);
        float lat = asin(clamp(dir.z, -1.0, 1.0));
        float lonStep = PI / 6.0;
        float latStep = PI / 6.0;
        float dlon = min(fract(lon / lonStep), 1.0 - fract(lon / lonStep)) * lonStep;
        float dlat = min(fract((lat + PI * 0.5) / latStep), 1.0 - fract((lat + PI * 0.5) / latStep)) * latStep;
        float grid = smoothstep(0.018, 0.004, dlon) + smoothstep(0.018, 0.004, dlat);
        col += vec3(0.055, 0.14, 0.22) * clamp(grid, 0.0, 1.0);
    }

    float a0 = acos(clamp(dot(dir, normalize(uSource0)), -1.0, 1.0));
    col += vec3(0.70, 0.84, 1.0) * exp(-pow(a0 / 0.016, 2.0)) * 3.5;
    col += vec3(0.25, 0.36, 0.62) * exp(-pow(a0 / 0.07, 2.0)) * 0.35;
    float a1 = acos(clamp(dot(dir, normalize(uSource1)), -1.0, 1.0));
    col += vec3(1.0, 0.74, 0.42) * exp(-pow(a1 / 0.020, 2.0)) * 2.4;
    col += vec3(0.50, 0.30, 0.18) * exp(-pow(a1 / 0.09, 2.0)) * 0.28;
    return col;
}

Deriv deriv_of(State s) {
    Deriv d;
    float r = length(s.x);
    float rm = max(r - RS, 1e-4);
    float xp = dot(s.x, s.p);
    float invr = 1.0 / max(r, 1e-4);
    float invr3 = invr * invr * invr;
    float invr5 = invr3 * invr * invr;
    float rsxp = RS * invr3 * xp;
    d.x = s.p - rsxp * s.x;
    float pt2 = s.pt * s.pt;
    float radial = -(0.5 * RS * pt2) / (r * rm * rm) - (1.5 * RS * invr5 * xp * xp);
    d.p = radial * s.x + rsxp * s.p;
    return d;
}

bool rk4(inout State s, float h, Deriv k1) {
    State s2 = s;
    s2.x += 0.5 * h * k1.x;
    s2.p += 0.5 * h * k1.p;
    if (length(s2.x) < RS + 1e-3) return false;
    Deriv k2 = deriv_of(s2);
    State s3 = s;
    s3.x += 0.5 * h * k2.x;
    s3.p += 0.5 * h * k2.p;
    if (length(s3.x) < RS + 1e-3) return false;
    Deriv k3 = deriv_of(s3);
    State s4 = s;
    s4.x += h * k3.x;
    s4.p += h * k3.p;
    if (length(s4.x) < RS + 1e-3) return false;
    Deriv k4 = deriv_of(s4);
    s.x += (h / 6.0) * (k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x);
    s.p += (h / 6.0) * (k1.p + 2.0 * k2.p + 2.0 * k3.p + k4.p);
    return length(s.x) > RS + 1e-3;
}

float choose_h(State s, vec3 vel) {
    float r = length(s.x);
    float speed = max(length(vel), 1e-5);
    // Small steps on the way in, where the bending happens. Once the ray is
    // climbing back out, the remaining path is almost straight and can be coarse.
    float target = uCoordStep * r;
    bool climbing = dot(s.x, vel) > 0.0 && r > 7.0;
    if (climbing) target = max(target, 0.28 * r);
    if (r < 12.0) target = min(target, 0.030 * r);
    if (r < 6.0) target = min(target, 0.014 * r);
    target = min(target, 0.30 * max(r - RS, 1e-3));
    float cyl = length(s.x.xy);
    // Keep plane crossings resolvable. Sign-change still catches a single
    // crossing, so this only has to stop a step from cutting the disk twice.
    if (!climbing && abs(s.x.z) < 1.4 && cyl < uDiskOut + 2.0) target = min(target, 0.22);
    return clamp(target / speed, 1e-4, 2.0);
}

vec3 corona(vec3 x, vec3 p, float pt, float dl) {
    if (uShowHalo == 0 || uShowDisk == 0 || uColorMode == 2) return vec3(0.0);
    float rc = length(x.xy);
    if (rc < uDiskIn || rc > uDiskOut) return vec3(0.0);
    float H = max(0.055 * rc, 0.08);
    if (abs(x.z) > 3.6 * H) return vec3(0.0);
    float dens = exp(-0.5 * (x.z * x.z) / (H * H));
    float g = g_factor(x, p, pt);
    vec3 emit = disk_radiance(rc, g, x) * dens * dl * uHaloGain;
    if (uColorMode == 1) return false_g(g, flux_at(rc)) * dens * dl * 0.35;
    return emit;
}

vec3 finish_color(vec3 col) {
    if (isnan(col.x) || isnan(col.y) || isnan(col.z)) return vec3(0.0);
    return min(max(col, vec3(0.0)), vec3(90.0));
}

void main() {
    vec2 ndc = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    ndc.x *= uAspect;
    vec3 local = normalize(vec3(ndc * uTanHalf, 1.0));
    vec3 d_coord = local.x * uRight + local.y * uUp + local.z * uFwd;

    float rcam = length(uCamPos);
    float alpha = 1.0 - RS / rcam;
    float sqrt_a = sqrt(max(alpha, 1e-4));
    float gamma = RS / max(rcam - RS, 1e-4);
    vec3 n = uCamPos / rcam;
    float nd = dot(n, d_coord);
    vec3 q = d_coord + gamma * n * nd;
    float qt = sqrt_a;

    // Conserved impact parameter. Used to check the camera tetrad without integrating.
    if (uColorMode == 3) {
        float b = length(cross(uCamPos, q)) / max(qt, 1e-6);
        fragColor = vec4(b < 5.196152 ? vec3(0.0) : vec3(1.0), 1.0);
        return;
    }

    if (uFlat == 1) {
        vec3 dir = normalize(d_coord);
        float tHit = 1e30;
        bool hitBall = false;
        bool hitDisk = false;
        vec3 xh = uCamPos;
        float b = dot(uCamPos, dir);
        float c = dot(uCamPos, uCamPos) - RS * RS;
        float disc = b * b - c;
        if (disc > 0.0) {
            float t = -b - sqrt(disc);
            if (t > 0.0) {
                tHit = t;
                hitBall = true;
            }
        }
        if (uShowDisk == 1 && abs(dir.z) > 1e-5) {
            float t = -uCamPos.z / dir.z;
            if (t > 0.35 && t < tHit) {
                vec3 p = uCamPos + t * dir;
                float rc = length(p.xy);
                if (rc >= uDiskIn && rc <= uDiskOut) {
                    tHit = t;
                    hitBall = false;
                    hitDisk = true;
                    xh = p;
                }
            }
        }
        vec3 col;
        if (uColorMode == 2) {
            col = hitBall ? vec3(0.0) : vec3(1.0);
        } else if (hitDisk) {
            float g = g_factor(xh, q, qt);
            col = (uColorMode == 1) ? false_g(g, flux_at(length(xh.xy))) : disk_radiance(length(xh.xy), g, xh);
        } else if (hitBall) {
            col = vec3(0.0);
        } else {
            col = sky_color(dir);
        }
        fragColor = vec4(finish_color(col), 1.0);
        return;
    }

    State s;
    s.x = uCamPos;
    s.p = q;
    s.pt = qt;
    float minr = rcam;
    float traveled = 0.0;
    vec3 glow = vec3(0.0);
    float escape_r = max(rcam * 0.98, 16.0);
    int limit = clamp(uStepLimit, 1, MAX_STEPS);

    for (int i = 0; i < MAX_STEPS; ++i) {
        if (i >= limit) break;
        float r = length(s.x);
        minr = min(minr, r);
        if (r < RS + 0.07) {
            vec3 col = (uColorMode == 2) ? vec3(0.0) : glow;
            fragColor = vec4(finish_color(col), 1.0);
            return;
        }
        Deriv k1 = deriv_of(s);
        float h = choose_h(s, k1.x);
        State s1 = s;
        if (!rk4(s1, h, k1)) {
            vec3 col = (uColorMode == 2) ? vec3(0.0) : glow;
            fragColor = vec4(finish_color(col), 1.0);
            return;
        }
        float seg = length(s1.x - s.x);
        traveled += seg;

        if (uColorMode != 2 && uShowDisk == 1 && s.x.z * s1.x.z < 0.0 && traveled > 0.45) {
            float t = s.x.z / (s.x.z - s1.x.z);
            vec3 xh = mix(s.x, s1.x, t);
            vec3 ph = mix(s.p, s1.p, t);
            float rc = length(xh.xy);
            if (rc >= uDiskIn && rc <= uDiskOut) {
                glow += corona(mix(s.x, xh, 0.5), mix(s.p, ph, 0.5), qt, seg * t);
                float g = g_factor(xh, ph, qt);
                vec3 surface = (uColorMode == 1) ? false_g(g, flux_at(rc)) : disk_radiance(rc, g, xh);
                fragColor = vec4(finish_color(surface + glow), 1.0);
                return;
            }
        }

        vec3 pmid = mix(s.p, s1.p, 0.5);
        glow += corona(mix(s.x, s1.x, 0.5), pmid, qt, seg);

        float r1 = length(s1.x);
        minr = min(minr, r1);
        vec3 vel = s1.x - s.x;
        bool outward = dot(s1.x, vel) > 0.0;
        // Outside the photon sphere, an outward null ray reaches infinity.
        bool unbound = outward && r1 > 6.5 && minr + 0.4 < r1 && minr < rcam - 0.5;
        if (unbound || (r1 > escape_r && outward && minr < 0.8 * r1)) {
            if (uColorMode == 2) {
                fragColor = vec4(1.0, 1.0, 1.0, 1.0);
                return;
            }
            State coast = s1;
            vec3 prevx = s.x;
            for (int k = 0; k < 18; ++k) {
                float rc = length(coast.x);
                if (rc > max(rcam * 1.6, 64.0)) break;
                Deriv dk = deriv_of(coast);
                float hk = (0.42 * rc) / max(length(dk.x), 1e-4);
                State nxt = coast;
                if (!rk4(nxt, hk, dk)) break;
                if (uShowDisk == 1 && coast.x.z * nxt.x.z < 0.0) {
                    float t = coast.x.z / (coast.x.z - nxt.x.z);
                    vec3 xh = mix(coast.x, nxt.x, t);
                    vec3 ph = mix(coast.p, nxt.p, t);
                    float rdisk = length(xh.xy);
                    if (rdisk >= uDiskIn && rdisk <= uDiskOut) {
                        float g = g_factor(xh, ph, qt);
                        vec3 surface = (uColorMode == 1) ? false_g(g, flux_at(rdisk)) : disk_radiance(rdisk, g, xh);
                        fragColor = vec4(finish_color(surface + glow), 1.0);
                        return;
                    }
                }
                prevx = coast.x;
                coast = nxt;
            }
            fragColor = vec4(finish_color(sky_color(coast.x - prevx) + glow), 1.0);
            return;
        }
        s = s1;
    }

    // Step budget ran out on the photon-sphere winding. The conserved impact
    // parameter still says whether the ray is above or below the critical curve.
    float bnow = length(cross(s.x, s.p)) / max(s.pt, 1e-5);
    if (uColorMode == 2) {
        fragColor = vec4(bnow > 5.196152 ? vec3(1.0) : vec3(0.0), 1.0);
        return;
    }
    if (bnow <= 5.196152) {
        fragColor = vec4(finish_color(glow), 1.0);
        return;
    }
    State coast = s;
    vec3 prevx = s.x;
    for (int k = 0; k < 18; ++k) {
        if (length(coast.x) > 70.0) break;
        Deriv dk = deriv_of(coast);
        float hk = (0.45 * length(coast.x)) / max(length(dk.x), 1e-4);
        State nxt = coast;
        if (!rk4(nxt, hk, dk)) break;
        prevx = coast.x;
        coast = nxt;
    }
    fragColor = vec4(finish_color(sky_color(coast.x - prevx) + glow), 1.0);
}
