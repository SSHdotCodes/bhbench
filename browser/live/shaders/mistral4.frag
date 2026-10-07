#version 300 es
precision highp float;
precision highp int;
precision highp float;

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uRes;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform float uTime;
uniform float uRs;
uniform float uDiskRin;
uniform float uDiskRout;
uniform float uDiskBrightness;
uniform float uHaloStrength;
uniform int   uDebug;   // 1 = false-color hit map

// ---------- hashing / noise ----------
float hash13(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 31.32);
    return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p){
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}
float vnoise(vec3 p){
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm(vec3 p){
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++){
        v += a * vnoise(p);
        p = p * 2.03 + vec3(11.7, 5.1, 17.3);
        a *= 0.5;
    }
    return v;
}

// ---------- blackbody color (Tanner Helland approximation) ----------
vec3 blackbody(float T){
    float t = clamp(T, 1000.0, 60000.0) * 0.01;
    float R = (t <= 66.0) ? 255.0 : 329.698727446 * pow(t - 60.0, -0.1332047592);
    float G = (t <= 66.0) ? 99.4708025861 * log(t) - 161.1195681661
                          : 288.1221695283 * pow(t - 60.0, -0.0755148492);
    float B = (t >= 66.0) ? 255.0 : (t <= 19.0 ? 0.0 : 138.5177312231 * log(t - 10.0) - 305.0447927307);
    return clamp(vec3(R, G, B) * (1.0 / 255.0), 0.0, 1.0);
}

// ---------- procedural background: stars + milky way ----------
vec3 background(vec3 d){
    vec3 col = vec3(0.010, 0.012, 0.018);
    vec3 mwN = normalize(vec3(0.55, 0.60, 0.58));
    float s = abs(dot(d, mwN));
    float band = smoothstep(0.14, 0.015, s) * 0.6;
    if (band > 0.001){
        float n = fbm(d * 22.0 + vec3(3.1, 8.2, 1.7));
        float dust = fbm(d * 47.0 - vec3(7.7, 2.2, 4.4));
        col += vec3(0.80, 0.86, 1.0) * band * (0.10 + 0.45 * n) * (0.55 + 0.45 * dust) * 0.15;
        vec3 core = normalize(vec3(0.2, 0.05, -0.95));
        col += vec3(1.0, 0.82, 0.55) * 0.15 * exp(-dot(d - core, d - core) * 16.0) * band;
    }
    for (int layer = 0; layer < 2; layer++){
        float scale = (layer == 0) ? 90.0 : 200.0;
        vec3 q = d * scale;
        vec3 cell = floor(q);
        vec3 rnd = hash33(cell);
        if (rnd.z > 0.982){
            vec3 sp = cell + hash33(cell + vec3(17.17, 9.31, 3.73));
            float dist = length(q - sp);
            float sz = ((layer == 0) ? 0.10 : 0.06) + 0.28 * rnd.x;
            float inten = 0.35 + 1.1 * rnd.y;
            if (rnd.z > 0.9965) inten *= 3.5;
            vec3 sc = mix(vec3(1.0, 0.85, 0.70), vec3(0.72, 0.84, 1.0), rnd.y);
            col += sc * inten * smoothstep(sz, 0.0, dist);
            col += sc * inten * 0.25 * smoothstep(sz * 3.5, 0.0, dist);
        }
    }
    return col;
}

// ---------- accretion disk emission ----------
vec3 diskEmission(float r, float phi, float D){
    // T ~ r^-3/4 thin-disk law; normalization set by accretion rate (luminous disk)
    float T = 9000.0 * pow(uDiskRin / r, 0.75) * D;   // doppler-shifted observed temperature
    vec3 bb = blackbody(T);
    float n = fbm(vec3(r * 2.5, phi * 6.0, uTime * 0.20));
    float streak = fbm(vec3(phi * 20.0, r * 0.8, uTime * 0.10 + 5.0));
    float flux = pow(uDiskRin / r, 2.5);              // thin-disk brightness falloff ~ r^-3 (softened for visibility)
    vec3 c = bb * (0.70 + 0.50 * n) * (0.70 + 0.60 * streak);
    return c * flux * pow(clamp(D, 0.05, 4.0), 3.0) * uDiskBrightness;
}

vec3 haloColor(float glow){
    return vec3(1.0, 0.62, 0.35) * glow * uHaloStrength * 0.7;
}

// ---------- exact Schwarzschild null geodesic ----------
// state = (r, phi, r', phi'); E = 1, conserved L = r^2 phi'
vec4 geodesicDeriv(float r, float phi, float vr, float vp, float rs, float L){
    float rr = max(r, rs * 1.0002);
    float f = 1.0 - rs / rr;
    float ar = rs * (vr * vr - 1.0) / (2.0 * rr * rr * f) + f * L * L / (rr * rr * rr);
    float ap = -2.0 * vr * vp / rr;
    return vec4(vr, vp, ar, ap);
}

// hit: 0 = escaped to background, 1 = captured by horizon, 2 = accretion disk, 3 = step budget
vec3 trace(vec3 ro, vec3 rd, out int hit){
    float rs = uRs;

    // every geodesic lies in the plane through the origin with normal ro x rd
    vec3 n = cross(ro, rd);
    float nl = length(n);
    if (nl < 1e-6){
        if (dot(rd, ro) < 0.0){ hit = 1; return vec3(0.0); }
        hit = 0; return background(rd);
    }
    n = n / nl;
    vec3 e1 = ro / length(ro);
    vec3 e2 = cross(n, e1);
    float e1y = e1.y;
    float e2y = e2.y;

    float r = length(ro);
    float f0 = 1.0 - rs / r;

    // initial tangent, normalized so E = f dt/dlambda = 1 (null condition)
    float vr = dot(e1, rd);
    float vp = dot(e2, rd) / r;
    float s = 1.0 / sqrt(vr * vr + f0 * vp * vp * r * r);
    vr *= s;
    vp *= s;
    float L = r * r * vp;
    float phi = 0.0;

    float glow = 0.0;
    float minR = r;
    float yPrev = r * e1y;

    for (int i = 0; i < 2200; i++){
        float h = clamp(0.010 + 0.0055 * r, 0.014, 0.11);

        vec4 k1 = geodesicDeriv(r, phi, vr, vp, rs, L);
        vec4 k2 = geodesicDeriv(r + 0.5*h*k1.x, phi + 0.5*h*k1.y, vr + 0.5*h*k1.z, vp + 0.5*h*k1.w, rs, L);
        vec4 k3 = geodesicDeriv(r + 0.5*h*k2.x, phi + 0.5*h*k2.y, vr + 0.5*h*k2.z, vp + 0.5*h*k2.w, rs, L);
        vec4 k4 = geodesicDeriv(r + h*k3.x, phi + h*k3.y, vr + h*k3.z, vp + h*k3.w, rs, L);

        r   += h * (k1.x + 2.0*k2.x + 2.0*k3.x + k4.x) * (1.0 / 6.0);
        phi += h * (k1.y + 2.0*k2.y + 2.0*k3.y + k4.y) * (1.0 / 6.0);
        vr  += h * (k1.z + 2.0*k2.z + 2.0*k3.z + k4.z) * (1.0 / 6.0);
        vp  += h * (k1.w + 2.0*k2.w + 2.0*k3.w + k4.w) * (1.0 / 6.0);

        float y = r * (cos(phi) * e1y + sin(phi) * e2y);
        minR = min(minR, r);

        // accretion disk crossing (thin disk in the equatorial plane y = 0)
        if (y * yPrev < 0.0 && r > uDiskRin && r < uDiskRout){
            vec3 rhat = cos(phi) * e1 + sin(phi) * e2;
            vec3 phat = -sin(phi) * e1 + cos(phi) * e2;
            vec3 u = vr * rhat + (r * vp) * phat;                    // photon spatial velocity
            vec3 k = normalize(u);
            vec3 that = normalize(cross(vec3(0.0, 1.0, 0.0), rhat)); // disk rotation direction
            float v = sqrt(rs / (2.0 * r));                          // Keplerian speed (c = 1, M = rs/2)
            float bk = v * dot(that, k);
            float gamma = 1.0 / sqrt(1.0 - v * v);
            float D = 1.0 / (gamma * (1.0 - bk));                    // relativistic Doppler factor
            hit = 2;
            return diskEmission(r, phi, D) + haloColor(glow);
        }

        // volumetric halo above/below the disk
        if (r > uDiskRin - 1.0 && r < uDiskRout + 2.0){
            float H = 0.05 * r + 0.03;
            float yh = y / H;
            glow += exp(-yh * yh) * h * (0.02 * pow(uDiskRin / max(r, uDiskRin), 1.5));
        }

        yPrev = y;

        if (r < rs * 1.0004){                                        // captured by the horizon
            hit = 1;
            return haloColor(glow);
        }
        if (vr > 0.0 && r > 24.0){                                  // escaped to infinity
            vec3 rhat = cos(phi) * e1 + sin(phi) * e2;
            vec3 phat = -sin(phi) * e1 + cos(phi) * e2;
            vec3 dir = normalize(vr * rhat + (r * vp) * phat);
            hit = 0;
            return background(dir) + haloColor(glow);
        }
    }

    // still orbiting after the step budget: faint photon-ring glow
    hit = 3;
    float ring = (minR < 2.0) ? 0.30 * exp(-(minR - 1.5) * 7.0) : 0.0;
    return haloColor(glow) + vec3(1.0, 0.55, 0.20) * ring;
}

vec3 aces(vec3 x){
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main(){
    vec2 vUV = vUV * 2.0 - 1.0;
    vUV.x *= uRes.x / uRes.y;
    vec3 rd = normalize(uCamFwd + uTanHalfFov * (vUV.x * uCamRight + vUV.y * uCamUp));
    if (uDebug == 1){
        int hit;
        trace(uCamPos, rd, hit);
        vec3 c = (hit == 0) ? vec3(0.10, 0.20, 0.60)   // escaped  -> blue
               : (hit == 1) ? vec3(0.60, 0.05, 0.05)   // captured -> red
               : (hit == 2) ? vec3(0.10, 0.70, 0.20)   // disk     -> green
                            : vec3(0.85, 0.70, 0.10);  // orbiting -> yellow
        fragColor = vec4(c, 1.0);
        return;
    }
    int hit;
    vec3 col = trace(uCamPos, rd, hit);
    col = aces(col);
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
