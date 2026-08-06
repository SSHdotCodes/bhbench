#version 410 core
// =============================================================================
//  General-relativistic ray tracer for the Kerr metric.
//
//  Every pixel launches one photon backwards in time from a zero-angular-
//  momentum observer (ZAMO) at the camera and integrates the null geodesic
//  equations in Boyer-Lindquist coordinates with an adaptive Cash-Karp RK4(5)
//  scheme.  There is no lensing "approximation" anywhere in here: the bending,
//  the photon ring, the frame dragging and the Doppler/gravitational shifts all
//  fall out of integrating the geodesics.
//
//  Units: G = c = M = 1.  Lengths are in gravitational radii r_g = GM/c^2.
// =============================================================================

in vec2 vUV;
layout(location = 0) out vec4 fragColor;

// ------------------------------------------------------------------ uniforms

uniform vec2  uRes;
uniform float uSpin;          // a/M
uniform vec3  uCamP;          // camera position, pseudo-Cartesian from BL
uniform vec3  uCamF, uCamR, uCamU;
uniform float uTanHalf;       // tan(vfov/2)
uniform float uAspect;
uniform vec2  uJitter;        // sub-pixel offset for progressive AA
uniform float uTimeCoord;     // accumulated coordinate time (disk animation)

// disk
uniform int   uDiskMode;      // 0 none, 1 opaque NT disk, 2 disk + corona, 3 corona only
uniform float uDiskIn, uDiskOut;
uniform float uDiskBright;
uniform float uTurb;
uniform sampler2D uTempLUT;   // R32F  effective temperature (K) vs log r
uniform vec2  uLUTLogRange;   // log(rIn), log(rOut)
uniform float uTempRef;       // normalisation temperature (K)
// Displayed colour temperature = uTempA * pow(T_observed, uTempB).
// Physical mode uses (1, 1); the "remapped" mode compresses the real range into
// the visible band so the temperature gradient can actually be seen.
uniform float uTempA, uTempB;

// corona / halo
uniform float uCoronaH;       // scale height / r
uniform float uCoronaOut;     // outer radius
uniform float uCoronaBright;
uniform float uCoronaTemp;    // fluid-frame temperature (K)

// blackbody colour table
uniform sampler2D uBBLUT;
uniform vec2  uBBLogRange;    // log10 Tmin, log10 Tmax

// sky
uniform float uStarBright;
uniform float uSkyGrid;       // 0 = off, else line brightness
uniform float uNebula;

// integration
uniform float uTol;
uniform int   uMaxSteps;
uniform float uRFar;

// display
uniform int   uDebugView;     // 0 beauty, 1 step count, 2 |H| violation, 3 redshift
uniform float uExposure;

// ------------------------------------------------------------------ constants

const float PI = 3.141592653589793;

// Per-ray constants of motion (the Kerr metric is stationary and axisymmetric,
// so E = -p_t and L = p_phi are conserved exactly along every geodesic).
float gA, gE, gL, gA2;
float gRh;

struct St { float t, r, th, ph, pr, pth; };

St add(St x, St y) { return St(x.t+y.t, x.r+y.r, x.th+y.th, x.ph+y.ph, x.pr+y.pr, x.pth+y.pth); }
St mul(float s, St x) { return St(s*x.t, s*x.r, s*x.th, s*x.ph, s*x.pr, s*x.pth); }

// ------------------------------------------------------ geodesic derivatives

// H = F / (2 Sigma),
// F = Delta pr^2 + pth^2 - P^2/Delta + (L - a E sin^2 th)^2 / sin^2 th
// with P = (r^2 + a^2) E - a L.  H vanishes identically on a null geodesic.
St deriv(St y) {
    float r = y.r, th = y.th;
    float s = sin(th), c = cos(th);
    float s2 = max(s * s, 1e-8);
    float r2 = r * r;

    float Sig = r2 + gA2 * c * c;
    float Del = r2 - 2.0 * r + gA2;
    float iSig = 1.0 / Sig;
    float iDel = 1.0 / (abs(Del) < 1e-7 ? sign(Del + 1e-12) * 1e-7 : Del);

    float P = (r2 + gA2) * gE - gA * gL;
    float q = gL - gA * gE * s2;

    float F = Del * y.pr * y.pr + y.pth * y.pth - P * P * iDel + q * q / s2;

    float dDel = 2.0 * r - 2.0;
    float dFdr = dDel * y.pr * y.pr - 4.0 * r * gE * P * iDel + P * P * dDel * iDel * iDel;
    float dFdth = 2.0 * s * c * (gA2 * gE * gE - gL * gL / (s2 * s2));

    float dHdr  = 0.5 * iSig * (dFdr  - F * iSig * (2.0 * r));
    float dHdth = 0.5 * iSig * (dFdth + F * iSig * (2.0 * gA2 * c * s));

    St d;
    d.t   = iSig * ((r2 + gA2) * P * iDel + gA * q);
    d.r   = Del * iSig * y.pr;
    d.th  = iSig * y.pth;
    d.ph  = iSig * (gA * P * iDel + gL / s2 - gA * gE);
    d.pr  = -dHdr;
    d.pth = -dHdth;
    return d;
}

float hamiltonian(St y) {
    float s = sin(y.th), c = cos(y.th);
    float s2 = max(s * s, 1e-8);
    float r2 = y.r * y.r;
    float Sig = r2 + gA2 * c * c;
    float Del = r2 - 2.0 * y.r + gA2;
    float iDel = 1.0 / (abs(Del) < 1e-7 ? 1e-7 : Del);
    float P = (r2 + gA2) * gE - gA * gL;
    float q = gL - gA * gE * s2;
    return (Del * y.pr * y.pr + y.pth * y.pth - P * P * iDel + q * q / s2) / (2.0 * Sig);
}

// ----------------------------------------------------------- helper: colour

vec3 blackbodyColor(float T) {
    float x = (log(max(T, 1.0)) / log(10.0) - uBBLogRange.x)
            / (uBBLogRange.y - uBBLogRange.x);
    return texture(uBBLUT, vec2(clamp(x, 0.0, 1.0), 0.5)).rgb;
}

// Colour of a thermal source at observed temperature T, after the display remap.
vec3 thermalColor(float T) {
    return blackbodyColor(uTempA * pow(max(T, 1.0), uTempB));
}

float diskTemperature(float r) {
    float x = (log(max(r, 1e-3)) - uLUTLogRange.x) / (uLUTLogRange.y - uLUTLogRange.x);
    if (x < 0.0 || x > 1.0) return 0.0;
    return texture(uTempLUT, vec2(x, 0.5)).r;
}

// ------------------------------------------------------------ helper: noise

float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
vec3 hash33(vec3 p) {
    // Same precaution as hash13: the small-multiplier form collapses whenever a
    // component lands on zero, which for the star grid is the cell column that
    // runs straight down the middle of the image.
    p = fract((p + 91.71) * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yxz + 19.19);
    return fract((p.xxy + p.yxx) * p.zyx);
}
// Multiplying by large primes before the fract keeps the hash from degenerating
// on lattice planes; without it the noise shows seams wherever a coordinate
// crosses zero, which lands right down the middle of the frame.
float hash13(vec3 p) {
    vec3 q = fract(p * vec3(443.8975, 441.4236, 437.1953));
    q += dot(q, q.yzx + 19.19);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec3 p) {
    p += 73.13;                       // keep the sampling domain well away from 0
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = 0.0;
    n += mix(mix(mix(hash13(i + vec3(0,0,0)), hash13(i + vec3(1,0,0)), f.x),
                 mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), f.x),
                 mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), f.x), f.y), f.z);
    return n;
}
float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p *= 2.03; a *= 0.5; }
    return s;
}

// --------------------------------------------------------------- kinematics

// Contravariant 4-velocity of the accreting gas in the equatorial plane.
// Keplerian outside the ISCO; a free-fall plunge conserving the ISCO's E and L
// inside it, which is the physically correct inner boundary behaviour for a
// geometrically thin disk.
vec3 diskFourVelocity(float r) {
    float a = gA, a2 = gA2;
    float Del = r * r - 2.0 * r + a2;
    float Sig = r * r;
    float A = (r * r + a2) * (r * r + a2) - a2 * Del;

    float Gtt = -A / (Sig * Del);
    float Gtp = -2.0 * a * r / (Sig * Del);
    float Gpp = (Del - a2) / (Sig * Del);
    float Grr = Del / Sig;

    if (r >= uDiskIn) {
        float Om = 1.0 / (pow(r, 1.5) + a);
        float g_tt = -(1.0 - 2.0 / r);
        float g_tp = -2.0 * a / r;
        float g_pp = r * r + a2 + 2.0 * a2 / r;
        float nrm = -(g_tt + 2.0 * Om * g_tp + Om * Om * g_pp);
        float ut = inversesqrt(max(nrm, 1e-6));
        return vec3(ut, 0.0, Om * ut);
    } else {
        float ri = uDiskIn;
        float vi = sqrt(ri);
        float den = sqrt(max(1e-6, 1.0 - 3.0 / ri + 2.0 * a / (ri * vi)));
        float E = (1.0 - 2.0 / ri + a / (ri * vi)) / den;
        float L = vi * (1.0 - 2.0 * a / (ri * vi) + a2 / (ri * ri)) / den;

        float ut  = -Gtt * E + Gtp * L;
        float uph = -Gtp * E + Gpp * L;
        float ur2 = -(1.0 + (Gtt * E * E - 2.0 * Gtp * E * L + Gpp * L * L)) / max(Grr, 1e-6);
        float ur  = Grr * (-sqrt(max(ur2, 0.0)));
        return vec3(ut, ur, uph);
    }
}

// Redshift factor g = nu_observed / nu_emitted.
// The ray is normalised so that k.u_camera = -1, hence g = -1/(k.u_emitter).
float redshift(St y, vec3 u) {
    float kdotu = -gE * u.x + y.pr * u.y + gL * u.z;
    return -1.0 / min(kdotu, -1e-6);
}

// ------------------------------------------------------------------- sky

vec2 cubeFace(vec3 d, out float face) {
    vec3 ad = abs(d);
    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0.0 ? 0.0 : 1.0; return d.yz / ad.x; }
    if (ad.y >= ad.z)                 { face = d.y > 0.0 ? 2.0 : 3.0; return d.xz / ad.y; }
    face = d.z > 0.0 ? 4.0 : 5.0;      return d.xy / ad.z;
}

// Procedural star field.  Stars carry a temperature so the gravitational and
// Doppler shift of the background sky can be applied to their colour, not just
// their brightness.
vec3 stars(vec3 dir, float g, float foot) {
    vec3 col = vec3(0.0);
    for (int layer = 0; layer < 3; ++layer) {
        float density = (layer == 0) ? 60.0 : ((layer == 1) ? 150.0 : 340.0);
        float amp     = (layer == 0) ? 1.0  : ((layer == 1) ? 0.45  : 0.18);
        float face;
        vec2 uv = cubeFace(dir, face);
        vec2 gp = uv * density;
        vec2 cell = floor(gp), fp = fract(gp);
        float rad = clamp(0.55 * foot * density, 0.012, 0.16);

        for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            vec2 c = cell + vec2(i, j);
            vec3 h = hash33(vec3(c, face * 37.0 + float(layer) * 91.0));
            if (h.z < 0.90) continue;
            vec2 sp = vec2(i, j) + fract(h.xy * 7.13);
            float d = length(sp - fp);
            float prof = exp(-0.5 * (d * d) / (rad * rad));
            float u = hash11(h.x * 311.0 + 7.0);
            // Hotter stars are intrinsically brighter, which is why a real sky
            // looks mostly white-blue even though red dwarfs dominate by number.
            float T = 2600.0 + 23000.0 * pow(u, 2.4);
            float mag = 0.010 * pow(max(hash11(h.z * 613.0 + face), 1e-3), -0.80) * (0.35 + 1.3 * u);
            col += amp * mag * prof * blackbodyColor(T * g) * (g * g * g * g);
        }
    }
    return col * uStarBright;
}

// Faint interstellar background so the lensing of an extended source is visible.
vec3 nebula(vec3 dir, float g) {
    if (uNebula <= 0.0) return vec3(0.0);
    // Tilt the galactic plane away from the disk plane so the two do not merge.
    vec3 d3 = vec3(dir.x, dir.y * 0.53 - dir.z * 0.85, dir.y * 0.85 + dir.z * 0.53);
    float band = exp(-7.0 * d3.z * d3.z);
    float n = fbm(dir * 3.0 + 11.0);
    float m = fbm(dir * 9.0 - 4.0);
    float d = band * smoothstep(0.32, 0.82, n) * (0.35 + 0.65 * m);
    float T = 4600.0 * g;
    return uNebula * d * 0.09 * blackbodyColor(T) * (g * g * g * g);
}

// A latitude/longitude grid painted on the celestial sphere.  Under lensing it
// shows directly how the black hole deforms the map from sky directions to
// image directions.
vec3 skyGrid(vec3 dir, float foot) {
    if (uSkyGrid <= 0.0) return vec3(0.0);
    float lat = asin(clamp(dir.z, -1.0, 1.0));
    float lon = atan(dir.y, dir.x);
    float step = PI / 18.0;                 // 10 degree spacing
    float w = max(foot * 2.2, 0.011);
    float a = abs(fract(lat / step + 0.5) - 0.5) * step;
    float b = abs(fract(lon / step + 0.5) - 0.5) * step * max(cos(lat), 0.05);
    float line = max(smoothstep(w, 0.0, a), smoothstep(w, 0.0, b));
    return uSkyGrid * line * vec3(0.16, 0.55, 1.0);
}

// ------------------------------------------------------------- disk shading

vec3 diskEmission(St y, float rHit, out float gOut) {
    vec3 u = diskFourVelocity(rHit);
    float g = redshift(y, u);
    gOut = g;

    float Te = diskTemperature(rHit);
    if (Te <= 0.0) return vec3(0.0);

    // The observed spectrum of a Doppler/gravitationally shifted blackbody is
    // again a blackbody, at T_obs = g * T_emit, and its bolometric surface
    // brightness is sigma T_obs^4 / pi.  The famous g^4 beaming is exactly this.
    float Tobs = g * Te;
    float I = pow(Tobs / uTempRef, 4.0);

    // Real disks do not end at a knife edge; taper the outermost 12%.
    I *= smoothstep(uDiskOut, uDiskOut * 0.88, rHit);

    if (uTurb > 0.0) {
        // Density fluctuations frozen into the flow: label each fluid element by
        // the azimuth it had at t = 0 and advect the pattern with the orbit.
        float Om = (rHit >= uDiskIn) ? 1.0 / (pow(rHit, 1.5) + gA) : 1.0 / (pow(uDiskIn, 1.5) + gA);
        float tAbs = uTimeCoord + y.t;
        float lab = y.ph - Om * tAbs;
        vec3 q = vec3(cos(lab), sin(lab), 0.0) * (2.2 + 0.9 * log(rHit)) + vec3(0.0, 0.0, log(rHit) * 3.4);
        float n = fbm(q * 1.7);
        float m = fbm(q * 5.5 + 13.0);
        I *= mix(1.0, 0.35 + 1.55 * n * (0.55 + 0.75 * m), clamp(uTurb, 0.0, 1.0));
    }

    return thermalColor(Tobs) * I * uDiskBright;
}

// ============================================================================

void main() {
    gA  = uSpin;
    gA2 = gA * gA;
    gRh = 1.0 + sqrt(max(1.0 - gA2, 0.0));

    // ---- primary ray in the camera's local frame ----
    vec2 px = (gl_FragCoord.xy + uJitter) / uRes * 2.0 - 1.0;
    vec3 dir = normalize(uCamF + uCamR * (px.x * uTanHalf * uAspect) + uCamU * (px.y * uTanHalf));

    // ---- camera position in Boyer-Lindquist coordinates ----
    float r0  = length(uCamP);
    float th0 = acos(clamp(uCamP.z / r0, -1.0, 1.0));
    float ph0 = atan(uCamP.y, uCamP.x);
    th0 = clamp(th0, 1e-3, PI - 1e-3);

    float sth = sin(th0), cth = cos(th0), sph = sin(ph0), cph = cos(ph0);
    vec3 er = vec3(sth * cph, sth * sph, cth);
    vec3 et = vec3(cth * cph, cth * sph, -sth);
    vec3 ep = vec3(-sph, cph, 0.0);

    // Local viewing direction resolved on the ZAMO orthonormal tetrad legs.
    vec3 n = vec3(dot(dir, er), dot(dir, et), dot(dir, ep));

    // ZAMO quantities: lapse alpha, frame dragging omega, and the phi leg's
    // circumferential radius varpi.
    float Sig0 = r0 * r0 + gA2 * cth * cth;
    float Del0 = r0 * r0 - 2.0 * r0 + gA2;
    float A0   = (r0 * r0 + gA2) * (r0 * r0 + gA2) - gA2 * Del0 * sth * sth;
    float alpha = sqrt(max(Sig0 * Del0 / A0, 1e-9));
    float omega = 2.0 * gA * r0 / A0;
    float varpi = sqrt(A0 / Sig0) * sth;

    // Photon travelling toward the observer has local components k^(a) = (1, -n)
    // and unit measured energy.  Lower the index with the ZAMO co-tetrad.
    gE = alpha - omega * varpi * n.z;   // -k_t
    gL = -varpi * n.z;                  //  k_phi

    St y;
    y.t = 0.0; y.r = r0; y.th = th0; y.ph = ph0;
    y.pr  = -n.x * sqrt(Sig0 / max(Del0, 1e-9));
    y.pth = -n.y * sqrt(Sig0);

    // ---- integrate backwards in affine parameter ----
    float h = -0.25 * max(1.0, 0.1 * r0);
    float rStop = gRh * 1.0015 + 1e-3;

    vec3  accum = vec3(0.0);      // optically thin emission picked up on the way
    float maxH = 0.0;             // largest |H| seen: numerical accuracy monitor
    int   steps = 0;
    bool  captured = false;
    bool  escaped = false;
    float hitG = 1.0;
    float prevCos = cos(th0);

    // Cash-Karp coefficients
    const float b21 = 0.2;
    const float b31 = 3.0/40.0,   b32 = 9.0/40.0;
    const float b41 = 0.3,        b42 = -0.9,       b43 = 1.2;
    const float b51 = -11.0/54.0, b52 = 2.5,        b53 = -70.0/27.0,  b54 = 35.0/27.0;
    const float b61 = 1631.0/55296.0, b62 = 175.0/512.0, b63 = 575.0/13824.0,
                b64 = 44275.0/110592.0, b65 = 253.0/4096.0;
    const float c1 = 37.0/378.0, c3 = 250.0/621.0, c4 = 125.0/594.0, c6 = 512.0/1771.0;
    const float d1 = 2825.0/27648.0, d3 = 18575.0/48384.0, d4 = 13525.0/55296.0,
                d5 = 277.0/14336.0, d6 = 0.25;

    for (int it = 0; it < uMaxSteps; ++it) {
        steps = it;

        // Keep the step from ever jumping across the horizon or the disk plane.
        float hmax = max(0.008, 0.16 * (y.r - gRh));
        if (abs(h) > hmax) h = -hmax;

        St k1 = deriv(y);
        St k2 = deriv(add(y, mul(h * b21, k1)));
        St k3 = deriv(add(add(y, mul(h * b31, k1)), mul(h * b32, k2)));
        St k4 = deriv(add(add(add(y, mul(h * b41, k1)), mul(h * b42, k2)), mul(h * b43, k3)));
        St k5 = deriv(add(add(add(add(y, mul(h * b51, k1)), mul(h * b52, k2)), mul(h * b53, k3)), mul(h * b54, k4)));
        St k6 = deriv(add(add(add(add(add(y, mul(h * b61, k1)), mul(h * b62, k2)), mul(h * b63, k3)), mul(h * b64, k4)), mul(h * b65, k5)));

        St yh = add(add(add(add(y, mul(h * c1, k1)), mul(h * c3, k3)), mul(h * c4, k4)), mul(h * c6, k6));
        St yl = add(add(add(add(add(y, mul(h * d1, k1)), mul(h * d3, k3)), mul(h * d4, k4)), mul(h * d5, k5)), mul(h * d6, k6));

        float err = max(max(abs(yh.r - yl.r), abs(yh.th - yl.th)),
                        max(abs(yh.ph - yl.ph), max(abs(yh.pr - yl.pr), abs(yh.pth - yl.pth))));
        float scale = max(1.0, max(abs(y.r), max(abs(y.pr), abs(y.pth))));
        float e = err / (scale * uTol);

        if (e > 1.0 && abs(h) > 0.009) {
            h *= max(0.2, 0.9 * pow(e, -0.25));
            continue;
        }

        St ynew = yh;

        // ---- equatorial plane crossing (cubic Hermite dense output) ----
        float c0 = cos(y.th), c1n = cos(ynew.th);
        if (uDiskMode != 3 && uDiskMode != 0 && c0 * c1n < 0.0) {
            St f1 = k1;
            St f2 = deriv(ynew);
            // Bisect the Hermite interpolant for cos(theta) = 0.
            float lo = 0.0, hi = 1.0;
            for (int b = 0; b < 14; ++b) {
                float s = 0.5 * (lo + hi);
                float s2 = s * s, s3 = s2 * s;
                float H00 = 2.0*s3 - 3.0*s2 + 1.0;
                float H10 = s3 - 2.0*s2 + s;
                float H01 = -2.0*s3 + 3.0*s2;
                float H11 = s3 - s2;
                float th = H00*y.th + H10*h*f1.th + H01*ynew.th + H11*h*f2.th;
                if (cos(th) * c0 > 0.0) lo = s; else hi = s;
            }
            float s = 0.5 * (lo + hi);
            float s2 = s * s, s3 = s2 * s;
            float H00 = 2.0*s3 - 3.0*s2 + 1.0;
            float H10 = s3 - 2.0*s2 + s;
            float H01 = -2.0*s3 + 3.0*s2;
            float H11 = s3 - s2;
            St hit;
            hit.t   = H00*y.t   + H10*h*f1.t   + H01*ynew.t   + H11*h*f2.t;
            hit.r   = H00*y.r   + H10*h*f1.r   + H01*ynew.r   + H11*h*f2.r;
            hit.th  = H00*y.th  + H10*h*f1.th  + H01*ynew.th  + H11*h*f2.th;
            hit.ph  = H00*y.ph  + H10*h*f1.ph  + H01*ynew.ph  + H11*h*f2.ph;
            hit.pr  = H00*y.pr  + H10*h*f1.pr  + H01*ynew.pr  + H11*h*f2.pr;
            hit.pth = H00*y.pth + H10*h*f1.pth + H01*ynew.pth + H11*h*f2.pth;

            if (hit.r >= uDiskIn && hit.r <= uDiskOut) {
                float gg;
                accum += diskEmission(hit, hit.r, gg);
                hitG = gg;
                captured = true;               // opaque surface: the ray stops here
                y = hit;
                break;
            }
        }

        // ---- optically thin corona / halo, integrated along the ray ----
        if (uDiskMode >= 2 && y.r < uCoronaOut && y.r > gRh * 1.05) {
            float rr = y.r;
            float z = rr * cos(y.th);
            float cyl = max(rr * sin(y.th), uDiskIn * 0.6);
            float H = max(uCoronaH * cyl, 0.30);
            float vert = exp(-0.5 * (z * z) / (H * H));
            if (vert > 3e-3) {
                // Emissivity follows the disk's own radiative profile, so the
                // halo has the same physical brightness scale as the disk
                // surface and falls off like r^-3 far out.
                float Tc = max(diskTemperature(cyl), 1.0);
                float w = Tc / uTempRef;
                // Taper to exactly zero at the outer edge.  A hard cutoff on r
                // shows up as a sharp-edged slab when the disk is seen edge on.
                float taper = smoothstep(uDiskOut * 1.10, uDiskOut * 0.80, cyl);
                float j = vert * taper * w * w * w * w;
                if (j > 1e-5) {
                    vec3 u = diskFourVelocity(cyl);
                    float g = redshift(y, u);
                    if (uTurb > 0.0) {
                        float Om = 1.0 / (pow(cyl, 1.5) + gA);
                        float lab = y.ph - Om * (uTimeCoord + y.t);
                        vec3 q = vec3(cos(lab), sin(lab), 0.0) * (2.0 + log(cyl)) + vec3(0.0, 0.0, z * 0.5);
                        j *= mix(1.0, 0.3 + 1.7 * fbm(q * 2.1), clamp(uTurb, 0.0, 1.0));
                    }
                    // Bolometric optically thin transfer: dI = g^4 j dlambda,
                    // with lambda normalised by k.u_camera = -1.
                    float g4 = g * g * g * g;
                    accum += thermalColor(uCoronaTemp * g)
                           * (g4 * j * abs(h) * uCoronaBright * 0.06);
                }
            }
        }

        y = ynew;
        maxH = max(maxH, abs(hamiltonian(y)));

        if (y.r < rStop) { captured = true; break; }
        if (y.r > uRFar) { escaped = true; break; }

        // grow the step for the next iteration
        h *= min(4.0, 0.9 * pow(max(e, 1e-4), -0.2));
    }

    // ---- shade ----
    vec3 col = accum;

    if (escaped) {
        // Direction of travel of the back-traced ray = direction of the source
        // on the sky.
        St d = deriv(y);
        vec3 vsph = vec3(-d.r, -d.th, -d.ph);   // steps advance along -deriv (h < 0)
        float s = sin(y.th), c = cos(y.th), sp = sin(y.ph), cp = cos(y.ph);
        vec3 Er = vec3(s * cp, s * sp, c);
        vec3 Et = vec3(c * cp, c * sp, -s);
        vec3 Ep = vec3(-sp, cp, 0.0);
        vec3 v = Er * vsph.x + Et * (y.r * vsph.y) + Ep * (y.r * s * vsph.z);
        vec3 skyDir = normalize(v);

        float gbg = 1.0 / max(gE, 1e-6);        // gravitational shift of the sky
        float foot = clamp(length(fwidth(skyDir)), 1e-4, 0.02);

        col += stars(skyDir, gbg, foot);
        col += nebula(skyDir, gbg);
        col += skyGrid(skyDir, foot) * (gbg * gbg * gbg * gbg);
    }

    // ---- debug overlays ----
    if (uDebugView == 1) {
        float f = float(steps) / float(uMaxSteps);
        col = vec3(f, f * f, 1.0 - f) * 1.2;
    } else if (uDebugView == 2) {
        float e = log(max(maxH, 1e-12) / max(gE * gE, 1e-9) + 1e-12) / log(10.0);
        float f = clamp((e + 9.0) / 9.0, 0.0, 1.0);
        col = vec3(f, 1.0 - abs(f - 0.5) * 2.0, 1.0 - f);
    } else if (uDebugView == 3) {
        if (captured && accum != vec3(0.0)) {
            float f = clamp((hitG - 0.4) / 1.6, 0.0, 1.0);
            col = vec3(f, 0.35, 1.0 - f) * 1.4;
        } else {
            col *= 0.15;
        }
    }

    fragColor = vec4(col * uExposure, 1.0);
}
