#version 330 core
// ===========================================================================
//  BLACK HOLE RAY TRACER  --  Schwarzschild null geodesics, GPU ray traced
// ---------------------------------------------------------------------------
//  Physics (geometrized units G = c = 1, lengths measured in Schwarzschild
//  radii, so RS = 1 and the mass is M = RS/2 = 0.5):
//
//   * Null geodesics of the Schwarzschild metric integrated in Cartesian
//     coordinates obey the exact second order system
//
//         d2x/dlam^2  = -(3/2) * h^2 * x / r^5 ,     h = x  x  dx/dlam
//
//     (derived from the geodesic equation; h is the conserved specific
//     angular momentum, r = |x|).  Integrated here with fixed structure
//     RK4 and an adaptive step size proportional to r.
//
//   * Conserved photon energy  E^2 = |v|^2 - RS*h^2/r^3   (|v| = E at infty).
//
//   * Accretion disk: geometrically thin Keplerian disk between the ISCO
//     (6M = 3 RS) and R_DISK.  At each equatorial crossing the ray is shaded
//         T(r)  = T_ISCO * (R_ISCO/r)^{3/4}                     (thin disk)
//         F(r)      ~ (1 - sqrt(R_ISCO/r)) / r^3                (flux/area)
//     with the full gravitational + Doppler redshift
//         g = u^t - (p_phi * Omega) / E ,
//         u^t = sqrt((r+M)/(r-RS)),  Omega = sqrt(M/r^3)
//     so T_obs = g*T and bolometric intensity ~ g^4 (relativistic beaming).
//     Up to 8 crossings per ray reproduce the photon ring halo.
//
//   * Spacetime grid: the ray itself is traced backwards in time, so the
//     coordinate lattice we see is *gravitationally lensed*.  Rays that
//     cross the event horizon terminate black -> the grid appears to be
//     swallowed by a dark "trapdoor", with lines wrapping around the shadow
//     (Einstein-ring behaviour of the lattice itself).
//
//   * Scene 3 draws the Flamm paraboloid w = 2*sqrt(RS(r-RS)), the exact
//     isometric embedding of the Schwarzschild spatial slice -- the classic
//     "trapdoor / funnel" picture of curved space.
// ===========================================================================

precision highp float;

out vec4 outColor;

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform vec2  uJitter;        // sub-pixel jitter, in pixels
uniform float uTime;          // animation time (disk phase)
uniform int   uMaxSteps;
uniform float uStepScale;
uniform int   uLayers;        // bit0 stars, bit1 disk, bit2 grid
uniform int   uScene;         // 0 full, 1 grid, 2 lensing only, 3 funnel
uniform float uGridSpacing;
uniform float uGridGain;     // grid brightness
uniform float uGridFade;     // distance falloff of grid lines
uniform float uTimeScale;

// ----------------------------- constants -----------------------------------
const float RS     = 1.0;        // Schwarzschild radius (our length unit)
const float M      = 0.5;        // geometrized black hole mass
const float R_ISCO = 3.0;        // innermost stable circular orbit = 6M = 3RS
const float R_DISK = 13.0;       // outer edge of the accretion disk (in RS)
const float T_ISCO = 9500.0;     // blackbody temperature at ISCO [K]
const float R_ESC  = 45.0;       // "escaped to infinity" radius
const float R_PHOT = 1.5;        // photon sphere = 1.5 RS
const float PI     = 3.141592653589793;

// ---------------------------------------------------------------------------
//  hashes / value noise
// ---------------------------------------------------------------------------
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float hash13(vec3 p3)
{
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec3 x)
{
    vec3 i = floor(x);
    vec3 f = x - i;
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
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

float fbm(vec3 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++)
    {
        s += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return s;
}

// ---------------------------------------------------------------------------
//  blackbody colour (Planck locus, Tanner-Helland style fit) -> linear RGB
// ---------------------------------------------------------------------------
vec3 blackbodyRGB(float K)
{
    float t = clamp(K, 800.0, 45000.0) / 100.0;
    float r, g, b;
    if (t <= 66.0) r = 1.0;
    else r = clamp(329.698727446 * pow(max(t - 60.0, 1e-3), -0.1332047592) / 255.0, 0.0, 1.0);
    if (t <= 66.0) g = clamp((99.4708025861 * log(max(t, 1.0)) - 161.1195681661) / 255.0, 0.0, 1.0);
    else           g = clamp(288.1221695283 * pow(max(t - 60.0, 1e-3), -0.0755148492) / 255.0, 0.0, 1.0);
    if (t >= 66.0)      b = 1.0;
    else if (t <= 19.0) b = 0.0;
    else                b = clamp((138.5177312231 * log(max(t - 10.0, 1e-3)) - 305.0447927307) / 255.0, 0.0, 1.0);
    return pow(vec3(r, g, b), vec3(2.2));   // sRGB -> linear
}

// ---------------------------------------------------------------------------
//  background sky: procedural stars + faint nebula.  Sampled with the ray's
//  *final* direction, so gravitational lensing Einstein-rings it for free.
// ---------------------------------------------------------------------------
vec2 cubeUV(vec3 d)
{
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.zy / a.x;
    if (a.y >= a.z)               return d.xz / a.y;
    return d.xy / a.z;
}

vec3 starLayer(vec2 uv, float scale, float thresh, float widthK)
{
    vec2 c = uv * scale;
    vec2 cell = floor(c);
    vec2 f = c - cell;
    vec3 acc = vec3(0.0);
    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            vec2 o = vec2(float(i), float(j));
            float h1 = hash12(cell + o);
            if (h1 > thresh) continue;                    // empty cell
            vec2 h2 = hash22(cell + o + vec2(17.13, 3.71));   // position in cell
            vec3 h3 = vec3(hash12(cell + o + 41.7),            // magnitude
                           hash12(cell + o + 71.3),            // colour temp
                           hash12(cell + o + 13.9));           // whiteness
            vec2 sp = o + h2 - f;                         // offset in cell units
            float d2 = dot(sp, sp);
            float mag = pow(h3.x, 4.0);
            vec3 tint = mix(vec3(1.00, 0.68, 0.42), vec3(0.60, 0.76, 1.00), h3.y);
            tint = mix(tint, vec3(1.0), h3.z * 0.35);
            acc += tint * mag * exp(-d2 * widthK);
        }
    }
    return acc;
}

vec3 nebula(vec3 d)
{
    float n = fbm(d * 2.6 + 3.7) * 0.65 + fbm(d * 6.1) * 0.35;
    n = smoothstep(0.30, 0.78, n);
    vec3 col = mix(vec3(0.012, 0.018, 0.055), vec3(0.04, 0.055, 0.13), n);
    col += vec3(0.055, 0.022, 0.085) * pow(n, 2.0);
    return col;
}

vec3 background(vec3 dirv)
{
    vec2 uv = cubeUV(dirv);
    float stretch = length(fwidth(dirv));      // screen-derivative: anti-alias when lensed hard
    float widen = 1.0 / (1.0 + stretch * 240.0);
    vec3 c = starLayer(uv,  24.0, 0.80,  170.0 * widen) * 1.8
           + starLayer(uv,  75.0, 0.93,  380.0 * widen) * 1.2
           + starLayer(uv, 260.0, 0.98,  850.0 * widen) * 0.6;
    return c + nebula(dirv) * 0.35;
}

// ---------------------------------------------------------------------------
//  geodesic machinery
// ---------------------------------------------------------------------------
//  d2x/dlam^2 = -(3/2) h^2 x / r^5
vec3 gravAccel(vec3 p, vec3 v)
{
    vec3 h = cross(p, v);
    float r2 = dot(p, p);
    float k = -1.5 * dot(h, h) / (r2 * r2 * sqrt(r2));
    return p * k;
}

// ---------------------------------------------------------------------------
//  accretion disk shading at an equatorial-plane crossing
// ---------------------------------------------------------------------------
vec4 diskSample(vec3 p, vec3 v, float E)
{
    float r = length(p);

    // photon azimuthal momentum (signed):  p_phi = -(x x v)_y
    float Lph = -cross(p, v).y;
    float Om  = sqrt(M / (r * r * r));            // Keplerian angular velocity
    float ut  = sqrt((r + M) / (r - RS));         // |u^t| of circular disk matter
    // g = nu_obs / nu_emit = (p.u)_obs / (p.u)_emit
    //   = (-E) / (-E*u^t + p_phi*Omega)  =  E / (E*u^t - p_phi*Omega)
    float g = clamp(E / (E * ut - Lph * Om), 0.02, 2.5);   // includes grav. + Doppler shift

    // thin-disk temperature profile, truncated at ISCO
    float T = T_ISCO * pow(R_ISCO / r, 0.75);

    // mild turbulent / spiral structure differentially sheared by Ω(r)
    float phi = atan(p.x, p.z);                   // matches (-z,0,x)/r azimuth
    float pat = phi + 2.2 * log(r) + sqrt(M / (r * r * r)) * uTimeScale * uTime * 3.0;
    float turb = fbm(vec3(cos(pat), sin(pat), 0.0) * 3.4 + vec3(0.0, 0.0, log(r) * 2.6));
    turb = mix(0.82, 1.18, turb);

    // Shakura-Sunyaev flux per unit area (zero-torque inner boundary).
    // The exponent is softened slightly from the exact r^-3 bolometric law so
    // that the outer disk remains visible on screen (exposure gradient only;
    // the T(r) profile and ISCO cut-off are exact).
    float flux = (1.0 - sqrt(R_ISCO / r)) / pow(r / R_ISCO, 2.0);

    // observed bolometric intensity scales as g^4; colour shifts to g*T
    vec3 col = blackbodyRGB(T * g) * flux * pow(g, 4.0) * 26.0 * turb;

    // soft radial edges
    float a = smoothstep(R_ISCO - 0.25, R_ISCO + 0.55, r)
            * (1.0 - smoothstep(R_DISK - 2.5, R_DISK + 0.2, r));
    return vec4(col, 0.94 * a);
}

// ---------------------------------------------------------------------------
//  spacetime grid: exact distance from the ray SEGMENT to the coordinate
//  lattice (lines parallel to each axis), evaluated over every step so the
//  lines render continuous.  The ray is the geodesic itself, so the lattice
//  we see is *lensed* by the black hole.
// ---------------------------------------------------------------------------
vec3 gridLines(vec3 a, vec3 b, vec3 camPos)
{
    float G = uGridSpacing;
    float pixelAngle = 2.0 * uTanHalfFov / uResolution.y;
    vec3 acc = vec3(0.0);
    vec3 d3 = b - a;

    for (int ax = 0; ax < 3; ax++)
    {
        int o1 = (ax + 1) % 3;
        int o2 = (ax + 2) % 3;
        vec2 d  = vec2(d3[o1], d3[o2]);            // transverse motion over the step
        vec2 ao = vec2(a[o1], a[o2]);
        vec2 m  = ao + d * 0.5;
        float dd = max(dot(d, d), 1e-12);

        // nearest lattice lines (in the transverse plane) around the segment
        int j0 = int(floor(m.x / G + 0.5));
        int k0 = int(floor(m.y / G + 0.5));
        float bestD = 1e9, bestT = 0.5;
        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                vec2 L = vec2(float(j0 + i), float(k0 + j)) * G;
                vec2 v = ao - L;
                float t = clamp(-dot(v, d) / dd, 0.0, 1.0);
                float dist = dot(v + t * d, v + t * d);
                if (dist < bestD) { bestD = dist; bestT = t; }
            }
        }
        bestD = sqrt(bestD);

        vec3 p = mix(a, b, bestT);
        float rcam = length(p - camPos);
        float w = max(rcam * pixelAngle * 1.1, G * 0.0015);   // ~1 pixel world width
        float line = 1.0 - smoothstep(w * 0.5, w * 1.6, bestD);
        if (line <= 0.002) continue;

        float rr = length(p);
        if (rr < RS * 1.02) continue;                        // swallowed by the hole

        // fade lines whose cells have shrunk below a few pixels (anti-alias)
        float cellAngular = G / max(rcam, RS * 1.02);
        float aaFade = smoothstep(1.2 * pixelAngle, 3.0 * pixelAngle, cellAngular);
        float distFade = exp(-rcam * uGridFade);

        float tau = sqrt(clamp(1.0 - RS / rr, 0.0, 1.0));    // dtau/dt (time dilation)
        vec3 tint = mix(vec3(1.00, 0.42, 0.18), vec3(0.24, 0.66, 1.00), tau);
        acc += tint * line * (0.10 + 0.90 * tau) * aaFade * distFade * 0.9 * uGridGain;
    }
    return acc;
}

// ---------------------------------------------------------------------------
//  scene 3 : Flamm paraboloid  w(r) = 2 sqrt(RS (r - RS))
//  the exact isometric embedding of the t=const, equatorial Schwarzschild
//  slice -- the "trapdoor in spacetime".
// ---------------------------------------------------------------------------
float parabHeight(float R)
{
    return 2.0 * sqrt(max(RS * (R - RS), 0.0));
}

vec3 renderFunnel(vec3 ro, vec3 rd)
{
    float f0 = ro.y - parabHeight(length(ro.xz));
    float t = 1.2;
    float prevF = (ro + rd * t).y - parabHeight(length((ro + rd * t).xz));
    float dt = 0.3;
    float tHit = -1.0;
    float dHit = dt;

    for (int i = 0; i < 260; i++)
    {
        vec3 p = ro + rd * t;
        float R = length(p.xz);
        float f = p.y - parabHeight(R);
        if (prevF * f <= 0.0 && i > 0) { tHit = t; break; }
        prevF = f;
        dHit = clamp(0.10 * R + 0.20, 0.12, 4.0);
        dt = dHit;
        t += dt;
        if (t > 320.0) break;
    }
    if (tHit < 0.0)
    {
        // nothing hit: deep space with a faint starfield for orientation
        vec2 uv = cubeUV(rd);
        return starLayer(uv, 34.0, 0.35, 900.0) * 0.35 + vec3(0.005);
    }

    // bisection refine
    float ta = tHit - dt, tb = tHit;
    float fa = (ro + rd * ta).y - parabHeight(length((ro + rd * ta).xz));
    for (int k = 0; k < 12; k++)
    {
        float tm = 0.5 * (ta + tb);
        vec3 pm = ro + rd * tm;
        float f = pm.y - parabHeight(length(pm.xz));
        if (fa * f <= 0.0) tb = tm; else { ta = tm; fa = f; }
    }
    float th = 0.5 * (ta + tb);
    vec3 p = ro + rd * th;

    float R = length(p.xz);
    if (R < RS * 1.005) return vec3(0.0);            // inside the event horizon
    float phi = atan(p.x, p.z);

    // surface normal (analytic derivative)
    float fp = sqrt(RS / max(R - RS, 1e-3));
    vec3 n = normalize(vec3(fp * p.x / R, 1.0, fp * p.z / R));
    vec3 v = -rd;

    vec3 key = normalize(vec3(0.35, 0.80, 0.50));
    float diff = clamp(dot(n, key), 0.0, 1.0);
    float facing = clamp(dot(n, v), 0.0, 1.0);
    float rim = pow(1.0 - facing, 3.0);

    float tau = sqrt(clamp(1.0 - RS / R, 0.0, 1.0));       // dτ/dt
    vec3 base = mix(vec3(0.055, 0.075, 0.13), vec3(0.10, 0.12, 0.21), smoothstep(1.0, 22.0, R));
    vec3 col = base * (0.22 + 0.78 * diff) + rim * vec3(0.08, 0.12, 0.22);
    col *= mix(vec3(2.1, 0.62, 0.38), vec3(0.80, 0.92, 1.12), tau);   // redshift tint near throat

    // coordinate lattice drawn on the surface
    float dwR = fwidth(R) * 1.4 + 0.006;
    float dR  = abs(fract(R + 0.5) - 0.5);
    float circleLine = 1.0 - smoothstep(0.0, dwR * 1.4, dR);
    float dwP = fwidth(phi / (PI / 12.0)) * 1.4 + 0.02;
    float dP  = abs(fract(phi / (PI / 12.0) + 0.5) - 0.5);
    float spokeLine = 1.0 - smoothstep(0.0, dwP * 1.4, dP);
    vec3 lineCol = mix(vec3(1.4, 0.85, 0.45), vec3(0.40, 0.85, 1.30), tau);
    col += (circleLine * 0.85 + spokeLine * 0.55) * lineCol * (0.35 + 0.65 * facing);

    // throat glow + distance fade
    col += vec3(1.5, 0.55, 0.22) * exp(-max(R - RS, 0.0) * 1.6) * 0.9 * (0.4 + 0.6 * facing);
    col *= exp(-th * 0.0035);
    return col;
}

// ---------------------------------------------------------------------------
//  main: build the camera ray and trace it
// ---------------------------------------------------------------------------
void main()
{
    vec2 ndc = (gl_FragCoord.xy + uJitter) / uResolution * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 rd = normalize(uCamFwd + uTanHalfFov * (ndc.x * aspect * uCamRight + ndc.y * uCamUp));

    if (uScene == 3)
    {
        outColor = vec4(renderFunnel(uCamPos, rd), 1.0);
        return;
    }

    bool wantStars = (uLayers & 1) != 0;
    bool wantDisk  = (uLayers & 2) != 0;
    bool wantGrid  = (uLayers & 4) != 0;

    vec3 pos = uCamPos;
    vec3 vel = rd;

    // conserved photon energy:  E^2 = |v|^2 - RS h^2 / r^3
    vec3 h0 = cross(pos, vel);
    float E2 = dot(vel, vel) - RS * dot(h0, h0) / pow(dot(pos, pos), 1.5);
    if (E2 <= 1e-5) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }  // critical ray
    float E = sqrt(E2);

    vec3 col = vec3(0.0);
    vec3 gridAdd = vec3(0.0);
    float alpha = 0.0;          // opacity accumulated from the disk
    int crossings = 0;
    vec3 prev = pos;
    float prevY = pos.y;

    for (int i = 0; i < uMaxSteps; i++)
    {
        float r = length(pos);
        float dt = clamp(0.055 * r, 0.006, 0.5) * uStepScale;

        // ---- RK4 on the second-order geodesic system ---------------------
        vec3 k1p = vel;
        vec3 k1v = gravAccel(pos, vel);
        vec3 k2p = vel + 0.5 * dt * k1v;
        vec3 k2v = gravAccel(pos + 0.5 * dt * k1p, vel + 0.5 * dt * k1v);
        vec3 k3p = vel + 0.5 * dt * k2v;
        vec3 k3v = gravAccel(pos + 0.5 * dt * k2p, vel + 0.5 * dt * k2v);
        vec3 k4p = vel + dt * k3v;
        vec3 k4v = gravAccel(pos + dt * k3p, vel + dt * k3v);

        vec3 posN = pos + dt * (k1p + 2.0 * k2p + 2.0 * k3p + k4p) / 6.0;
        vec3 velN = vel + dt * (k1v + 2.0 * k2v + 2.0 * k3v + k4v) / 6.0;

        // ---- accretion disk (equatorial crossings) ------------------------
        if (wantDisk && crossings < 8 && prevY * posN.y < 0.0)
        {
            float tc = prevY / (prevY - posN.y);
            vec3 cp = mix(prev, posN, tc);
            vec3 cv = mix(vel, velN, tc);
            float rc = length(cp);
            if (rc >= R_ISCO - 0.05 && rc <= R_DISK + 0.2)
            {
                vec4 dc = diskSample(cp, cv, E);
                col += (1.0 - alpha) * dc.rgb;
                alpha += (1.0 - alpha) * dc.a;
                crossings++;
            }
        }

        // ---- spacetime grid ----------------------------------------------
        if (wantGrid) gridAdd += gridLines(prev, posN, uCamPos);

        // ---- termination conditions --------------------------------------
        float rN = length(posN);
        if (rN <= RS) { alpha = 1.0; break; }                              // horizon
        if (rN < R_PHOT && dot(posN, velN) < 0.0) { alpha = 1.0; break; }  // spiral-in
        if (rN > R_ESC && dot(posN, velN) > 0.0)
        {
            if (wantStars) col += (1.0 - alpha) * background(normalize(velN));
            alpha = 1.0;
            break;
        }
        if (alpha > 0.985) break;

        prev = posN;
        prevY = posN.y;
        pos = posN;
        vel = velN;
    }

    col += gridAdd * (1.0 - 0.7 * alpha);
    outColor = vec4(max(col, vec3(0.0)), 1.0);
}
