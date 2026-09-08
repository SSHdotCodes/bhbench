#version 410 core
// Schwarzschild black-hole ray tracer (gravitational lensing + accretion disk).
//
// Units / conventions (must match src/physics.h):
//   Rs = uRs (=1). Horizon r=Rs, photon sphere 1.5*Rs, ISCO 3*Rs.
//   Disk: thin equatorial plane y=0, r in [3*Rs, 9*Rs].
//   Geodesic spatial track: h=r x v, a = -1.5*Rs*|h|^2 * r_vec / r^5.
//     (3-D form of d^2u/dphi^2 + u = 3M u^2, M = Rs/2.)
// Backward tracing: ray leaves the camera; physical photons travel opposite.
// Redshift: g = sqrt(1-Rs/r_e) / (gamma*(1 - dot(betaVec, p_hat))),
//   p_hat = direction of the physical photon at emission (toward camera,
//   i.e. -rayVel), betaVec = emitter Keplerian velocity. I_obs ~ g^3 I_emit.
in vec2 vUV;
out vec4 fragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uFovDeg;
uniform float uRs;
uniform int   uSteps;
uniform float uStepScale;
uniform int   uBending;    // 1 = GR null geodesics, 0 = straight lines
uniform int   uDoppler;    // 1 = beaming + gravitational redshift
uniform int   uDisk;       // 1 = accretion disk on
uniform float uExposure;
uniform float uBgGain;

// ---- hashing / noise -------------------------------------------------------
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1,0,0));
    float n010 = hash13(i + vec3(0,1,0));
    float n110 = hash13(i + vec3(1,1,0));
    float n001 = hash13(i + vec3(0,0,1));
    float n101 = hash13(i + vec3(1,0,1));
    float n011 = hash13(i + vec3(0,1,1));
    float n111 = hash13(i + vec3(1,1,1));
    return mix(mix(mix(n000,n100,u.x), mix(n010,n110,u.x), u.y),
               mix(mix(n001,n101,u.x), mix(n011,n111,u.x), u.y), u.z);
}
float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p *= 2.03; a *= 0.5; }
    return s;
}

// ---- blackbody-ish palette -------------------------------------------------
// t in [0,1]: cold rim -> orange -> white-hot inner edge.
vec3 blackbody(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c1 = vec3(0.35, 0.02, 0.0);   // dim red rim
    vec3 c2 = vec3(1.00, 0.36, 0.05);  // orange
    vec3 c3 = vec3(1.00, 0.80, 0.55);  // warm white
    vec3 c4 = vec3(0.75, 0.86, 1.00);  // blue-white inner rim
    vec3 col = mix(c1, c2, smoothstep(0.0, 0.38, t));
    col = mix(col, c3, smoothstep(0.38, 0.72, t));
    col = mix(col, c4, smoothstep(0.72, 1.0, t));
    return col;
}

// ---- background sky (source at infinity) -----------------------------------
// Structured on several scales so lensing (Einstein ring, weak shear) is
// visible everywhere: faint gradient + tilted Milky-Way band + two star
// layers + a few colored marker galaxies/stars.
vec3 background(vec3 d) {
    d = normalize(d);
    // base gradient
    vec3 col = mix(vec3(0.012, 0.014, 0.028), vec3(0.004, 0.005, 0.012),
                   0.5 + 0.5 * d.y);
    // Milky-Way band around a tilted great circle
    vec3 bandN = normalize(vec3(0.25, 1.0, 0.18));
    float band = exp(-pow(abs(dot(d, bandN)) * 3.4, 2.0));
    float clouds = fbm(d * 6.0 + vec3(11.0));
    float lanes  = fbm(d * 13.0 + vec3(4.7));
    col += vec3(0.55, 0.58, 0.66) * band * (0.10 + 0.55 * clouds) * (1.0 - 0.55 * lanes);
    col += vec3(0.30, 0.22, 0.16) * band * lanes * 0.10;

    // star layers: 3-D cell point stars (direction * scale trick)
    for (int layer = 0; layer < 2; ++layer) {
        float scale = (layer == 0) ? 90.0 : 220.0;
        vec3 p = d * scale;
        vec3 cell = floor(p);
        vec3 rnd = hash33(cell);
        vec3 starPos = vec3(rnd.xy - 0.5, rnd.z - 0.5);
        vec3 rel = fract(p) - 0.5 - starPos * 0.7;
        float m2 = dot(rel, rel);
        float bright = pow(hash11(dot(cell, vec3(12.9898, 78.233, 37.719)) + float(layer) * 7.0), 18.0);
        float star = bright * smoothstep(0.09, 0.0, m2);
        float tint = hash11(dot(cell, vec3(3.1, 7.7, 5.3)));
        vec3 starCol = mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.85, 0.7), tint);
        if (layer == 1) star *= 0.55;
        col += starCol * star * 3.2;
    }
    // a few bright reference stars so image doubling is obvious
    vec3 s1 = normalize(vec3(0.7, 0.25, -0.6));
    vec3 s2 = normalize(vec3(-0.8, -0.15, 0.5));
    col += vec3(1.0, 0.95, 0.9) * pow(max(dot(d, s1), 0.0), 4000.0) * 6.0;
    col += vec3(0.8, 0.88, 1.0) * pow(max(dot(d, s2), 0.0), 9000.0) * 8.0;
    return col * uBgGain;
}

// ---- accretion-disk emission at hit point ----------------------------------
vec3 diskEmission(vec3 hit, vec3 rayVel, float rs, out float alphaOut) {
    float r = length(hit.xz);
    float rIn = 3.0 * rs, rOut = 9.0 * rs;
    alphaOut = 0.0;
    if (r < rIn || r > rOut) return vec3(0.0);

    // Novikov-Thorne shape F ~ r^-3 (1 - sqrt(rIn/r)); T ~ F^1/4
    float shape = (1.0 / pow(r / rs, 3.0)) * (1.0 - sqrt(rIn / max(r, 1e-4)));
    shape = max(shape, 0.0) / 0.00507;      // normalize peak to ~1
    float temp = pow(clamp(shape, 0.0, 1.5), 0.25);

    // differential rotation advects the turbulence pattern
    float Om = sqrt(0.5 * rs / pow(max(r, 1e-3), 3.0)); // sqrt(M/r^3), M=rs/2
    float phi = atan(hit.z, hit.x);
    float phiRot = phi - Om * uTime * 3.0;
    vec3 pat = vec3(cos(phiRot), 0.35 * sin(phiRot * 2.0), sin(phiRot)) * r * 1.4;
    float turb = fbm(pat * 2.2 + vec3(0.0, uTime * 0.15, 0.0));
    float streak = fbm(vec3(phiRot * 3.0, r * 2.5 - uTime * 0.25, 0.0));
    float mod = 0.62 + 0.75 * turb + 0.25 * streak;

    vec3 emit = blackbody(temp) * (0.25 + 2.6 * temp) * mod;

    // inner-edge hot rim
    float rim = exp(-max(r - rIn, 0.0) * 2.2 / rs);
    emit += vec3(0.9, 0.95, 1.0) * rim * 0.55;

    // relativistic redshift / beaming
    float g = 1.0;
    if (uDoppler == 1) {
        // Keplerian velocity, prograde about +y
        vec3 tang = normalize(vec3(-hit.z, 0.0, hit.x) + vec3(1e-6));
        float v = sqrt((0.5 * rs) / max(r - rs, 0.05 * rs));
        v = min(v, 0.85);
        float gamma = inversesqrt(max(1.0 - v * v, 1e-3));
        vec3 betaVec = tang * v;
        vec3 pHat = normalize(-rayVel);      // physical photon flight dir
        float grav = sqrt(max(1.0 - rs / max(r, rs * 1.001), 1e-4));
        g = grav / (gamma * max(1.0 - dot(betaVec, pHat), 0.08));
        emit *= g * g * g;                   // I_nu/nu^3 invariant
        emit = mix(emit, blackbody(clamp(temp * g * 1.15, 0.0, 1.0))
                         * (0.25 + 2.6 * temp * g) * mod, 0.55);
    }
    alphaOut = 0.93;
    return emit;
}

void main() {
    vec2 px = (2.0 * gl_FragCoord.xy - uResolution) / uResolution.y;
    float f = tan(radians(uFovDeg) * 0.5);
    vec3 rayDir = normalize(uCamFwd + px.x * f * uCamRight + px.y * f * uCamUp);

    float rs = uRs;
    vec3 pos = uCamPos;
    vec3 vel = rayDir;
    vec3 prev = pos;

    vec3 col = vec3(0.0);
    float alpha = 0.0;
    bool captured = false;
    bool escaped = false;
    vec3 escDir = rayDir;

    int maxSteps = 320;
    int n = (uSteps < maxSteps) ? uSteps : maxSteps;
    for (int i = 0; i < 320; ++i) {
        if (i >= n) break;
        if (alpha > 0.995) break;
        float r = length(pos);

        if (r < rs) { captured = true; break; }
        if (r > 40.0 && dot(pos, vel) > 0.0) { escaped = true; escDir = normalize(vel); break; }

        // thin-disk crossing between prev and pos
        if (uDisk == 1 && prev.y * pos.y < 0.0) {
            float t = prev.y / (prev.y - pos.y);
            vec3 hit = mix(prev, pos, t);
            float hr = length(hit);
            if (hr > 3.0 * rs * 0.995 && hr < 9.0 * rs) {
                float a = 0.0;
                vec3 e = diskEmission(hit, vel, rs, a);
                col += (1.0 - alpha) * e;
                alpha += (1.0 - alpha) * a;
            }
        }

        // GR bend (skipped when user selects straight-line comparison)
        if (uBending == 1) {
            vec3 h = cross(pos, vel);
            float h2 = dot(h, h);
            float r2 = max(dot(pos, pos), 1e-6);
            float rlen = sqrt(r2);
            vec3 acc = (-1.5 * rs * h2 / (r2 * r2 * rlen)) * pos;
            float dt = clamp((rlen - rs * 0.92) * 0.35, 0.02, 0.55) * uStepScale;
            vel += acc * dt;
            prev = pos;
            pos += vel * dt;
        } else {
            float rlen = max(length(pos), 1e-3);
            float dt = 0.35 * uStepScale;
            prev = pos;
            pos += vel * dt;
            if (rlen > 60.0) { escaped = true; escDir = normalize(vel); break; }
        }
    }

    vec3 outCol;
    if (captured) {
        outCol = col; // shadow: only foreground disk light survives
    } else {
        vec3 bg = background(escaped ? escDir : normalize(vel));
        outCol = col + (1.0 - alpha) * bg;
    }

    // filmic-ish tonemap + gamma
    outCol *= uExposure;
    outCol = outCol / (outCol + vec3(0.55));
    outCol = pow(max(outCol, vec3(0.0)), vec3(1.0 / 2.2));
    // subtle vignette
    vec2 q = gl_FragCoord.xy / uResolution - 0.5;
    outCol *= 1.0 - 0.35 * dot(q, q);
    fragColor = vec4(outCol, 1.0);
}
