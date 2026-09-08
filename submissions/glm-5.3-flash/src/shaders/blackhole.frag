#version 410 core
precision highp float;
precision highp int;

// ============================================================================
//  Schwarzschild black hole -- backwards ray-traced null geodesics
//
//  Units: G = c = 1, Schwarzschild radius rs = 2M = 1.
//  Photon paths satisfy (exactly, in these units):
//      d2u/dphi2 + u = (3/2) u^2 ,   u = 1/r        [Binet form]
//  which is equivalent to the vector ODE used below:
//      d2x/dl2 = -(3/2) h^2 x / r^5 ,   h = |x x v| (conserved)
//  This reproduces quantitatively:
//    * weak-field deflection  alpha = 2 rs / b
//    * photon sphere at r = 1.5 rs
//    * shadow angular radius  b_crit = (3*sqrt(3)/2) rs ~= 2.598 rs
//    * ISCO at r = 3 rs (disk inner edge)
// ============================================================================

in  vec2 vUV;
out vec4 fragColor;

uniform vec2  uRes;
uniform float uTime;
uniform vec3  uCamPos;
uniform mat3  uCamBasis;      // columns: right, up, forward
uniform float uTanHalfFov;
uniform int   uMaxSteps;
uniform float uStepBase;
uniform float uDiskInner;     // 3.0  = ISCO (6M)
uniform float uDiskOuter;     // ~14 rs
uniform float uTpeak;         // peak disk temperature (K)
uniform float uDiskSpeed;     // time multiplier for orbital motion
uniform int   uGridMode;      // 1 = spacetime grid on equatorial plane
uniform float uExposure;

const float PI = 3.14159265358979;

// ---------------------------------------------------------------- hashing --
float hash12(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash13(vec3 p3){
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1,0,0));
    float n010 = hash13(i + vec3(0,1,0));
    float n110 = hash13(i + vec3(1,1,0));
    float n001 = hash13(i + vec3(0,0,1));
    float n101 = hash13(i + vec3(1,0,1));
    float n011 = hash13(i + vec3(0,1,1));
    float n111 = hash13(i + vec3(1,1,1));
    return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
               mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
}
float fbm3(vec3 p){
    float a = 0.5, s = 0.0;
    for(int i = 0; i < 5; ++i){ s += a * vnoise(p); p = p * 2.02 + 17.13; a *= 0.5; }
    return s;
}

// ------------------------------------------------- blackbody color (K->RGB)--
vec3 blackbody(float T){
    float u = clamp(T, 1000.0, 40000.0) * 0.01;
    vec3 c;
    c.r = (u <= 66.0) ? 1.0 : clamp(1.292936 * pow(u - 60.0, -0.1332047), 0.0, 1.0);
    c.g = (u <= 66.0) ? clamp(0.3900816 * log(u) - 0.6318414, 0.0, 1.0)
                      : clamp(1.1298909 * pow(u - 60.0, -0.0755148), 0.0, 1.0);
    c.b = (u >= 66.0) ? 1.0 : ((u <= 19.0) ? 0.0
                      : clamp(0.5432068 * log(u - 10.0) - 1.19625408, 0.0, 1.0));
    return c;
}

// ------------------------------------------------------------ star field ---
// Procedural, stable in direction -> survives lensing unchanged.
vec3 background(vec3 d){
    vec3 col = vec3(0.0);

    // two layers of hashed point stars on cube-projected cells
    for(int layer = 0; layer < 2; ++layer){
        float scale = (layer == 0) ? 22.0 : 55.0;
        vec3 a = abs(d);
        vec2 uv; float faceId;
        if(a.x >= a.y && a.x >= a.z){ uv = d.zy / a.x;  faceId = d.x > 0.0 ? 0.0 : 1.0; }
        else if(a.y >= a.z)         { uv = d.xz / a.y;  faceId = d.y > 0.0 ? 2.0 : 3.0; }
        else                        { uv = d.xy / a.z;  faceId = d.z > 0.0 ? 4.0 : 5.0; }
        vec2 g = uv * scale;
        vec2 cell = floor(g);
        vec2 f = fract(g);
        for(int j = -1; j <= 1; ++j)
        for(int i = -1; i <= 1; ++i){
            vec2 o = vec2(float(i), float(j));
            vec2 cid = cell + o;
            float h = hash12(cid + faceId * 137.31 + float(layer) * 71.7);
            if(h < 0.06){
                vec2 sp = o + vec2(hash12(cid + 3.7), hash12(cid + 9.1));
                float dist = length(f - sp);
                float mag = hash12(cid + 55.1);
                float bright = 0.15 + 1.4 * mag * mag * mag;
                float size = mix(0.055, 0.028, float(layer));
                float star = bright * exp(-dist * dist / (size * size));
                float tStar = mix(3200.0, 12000.0, hash12(cid + 21.37));
                col += star * blackbody(tStar);
            }
        }
    }

    // faint galactic band (tilted plane through origin)
    vec3 bn = normalize(vec3(0.32, 0.88, -0.35));
    float bandDist = dot(d, bn);
    float band = exp(-bandDist * bandDist * 18.0);
    float neb = fbm3(d * 3.1 + 7.0) * fbm3(d * 7.7 - 3.0);
    vec3 bandCol = mix(vec3(0.30, 0.38, 0.62), vec3(0.62, 0.48, 0.34),
                       fbm3(d * 2.2 + 31.0));
    col += band * bandCol * neb * 0.55;

    // very faint deep-space tint so shadow reads against black
    col += vec3(0.004, 0.005, 0.009);
    return col;
}

// ------------------------------------------------------- accretion disk ----
// Novikov-Thorne shaped temperature profile T(r) ~ (r_in/r)^{3/4} (1-sqrt(r_in/r))^{1/4}
// Keplerian speed measured by a local static observer:
//     beta(r) = sqrt( M / (r - 2M) )  ->  sqrt(1/(2(r-1))) in rs=1 units
// (= 0.5 c at the ISCO). Observed/emitted frequency ratio:
//     g = delta_doppler * sqrt(1 - rs/r)
// Bolometric intensity transforms as I_obs = g^4 I_emit ; color from
// blackbody(g * T_emit).
vec3 diskShade(vec3 xp, vec3 vd, out float alpha){
    float r = length(xp.xz);

    // relativistic Doppler factor
    float beta = sqrt(0.5 / max(r - 1.0, 0.10));
    vec3 orbitDir = normalize(vec3(-xp.z, 0.0, xp.x));       // prograde (+y)
    vec3 toObs = -vd;                                        // photon travel dir reversed
    float cosA = clamp(dot(orbitDir, toObs), -1.0, 1.0);
    float gamma_ = inversesqrt(max(1.0 - beta * beta, 1e-4));
    float dopp = 1.0 / (gamma_ * (1.0 - beta * cosA));
    float grav = sqrt(max(1.0 - 1.0 / r, 0.0));              // gravitational redshift
    float g = dopp * grav;

    // temperature profile, normalized so T(r_peak) = uTpeak
    float xin = uDiskInner / r;
    float prof = pow(xin, 0.75) * pow(max(1.0 - sqrt(xin * 0.985), 0.0), 0.25);
    float Temit = uTpeak * prof * 1.4527;                    // 1/peak(prof)

    // differentially rotating turbulence (Keplerian shear, periodic in phi)
    float phi = atan(xp.z, xp.x);
    float omega = sqrt(0.5 / (r * r * r));                   // coordinate Omega(r)
    float ang = phi - omega * uTime * uDiskSpeed;
    vec3 q = vec3(cos(ang), sin(ang), 0.0);
    float turb = fbm3(q * 2.2 + vec3(0.0, 0.0, log(r) * 14.0));
    turb = 0.5 * turb + 0.5 * fbm3(q * 5.0 + vec3(log(r) * 28.0, 4.7, 0.0));

    float emis = (0.45 + 1.75 * turb) * xin * xin * 1.25;     // emissivity ~ F(r)
    vec3 c = blackbody(g * Temit);
    float I = emis * pow(g, 4.0);                            // relativistic beaming

    float fadeIn  = smoothstep(uDiskInner, uDiskInner * 1.06, r);
    float fadeOut = 1.0 - smoothstep(uDiskOuter * 0.70, uDiskOuter, r);
    float wisp = smoothstep(0.25, 0.75, turb + fadeOut * 0.5);

    alpha = clamp(0.92 * fadeIn * mix(wisp, 1.0, fadeIn * 0.8), 0.0, 1.0);
    return c * I * fadeIn * fadeOut;
}

// --------------------------------------------- lensed spacetime grid -------
// Polar grid on the equatorial plane. The raytracer bends it around the hole,
// so rings pile up near the photon sphere and wrap over/under the shadow.
vec3 gridShade(vec3 xp, out float alpha){
    float r = length(xp.xz);
    const float S = 1.0;                                     // ring spacing (rs)
    float ringCoord = r / S;
    vec2 gR = vec2(abs(fract(ringCoord) - 0.5), fwidth(ringCoord));
    float ring = 1.0 - smoothstep(0.0, gR.y * 1.6, gR.x);    // AA radial lines

    // 8 spokes via distance to 4 axis-aligned/diagonal line families
    vec2 xz = xp.xz;
    vec2 ax = abs(xz);
    float dLine = min(min(ax.x, ax.y), min(abs(xz.x + xz.y), abs(xz.x - xz.y)) * 0.70710678);
    float dlw = fwidth(dLine) * 1.6 + 1e-4;
    float spoke = 1.0 - smoothstep(0.0, dlw, dLine - 0.006);

    float atten = 1.0 / (1.0 + gR.y * 7.0 + dlw * 30.0);   // fade sub-pixel lines (anti-moire)
    float lines = max(ring, spoke * step(1.2, r)) * atten;
    float fade = exp(-r * 0.09) * smoothstep(0.85, 1.6, r);
    vec3 col = vec3(0.20, 0.95, 0.85) * lines * fade * 2.2;
    col += vec3(0.9, 0.45, 0.15) * ring * smoothstep(2.9, 3.0, r) *
           (1.0 - smoothstep(3.0, 3.12, r)) * fade * 1.5;    // ISCO marker ring
    alpha = clamp(lines * fade * 0.85, 0.0, 1.0);
    return col;
}

// ------------------------------------------------------------ geodesic -----
vec3 traceRay(vec3 ro, vec3 rd, float seed){
    vec3 p = ro;
    vec3 v = normalize(rd);
    p += v * uStepBase * seed;                              // jitter: decorrelate banding
    vec3 Lv = cross(p, v);
    float h2 = dot(Lv, Lv);                 // conserved along exact solution

    vec3 col = vec3(0.0);
    float T = 1.0;                          // transmittance
    bool hitHorizon = false;

    // halo parameters (thin atmosphere around the disk + photon-sphere shimmer)
    const float Hh = 0.25;
    const float sigS = 0.020;
    const float sigE = 0.045;

    for(int i = 0; i < 700; ++i){
        if(i >= uMaxSteps) break;
        float r2 = dot(p, p);
        float r  = sqrt(r2);

        if(r < 1.0){ hitHorizon = true; break; }             // event horizon
        if(r > 60.0 && dot(p, v) > 0.0) break;               // escaped

        float dt = uStepBase * clamp(0.35 * (r - 0.9), 0.045, 4.0);

        // velocity-Verlet on  d2x/dl2 = -1.5 h2 x / r^5
        vec3 a1 = -1.5 * h2 * p / (r2 * r2 * r);
        vec3 pn = p + v * dt + 0.5 * a1 * dt * dt;
        float rn2 = dot(pn, pn);
        float rn  = sqrt(rn2);
        vec3 a2 = -1.5 * h2 * pn / (rn2 * rn2 * rn);
        vec3 vn = v + 0.5 * (a1 + a2) * dt;

        // ---- equatorial-plane crossing -> disk / grid ----
        if(p.y * pn.y < 0.0){
            float f = p.y / (p.y - pn.y);
            vec3 xp = mix(p, pn, f);
            vec3 vd = normalize(mix(v, vn, f));
            float rr = length(xp.xz);
            if(rr > uDiskInner && rr < uDiskOuter){
                if(uGridMode == 1){
                    float ga;
                    vec3 gc = gridShade(xp, ga);
                    col += T * gc;
                    T *= (1.0 - ga * 0.55);
                } else {
                    float da;
                    vec3 dc = diskShade(xp, vd, da);
                    col += T * dc;
                    T *= (1.0 - da);
                }
            }
        }

        // ---- volumetric halos ----
        float hr = length(p.xz);
        float band = exp(-p.y * p.y / (2.0 * Hh * Hh))
                   * smoothstep(uDiskInner * 0.75, uDiskInner + 0.8, hr)
                   * (1.0 - smoothstep(uDiskOuter * 0.72, uDiskOuter, hr));
        vec3 hazeCol = (uGridMode == 1) ? vec3(0.15, 0.55, 0.55)
                                        : blackbody(uTpeak * 0.55);
        col += T * hazeCol * band * dt * sigS * 0.5;        T *= exp(-band * dt * sigE);

        float psGlow = exp(-(r - 1.5) * (r - 1.5) * 3.0);
        col += T * vec3(1.0, 0.72, 0.45) * psGlow * dt * 0.010;

        p = pn; v = vn;
        if(T < 0.004) break;
    }

    if(!hitHorizon && T > 0.004){
        col += T * background(normalize(v));
    }
    return col;
}

void main(){
    vec2 uv = (vUV * uRes - 0.5 * uRes) / uRes.y;
    vec3 rd = normalize(uCamBasis * vec3(uv * uTanHalfFov, 1.0));

    vec3 c = traceRay(uCamPos, rd, hash12(gl_FragCoord.xy + fract(uTime) * 61.7)) * uExposure;
    fragColor = vec4(c, 1.0);
}
