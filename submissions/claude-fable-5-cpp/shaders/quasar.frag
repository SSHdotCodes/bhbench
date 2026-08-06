#version 410 core
// ============================================================================
// Quasar — a supermassive black hole accreting near the Eddington limit,
// built on the same exact Schwarzschild geodesic integrator as the black
// hole scene (d2u/dphi2 = 3u^2 - u, RK4, analytic disk crossings), plus
// volumetric emission/absorption accumulated along the bent rays:
//
//   * accretion disk : Novikov-Thorne T(r), exact Doppler + gravitational
//     redshift g, I ~ g^4 beaming — hotter and larger than the stellar-mass
//     scene (quasar disks radiate ~1e13 Lsun)
//   * relativistic jets : bipolar outflow along the spin axis, bulk beta =
//     0.8. Each sample is Doppler-boosted by delta^3 with the local photon
//     direction, so the approaching jet blazes while the counter-jet nearly
//     vanishes — the classic one-sided appearance of real quasar jets.
//     Helical filaments + knots drift outward.
//   * dusty torus : clumpy obscuring ring (r ~ 32M) with true absorption
//     along the ray and a disk-heated inner rim — the AGN unification
//     "doughnut" that hides the nucleus edge-on
//   * X-ray corona : compact hot glow around the innermost flow
//
// Geometric units G = c = M = 1. Metric is Schwarzschild (no spin) — jets
// are anchored on the rotation axis for visualization.
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
uniform int   uDisk;
uniform int   uStars;
uniform float uDiskGain;

const float PI         = 3.14159265358979;
const float U_CAP      = 0.5;
const float R_ISCO     = 6.0;
const float R_DISK_OUT = 20.0;
const float U_ESC      = 1.0 / 150.0;
const float PHI_MAX    = 20.0;
const int   MAX_STEPS  = 600;

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

// ------------------------------------------------------------- disk --------
// Two-layer flow-map advection (bounded shear) — see blackhole.frag.
vec2 diskTex(float chi, float r, float Om, float tOff, float drift, float seed) {
    float chiM = chi - Om * tOff * 8.0;
    float tex  = fbm(vec3(cos(chiM) * 0.9, sin(chiM) * 0.9, r * 1.35) * 2.2
                     + vec3(seed, seed * 1.7, seed * 0.6));
    float tex2 = fbm(vec3(cos(chiM) * 3.0, sin(chiM) * 3.0, r * 0.7)
                     + vec3(seed * 2.3, seed, drift));
    return vec2(tex, tex2);
}

vec3 diskShade(float r, float chi, float Lz, float sqrtFcam, out float alpha) {
    float Om = pow(r, -1.5);
    float g = sqrt(max(1.0 - 3.0 / r, 0.0))
            / (max(1.0 - Om * Lz, 1e-3) * sqrtFcam);

    float xx = max(1.0 - sqrt(R_ISCO / r), 0.0);
    float Trel = pow(xx / (r * r * r), 0.25) * 7.857;

    const float FLOW_P = 40.0;
    float f0 = fract(uTime / FLOW_P);
    float wA = 1.0 - abs(2.0 * f0 - 1.0);
    float wB = 1.0 - wA;
    float tA = (f0 - 0.5) * FLOW_P;
    float tB = (fract(uTime / FLOW_P + 0.5) - 0.5) * FLOW_P;
    float drift = uTime * 0.05;
    vec2 xA = diskTex(chi, r, Om, tA, drift, 0.0);
    vec2 xB = diskTex(chi, r, Om, tB, drift, 19.7);
    vec2 x = 0.5 + ((xA - 0.5) * wA + (xB - 0.5) * wB)
                   * inversesqrt(max(wA * wA + wB * wB, 0.5));
    float tex = x.x, tex2 = x.y;
    float pattern = pow(0.35 + 1.30 * tex, 2.2) + 0.35 * tex2;

    float edgeIn  = smoothstep(R_ISCO, R_ISCO + 0.6, r);
    float edgeOut = 1.0 - smoothstep(R_DISK_OUT - 5.5, R_DISK_OUT, r);
    float shape = edgeIn * edgeOut;

    float Tobs = 6600.0 * Trel * g;                 // hotter: near-Eddington
    float boost = pow(max(Trel, 0.0), 4.0) * pow(g, 4.0);

    alpha = clamp((0.40 + 0.60 * tex) * shape * 1.1, 0.0, 0.92);
    return blackbody(Tobs) * (boost * pattern * shape * 3.6 * uDiskGain);
}

// ------------------------------------------------------------- jets --------
vec3 jetEmis(vec3 P, vec3 dir) {
    float az = abs(P.z);
    if (az < 1.6 || az > 80.0) return vec3(0.0);
    float rho = length(P.xy);
    float Rj = 0.55 + 0.17 * az;                    // ~9.6 deg half-opening
    if (rho > 2.6 * Rj) return vec3(0.0);
    float prof = exp(-pow(rho / Rj, 2.0) * 1.4);
    float sz = sign(P.z);

    // helical filaments + outward-drifting knots
    float phih = atan(P.y, P.x) - sz * az * 0.5 + uTime * 0.6 * sz;
    float fil = 0.5 + 0.85 * fbm(vec3(cos(phih) * 1.1, sin(phih) * 1.1,
                                      az * 0.42 - uTime * 2.6));
    float kn = pow(0.45 + 0.55 * vnoise(vec3(0.0, sz * 2.7, az * 0.55 - uTime * 3.2)), 3.0);

    // Doppler boost with the local photon direction (toward camera = -dir).
    // beta = 0.88 with delta^3.2 makes the counter-jet nearly vanish, as in
    // real one-sided quasar jets.
    float cth = dot(-dir, vec3(0.0, 0.0, sz));
    const float beta = 0.88, gam = 2.1067;
    float dop = 1.0 / (gam * (1.0 - beta * cth));
    float boost = clamp(pow(dop, 3.2), 0.004, 40.0);

    float rad = 9.0 / (10.0 + az * az * 0.06) * smoothstep(1.6, 6.5, az);
    vec3 cj = mix(vec3(0.45, 0.55, 1.00), vec3(0.95, 0.92, 1.00), prof);
    return cj * (prof * (0.5 + fil) * (0.55 + kn) * rad * boost * 2.0);
}

// ------------------------------------------------------------ torus --------
float torusField(vec3 P, out float heat) {
    heat = 0.0;
    float rc = length(P.xy);
    float d = sqrt((rc - 33.0) * (rc - 33.0) + P.z * P.z * 1.6);
    if (d > 9.0) return 0.0;
    float dens = smoothstep(9.0, 4.0, d) * smoothstep(24.0, 29.0, rc);
    if (dens < 1e-3) return 0.0;
    float phi = atan(P.y, P.x);
    dens *= 0.30 + 0.85 * fbm(vec3(cos(phi) * 6.0, sin(phi) * 6.0, P.z * 0.4)
                              + vec3(0.0, 0.0, uTime * 0.05));
    heat = exp(-max(rc - 27.0, 0.0) / 7.0);
    return dens;
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
    float sqrtFcam = sqrt(fcam);
    float vr = dot(rd, er);
    float vt = nlen;

    float u = 1.0 / r0;
    float w = -u * sqrtFcam * vr / vt;
    float b = r0 * vt / sqrtFcam;
    float Lz = b * nh.z;

    float A = er.z, B = e2.z;
    float Rz = sqrt(A * A + B * B);
    float phiC = 1e9;
    if (Rz > 1e-4) {
        float phi0 = atan(-A, B);
        float k = ceil((1e-4 - phi0) / PI);
        phiC = phi0 + k * PI;
    }

    vec3 col = vec3(0.0);
    float trans = 1.0;
    float phi = 0.0;
    vec3 Pprev = ro;
    bool escaped = false;

    for (int i = 0; i < MAX_STEPS; i++) {
        float h = (u > 0.15) ? 0.035 : ((u > 0.04) ? 0.06 : 0.09);

        float u0 = u, w0 = w;
        float k1u = w0,                k1w = 3.0 * u0 * u0 - u0;
        float ua = u0 + 0.5 * h * k1u, wa = w0 + 0.5 * h * k1w;
        float k2u = wa,                k2w = 3.0 * ua * ua - ua;
        float ub = u0 + 0.5 * h * k2u, wb = w0 + 0.5 * h * k2w;
        float k3u = wb,                k3w = 3.0 * ub * ub - ub;
        float uc3 = u0 + h * k3u,      wc3 = w0 + h * k3w;
        float k4u = wc3,               k4w = 3.0 * uc3 * uc3 - uc3;
        float un = u0 + h / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
        float wn = w0 + h / 6.0 * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
        float phin = phi + h;

        // thin-disk crossings (photon-ring images come along for free)
        while (phiC <= phin && trans > 0.02) {
            float t = (phiC - phi) / h;
            float h00 = (2.0 * t - 3.0) * t * t + 1.0;
            float h10 = ((t - 2.0) * t + 1.0) * t;
            float h01 = (3.0 - 2.0 * t) * t * t;
            float h11 = (t - 1.0) * t * t;
            float ucr = h00 * u0 + h10 * h * w0 + h01 * un + h11 * h * wn;
            float rc = 1.0 / max(ucr, 1e-6);
            if (uDisk == 1 && rc >= R_ISCO && rc <= R_DISK_OUT) {
                vec3 P = rc * (cos(phiC) * er + sin(phiC) * e2);
                float chi = atan(P.y, P.x);
                float a;
                vec3 dc = diskShade(rc, chi, Lz, sqrtFcam, a);
                col += trans * dc;
                trans *= (1.0 - a);
            }
            phiC += PI;
        }

        // volumetrics: jets, torus, corona sampled along the bent ray
        vec3 P = (cos(phin) * er + sin(phin) * e2) / un;
        float ds = length(P - Pprev);
        vec3 Pm = 0.5 * (P + Pprev);
        vec3 tangent = (P - Pprev) / max(ds, 1e-6);
        col += trans * ds * jetEmis(Pm, tangent);
        float heat;
        float dens = torusField(Pm, heat);
        if (dens > 0.0) {
            col += trans * ds * dens * heat * vec3(1.0, 0.42, 0.14) * 0.02;
            trans *= exp(-dens * 0.10 * ds);
        }
        float rm = length(Pm);
        if (rm < 8.0)
            col += trans * ds * exp(-(rm - 2.0) / 1.5) * vec3(0.65, 0.78, 1.00) * 0.012;
        Pprev = P;

        u = un; w = wn; phi = phin;
        if (trans <= 0.02) break;
        if (u > U_CAP) break;
        if (u < U_ESC && w < 0.0) { escaped = true; break; }
        if (phi > PHI_MAX) break;
    }

    if (escaped && trans > 0.02 && uStars == 1) {
        vec3 rhat = cos(phi) * er + sin(phi) * e2;
        vec3 that = -sin(phi) * er + cos(phi) * e2;
        vec3 escDir = normalize(-w * rhat + u * that);
        col += trans * background(escDir);
    }

    fragColor = vec4(col, 1.0);
}
