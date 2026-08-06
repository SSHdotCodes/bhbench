#version 410 core
// ============================================================================
// Schwarzschild black hole ray tracer.
//
// Geometric units G = c = M = 1 (horizon r_s = 2, photon sphere r = 3,
// ISCO r = 6, critical impact parameter b_crit = 3*sqrt(3) ~ 5.196).
//
// Each pixel launches a photon backwards from the camera. Because the
// Schwarzschild spacetime is spherically symmetric every null geodesic stays
// in a plane, so the trajectory is integrated in that plane with the exact
// null geodesic ("Binet") equation
//
//      d^2 u / d phi^2 = 3 u^2 - u ,      u = 1/r
//
// using RK4. Accretion-disk / grid-plane intersections are located
// analytically per half-winding (z ~ sin(phi - phi0)) and the crossing radius
// is recovered with cubic Hermite interpolation, so higher-order (photon-ring)
// images come out of the same integration.
//
// Disk radiation is fully relativistic: for gas on circular Keplerian orbits
// (Omega = r^-3/2) the combined gravitational + Doppler shift of a photon with
// conserved energy E = 1 and axial angular momentum L_z is
//
//      g = E_obs / E_emit = sqrt(1 - 3/r) / ((1 - Omega * L_z) * sqrt(f_cam))
//
// and the bolometric intensity transforms as I_obs = g^4 * I_emit
// (Liouville: I_nu / nu^3 invariant). Color = Planck spectrum at g * T(r) with
// the Novikov-Thorne thin-disk profile T(r) ~ [ (1 - sqrt(6/r)) / r^3 ]^(1/4).
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
uniform int   uGrid;
uniform int   uStars;
uniform float uDiskGain;

const float PI         = 3.14159265358979;
const float U_CAP      = 0.5;          // 1/r at the event horizon (r = 2M)
const float R_ISCO     = 6.0;          // innermost stable circular orbit
const float R_DISK_OUT = 16.0;
const float U_ESC      = 1.0 / 150.0;  // escape radius for the background
const float PHI_MAX    = 20.0;         // max winding angle before "captured"
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
    for (int i = 0; i < 5; i++) {
        s += a * vnoise(p);
        p = p * 2.03 + vec3(11.7, 5.3, 7.9);
        a *= 0.55;
    }
    return s;
}

// -------------------------------------------------- Planck (blackbody) -----
// Kelvin -> linear RGB, ~white at 6600 K (Tanner Helland fit).
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
    // Milky-Way band + faint nebulosity (procedural, lensed like everything else)
    vec3 gpole = normalize(vec3(0.36, 0.18, 0.92));
    float mu = dot(d, gpole);
    float band = exp(-mu * mu * 16.0);
    float neb  = fbm(d * 5.0 + vec3(3.1, 0.0, 1.7));
    float neb2 = fbm(d * 13.0 - vec3(7.3, 2.2, 0.0));
    vec3 nebCol = mix(vec3(0.10, 0.13, 0.30), vec3(0.45, 0.27, 0.15), neb2);
    col += band * (0.04 + 0.40 * neb * neb) * nebCol;
    col += vec3(0.005, 0.006, 0.011) * (0.35 + 0.65 * neb);
    // three star strata
    col += starLayer(d, 23.0, 0.42, 2.0);
    col += starLayer(d, 47.0, 0.38, 1.1);
    col += starLayer(d, 91.0, 0.35, 0.6);
    return col;
}

// ------------------------------------------------------------- disk --------
// One advected pattern shears without bound under differential rotation
// (Omega ~ r^-3/2): after minutes of run time it winds into near-perfect
// concentric rings, and a rotating axisymmetric pattern looks STATIC.
// Classic flow-map fix: two layers with shear bounded to +-FLOW_P/2,
// cross-faded so each layer resets while its weight is zero.
vec2 diskTex(float chi, float r, float Om, float tOff, float drift, float seed) {
    float chiM = chi - Om * tOff * 8.0;
    float tex  = fbm(vec3(cos(chiM) * 0.9, sin(chiM) * 0.9, r * 1.65) * 2.2
                     + vec3(seed, seed * 1.7, seed * 0.6));
    float tex2 = fbm(vec3(cos(chiM) * 3.0, sin(chiM) * 3.0, r * 0.8)
                     + vec3(seed * 2.3, seed, drift));
    return vec2(tex, tex2);
}

vec3 diskShade(float r, float chi, float Lz, float sqrtFcam, out float alpha) {
    float Om = pow(r, -1.5);                       // Keplerian angular velocity
    float g = sqrt(max(1.0 - 3.0 / r, 0.0))
            / (max(1.0 - Om * Lz, 1e-3) * sqrtFcam);

    // Novikov-Thorne relative temperature, normalized to 1 at its peak r = 49/6
    float xx = max(1.0 - sqrt(R_ISCO / r), 0.0);
    float Trel = pow(xx / (r * r * r), 0.25) * 7.857;

    const float FLOW_P = 40.0;                     // seconds per shear cycle
    float f0 = fract(uTime / FLOW_P);
    float wA = 1.0 - abs(2.0 * f0 - 1.0);
    float wB = 1.0 - wA;
    float tA = (f0 - 0.5) * FLOW_P;
    float tB = (fract(uTime / FLOW_P + 0.5) - 0.5) * FLOW_P;
    float drift = uTime * 0.05;
    vec2 xA = diskTex(chi, r, Om, tA, drift, 0.0);
    vec2 xB = diskTex(chi, r, Om, tB, drift, 19.7);
    // contrast-preserving blend of the two independent noise fields
    vec2 x = 0.5 + ((xA - 0.5) * wA + (xB - 0.5) * wB)
                   * inversesqrt(max(wA * wA + wB * wB, 0.5));
    float tex = x.x, tex2 = x.y;
    float pattern = pow(0.35 + 1.30 * tex, 2.2) + 0.35 * tex2;

    float edgeIn  = smoothstep(R_ISCO, R_ISCO + 0.6, r);
    float edgeOut = 1.0 - smoothstep(R_DISK_OUT - 4.5, R_DISK_OUT, r);
    float shape = edgeIn * edgeOut;

    float Tobs = 5900.0 * Trel * g;                 // display-band temperature
    float boost = pow(max(Trel, 0.0), 4.0) * pow(g, 4.0);   // sigma T^4 * g^4

    alpha = clamp((0.40 + 0.60 * tex) * shape * 1.1, 0.0, 0.92);
    return blackbody(Tobs) * (boost * pattern * shape * 2.0 * uDiskGain);
}

// -------------------------------------------- lensed spacetime grid --------
// Schwarzschild coordinate grid painted on the equatorial plane; color keyed
// to the lapse sqrt(1 - 2M/r): time runs slower (redder) toward the horizon.
vec3 gridShade(float r, float chi, out float alpha) {
    float lapse = sqrt(max(1.0 - 2.0 / r, 0.0));
    // circles of constant r every 2M
    float ringD = abs(fract(r * 0.5 + 0.5) - 0.5) * 2.0;         // dist in M
    float ring = 1.0 - smoothstep(0.05, 0.22, ringD);
    // radial spokes every 15 degrees, constant arc-length width
    float sp = PI / 12.0;
    float spokeD = abs(fract(chi / sp + 0.5) - 0.5) * sp * r;    // arc dist in M
    float spoke = 1.0 - smoothstep(0.05, 0.22, spokeD);
    float line = max(ring, 0.8 * spoke);

    vec3 c = mix(vec3(1.00, 0.16, 0.04), vec3(0.25, 0.75, 1.00),
                 smoothstep(0.0, 0.95, lapse));
    float I = line * (0.30 + 3.0 * pow(1.0 - lapse, 2.0));
    alpha = clamp(0.32 * line + 0.03, 0.0, 1.0);
    return c * I + c * 0.015;                                    // faint sheet
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
    if (nlen < 1e-4) {                     // (near-)radial ray: degenerate plane
        rd = normalize(rd + 1e-3 * uCamUp);
        nv = cross(er, rd);
        nlen = length(nv);
    }
    vec3 nh = nv / nlen;                   // orbital-plane normal (~ L direction)
    vec3 e2 = cross(nh, er);               // in-plane tangential basis vector

    float fcam = 1.0 - 2.0 / r0;
    float sqrtFcam = sqrt(fcam);
    float vr = dot(rd, er);                // local-tetrad radial component
    float vt = nlen;                       // local-tetrad tangential component

    float u = 1.0 / r0;
    float w = -u * sqrtFcam * vr / vt;     // du/dphi from the static tetrad
    float b = r0 * vt / sqrtFcam;          // impact parameter (E = 1)
    float Lz = b * nh.z;                   // conserved axial angular momentum

    // Equatorial-plane crossings: z(phi) ~ sin(phi - phi0), roots phi0 + k*pi
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
    bool escaped = false;

    for (int i = 0; i < MAX_STEPS; i++) {
        float h = (u > 0.15) ? 0.035 : ((u > 0.04) ? 0.07 : 0.12);

        // RK4 on (u, w):  u' = w,  w' = 3u^2 - u
        float u0 = u, w0 = w;
        float k1u = w0,               k1w = 3.0 * u0 * u0 - u0;
        float ua = u0 + 0.5 * h * k1u, wa = w0 + 0.5 * h * k1w;
        float k2u = wa,               k2w = 3.0 * ua * ua - ua;
        float ub = u0 + 0.5 * h * k2u, wb = w0 + 0.5 * h * k2w;
        float k3u = wb,               k3w = 3.0 * ub * ub - ub;
        float uc3 = u0 + h * k3u,      wc3 = w0 + h * k3w;
        float k4u = wc3,              k4w = 3.0 * uc3 * uc3 - uc3;
        float un = u0 + h / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
        float wn = w0 + h / 6.0 * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
        float phin = phi + h;

        // handle every equatorial crossing inside this step
        while (phiC <= phin && trans > 0.02) {
            float t = (phiC - phi) / h;
            float h00 = (2.0 * t - 3.0) * t * t + 1.0;
            float h10 = ((t - 2.0) * t + 1.0) * t;
            float h01 = (3.0 - 2.0 * t) * t * t;
            float h11 = (t - 1.0) * t * t;
            float ucr = h00 * u0 + h10 * h * w0 + h01 * un + h11 * h * wn;
            float rc = 1.0 / max(ucr, 1e-6);
            if (rc > 2.05 && rc < 60.0) {
                vec3 P = rc * (cos(phiC) * er + sin(phiC) * e2);
                float chi = atan(P.y, P.x);
                if (uDisk == 1 && rc >= R_ISCO && rc <= R_DISK_OUT) {
                    float a;
                    vec3 dc = diskShade(rc, chi, Lz, sqrtFcam, a);
                    col += trans * dc;
                    trans *= (1.0 - a);
                }
                if (uGrid == 1 && rc < 30.0
                    && (uDisk == 0 || rc < R_ISCO - 0.2 || rc > R_DISK_OUT)) {
                    float a;
                    vec3 gc = gridShade(rc, chi, a);
                    col += trans * gc;
                    trans *= (1.0 - a);
                }
            }
            phiC += PI;
        }

        u = un; w = wn; phi = phin;
        if (trans <= 0.02) break;
        if (u > U_CAP) break;                          // through the horizon
        if (u < U_ESC && w < 0.0) { escaped = true; break; }
        if (phi > PHI_MAX) break;                      // trapped near photon sphere
    }

    if (escaped && trans > 0.02 && uStars == 1) {
        // asymptotic direction: dP/dphi ~ -w * rhat + u * that
        vec3 rhat = cos(phi) * er + sin(phi) * e2;
        vec3 that = -sin(phi) * er + cos(phi) * e2;
        vec3 escDir = normalize(-w * rhat + u * that);
        col += trans * background(escDir);
    }

    fragColor = vec4(col, 1.0);
}
