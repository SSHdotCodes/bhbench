#version 300 es
precision highp float;
precision highp int;
precision highp float;
out vec4 fragColor;

uniform vec2  uRes;            // viewport resolution (px) for this pass
uniform float uTime;           // animation time (s)
uniform vec3  uCamPos, uCamRight, uCamUp, uCamFwd;  // camera frame
uniform float uTanHalfFov;
uniform float uRs;             // Schwarzschild radius (= 1)
uniform float uEscapeR;        // radius beyond which an outward ray is free
uniform int   uSteps;          // max RK4 iterations (quality knob)
uniform int   uDiskOn, uLensingOn, uRelOn, uStarsOn, uHaloOn;
uniform float uDiskInner, uDiskOuter;   // ISCO (3 rs) .. outer radius
uniform float uDiskBright;
uniform float uExposure;
uniform float uTpeak;          // peak effective disk temperature (display mapping)
uniform float uCorona;         // optically-thin halo strength
uniform float uCoronaThick;
uniform float uSpinSign;       // +1 / -1 disk rotation sense
uniform vec3  uAxis;           // disk / spin axis (0,1,0)

const float PI = 3.141592653589793;

// --------------------------------------------------------------------------
//  Hash / value noise (procedural starfield, no textures)
// --------------------------------------------------------------------------
float hash13(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p){
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// --------------------------------------------------------------------------
//  Background sky as a function of the *asymptotic outgoing direction*.
//  Because we sample with the bent direction, gravitational lensing distorts
//  the starfield and builds the Einstein ring automatically.
// --------------------------------------------------------------------------
vec3 starfield(vec3 d){
    d = normalize(d);
    vec3 col = vec3(0.0);

    // very faint, smoothly structured galactic band (no blocky hash noise)
    vec3 gp = normalize(vec3(0.35, 1.0, 0.18));
    float band = exp(-pow(dot(d, gp) * 3.0, 2.0));
    float neb = 0.5 + 0.5 * sin(d.x*13.0 + d.z*7.0) * sin(d.y*11.0 - d.x*5.0);
    col += vec3(0.010, 0.014, 0.028) * band * (0.55 + 0.45 * neb);

    // two star layers with different densities / sizes
    for (int layer = 0; layer < 2; ++layer){
        float scale  = (layer == 0) ? 110.0 : 260.0;
        float thresh = (layer == 0) ? 0.986  : 0.990;
        vec3 q  = d * scale;
        vec3 id = floor(q);
        vec3 f  = fract(q);
        vec3 rnd = hash33(id + float(layer) * 19.0);
        float present = step(thresh, hash13(id + 3.7 + float(layer) * 13.0));
        vec3  dv    = f - (0.2 + 0.6 * rnd);
        float dist2 = dot(dv, dv);
        float sig2  = (layer == 0) ? 0.020 : 0.012;
        float s = present * exp(-dist2 / (2.0 * sig2));
        float ct = rnd.x;                       // colour temperature
        vec3  sc = mix(vec3(1.0, 0.70, 0.42), vec3(0.66, 0.79, 1.0), ct);
        sc = mix(sc, vec3(1.0), 0.25);
        float mag = 0.6 + 2.2 * pow(hash13(id + 9.1), 3.0);
        col += s * sc * mag;
    }
    // faint uniform intergalactic glow so the shadow edge is not pure black
    col += vec3(0.004, 0.005, 0.010);
    return col;
}

// --------------------------------------------------------------------------
//  Blackbody colour: Planckian-locus chromaticity (CIE 1931 xy approximations)
//  converted to LINEAR sRGB and normalized to unit luminance. The caller then
//  multiplies by (T/Tref)^4  (Stefan-Boltzmann) to get a physical radiance.
// --------------------------------------------------------------------------
vec3 blackbodyChroma(float T){
    T = clamp(T, 1000.0, 25000.0);
    float t2 = T*T, t3 = t2*T;
    float x;
    if (T < 4000.0)
        x = -0.2661239e9/t3 - 0.2343589e6/t2 + 0.8776956e3/T + 0.179910;
    else
        x = -3.0258469e9/t3 + 2.1070379e6/t2 + 0.2226347e3/T + 0.240390;
    float x2 = x*x, x3 = x2*x;
    float y;
    if (T < 2222.0)
        y = -1.1063814*x3 - 1.34811020*x2 + 2.18555832*x - 0.20219683;
    else if (T < 4000.0)
        y = -0.9549476*x3 - 1.37418593*x2 + 2.09137015*x - 0.16748867;
    else
        y =  3.0817580*x3 - 5.87338670*x2 + 3.75112997*x - 0.37001483;
    y = clamp(y, 0.05, 0.7);
    float Y = 1.0;
    float X = x * Y / y;
    float Z = (1.0 - x - y) * Y / y;
    vec3 rgb = vec3(
         3.2406*X - 1.5372*Y - 0.4986*Z,
        -0.9689*X + 1.8758*Y + 0.0415*Z,
         0.0557*X - 0.2040*Y + 1.0570*Z);
    rgb = max(rgb, vec3(0.0));
    float lum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    return rgb / max(lum, 1e-4);           // unit-luminance chromaticity
}

// ACES filmic tone-mapping (input is LINEAR HDR radiance)
vec3 aces(vec3 x){
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a*x + b)) / (x * (c*x + d) + e), 0.0, 1.0);
}

// --------------------------------------------------------------------------
//  Exact central-force acceleration for a Schwarzschild null geodesic,
//  d^2 r/dlambda^2 = -1.5 rs h^2 r / r^5   (h = |r x v| conserved).
//  uLensingOn = 0 -> straight rays (Newtonian comparison mode).
// --------------------------------------------------------------------------
vec3 accelAt(vec3 p, float h2){
    float r2 = dot(p, p);
    float r  = sqrt(r2);
    float coeff = -1.5 * uRs * h2 / (r2 * r2 * r);
    return p * coeff * float(uLensingOn);
}

// --------------------------------------------------------------------------
//  Disk emission. Returns LINEAR HDR radiance for an opaque, geometrically
//  thin, optically thick Novikov-Thorne / Shakura-Sunyaev disk.
// --------------------------------------------------------------------------
vec3 diskRadiance(vec3 hit, vec3 rayVel){
    float r = length(hit);
    vec3 er = hit / r;                                  // radial (equatorial plane)

    // Local orbital velocity of a circular geodesic measured by a STATIC observer
    // at r:  v = sqrt(M/(r - 2M)) = sqrt(rs / (2 (r - rs)))  with rs = 2M.
    float v = clamp(sqrt(0.5 * uRs / max(r - uRs, 1e-3)), 0.0, 0.985);
    float gamma = 1.0 / sqrt(1.0 - v*v);
    vec3  ephi  = uSpinSign * normalize(cross(uAxis, er));   // prograde tangential
    float gGrav = sqrt(max(1.0 - uRs / r, 1e-4));            // gravitational redshift

    // Forward photon velocity (disk -> observer) = - (backward) ray velocity.
    // Convert coordinate velocity to the LOCAL STATIC tetrad: radial physical
    // component = f_r / sqrt(1-rs/r), tangential physical component unchanged.
    vec3  f  = -normalize(rayVel);
    float fr = dot(f, er);
    vec3  ft = f - fr * er;
    vec3  k  = (fr / gGrav) * er + ft;                       // local photon direction
    vec3  khat = normalize(k);
    float betaDotK = v * dot(khat, ephi);

    // Relativistic Doppler factor and total redshift g = nu_obs/nu_emit.
    // g = sqrt(1-rs/r) / (gamma (1 - beta.k))  (exact for Schwarzschild).
    float doppler = 1.0 / (gamma * (1.0 - betaDotK));
    float g = (uRelOn == 1) ? (gGrav * doppler) : 1.0;

    // Thin-disk temperature profile: flux F ~ (1/r^3)(1 - sqrt(r_in/r)),
    // T ~ F^{1/4}. Normalized so the peak (r ~ 1.36 r_in) equals Tpeak.
    float x = r / uDiskInner;
    float flux  = (1.0 / (x*x*x)) * max(1.0 - 1.0/sqrt(x), 0.0);
    float fluxN = max(flux / 0.0566, 1e-9);
    float Temit = uTpeak * pow(fluxN, 0.25);
    float Tobs  = g * Temit;         // a redshifted blackbody is a blackbody at g*T

    // Liouville: I_obs = g^4 I_emit, and I_emit ~ Temit^4 ~ fluxN. Normalizing
    // at the flux peak (fluxN = 1) gives radiance = g^4 * fluxN. So the g^4
    // relativistic beaming / dimming is included exactly, and the colour is a
    // blackbody at the shifted temperature Tobs.
    float radiance = pow(g, 4.0) * fluxN;
    return blackbodyChroma(Tobs) * radiance * uDiskBright;
}

void main(){
    // ---- Build the initial ray direction from the camera frame ----
    vec2 p = (2.0 * gl_FragCoord.xy - uRes) / uRes.y;   // [-aspect,aspect] x [-1,1]
    vec3 dir = normalize(uCamFwd
                       + p.x * uTanHalfFov * uCamRight
                       + p.y * uTanHalfFov * uCamUp);

    vec3 pos = uCamPos;
    vec3 vel = dir;
    vec3 L   = cross(pos, vel);
    float h2 = dot(L, L);                 // conserved (specific ang. momentum)^2

    vec3 radiance = vec3(0.0);
    float haloAcc = 0.0;
    int  outcome  = 0;                    // 0 captured, 1 escaped, 2 disk hit
    vec3 escDir   = vel;

    for (int i = 0; i < uSteps; ++i){
        float r = length(pos);

        // Event horizon: photon captured -> black shadow.
        if (r <= uRs * 1.001){ outcome = 0; break; }
        // Free escape: sample the (lensed) background sky.
        if (r > uEscapeR && dot(pos, vel) > 0.0){ outcome = 1; escDir = vel; break; }

        // Adaptive integration step: fine near the photon sphere, coarse far away.
        float ds = clamp(0.045 * (r - 0.9*uRs) + 0.012*uRs, 0.008*uRs, 3.0*uRs);

        // ---- classical RK4 ----
        vec3 a1 = accelAt(pos, h2);
        vec3 k1p = vel;
        vec3 k2p = vel + 0.5*ds*a1;
        vec3 a2 = accelAt(pos + 0.5*ds*k1p, h2);
        vec3 k3p = vel + 0.5*ds*a2;
        vec3 a3 = accelAt(pos + 0.5*ds*k2p, h2);
        vec3 k4p = vel + ds*a3;
        vec3 a4 = accelAt(pos + ds*k3p, h2);
        vec3 p1 = pos + (ds/6.0)*(k1p + 2.0*k2p + 2.0*k3p + k4p);
        vec3 v1 = vel + (ds/6.0)*(a1 + 2.0*a2 + 2.0*a3 + a4);

        // ---- optically-thin hot corona / halo (accumulated along the ray) ----
        if (uHaloOn == 1){
            float rr = length(pos);
            if (rr < uDiskOuter * 1.45 && rr > uDiskInner * 0.45){
                float vert = exp(-pow(pos.y / uCoronaThick, 2.0));
                float rad  = smoothstep(uDiskInner*0.45, uDiskInner, rr)
                           * (1.0 - smoothstep(uDiskOuter, uDiskOuter*1.45, rr));
                haloAcc += vert * rad * ds * uCorona;
            }
        }

        // ---- equatorial-plane crossing -> opaque thin disk ----
        if (uDiskOn == 1 && pos.y * p1.y < 0.0){
            float t   = pos.y / (pos.y - p1.y);           // interpolate crossing
            vec3  hit = mix(pos, p1, t);
            float rHit = length(hit);
            if (rHit > uDiskInner && rHit < uDiskOuter){
                vec3 hitVel = normalize(mix(vel, v1, t));
                radiance = diskRadiance(hit, hitVel);
                outcome = 2;
                break;                                   // disk is opaque: stop
            }
            // crossing outside the annulus is transparent -> the ray loops and
            // may hit the disk on a later pass (secondary / higher-order images,
            // i.e. the lensed "halos" and photon ring).
        }

        pos = p1;
        vel = v1;
    }
    // Budget exhausted on a near-critical ray: classify by local radius.
    // (rare; only affects an extremely thin ring of pixels)
    if (outcome == 0 && length(pos) > 3.0 * uRs){
        outcome = 1;
        escDir = vel;
    }

    if (outcome == 1 && uStarsOn == 1) radiance += starfield(escDir);
    haloAcc = min(haloAcc, 4.0);
    radiance += haloAcc * vec3(1.0, 0.55, 0.26);        // warm coronal glow

    vec3 col = aces(radiance * uExposure);
    col = pow(col, vec3(1.0/2.2));                      // linear -> sRGB display
    fragColor = vec4(col, 1.0);
}
