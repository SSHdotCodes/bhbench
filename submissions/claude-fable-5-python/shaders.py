"""Vulkan-dialect GLSL shaders for the black hole renderer + SPIR-V compilation.

Shaders are compiled with glslc (Homebrew `shaderc`) and cached in .spv_cache/
keyed by a hash of the source, so glslc only runs when a shader changes.
"""

import hashlib
import pathlib
import shutil
import subprocess

# ----------------------------------------------------------------------------
# Fullscreen triangle vertex shader (no vertex buffers; uses gl_VertexIndex)
# ----------------------------------------------------------------------------

VERT_FULLSCREEN = """
#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
"""

# ----------------------------------------------------------------------------
# Shared procedural library: noise, blackbody, sky and disk-noise generators.
# The heavy sky/disk-noise functions run ONCE at startup in the bake shaders
# (sky -> cubemap, disk noise -> wrapping 2D texture); the per-frame scene
# shader just samples the textures.

GLSL_COMMON = """
#define PI  3.14159265359
#define TAU 6.28318530718

// ---- hashing / noise -------------------------------------------------------
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i),              hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i),               hash13(i + vec3(1,0,0)), u.x),
                   mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), u.x), u.y),
               mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), u.x),
                   mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), u.x), u.y), u.z);
}
float fbm2(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) { s += a * vnoise2(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
float fbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a * vnoise3(p); p = p * 2.07 + 11.5; a *= 0.5; }
    return s;
}

// ---- blackbody (Tanner Helland fit), T in Kelvin ----------------------------
vec3 blackbody(float t) {
    t = clamp(t, 1000.0, 40000.0) / 100.0;
    vec3 c;
    if (t <= 66.0) {
        c.r = 1.0;
        c.g = (99.4708025861 * log(t) - 161.1195681661) / 255.0;
        c.b = t <= 19.0 ? 0.0
              : (138.5177312231 * log(t - 10.0) - 305.0447927307) / 255.0;
    } else {
        c.r = 329.698727446  * pow(t - 60.0, -0.1332047592) / 255.0;
        c.g = 288.1221695283 * pow(t - 60.0, -0.0755148492) / 255.0;
        c.b = 1.0;
    }
    // the fit is in gamma space; convert to linear for HDR math
    return pow(clamp(c, 0.0, 1.0), vec3(2.2));
}

// ---- sky: starfield + milky way + nebula (gravitationally lensed) -----------
vec3 starLayer(vec3 d, float scale, float boost) {
    vec3 q = d * scale;
    vec3 cell = floor(q);
    vec3 h = hash33(cell);
    if (h.z < 0.82) return vec3(0.0);
    vec3 sp = cell + mix(vec3(0.2), vec3(0.8), h);
    float dist = length(q - sp);
    float bri = pow(h.x, 9.0) * 22.0 + pow(h.x, 3.0) * 0.35;
    float core = pow(max(0.0, 1.0 - dist * 2.4), 14.0);
    float temp = mix(2600.0, 15000.0, h.y * h.y);
    return blackbody(temp) * (bri * core * boost);
}
float mwBand(vec3 d) {
    vec3 nMW = normalize(vec3(0.28, 1.0, 0.16));
    return exp(-pow(dot(d, nMW) * 3.6, 2.0));
}
// smooth sky (nebulae + milky way): 16 fbm octaves/dir — baked to a cubemap
// at startup, sampled per pixel afterwards
vec3 skyNebula(vec3 d) {
    float band = mwBand(d);

    vec3 col = vec3(0.0);
    float n1 = fbm3(d * 2.6);
    float n2 = fbm3(d * 5.2 + 7.7);
    col += vec3(0.016, 0.022, 0.045) * pow(n1, 2.2) * 0.9;
    col += vec3(0.045, 0.020, 0.036) * pow(n2, 3.0) * 0.7;

    float wisp = fbm3(d * 4.0 + 3.1);
    float dust = fbm3(d * 7.5 - 2.0);
    vec3 mw = vec3(0.070, 0.060, 0.053) * band * (0.35 + 0.9 * wisp);
    mw *= 1.0 - 0.75 * smoothstep(0.35, 0.75, dust) * band;
    col += mw;
    return col;
}
// point stars stay analytic so they remain subpixel-crisp at any resolution
vec3 skyStars(vec3 d) {
    return starLayer(d, 36.0, 1.0)
         + starLayer(d + 11.7, 74.0, 0.55)
         + starLayer(d - 5.3, 150.0, 0.28 + mwBand(d) * 0.6);
}

// ---- accretion disk ---------------------------------------------------------
// seam-free wrap helper: blend two samples 2*PI apart across the wrap
float wfbm(float x, float phi, float w, float fr, float off) {
    return mix(fbm2(vec2(x + off, phi * fr)),
               fbm2(vec2(x + off, (phi - TAU) * fr)), w);
}
// four independent plasma fields, baked into the RGBA noise texture:
//   x base streaks   y fine filaments (ridged)   z large clumps   w alt layer
vec4 diskNoise(float r, float phi) {
    float x = log(r) * 6.5;
    float w = (phi + PI) / TAU;
    float base = wfbm(x, phi, w, 1.15, 0.0) * 0.72
               + wfbm(x * 2.2, phi, w, 2.1, 31.0) * 0.28;
    float fil = wfbm(x * 3.4, phi, w, 3.1, 7.0);
    fil = 1.0 - abs(2.0 * fil - 1.0);            // ridged: bright filaments
    float clump = wfbm(x * 0.55, phi, w, 0.55, 51.0);
    float alt = wfbm(x * 1.6, phi + 2.2, w, 1.5, -19.0);
    return vec4(base, fil * fil, clump, alt);
}
"""

# ----------------------------------------------------------------------------
# Bake shaders: run once at startup. The nebula sky is rendered into a small
# cubemap and the advected disk noise into a wrapping (log r, phi) texture,
# turning ~200 hash evaluations per pixel per frame into two texture fetches.
# Domain of the noise texture: u = (phi+pi)/tau (wraps), v = ln(r)/ln(30).
# ----------------------------------------------------------------------------

DISK_NOISE_LOG_RMAX = 3.4011974       # ln(30); GLSL constants below must match

FRAG_BAKESKY = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(push_constant) uniform PC { vec4 p; } pc;   // x = cube face index
""" + GLSL_COMMON + """
void main() {
    vec2 st = v_uv * 2.0 - 1.0;
    int face = int(pc.p.x + 0.5);
    vec3 d;                            // Vulkan/GL cube face conventions
    if      (face == 0) d = vec3( 1.0, -st.y, -st.x);
    else if (face == 1) d = vec3(-1.0, -st.y,  st.x);
    else if (face == 2) d = vec3( st.x,  1.0,  st.y);
    else if (face == 3) d = vec3( st.x, -1.0, -st.y);
    else if (face == 4) d = vec3( st.x, -st.y,  1.0);
    else                d = vec3(-st.x, -st.y, -1.0);
    fragColor = vec4(skyNebula(normalize(d)), 1.0);
}
"""

FRAG_BAKEDISK = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
""" + GLSL_COMMON + """
void main() {
    float phi = v_uv.x * TAU - PI;
    float r = exp(v_uv.y * 3.4011974);            // r in [1, 30]
    fragColor = diskNoise(r, phi);
}
"""

# ----------------------------------------------------------------------------
# Scene: general-relativistic ray marcher — Kerr spacetime, Kerr-Schild
# Cartesian coordinates, Hamiltonian null geodesics (units: M = 1)
# ----------------------------------------------------------------------------

FRAG_SCENE = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform samplerCube u_sky;    // baked nebula
layout(set = 0, binding = 1) uniform sampler2D u_noise;    // baked disk noise

layout(push_constant) uniform PC {
    vec4 cam0, cam1, cam2;    // xyz: camera basis; w: camera f, k.x, k.y
    vec4 campos_time;         // xyz camera position, w sim time
    vec4 res_fov_steps;       // x aspect, y camera k.z, z tan(fov/2), w steps
    vec4 disk;                // rin (ISCO), rout, novikov-thorne peak, T_max
    vec4 misc;                // disk gain, doppler on, glow on, escape radius
    vec4 kerr;                // spin a, disk time scale, horizon r, photon sphere r
} pc;
""" + GLSL_COMMON + """

#define NOISE_V 0.2940146            // 1/ln(30): disk r -> noise texture v
#define DISK_SLACK 1.6               // slab shading margin inside the ISCO
#define DISK_OSLACK 3.5              // slab shading margin beyond the outer rim
#define WIND_T 48.0                  // advection loop period (rot-time units)

// disk half-thickness: flares outward, with a puffed rim just past the ISCO
float diskH(float r) {
    return 0.05 * r + 0.014 * r * r / 26.0
         + 0.85 * exp(-pow((r - pc.disk.x - 1.2) * 0.60, 2.0));
}

// explicit mip level for the baked noise: derivative-based LOD is undefined
// in the divergent crossing branches, so compute it from the pixel footprint
// at the hit (pixAng = radians subtended by one pixel). `stretch` accounts
// for grazing incidence: the projected radial footprint grows as 1/|dz|,
// and ignoring that renders as scratchy radial aliasing near the limbs.
float noiseLod(float r, float rho, float phi, float pixAng, float stretch) {
    vec3 hp = vec3(rho * cos(phi), rho * sin(phi), 0.0);
    float foot = length(pc.campos_time.xzy - hp) * pixAng;   // world units/px
    float tu = foot * (2048.0 / (TAU * max(rho, 1.5)));      // texels/px in u
    float footR = foot * stretch;                            // radial world/px
    float tv = footR * (2048.0 * NOISE_V / max(r, 1.5));
    // radial shear of the advected pattern (bounded by WIND_T/2)
    float om = 1.0 / (r * sqrt(r) + pc.kerr.x);
    tu = max(tu, footR * 1.5 * sqrt(r) * om * om * (0.5 * WIND_T)
                 * (2048.0 / TAU));
    float l = log2(max(max(tu, tv), 1.0));
    // trilinear is an imperfect low-pass: bias deeper once minifying or the
    // last near-Nyquist octave shimmers as 1px lines
    return min(l + min(l, 0.75), 8.0);
}

// Differentially advected disk noise with bounded winding: a raw
// phi - omega(r)*t phase shears into ever-tighter spirals (radial frequency
// grows without bound, aliasing into scratchy lines and, over minutes,
// mush). Standard flow-map trick: two layers advecting at the local Kerr
// rate whose phases reset every WIND_T while their crossfade weight is
// zero, so |phase| <= WIND_T/2 forever — and the plasma visibly churns.
vec4 sampleDiskNoise(float phi, float om, float rotT, float v, float lod) {
    float f0 = fract(rotT / WIND_T);
    float f1 = fract(f0 + 0.5);
    float w0 = 1.0 - abs(2.0 * f0 - 1.0);
    vec4 nA = textureLod(u_noise,
        vec2((phi - om * (f0 - 0.5) * WIND_T + PI) / TAU, v), lod);
    vec4 nB = textureLod(u_noise,
        vec2((phi - om * (f1 - 0.5) * WIND_T + PI) / TAU, v), lod);
    return mix(nB, nA, w0);
}

// exact Kerr redshift for a prograde circular emitter:
//   g = 1 / [u^t (1 - Omega * L_photon)],  u^t from the circular geodesic
float diskG(float r, float sr, float omega, float Lphoton) {
    float r2 = r * r;
    float ut = (r * sr + pc.kerr.x)
             / sqrt(max(r2 * r - 3.0 * r2 + 2.0 * pc.kerr.x * r * sr, 1e-6));
    float g = (pc.misc.y > 0.5) ? 1.0 / (ut * (1.0 - omega * Lphoton))
                                : 1.0 / ut;
    return clamp(g, 0.05, 3.0);
}

// volumetric slab emission for the two primary disk images: K samples along
// the photon's chord through a flared, clumpy slab. Grazing rays see a longer
// path (limb thickening/brightening); z-dependent structure gives parallax.
// (r0, phi0) is the equatorial crossing; (dz, dr) the unit chord components.
vec4 diskEmissionVol(float r0, float phi0, float dz, float dr, float Lphoton,
                     float pixAng, float lodExtra) {
    float a = pc.kerr.x;
    float rin = pc.disk.x, rout = pc.disk.y;
    if (r0 < rin - DISK_SLACK || r0 > rout + DISK_OSLACK) return vec4(0.0);

    float sr0 = sqrt(r0);
    float omega0 = 1.0 / (r0 * sr0 + a);
    float g = diskG(r0, sr0, omega0, Lphoton);
    float g2 = g * g;
    float beam = g2 * g2;               // bolometric beaming I_obs = g^4 I_em

    // chord through the slab, decomposed at the crossing: vertical (dz),
    // radial (dr), azimuthal (sign of the conserved angular momentum)
    float dphi = sqrt(max(1.0 - dz * dz - dr * dr, 0.0))
               * (Lphoton < 0.0 ? -1.0 : 1.0);
    float H = diskH(r0);
    float invdz = 1.0 / max(abs(dz), 0.25);          // grazing path cap
    float lod = max(noiseLod(r0, sqrt(r0 * r0 + a * a), phi0, pixAng,
                             1.0 / max(abs(dz), 0.03)), lodExtra);
    // in the extreme-tangent sliver no finite mip level resolves the radial
    // compression; let the structure settle to its mean there
    float tangent = smoothstep(0.10, 0.035, abs(dz));
    const int K = 5;
    float tmax = min(1.30 * H * invdz, 3.5);         // bound edge-on chords
    float stp = 2.0 * tmax / float(K - 1);
    // soft window at the shading annulus boundary so crossings just past it
    // don't pop against accepted neighbours
    float window = 1.0 - smoothstep(rout + DISK_OSLACK - 1.5,
                                    rout + DISK_OSLACK, r0);
    float rotT = pc.campos_time.w * pc.kerr.y;

    // per-crossing fields: the slow-orbiting clump pattern is large-scale, so
    // one (two-layer) sample at the crossing sets the local surface height
    // and outer edge
    vec4 nB = sampleDiskNoise(phi0, omega0 * 0.6, rotT,
                              log(r0) * NOISE_V, lod + 1.0);
    float Hj = max(H * (0.45 + 0.95 * nB.z), 1e-3);
    float outerEdge = rout * (0.62 + 0.16 * nB.z);

    vec3 col = vec3(0.0);
    float T = 1.0;
    for (int j = 0; j < K; j++) {
        float tj = -tmax + stp * float(j);           // front to back
        // clamp the radial excursion: the streak field varies fast in r, so
        // unbounded grazing chords would smear it into fuzz at the limbs
        float rj = max(r0 + clamp(tj * dr, -1.25, 1.25), 1.03);
        float zj = tj * dz;
        float srj = sqrt(rj);
        float omj = 1.0 / (rj * srj + a);            // differential rotation
        float hv = zj / Hj;
        // vertical shear: the slab spirals through its own thickness, so a
        // grazing chord threads genuinely different plasma top vs. bottom —
        // a fixed (time-independent, so alias-free) twist that reads as 3D
        float phij = phi0 + tj * dphi / max(rj, 2.0) + hv * 0.42;
        vec4 nA = sampleDiskNoise(phij, omj, rotT, log(rj) * NOISE_V, lod);

        // internal layers change with height so the top and bottom of the
        // slab genuinely differ (parallax); the alt channel is the layer field
        float lay = clamp(0.5 + 0.5 * hv, 0.0, 1.0);
        float streak = smoothstep(0.22, 0.88, nA.x);
        float fila = mix(nA.y, nA.w, lay);
        float structure = (0.16 + 0.84 * streak) * (0.40 + 0.60 * fila);
        structure = mix(structure, 0.40, tangent);
        // billowing: clumps rise and thin through the slab instead of a
        // smooth Gaussian puff — the volume churns as it advects
        float billow = 0.60 + 0.80 * mix(nA.z, nA.y, lay);

        float inner = smoothstep(rin, rin + 0.55, rj);
        float outer = 1.0 - smoothstep(outerEdge * 0.72, outerEdge, rj);
        float dens = exp(-hv * hv * 2.1) * inner * outer
                   * (0.38 + 0.62 * structure) * billow * window;
        float dtau = dens * stp * 2.8;               // optical depth/sample
        if (dtau < 1e-4) continue;

        // Novikov-Thorne profile at the sample radius; midplane runs hotter
        // than the photosphere
        float f = (1.0 - sqrt(rin / max(rj, rin))) / (rj * rj * rj);
        float fn = max(f / pc.disk.z, 0.0);
        float Tobs = clamp(pc.disk.w * sqrt(sqrt(fn)) * g
                           * (1.0 - 0.18 * min(abs(hv), 1.0)), 800.0, 39000.0);
        vec3 bb = blackbody(Tobs);
        float ov = rin / rj;
        float orbitV = clamp(ov * sqrt(ov), 0.0, 1.0);
        bb = mix(bb, pow(bb, vec3(0.80)), orbitV * 0.6);
        float inten = pow(fn, 0.84) * beam * pc.misc.x;
        inten *= 0.60 + 0.80 * structure;              // turbulent emissivity
        inten *= 1.0 + 1.5 * exp(-(rj - rin) * 0.9);   // hot rim at the ISCO
        inten *= 1.0 + 2.4 * orbitV;                   // fastest parts glow

        col += T * (1.0 - exp(-dtau)) * bb * inten;
        T *= exp(-dtau);
        if (T < 0.02) break;                          // opaque: stop early
    }
    return vec4(col, 1.0 - T);
}

// single-sample shading for the heavily damped higher-order ring images
vec4 diskEmissionThin(float r, float phi, float Lphoton, float pixAng,
                      float lodExtra) {
    float a = pc.kerr.x;
    float rin = pc.disk.x, rout = pc.disk.y;
    if (r < rin || r > rout) return vec4(0.0);

    float sr = sqrt(r);
    float omega = 1.0 / (r * sr + a);
    float lod = max(noiseLod(r, sqrt(r * r + a * a), phi, pixAng, 2.0),
                    lodExtra);
    vec4 n4 = sampleDiskNoise(phi, omega, pc.campos_time.w * pc.kerr.y,
                              log(r) * NOISE_V, lod);
    float n = n4.x;
    float streak = 0.18 + 0.82 * smoothstep(0.30, 0.80, n);

    float inner = smoothstep(rin, rin + 0.6, r);
    float outerEdge = rout * (0.62 + 0.16 * n4.z);
    float outer = 1.0 - smoothstep(outerEdge * 0.72, outerEdge, r);
    float alpha = clamp(streak * inner * outer, 0.0, 1.0);
    if (alpha < 0.004) return vec4(0.0);

    float f = (1.0 - sqrt(rin / r)) / (r * r * r);
    float fn = max(f / pc.disk.z, 0.0);
    float g = diskG(r, sr, omega, Lphoton);
    float Tobs = clamp(pc.disk.w * sqrt(sqrt(fn)) * g, 800.0, 39000.0);
    vec3 col = blackbody(Tobs);

    float orbitV = clamp(pow(rin / r, 1.5), 0.0, 1.0);
    col = mix(col, pow(col, vec3(0.80)), orbitV * 0.6);
    float g2 = g * g;
    float inten = pow(fn, 0.9) * (g2 * g2) * pc.misc.x;
    inten *= 0.30 + 0.70 * smoothstep(0.3, 0.9, n);
    inten *= 1.0 + 1.4 * exp(-(r - rin) * 0.9);
    inten *= 1.0 + 2.2 * orbitV;

    return vec4(col * inten, alpha);
}

// ---- Kerr null geodesics: Hamiltonian in Kerr-Schild Cartesian coordinates -
// Horizon-penetrating and regular everywhere except the ring singularity, so
// there is no polar-axis seam and no coordinate stall at the horizon.
//   2H = |p|^2 - 1 - f (1 + k.p)^2,   E = -p_t = 1
//   f = 2 r^3/(r^4 + a^2 z^2),  k = ((rx+ay)/(r^2+a^2), (ry-ax)/(r^2+a^2), z/r)
// KS z = spin axis = world +y (swizzled once at init).
float ksRadiusA2(vec3 q, float a2) {
    float b = dot(q, q) - a2;
    return sqrt(0.5 * (b + sqrt(b * b + 4.0 * a2 * q.z * q.z)));
}

float ksRadius(vec3 q, float a) {
    return ksRadiusA2(q, a * a);
}

void derivR(vec3 q, vec3 p, float a, float a2, float r, out vec3 dq, out vec3 dp) {
    float r2 = r * r;
    float D = r2 * r2 + a2 * q.z * q.z;
    float f = 2.0 * r * r2 / D;
    float b = r2 + a2;
    float nx = r * q.x + a * q.y;
    float ny = r * q.y - a * q.x;
    vec3 k = vec3(nx / b, ny / b, q.z / r);
    float kp = dot(k, p);
    float Q = 1.0 + kp;

    dq = p - f * Q * k;

    vec3 gr = r * (r2 * q + vec3(0.0, 0.0, a2 * q.z)) / D;
    float cf = (6.0 * r2 * D - 8.0 * r2 * r2 * r2) / (D * D);
    vec3 gf = cf * gr;
    gf.z -= 4.0 * a2 * q.z * r * r2 / (D * D);

    float tb = 2.0 * r / (b * b);
    vec3 gkp;
    gkp.x = p.x * ((q.x * gr.x + r) / b - nx * tb * gr.x)
          + p.y * ((q.y * gr.x - a) / b - ny * tb * gr.x)
          + p.z * (-q.z * gr.x / r2);
    gkp.y = p.x * ((q.x * gr.y + a) / b - nx * tb * gr.y)
          + p.y * ((q.y * gr.y + r) / b - ny * tb * gr.y)
          + p.z * (-q.z * gr.y / r2);
    gkp.z = p.x * (q.x * gr.z / b - nx * tb * gr.z)
          + p.y * (q.y * gr.z / b - ny * tb * gr.z)
          + p.z * (1.0 / r - q.z * gr.z / r2);

    dp = 0.5 * Q * Q * gf + f * Q * gkp;
}

void deriv(vec3 q, vec3 p, float a, float a2, out vec3 dq, out vec3 dp) {
    derivR(q, p, a, a2, ksRadiusA2(q, a2), dq, dp);
}

void rk4(inout vec3 q, inout vec3 p, float a, float a2, float dl, vec3 k1q, vec3 k1p) {
    vec3 k2q, k2p, k3q, k3p, k4q, k4p;
    deriv(q + 0.5 * dl * k1q, p + 0.5 * dl * k1p, a, a2, k2q, k2p);
    deriv(q + 0.5 * dl * k2q, p + 0.5 * dl * k2p, a, a2, k3q, k3p);
    deriv(q + dl * k3q, p + dl * k3p, a, a2, k4q, k4p);
    q += dl * (k1q + 2.0 * k2q + 2.0 * k3q + k4q) / 6.0;
    p += dl * (k1p + 2.0 * k2p + 2.0 * k3p + k4p) / 6.0;
}

// midpoint method: half the derivative evaluations of RK4. Sufficient away
// from the photon shell, where the field varies slowly over one step.
void rk2(inout vec3 q, inout vec3 p, float a, float a2, float dl, vec3 k1q, vec3 k1p) {
    vec3 k2q, k2p;
    deriv(q + 0.5 * dl * k1q, p + 0.5 * dl * k1p, a, a2, k2q, k2p);
    q += dl * k2q;
    p += dl * k2p;
}

void main() {
    float aspect = pc.res_fov_steps.x;
    float tanfov = pc.res_fov_steps.z;
    int steps = int(pc.res_fov_steps.w);
    float rin = pc.disk.x, rout = pc.disk.y;
    float escape = pc.misc.w;
    float a = pc.kerr.x;
    float a2 = a * a;
    float rhor = pc.kerr.z;
    float escape2 = escape * escape;

    // v_uv.y = 0 is the top of the image; world up must point up on screen
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
    ndc.x *= aspect;
    mat3 cam = mat3(pc.cam0.xyz, pc.cam1.xyz, pc.cam2.xyz);
    vec3 dir = normalize(cam * vec3(ndc * tanfov, -1.0));

    // ---- camera ray -> Kerr-Schild state (world y -> KS z) ------------------
    vec3 q = pc.campos_time.xzy;
    vec3 d = dir.xzy;
    {
        float f0 = pc.cam0.w;
        vec3 k0 = vec3(pc.cam1.w, pc.cam2.w, pc.res_fov_steps.y);
        float kd = dot(k0, d);
        // null-condition root simplified from the quadratic form;
        // E is the same square root used to normalize the momentum.
        float E = sqrt(max(1.0 - f0 + f0 * kd * kd, 0.0));
        d = (f0 * k0 * ((E + kd) / (1.0 - f0)) + d) / E;
    }
    vec3 p = d;
    float L = q.x * p.y - q.y * p.x;           // conserved azimuthal momentum

    vec3 col = vec3(0.0);
    float trans = 1.0;
    float haze = 0.0;
    int diskHits = 0;
    int slot = 0;
    bool captured = false;
    float rPhoton = pc.kerr.w;
    float shadowLeakR = rPhoton + 0.12;
    float rMin = ksRadiusA2(q, a2);
    // radians per pixel (v_uv is linear over the viewport, so this is uniform)
    float pixAng = fwidth(v_uv.y) * 2.0 * tanfov;

    for (int i = 0; i < 720; i++) {
        if (i >= steps) {
            // outward rays that cleared the photon shell escaped numerically —
            // only mark captured when still bound in the strong-field region
            captured = !(dot(q, p) > 0.0 && rMin > rPhoton * 0.82);
            break;
        }

        float r = ksRadiusA2(q, a2);
        rMin = min(rMin, r);
        if (r < rhor + 0.01) { captured = true; break; }

        // adaptive step: fine near the horizon. Disk crossings are found by
        // sign change and interpolated, so no plane damping is needed where
        // the field is weak (the 1.2/|k1p| cap below bounds the chord error);
        // only the strongly curved inner region needs small near-plane steps.
        float dl = clamp(0.045 + 0.32 * (r - rhor), 0.02, 9.0);
        if (r < 9.0 && r > rin * 0.35)
            dl = min(dl, max(0.26, abs(q.z) * 2.0));
        // resolve the photon-shell whirl finely or near-critical rays get
        // chaotically ejected and leak stars into the shadow
        if (r < 5.0)
            dl = min(dl, max(0.05, 0.12 * (r - rhor)));

        // error control: cap the momentum change per step (r reused from the
        // horizon check above — one ksRadius fewer per step)
        vec3 k1q, k1p;
        derivR(q, p, a, a2, r, k1q, k1p);
        dl = min(dl, 1.2 / (length(k1p) + 1e-3));

        vec3 qPrev = q;
        if (r < 7.0) rk4(q, p, a, a2, dl, k1q, k1p);
        else         rk2(q, p, a, a2, dl, k1q, k1p);

        // equatorial plane crossing -> disk hit (multiple lensed images).
        // The first two crossings get the full volumetric slab; later ones
        // (higher-order photon-ring images) are subpixel-thin and damped.
        if (q.z * qPrev.z < 0.0 && slot < 5) {
            float t = qPrev.z / (qPrev.z - q.z);
            vec3 hit = mix(qPrev, q, t);
            float w2 = hit.x * hit.x + hit.y * hit.y;
            float rH = sqrt(max(w2 - a2, 0.0));         // BL r in the plane
            if (rH > rin - DISK_SLACK && rH < rout + DISK_OSLACK) {
                vec3 cd = normalize(q - qPrev);
                float drr = (hit.x * cd.x + hit.y * cd.y)
                          / max(sqrt(w2), 1e-4);
                float phiH = atan(hit.y, hit.x);
                vec4 em = slot < 2
                    ? diskEmissionVol(rH, phiH, cd.z, drr, L, pixAng, 0.0)
                    : diskEmissionThin(rH, phiH, L, pixAng, 0.0);
                float ema = em.a;
                if (diskHits >= 2) {
                    // higher-order photon-ring images are lensed re-renders of
                    // the same disc, not foreground occluders: add their damped
                    // light but let them absorb only weakly, so stacked
                    // sub-pixel arcs blend into a smooth halo instead of carving
                    // dark lanes that make thin beaded lines pop
                    float ringFade = pow(0.45, float(diskHits - 1));
                    em.rgb *= ringFade;
                    ema *= ringFade * 0.15;
                }
                col += trans * em.rgb;
                trans *= 1.0 - ema * 0.88;
                if (em.a > 0.004) diskHits++;
                slot++;
                if (trans < 0.015) break;
            }
        }

        // faint hot plasma atmosphere over the inner disk, capped so deep
        // rays don't fill the shadow with glow
        if (pc.misc.z > 0.5 && r < 12.0 && haze < 0.05) {
            float dh = dl * 0.008 / (r * r * 0.14 + 0.25)
                     * smoothstep(rhor + 0.4, rhor + 2.0, r)
                     * exp(-abs(q.z) * 0.6);
            haze += dh;
            col += trans * vec3(1.0, 0.55, 0.26) * dh;
        }

        if (dot(q, q) > escape2 && dot(q, p) > 0.0) break;
    }

    // reject numerically leaked sky inside the shadow cone
    if (!captured && rMin < shadowLeakR && dot(q, q) < escape2 * 0.42)
        captured = true;

    if (!captured && trans > 0.015) {
        vec3 sd = normalize(p).xzy;             // KS -> world swizzle
        // rays that skimmed the photon shell map huge sky regions onto one
        // pixel; point stars there bead into dotted trails, so fade them by
        // the demagnification proxy rMin (the nebula texture stays smooth)
        float starFade = smoothstep(rPhoton + 0.03, rPhoton + 0.45, rMin);
        col += trans * (texture(u_sky, sd).rgb + skyStars(sd) * starFade);
    }

    // keep f16 targets finite: inf here becomes NaN in ACES and renders as
    // garbage tiles on Apple GPUs
    col = clamp(col, vec3(0.0), vec3(400.0));
    if (any(isnan(col))) col = vec3(0.0);

    fragColor = vec4(col, 1.0);
}
"""

_SCENE_PREFIX = FRAG_SCENE.split("void main() {", 1)[0]

_TRANSPORT_PREFIX = _SCENE_PREFIX.replace(
    "layout(location = 0) out vec4 fragColor;\n",
    "layout(location = 0) out vec4 cache0;\n"
    "layout(location = 1) out vec4 cache1;\n"
    "layout(location = 2) out vec4 cache2;\n"
    "layout(location = 3) out vec4 cache3;\n"
    "layout(location = 4) out vec4 cache4;\n")

FRAG_TRANSPORT = _TRANSPORT_PREFIX + """
void main() {
    float aspect = pc.res_fov_steps.x;
    float tanfov = pc.res_fov_steps.z;
    int steps = int(pc.res_fov_steps.w);
    float rin = pc.disk.x, rout = pc.disk.y;
    float escape = pc.misc.w;
    float a = pc.kerr.x;
    float a2 = a * a;
    float rhor = pc.kerr.z;
    float escape2 = escape * escape;

    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
    ndc.x *= aspect;
    mat3 cam = mat3(pc.cam0.xyz, pc.cam1.xyz, pc.cam2.xyz);
    vec3 dir = normalize(cam * vec3(ndc * tanfov, -1.0));

    vec3 q = pc.campos_time.xzy;
    vec3 d = dir.xzy;
    {
        float f0 = pc.cam0.w;
        vec3 k0 = vec3(pc.cam1.w, pc.cam2.w, pc.res_fov_steps.y);
        float kd = dot(k0, d);
        float E = sqrt(max(1.0 - f0 + f0 * kd * kd, 0.0));
        d = (f0 * k0 * ((E + kd) / (1.0 - f0)) + d) / E;
    }
    vec3 p = d;
    float L = q.x * p.y - q.y * p.x;

    // cache layout: two "rich" hits carry the chord direction for volumetric
    // shading (r, phi, dz, dr); three thin hits carry (r, phi) only
    vec4 rich0 = vec4(-1.0);
    vec4 rich1 = vec4(-1.0);
    vec4 h23 = vec4(-1.0);
    vec2 h4 = vec2(-1.0);
    float haze = 0.0;
    int storedHits = 0;
    bool captured = false;
    float rPhoton = pc.kerr.w;
    float rMin = ksRadiusA2(q, a2);

    for (int i = 0; i < 720; i++) {
        if (i >= steps) {
            captured = !(dot(q, p) > 0.0 && rMin > rPhoton * 0.82);
            break;
        }

        float r = ksRadiusA2(q, a2);
        rMin = min(rMin, r);
        if (r < rhor + 0.01) { captured = true; break; }

        float dl = clamp(0.045 + 0.32 * (r - rhor), 0.02, 9.0);
        if (r < 9.0 && r > rin * 0.35)
            dl = min(dl, max(0.26, abs(q.z) * 2.0));
        if (r < 5.0)
            dl = min(dl, max(0.05, 0.12 * (r - rhor)));

        vec3 k1q, k1p;
        derivR(q, p, a, a2, r, k1q, k1p);
        dl = min(dl, 1.2 / (length(k1p) + 1e-3));

        vec3 qPrev = q;
        if (r < 7.0) rk4(q, p, a, a2, dl, k1q, k1p);
        else         rk2(q, p, a, a2, dl, k1q, k1p);

        if (q.z * qPrev.z < 0.0 && storedHits < 5) {
            float t = qPrev.z / (qPrev.z - q.z);
            vec3 hit = mix(qPrev, q, t);
            float w2 = hit.x * hit.x + hit.y * hit.y;
            float rH = sqrt(max(w2 - a2, 0.0));
            if (rH > rin - DISK_SLACK && rH < rout + DISK_OSLACK) {
                vec3 cd = normalize(q - qPrev);
                float drr = (hit.x * cd.x + hit.y * cd.y)
                          / max(sqrt(w2), 1e-4);
                vec2 hp = vec2(rH, atan(hit.y, hit.x));
                if (storedHits == 0) rich0 = vec4(hp, cd.z, drr);
                else if (storedHits == 1) rich1 = vec4(hp, cd.z, drr);
                else if (storedHits == 2) h23.xy = hp;
                else if (storedHits == 3) h23.zw = hp;
                else h4 = hp;
                storedHits++;
            }
        }

        if (pc.misc.z > 0.5 && r < 12.0 && haze < 0.05) {
            float dh = dl * 0.008 / (r * r * 0.14 + 0.25)
                     * smoothstep(rhor + 0.4, rhor + 2.0, r)
                     * exp(-abs(q.z) * 0.6);
            haze += dh;
        }

        if (dot(q, q) > escape2 && dot(q, p) > 0.0) break;
    }

    if (!captured && rMin < rPhoton + 0.12 && dot(q, q) < escape2 * 0.42)
        captured = true;

    // sky direction scaled by the star fade (see the scene shader): the
    // shading pass recovers fade = length, direction = normalize
    float starFade = smoothstep(rPhoton + 0.03, rPhoton + 0.45, rMin);
    vec3 sd = captured ? vec3(0.0) : normalize(p).xzy * max(starFade, 1e-3);
    cache0 = vec4(sd, captured ? 1.0 : 0.0);
    cache1 = rich0;
    cache2 = rich1;
    cache3 = h23;
    cache4 = vec4(h4, L, haze);
}
"""

_SHADE_CACHE_PREFIX = _SCENE_PREFIX.replace(
    "layout(location = 0) out vec4 fragColor;\n",
    "layout(location = 0) out vec4 fragColor;\n")
_SHADE_CACHE_PREFIX = _SHADE_CACHE_PREFIX.replace(
    "layout(set = 0, binding = 1) uniform sampler2D u_noise;    // baked disk noise\n",
    "layout(set = 0, binding = 1) uniform sampler2D u_noise;    // baked disk noise\n"
    "layout(set = 0, binding = 2) uniform sampler2D u_cache0;\n"
    "layout(set = 0, binding = 3) uniform sampler2D u_cache1;\n"
    "layout(set = 0, binding = 4) uniform sampler2D u_cache2;\n"
    "layout(set = 0, binding = 5) uniform sampler2D u_cache3;\n"
    "layout(set = 0, binding = 6) uniform sampler2D u_cache4;\n")

FRAG_SHADE_CACHE = _SHADE_CACHE_PREFIX + """
void compositeEm(vec4 em, inout vec3 col, inout float trans, inout int diskHits) {
    // matches the direct path: higher-order lensed images add damped light but
    // barely absorb, so stacked sub-pixel photon-ring arcs blend into a smooth
    // halo rather than beaded lines separated by dark lanes
    float ema = em.a;
    if (diskHits >= 2) {
        float ringFade = pow(0.45, float(diskHits - 1));
        em.rgb *= ringFade;
        ema *= ringFade * 0.15;
    }
    col += trans * em.rgb;
    trans *= 1.0 - ema * 0.88;
    if (em.a > 0.004) diskHits++;
}

// texels per pixel implied by the hit-coordinate change to neighbouring
// pixels: the true minification rate, including lensing magnification.
// At a hit/no-hit boundary (shadow edge, silhouette) no neighbour matches;
// those pixels sit on an extreme-compression band, so blur conservatively
// rather than falling back to no filtering.
void lodAccum(vec4 h, vec4 n, inout float dv, inout float du) {
    if (n.x < -0.5) return;
    dv = max(dv, abs(n.x - h.x));
    du = max(du, abs(atan(sin(n.y - h.y), cos(n.y - h.y))));
}
float lodDeriv(vec4 h, vec4 nU, vec4 nD, vec4 nR, vec4 nL) {
    if (h.x < -0.5) return 0.0;
    float dv = -1.0, du = -1.0;
    lodAccum(h, nU, dv, du);
    lodAccum(h, nD, dv, du);
    lodAccum(h, nR, dv, du);
    lodAccum(h, nL, dv, du);
    if (dv < 0.0) return 6.0;               // isolated hit: assume compressed
    float om = 1.0 / (h.x * sqrt(h.x) + pc.kerr.x);
    du += dv * 1.5 * sqrt(h.x) * om * om * (0.5 * WIND_T);   // winding shear
    float t = max(dv * 2048.0 * NOISE_V / max(h.x, 1.5),
                  du * (2048.0 / TAU));
    return clamp(log2(max(t, 1.0)), 0.0, 8.0);
}

vec3 shadeTexel(ivec2 p, float pixAng) {
    ivec2 sz = textureSize(u_cache0, 0);
    p = clamp(p, ivec2(0), sz - ivec2(1));
    vec4 c0 = texelFetch(u_cache0, p, 0);
    vec4 rich0 = texelFetch(u_cache1, p, 0);
    vec4 rich1 = texelFetch(u_cache2, p, 0);
    vec4 h23 = texelFetch(u_cache3, p, 0);
    vec4 c4 = texelFetch(u_cache4, p, 0);
    ivec2 pU = min(p + ivec2(0, 1), sz - 1);
    ivec2 pD = max(p - ivec2(0, 1), ivec2(0));
    ivec2 pR = min(p + ivec2(1, 0), sz - 1);
    ivec2 pL = max(p - ivec2(1, 0), ivec2(0));
    float lodX0 = lodDeriv(rich0, texelFetch(u_cache1, pU, 0),
                           texelFetch(u_cache1, pD, 0),
                           texelFetch(u_cache1, pR, 0),
                           texelFetch(u_cache1, pL, 0));
    float lodX1 = lodDeriv(rich1, texelFetch(u_cache2, pU, 0),
                           texelFetch(u_cache2, pD, 0),
                           texelFetch(u_cache2, pR, 0),
                           texelFetch(u_cache2, pL, 0));

    vec3 col = vec3(0.0);
    float trans = 1.0;
    int diskHits = 0;
    float L = c4.z;

    if (rich0.x > -0.5 && trans > 0.015)
        compositeEm(diskEmissionVol(rich0.x, rich0.y, rich0.z, rich0.w, L,
                                    pixAng, lodX0), col, trans, diskHits);
    if (rich1.x > -0.5 && trans > 0.015)
        compositeEm(diskEmissionVol(rich1.x, rich1.y, rich1.z, rich1.w, L,
                                    pixAng, lodX1), col, trans, diskHits);
    float lodT = max(lodX0, lodX1);
    if (h23.x > -0.5 && trans > 0.015)
        compositeEm(diskEmissionThin(h23.x, h23.y, L, pixAng, lodT),
                    col, trans, diskHits);
    if (h23.z > -0.5 && trans > 0.015)
        compositeEm(diskEmissionThin(h23.z, h23.w, L, pixAng, lodT),
                    col, trans, diskHits);
    if (c4.x > -0.5 && trans > 0.015)
        compositeEm(diskEmissionThin(c4.x, c4.y, L, pixAng, lodT),
                    col, trans, diskHits);

    if (pc.misc.z > 0.5)
        col += vec3(1.0, 0.55, 0.26) * c4.w;

    if (c0.w < 0.5 && trans > 0.015) {
        float starFade = min(length(c0.xyz), 1.0);
        vec3 sd = c0.xyz / max(starFade, 1e-4);
        col += trans * (texture(u_sky, sd).rgb + skyStars(sd) * starFade);
    }
    return col;
}

void main() {
    ivec2 sz = textureSize(u_cache0, 0);
    ivec2 p = ivec2(clamp(v_uv, vec2(0.0), vec2(0.999999)) * vec2(sz));
    float pixAng = fwidth(v_uv.y) * 2.0 * pc.res_fov_steps.z;
    vec3 col = clamp(shadeTexel(p, pixAng), vec3(0.0), vec3(400.0));
    if (any(isnan(col))) col = vec3(0.0);
    fragColor = vec4(col, 1.0);
}
"""

# ----------------------------------------------------------------------------
# Post-processing
# ----------------------------------------------------------------------------

FRAG_BRIGHT = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_a;
layout(set = 0, binding = 1) uniform sampler2D u_b;
layout(set = 0, binding = 2) uniform sampler2D u_c;
layout(push_constant) uniform PC { vec4 p; } pc;   // x = threshold
void main() {
    vec3 c = texture(u_a, v_uv).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(pc.p.x * 0.5, pc.p.x * 1.5, l);
    fragColor = vec4(c * w, 1.0);
}
"""

FRAG_BLUR = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_a;
layout(set = 0, binding = 1) uniform sampler2D u_b;
layout(set = 0, binding = 2) uniform sampler2D u_c;
layout(push_constant) uniform PC { vec4 p; } pc;   // xy = blur step in uv
void main() {
    // Same 9-tap Gaussian kernel as before, collapsed to 5 bilinear samples.
    // The sampler is linear, so fractional offsets combine adjacent taps.
    const float w0 = 0.2270270;
    const float w12 = 0.3162162;       // 0.1945946 + 0.1216216
    const float w34 = 0.0702700;       // 0.0540540 + 0.0162160
    vec2 d12 = pc.p.xy * 1.3846154;
    vec2 d34 = pc.p.xy * 3.2307692;
    vec3 c = texture(u_a, v_uv).rgb * w0;
    c += (texture(u_a, v_uv + d12).rgb + texture(u_a, v_uv - d12).rgb) * w12;
    c += (texture(u_a, v_uv + d34).rgb + texture(u_a, v_uv - d34).rgb) * w34;
    fragColor = vec4(c, 1.0);
}
"""

FRAG_COMPOSITE = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_scene;
layout(set = 0, binding = 1) uniform sampler2D u_bloom;
layout(set = 0, binding = 2) uniform sampler2D u_bloom2;
layout(push_constant) uniform PC { vec4 p; vec4 q; } pc;
// p = (bloom strength, exposure, time, EDR peak — 0 selects the SDR path)
// q = (upscale active, 0, 0, 0)

// Catmull-Rom resampling in 9 bilinear taps (MJP / vec3.ca): recovers most
// of the sharpness bilinear upscaling loses when dynamic resolution renders
// below the output size
vec3 sampleSceneCR(vec2 uv) {
    vec2 texSize = vec2(textureSize(u_scene, 0));
    vec2 samplePos = uv * texSize;
    vec2 tc1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - tc1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc0 = (tc1 - 1.0) / texSize;
    vec2 tc3 = (tc1 + 2.0) / texSize;
    vec2 tc12 = (tc1 + w2 / w12) / texSize;
    vec3 c = texture(u_scene, vec2(tc0.x,  tc0.y)).rgb  * w0.x  * w0.y
           + texture(u_scene, vec2(tc12.x, tc0.y)).rgb  * w12.x * w0.y
           + texture(u_scene, vec2(tc3.x,  tc0.y)).rgb  * w3.x  * w0.y
           + texture(u_scene, vec2(tc0.x,  tc12.y)).rgb * w0.x  * w12.y
           + texture(u_scene, vec2(tc12.x, tc12.y)).rgb * w12.x * w12.y
           + texture(u_scene, vec2(tc3.x,  tc12.y)).rgb * w3.x  * w12.y
           + texture(u_scene, vec2(tc0.x,  tc3.y)).rgb  * w0.x  * w3.y
           + texture(u_scene, vec2(tc12.x, tc3.y)).rgb  * w12.x * w3.y
           + texture(u_scene, vec2(tc3.x,  tc3.y)).rgb  * w3.x  * w3.y;
    return max(c, vec3(0.0));   // negative lobes can undershoot in HDR
}

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
float hash(vec2 q) {
    vec3 p3 = fract(vec3(q.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
void main() {
    vec2 off = v_uv - 0.5;
    float r2 = dot(off, off);

    // chromatic aberration confined to the corners (quartic falloff) so it
    // never desaturates fine detail near the center. The offset is kept
    // sub-pixel across the inner frame and hard-capped at the corners:
    // a multi-pixel split on a razor-sharp HDR edge (e.g. the photon ring)
    // sampled R at +ca / B at -ca prisms into a rainbow fringe and dices
    // thin lensed images into coloured dashes.
    vec2 ca = off * r2 * r2 * 0.010;
    ca = clamp(ca, vec2(-0.0016), vec2(0.0016));
    vec3 scene;
    if (pc.q.x > 0.5) {
        // upscaling: sharp Catmull-Rom base + chromatic aberration applied
        // as a bilinear delta (cancels exactly where ca ~ 0)
        scene = sampleSceneCR(v_uv);
        scene.r += texture(u_scene, v_uv + ca).r - texture(u_scene, v_uv).r;
        scene.b += texture(u_scene, v_uv - ca).b - texture(u_scene, v_uv).b;
    } else {
        scene.r = texture(u_scene, v_uv + ca).r;
        scene.g = texture(u_scene, v_uv).g;
        scene.b = texture(u_scene, v_uv - ca).b;
    }

    vec3 bloom = vec3(0.0);
    if (pc.p.x > 0.0)
        bloom = texture(u_bloom, v_uv).rgb + texture(u_bloom2, v_uv).rgb * 1.6;
    vec3 hdr = (scene + bloom * pc.p.x) * pc.p.y;

    float grain = (hash(v_uv * 913.0 + fract(pc.p.z) * 71.0) - 0.5) / 255.0 * 1.5;
    float peak = pc.p.w;
    vec3 c;
    if (peak > 0.0) {
        // EDR output: linear extended sRGB, 1.0 = SDR white, peak = the
        // display's live EDR headroom. Luminance below the knee passes
        // through untouched (full linear brightness); above it a C1
        // rational shoulder rolls highlights off toward the peak. Scaling
        // rgb by ln/l preserves hue.
        float l = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
        const float knee = 0.85;
        float pk = max(peak, knee + 0.15);
        float ln = l <= knee ? l
                 : knee + (pk - knee) * (l - knee) / (pk - 2.0 * knee + l);
        c = hdr * (ln / max(l, 1e-6));
        c = max(c * (1.0 - 0.32 * r2) + grain, 0.0);            // stays linear
    } else {
        c = pow(aces(hdr), vec3(1.0 / 2.2));
        c = c * (1.0 - 0.32 * r2) + grain;
    }
    fragColor = vec4(c, 1.0);
}
"""

# 2x2 box downsample (bilinear tap at the texel corner): builds the noise
# texture's mip chain at startup so minification can't moire
FRAG_DOWNSAMPLE = """
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_a;
layout(set = 0, binding = 1) uniform sampler2D u_b;
layout(set = 0, binding = 2) uniform sampler2D u_c;
void main() {
    fragColor = texture(u_a, v_uv);
}
"""

SHADERS = {
    "fullscreen.vert": VERT_FULLSCREEN,
    "bakesky.frag": FRAG_BAKESKY,
    "bakedisk.frag": FRAG_BAKEDISK,
    "downsample.frag": FRAG_DOWNSAMPLE,
    "scene.frag": FRAG_SCENE,
    "transport.frag": FRAG_TRANSPORT,
    "shadecache.frag": FRAG_SHADE_CACHE,
    "bright.frag": FRAG_BRIGHT,
    "blur.frag": FRAG_BLUR,
    "composite.frag": FRAG_COMPOSITE,
}


def find_glslc():
    return shutil.which("glslc") or (
        "/opt/homebrew/bin/glslc"
        if pathlib.Path("/opt/homebrew/bin/glslc").exists() else None)


def compile_shader(name, source, cache_dir=None):
    """Compile one GLSL source to SPIR-V bytes, using an on-disk cache."""
    cache_dir = pathlib.Path(cache_dir or pathlib.Path(__file__).parent / ".spv_cache")
    cache_dir.mkdir(exist_ok=True)
    digest = hashlib.sha256(source.encode()).hexdigest()[:16]
    cached = cache_dir / f"{name}.{digest}.spv"
    if cached.exists():
        return cached.read_bytes()

    glslc = find_glslc()
    if not glslc:
        raise RuntimeError("glslc not found — install it with: brew install shaderc")
    stage = name.rsplit(".", 1)[1]
    proc = subprocess.run(
        [glslc, f"-fshader-stage={stage}", "-O", "-o", "-", "-"],
        input=source.encode(), capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError(f"glslc failed for {name}:\n{proc.stderr.decode()}")
    for old in cache_dir.glob(f"{name}.*.spv"):
        old.unlink()
    cached.write_bytes(proc.stdout)
    return proc.stdout


def compile_all(cache_dir=None):
    return {name: compile_shader(name, src, cache_dir) for name, src in SHADERS.items()}
