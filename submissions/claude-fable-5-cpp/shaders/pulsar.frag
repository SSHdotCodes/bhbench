#version 410 core
// ============================================================================
// Pulsar — an oblique-rotator neutron star, ray traced through its own
// curved spacetime.
//
// Geometric units G = c = M = 1 (M ~ 1.4 Msun). The star has R_NS = 4.2M
// (~12 km): compactness 2M/R = 0.48, so photons follow strongly bent
// Schwarzschild null geodesics (same exact integrator as the black hole,
// d2u/dphi2 = 3u^2 - u) and the *far side* of the star is lensed into view —
// both magnetic polar caps can be visible at once, as in real NICER
// pulse-profile modeling. The photon sphere (r = 3M) lies inside the star,
// so there is no shadow — just a hugely magnified surface
// (apparent radius R/sqrt(1 - 2M/R) = 1.38 R).
//
// The magnetic dipole axis is inclined alpha = 35 deg to the spin axis and
// corotates (display period 3 s; real pulsars spin faster — time is slowed
// for legibility, everything else is to scale):
//   * surface: blackbody photosphere with hot polar caps (return-current
//     heating), fbm granulation, and field-aligned striations; surface
//     gravitational redshift sqrt(1 - 2M/R) = 0.72 baked into display temps
//   * beams: hollow curvature-emission cones from both magnetic poles
//     (half-angle ~8 deg) — the volume integral along bent rays produces the
//     lighthouse flash naturally when a cone sweeps the camera
//   * magnetosphere: teal glow on dipole L-shells (r = L sin^2 theta_m)
// ============================================================================

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uRes;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform int   uStars;

const float PI      = 3.14159265358979;
const float R_NS    = 4.2;             // neutron-star radius in M
const float U_SURF  = 1.0 / R_NS;
const float U_ESC   = 1.0 / 70.0;
const float PHI_MAX = 12.0;
const int   MAX_STEPS = 420;

const float OMEGA = 2.0943951;         // spin 2*pi / 3 s
const float SINA  = 0.573576;          // magnetic inclination 35 deg
const float COSA  = 0.819152;
const vec3  M0    = vec3(SINA, 0.0, COSA);   // magnetic axis, corotating frame

// ---------------------------------------------------------------- noise ----
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
    vec3 ip = floor(p), fp = fract(p);
    vec3 s = fp * fp * (3.0 - 2.0 * fp);
    float n000 = hash13(ip);
    float n100 = hash13(ip + vec3(1, 0, 0));
    float n010 = hash13(ip + vec3(0, 1, 0));
    float n110 = hash13(ip + vec3(1, 1, 0));
    float n001 = hash13(ip + vec3(0, 0, 1));
    float n101 = hash13(ip + vec3(1, 0, 1));
    float n011 = hash13(ip + vec3(0, 1, 1));
    float n111 = hash13(ip + vec3(1, 1, 1));
    float xy0 = mix(mix(n000, n100, s.x), mix(n010, n110, s.x), s.y);
    float xy1 = mix(mix(n001, n101, s.x), mix(n011, n111, s.x), s.y);
    return mix(xy0, xy1, s.z);
}
float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) {
        s += a * vnoise(p);
        p = p * 2.03 + vec3(11.7, 5.3, 7.9);
        a *= 0.55;
    }
    return s;
}

vec3 rotZ(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// -------------------------------------------------- Planck (blackbody) -----
vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = T / 100.0;
    vec3 c;
    c.r = (t <= 66.0) ? 1.0 : clamp(1.29293 * pow(t - 60.0, -0.1332047), 0.0, 1.0);
    c.g = (t <= 66.0) ? clamp(0.39008 * log(t) - 0.63184, 0.0, 1.0)
                      : clamp(1.12989 * pow(t - 60.0, -0.0755148), 0.0, 1.0);
    c.b = (t >= 66.0) ? 1.0
        : ((t <= 19.0) ? 0.0 : clamp(0.54323 * log(t - 10.0) - 1.19625, 0.0, 1.0));
    return pow(c, vec3(2.2));
}

// --------------------------------------------------------- background ------
vec3 starLayer(vec3 d, float S, float density, float bright) {
    vec3 cell = floor(d * S);
    vec3 h = hash33(cell);
    if (h.x > density) return vec3(0.0);
    vec3 sp = normalize((cell + 0.15 + 0.70 * hash33(cell + 17.0)) / S);
    float ca = clamp(dot(d, sp), -1.0, 1.0);
    float ang = acos(ca);
    float sigma = 0.0006 + 0.0013 * h.y * h.y;
    float I = exp(-ang * ang / (2.0 * sigma * sigma));
    float lum = bright * (0.10 + 1.8 * h.z * h.z * h.z * h.z);
    float Tstar = mix(3000.0, 14000.0, h.z);
    return blackbody(Tstar) * (I * lum);
}
vec3 background(vec3 d) {
    vec3 col = vec3(0.0);
    vec3 gpole = normalize(vec3(0.36, 0.18, 0.92));
    float mu = dot(d, gpole);
    float band = exp(-mu * mu * 16.0);
    float neb  = fbm(d * 5.0 + vec3(3.1, 0.0, 1.7));
    float neb2 = fbm(d * 13.0 - vec3(7.3, 2.2, 0.0));
    vec3 nebCol = mix(vec3(0.10, 0.13, 0.30), vec3(0.45, 0.27, 0.15), neb2);
    col += band * (0.04 + 0.40 * neb * neb) * nebCol;
    col += vec3(0.005, 0.006, 0.011) * (0.35 + 0.65 * neb);
    col += starLayer(d, 23.0, 0.42, 2.0);
    col += starLayer(d, 47.0, 0.38, 1.1);
    col += starLayer(d, 91.0, 0.35, 0.6);
    return col;
}

// ---------------------------------------------- magnetosphere volume -------
// Emission density at world point P: beam cones + dipole L-shell glow.
vec3 volumeEmis(vec3 P) {
    float r = length(P);
    if (r < R_NS || r > 60.0) return vec3(0.0);
    vec3 pc = rotZ(P, -OMEGA * uTime);           // corotating frame
    vec3 pn = pc / r;
    float ca = clamp(abs(dot(pn, M0)), 0.0, 1.0);
    float ang = acos(ca);

    // magnetic longitude for filament coordinates
    vec3 e1m = normalize(cross(M0, vec3(0.0, 0.0, 1.0)));
    vec3 e2m = cross(M0, e1m);
    float pm = atan(dot(pn, e2m), dot(pn, e1m));

    vec3 emis = vec3(0.0);

    // --- radio/curvature beams: hollow cones from both magnetic poles
    float cone = exp(-pow((ang - 0.12) / 0.035, 2.0))
               + 0.55 * exp(-pow(ang / 0.05, 2.0));
    if (cone > 1e-3) {
        float rad = smoothstep(R_NS, R_NS + 2.2, r) * exp(-r / 18.0);
        float fil = 0.55 + 0.75 * fbm(vec3(pm * 1.5, ang * 18.0, r * 0.45 - uTime * 3.0));
        vec3 beamCol = mix(vec3(0.45, 0.60, 1.00), vec3(0.85, 0.92, 1.00),
                           exp(-pow(ang / 0.06, 2.0)));
        emis += beamCol * (cone * rad * fil * 0.55);
    }

    // --- dipole L-shells (r = L sin^2 theta_m): aurora-like arcs
    float s2 = max(1.0 - ca * ca, 1e-4);
    float L = r / s2;
    float shell = exp(-pow((L - 5.5) / 0.35, 2.0))
                + 0.60 * exp(-pow((L - 8.2) / 0.55, 2.0));
    if (shell > 1e-3) {
        shell *= exp(-(r - R_NS) / 5.5) * smoothstep(R_NS + 0.15, R_NS + 1.1, r);
        float wisp = fbm(pn * 3.2 + vec3(0.0, 0.0, uTime * 0.22));
        wisp = pow(max(wisp, 0.0), 2.0) * 1.6;         // clumpy, mostly dark
        emis += vec3(0.30, 0.90, 0.80) * (shell * wisp * 0.020);
    }
    return emis;
}

// ------------------------------------------------------ surface shading ----
vec3 surfaceShade(vec3 Phit, vec3 dir) {
    vec3 n = normalize(Phit);                    // radial normal
    vec3 nc = rotZ(n, -OMEGA * uTime);           // corotating frame
    float cm = clamp(abs(dot(nc, M0)), 0.0, 1.0);
    float am = acos(cm);                         // colatitude from nearest pole

    vec3 e1m = normalize(cross(M0, vec3(0.0, 0.0, 1.0)));
    vec3 e2m = cross(M0, e1m);
    float phm = atan(dot(nc, e2m), dot(nc, e1m));

    // Magnetic-thermal surface map: heat conduction across B is suppressed,
    // so temperature is organized by the (tilted, corotating) dipole — hot
    // polar caps, a cool belt along the magnetic equator, and subtle
    // field-aligned banding. Display temps: real surface ~1e6 K; ratios
    // physical, band compressed; the 0.72 surface redshift folded in.
    float cap  = exp(-pow(am / 0.16, 2.0));
    float cap2 = exp(-pow(am / 0.40, 2.0));
    float belt = smoothstep(0.80, 1.30, am);       // magnetic-equator cool belt
    float band = fbm(vec3(am * 6.0, phm * 1.2, 4.1));   // field-aligned bands
    float low  = fbm(nc * 3.2 + vec3(7.7, 2.9, 5.1));   // large-scale mottle

    float T = mix(9200.0, 3300.0, belt)
            + 1100.0 * (band - 0.5) + 600.0 * (low - 0.5)
            + 11000.0 * cap + 2600.0 * cap2;

    float mu = clamp(abs(dot(dir, n)), 0.0, 1.0);
    float limb = 0.50 + 0.50 * mu;               // mild limb darkening
    float mottle = 0.88 + 0.35 * (low - 0.5) + 0.20 * (band - 0.5);
    // compressed Stefan-Boltzmann falloff so the cool belt still glows ember
    float bright = clamp(pow(T / 8800.0, 2.5), 0.14, 2.2);
    vec3 col = blackbody(T) * bright * (limb * mottle * 1.15);
    col *= mix(vec3(1.0), vec3(1.30, 0.62, 0.28), belt * 0.85);

    // cap cores glow into the bloom pass
    col += cap * vec3(0.75, 0.86, 1.00) * 2.4;

    // thin-atmosphere rim: grazing sight lines pick up scattered light
    col += pow(1.0 - mu, 4.0) * vec3(0.50, 0.68, 1.00) * 0.5;
    return col;
}

// ------------------------------------------------------------- main --------
void main() {
    vec2 px = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y * 2.0;
    vec3 rd = normalize(uCamFwd + uTanHalfFov * (px.x * uCamRight + px.y * uCamUp));
    vec3 ro = uCamPos;

    float r0 = length(ro);
    vec3 er = ro / r0;
    vec3 nv = cross(er, rd);
    float nlen = length(nv);
    if (nlen < 1e-4) {
        rd = normalize(rd + 1e-3 * uCamUp);
        nv = cross(er, rd);
        nlen = length(nv);
    }
    vec3 nh = nv / nlen;
    vec3 e2 = cross(nh, er);

    float fcam = 1.0 - 2.0 / r0;
    float vr = dot(rd, er);
    float vt = nlen;

    float u = 1.0 / r0;
    float w = -u * sqrt(fcam) * vr / vt;

    vec3 col = vec3(0.0);
    float phi = 0.0;
    vec3 Pprev = ro;
    bool hit = false, escaped = false;
    vec3 Phit = vec3(0.0), dirHit = rd;

    for (int i = 0; i < MAX_STEPS; i++) {
        float h = (u > 0.15) ? 0.03 : ((u > 0.05) ? 0.05 : 0.08);

        float u0 = u, w0 = w;
        float k1u = w0,                k1w = 3.0 * u0 * u0 - u0;
        float ua = u0 + 0.5 * h * k1u, wa = w0 + 0.5 * h * k1w;
        float k2u = wa,                k2w = 3.0 * ua * ua - ua;
        float ub = u0 + 0.5 * h * k2u, wb = w0 + 0.5 * h * k2w;
        float k3u = wb,                k3w = 3.0 * ub * ub - ub;
        float uc = u0 + h * k3u,       wc = w0 + h * k3w;
        float k4u = wc,                k4w = 3.0 * uc * uc - uc;
        float un = u0 + h / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
        float wn = w0 + h / 6.0 * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
        float phin = phi + h;

        if (un >= U_SURF) {                       // hit the stellar surface
            float t = clamp((U_SURF - u0) / max(un - u0, 1e-9), 0.0, 1.0);
            float phiH = phi + h * t;
            Phit = R_NS * (cos(phiH) * er + sin(phiH) * e2);
            float wH = mix(w0, wn, t);
            vec3 rhat = cos(phiH) * er + sin(phiH) * e2;
            vec3 that = -sin(phiH) * er + cos(phiH) * e2;
            dirHit = normalize(-wH * rhat + U_SURF * that);
            // volume up to the surface
            float ds = length(Phit - Pprev);
            col += ds * volumeEmis(0.5 * (Phit + Pprev));
            hit = true;
            break;
        }

        vec3 P = (cos(phin) * er + sin(phin) * e2) / un;
        float ds = length(P - Pprev);
        col += ds * volumeEmis(0.5 * (P + Pprev));
        Pprev = P;

        u = un; w = wn; phi = phin;
        if (u < U_ESC && w < 0.0) { escaped = true; break; }
        if (phi > PHI_MAX) break;
    }

    if (hit) {
        col += surfaceShade(Phit, dirHit);
    } else if (escaped && uStars == 1) {
        vec3 rhat = cos(phi) * er + sin(phi) * e2;
        vec3 that = -sin(phi) * er + cos(phi) * e2;
        vec3 escDir = normalize(-w * rhat + u * that);
        col += background(escDir);
    }

    fragColor = vec4(col, 1.0);
}
