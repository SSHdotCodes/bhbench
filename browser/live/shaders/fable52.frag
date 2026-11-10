#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp samplerCube;
// =============================================================================
//  raytrace.frag — one null geodesic of the Kerr metric per pixel.
//
//  The geodesic integrator lives in kerr_geodesic.glsl (shared with the CPU
//  verification).  This file supplies the camera, the accretion disk and the
//  sky.
// =============================================================================
// =============================================================================
//  kerr_geodesic.glsl
//
//  Null geodesics of the Kerr spacetime in Cartesian Kerr-Schild coordinates.
//
//  This file is deliberately written in the common subset of GLSL 4.10 and
//  C++: the GPU ray tracer includes it verbatim, and tests/verify.cpp compiles
//  the *same source* on the CPU (in float32 and float64) to check it against
//  analytic results.  Keep it free of swizzles, out-parameters and uniforms.
//
//  Units: G = c = M = 1, lengths in gravitational radii GM/c^2.
//
//  Metric (ingoing Kerr-Schild form, signature -+++):
//      g_mu_nu = eta_mu_nu + f l_mu l_nu
//      f   = 2 r^3 / (r^4 + a^2 z^2)
//      l_mu = ( 1, (r x + a y)/(r^2+a^2), (r y - a x)/(r^2+a^2), z/r )
//  with r(x,y,z) defined by (x^2+y^2)/(r^2+a^2) + z^2/r^2 = 1.
//  These coordinates are regular across the future horizon, so a photon that
//  falls in is integrated straight through r = r+ with no coordinate trouble.
//
//  Hamiltonian for a photon with covariant momentum p_mu, E = -p_t conserved:
//      H = 1/2 g^{mu nu} p_mu p_nu = 1/2 [ -E^2 + |p|^2 - f (E + l.p)^2 ] = 0
//
//  Backward ray tracing: a camera pixel receives light that travelled *towards*
//  the camera.  Kerr is invariant under (t, a) -> (-t, -a), so tracing that
//  photon backwards in time through a hole of spin a is identical to tracing a
//  photon forwards in time, away from the camera, through a hole of spin -a.
//  The renderer therefore calls this code with the chart spin  a_chart = -a
//  and with all matter velocities reversed.  Captured rays then cross the
//  horizon smoothly instead of piling up on it.
// =============================================================================

struct KSState {
    vec3  x;      // position (x, y, z)
    vec3  p;      // covariant spatial momentum (p_x, p_y, p_z)
    float t;      // coordinate time elapsed along the ray
};

struct KSDeriv {
    vec3  dx;     // dx^i/dlambda
    vec3  dp;     // dp_i/dlambda
    float dt;     // dt/dlambda
    float r;      // Kerr radial coordinate at the evaluation point
};

struct TraceParams {
    float a;          // chart spin (= -a_physical for backward tracing)
    float rHorizon;   // outer horizon radius r+
    float rFar;       // beyond this radius outgoing rays finish analytically
    float eps;        // step size as a fraction of r
    int   maxSteps;   // integration step budget
    float rPlaneMin;  // equatorial crossings are reported for
    float rPlaneMax;  //   rPlaneMin < r < rPlaneMax
};

struct TraceResult {
    vec3  color;      // emission accumulated front-to-back along the ray
    float trans;      // remaining transmittance
    vec3  dir;        // direction at infinity (valid when status == 0)
    int   status;     // 0 escaped | 1 captured | 2 out of steps | 3 absorbed
    float depth;      // Euclidean distance to the first opaque equatorial hit
    int   steps;      // integration steps used
};

// The including program supplies the appearance of the equatorial plane.
// x, p: photon state at the crossing; E: conserved energy; t: coordinate time
// elapsed since leaving the camera; rc: Kerr radius of the crossing.
// Returns (premultiplied emission rgb, opacity).
vec4 planeShade(vec3 x, vec3 p, float E, float t, float rc);

// -----------------------------------------------------------------------------
// Kerr radial coordinate r of a Cartesian Kerr-Schild point.
// -----------------------------------------------------------------------------
float ksRadius(vec3 x, float a) {
    float a2 = a * a;
    float w  = dot(x, x) - a2;
    return sqrt(0.5 * (w + sqrt(w * w + 4.0 * a2 * x.z * x.z)));
}

// -----------------------------------------------------------------------------
// Right-hand side of Hamilton's equations.
//   dx^i/dl =  dH/dp_i = p_i - f w l_i                    (w = E + l.p)
//   dp_i/dl = -dH/dx^i = 1/2 w^2 d_i f + f w d_i (l.p)
//   dt/dl   =  dH/dp_t = E + f w
// All gradients are analytic.
// -----------------------------------------------------------------------------
KSDeriv ksDerivative(vec3 x, vec3 p, float E, float a) {
    float a2 = a * a;
    float z2 = x.z * x.z;
    float w  = dot(x, x) - a2;
    float r2 = 0.5 * (w + sqrt(w * w + 4.0 * a2 * z2));
    float r  = sqrt(r2);
    float r3 = r2 * r;

    float isig = 1.0 / (r2 * r2 + a2 * z2);     // 1 / (r^4 + a^2 z^2)
    float ira  = 1.0 / (r2 + a2);
    float ir   = 1.0 / r;
    float f    = 2.0 * r3 * isig;

    float lxn = r * x.x + a * x.y;              // numerators of l_x, l_y
    float lyn = r * x.y - a * x.x;
    vec3  l   = vec3(lxn * ira, lyn * ira, x.z * ir);

    float lp = dot(l, p);
    float om = E + lp;

    // grad r
    vec3 gr = vec3(r3 * x.x, r3 * x.y, r * (r2 + a2) * x.z) * isig;

    // grad f = (df/dr) grad r + (df/dz)|_r z^
    float dfdr = 2.0 * r2 * (3.0 * a2 * z2 - r2 * r2) * isig * isig;
    float dfdz = -4.0 * a2 * r3 * x.z * isig * isig;
    vec3  gf   = dfdr * gr + vec3(0.0, 0.0, dfdz);

    // grad (l.p) at fixed p = (d(l.p)/dr) grad r + explicit part
    float dlpdr = (x.x * p.x + x.y * p.y) * ira
                - 2.0 * r * (lxn * p.x + lyn * p.y) * ira * ira
                - x.z * p.z * ir * ir;
    vec3 glp = dlpdr * gr
             + vec3((r * p.x - a * p.y) * ira, (a * p.x + r * p.y) * ira, p.z * ir);

    KSDeriv d;
    d.dx = p - (f * om) * l;
    d.dp = (0.5 * om * om) * gf + (f * om) * glp;
    d.dt = E + f * om;
    d.r  = r;
    return d;
}

// H = 1/2 g^{mu nu} p_mu p_nu ; zero for a photon.  Used by the verification.
float ksHamiltonian(vec3 x, vec3 p, float E, float a) {
    float a2 = a * a;
    float r  = ksRadius(x, a);
    float r2 = r * r;
    float f  = 2.0 * r2 * r / (r2 * r2 + a2 * x.z * x.z);
    vec3  l  = vec3((r * x.x + a * x.y) / (r2 + a2), (r * x.y - a * x.x) / (r2 + a2), x.z / r);
    float om = E + dot(l, p);
    return 0.5 * (dot(p, p) - E * E - f * om * om);
}

KSState ksAdvance(KSState s, KSDeriv d, float h) {
    KSState n;
    n.x = s.x + h * d.dx;
    n.p = s.p + h * d.dp;
    n.t = s.t + h * d.dt;
    return n;
}

// Classical 4th-order Runge-Kutta step; k1 is the derivative at s.
KSState ksRK4(KSState s, KSDeriv k1, float h, float E, float a) {
    KSState s2 = ksAdvance(s, k1, 0.5 * h);
    KSDeriv k2 = ksDerivative(s2.x, s2.p, E, a);
    KSState s3 = ksAdvance(s, k2, 0.5 * h);
    KSDeriv k3 = ksDerivative(s3.x, s3.p, E, a);
    KSState s4 = ksAdvance(s, k3, h);
    KSDeriv k4 = ksDerivative(s4.x, s4.p, E, a);
    float w = h / 6.0;
    KSState n;
    n.x = s.x + w * (k1.dx + 2.0 * (k2.dx + k3.dx) + k4.dx);
    n.p = s.p + w * (k1.dp + 2.0 * (k2.dp + k3.dp) + k4.dp);
    n.t = s.t + w * (k1.dt + 2.0 * (k2.dt + k3.dt) + k4.dt);
    return n;
}

// -----------------------------------------------------------------------------
// Affine step length.  A fixed fraction of r gives constant angular resolution
// around the hole, so the cost of a ray grows only logarithmically with
// distance.  The fraction is tightened inside r < 6M, where orbits are
// unstable and errors amplify, and relaxed (up to 2x) beyond r ~ 16M, where
// the field is weak and the path nearly straight: the RK4 error per step
// scales as (h/r)^5 (M/r).
// -----------------------------------------------------------------------------
float ksStepSize(float r, float speed, float eps) {
    float f = min(1.0, 0.2 + 0.1333 * r) * (1.0 + clamp((r - 16.0) / 30.0, 0.0, 1.0));
    return eps * r * f / max(speed, 0.05);
}

// -----------------------------------------------------------------------------
// Lower the index of a contravariant null vector k^mu = (k.xyz, k.w = k^t).
// Returns (p_x, p_y, p_z, E) with E = -p_t.
// -----------------------------------------------------------------------------
vec4 ksLower(vec3 x, vec4 k, float a) {
    float a2 = a * a;
    float r  = ksRadius(x, a);
    float r2 = r * r;
    float f  = 2.0 * r2 * r / (r2 * r2 + a2 * x.z * x.z);
    vec3  l  = vec3((r * x.x + a * x.y) / (r2 + a2), (r * x.y - a * x.x) / (r2 + a2), x.z / r);
    vec3  kv = vec3(k.x, k.y, k.z);
    float lk = k.w + dot(l, kv);                 // l_mu k^mu
    vec3  p  = kv + (f * lk) * l;
    return vec4(p.x, p.y, p.z, k.w - f * lk);
}

// -----------------------------------------------------------------------------
// Photon leaving the camera through a point of the image plane.
// tetT/R/U/F are the camera's orthonormal tetrad legs (contravariant, stored
// as (x, y, z, t)); screen = (right, up) tangents of the viewing angle.
// The photon is normalised so the camera measures unit energy: -p.u_cam = 1.
// Returns (p_x, p_y, p_z, E).
// -----------------------------------------------------------------------------
vec4 ksCameraRay(vec3 camPos, vec4 tetT, vec4 tetR, vec4 tetU, vec4 tetF, vec2 screen, float a) {
    vec3 n = normalize(vec3(screen.x, screen.y, 1.0));
    vec4 k = tetT + n.x * tetR + n.y * tetU + n.z * tetF;
    return ksLower(camPos, k, a);
}

// -----------------------------------------------------------------------------
// Direction at infinity of an outgoing ray at position x with coordinate
// velocity v.  To first order in M the bending still to come is, in the areal
// radial gauge of these coordinates,
//     dpsi = (M/b) (1 - c)^2 (2 + c),   c = cos(angle between x and v),
// which follows from the orbit u(phi) = sin(phi)/b + (M/b^2)(1 + cos^2(phi)).
// Written with (1 - c) = b^2 / (r (r + s)) so it stays finite as b -> 0.
// The neglected terms are O(M^2/r^2).
// -----------------------------------------------------------------------------
vec3 ksAsymptoticDir(vec3 x, vec3 v) {
    float r = length(x);
    vec3  d = v / length(v);
    float s = dot(x, d);
    vec3  b = x - s * d;
    float q = r * (r + s);
    return normalize(d - ((2.0 + s / r) * dot(b, b) / (q * q)) * b);
}

// -----------------------------------------------------------------------------
// Redshift factor g = E_observed / E_emitted for matter on a circular
// equatorial orbit with coordinate angular velocity Omega = dphi/dt.
// The emitter four-velocity u = u^t (1, -Omega y, Omega x, 0) is normalised
// with the Kerr-Schild metric itself, so no Boyer-Lindquist formula enters.
// The photon is normalised to unit energy at the camera, hence g = 1/E_emit.
// (x must lie in the equatorial plane; rc is its Kerr radius.)
// -----------------------------------------------------------------------------
float diskRedshift(vec3 x, vec3 p, float E, float a, float rc, float Omega) {
    float ira = 1.0 / (rc * rc + a * a);
    vec3  l   = vec3((rc * x.x + a * x.y) * ira, (rc * x.y - a * x.x) * ira, 0.0);
    vec3  v   = vec3(-Omega * x.y, Omega * x.x, 0.0);
    float f   = 2.0 / rc;
    float lv  = 1.0 + dot(l, v);
    float n2  = 1.0 - dot(v, v) - f * lv * lv;       // = -g(u,u)/(u^t)^2
    float ut  = inversesqrt(max(n2, 1.0e-12));
    return 1.0 / (ut * (E - dot(p, v)));
}

// -----------------------------------------------------------------------------
// Integrate one camera ray.
// -----------------------------------------------------------------------------
TraceResult traceRay(vec3 x0, vec3 p0, float E, TraceParams P) {
    KSState s;
    s.x = x0;
    s.p = p0;
    s.t = 0.0;

    TraceResult R;
    R.color  = vec3(0.0);
    R.trans  = 1.0;
    R.dir    = vec3(0.0, 0.0, 1.0);
    R.status = 2;
    R.depth  = 1.0e30;
    R.steps  = 0;

    float a2 = P.a * P.a;
    float pmin2 = P.rPlaneMin * P.rPlaneMin;
    float pmax2 = P.rPlaneMax * P.rPlaneMax;

    for (int i = 0; i < P.maxSteps; ++i) {
        KSDeriv k1 = ksDerivative(s.x, s.p, E, P.a);
        R.steps = i;

        if (k1.r < P.rHorizon) {              // fell through the event horizon
            R.status = 1;
            break;
        }
        // Far away and receding steeply enough that the bending still to come
        // is tiny: finish with the analytic weak-field tail.
        float speed = length(k1.dx);
        if (k1.r > P.rFar && dot(s.x, k1.dx) > 0.8 * k1.r * speed) {
            R.status = 0;
            R.dir = ksAsymptoticDir(s.x, k1.dx);
            break;
        }

        float h = ksStepSize(k1.r, speed, P.eps);
        KSState n = ksRK4(s, k1, h, E, P.a);

        // Equatorial plane crossing -> locate it accurately and shade it.
        if (s.x.z * n.x.z < 0.0) {
            float fr = s.x.z / (s.x.z - n.x.z);
            KSState c  = ksRK4(s, k1, h * fr, E, P.a);
            KSDeriv dc = ksDerivative(c.x, c.p, E, P.a);
            float vz = dc.dx.z;
            if (abs(vz) > 1.0e-6) {
                // Newton polish onto z = 0
                c = ksAdvance(c, dc, clamp(-c.x.z / vz, -h, h));
            }
            float rc2 = c.x.x * c.x.x + c.x.y * c.x.y - a2;
            if (rc2 > pmin2 && rc2 < pmax2) {
                vec4 e = planeShade(c.x, c.p, E, c.t, sqrt(rc2));
                R.color = R.color + R.trans * vec3(e.x, e.y, e.z);
                if (e.w > 0.5 && R.depth > 1.0e29) {
                    R.depth = length(c.x - x0);
                }
                R.trans = R.trans * (1.0 - e.w);
                if (R.trans < 0.004) {
                    R.trans = 0.0;
                    R.status = 3;
                    break;
                }
            }
        }
        s = n;
    }
    return R;
}


layout(location = 0) out vec4  oColor;     // linear HDR radiance
layout(location = 1) out float oDepth;     // Euclidean distance to first opaque hit

uniform vec2  uResolution;
uniform vec2  uJitter;          // sub-pixel offset for progressive supersampling
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uPixelAngle;      // angular size of a pixel (radians)

uniform vec3  uCamPos;          // Kerr-Schild Cartesian position of the observer
uniform vec4  uTetT, uTetR, uTetU, uTetF;   // observer tetrad, components (x,y,z,t)

uniform float uSpin;            // physical spin a/M (hole spins about +z)
uniform float uRh;              // outer horizon
uniform float uRfar;
uniform float uEps;
uniform int   uMaxSteps;

uniform float uDiskIn;          // ISCO
uniform float uDiskOut;
uniform float uLutRmax;         // radius covered by the temperature table
uniform float uTpeak;           // maximum disk temperature [K]
uniform vec2  uLogT;            // ln(Tmin), ln(Tmax) of the blackbody table
uniform float uLutN;            // blackbody table size
uniform float uLogLumRef;       // ln(luminance) of a blackbody at uTpeak
uniform float uDiskContrast;
uniform float uTime;            // observer coordinate time [M]
uniform float uSkyGain;
uniform int   uFlags;           // 1 doppler+redshift | 2 turbulence | 4 limb darkening
                                // 8 disk | 16 sky | 32 redshift map | 64 lensed plane grid

uniform samplerCube uSky;
uniform sampler2D   uBlackbody;
uniform sampler2D   uDiskLUT;


// Manual equivalent of native linear float lookup sampling, without OES_texture_float_linear.
vec4 nativeLookup(sampler2D tab, vec2 uv) {
    int n=textureSize(tab,0).x;float x=clamp(uv.x*float(n)-0.5,0.0,float(n-1));
    int a=int(floor(x)),b=min(a+1,n-1);return mix(texelFetch(tab,ivec2(a,0),0),texelFetch(tab,ivec2(b,0),0),fract(x));
}
const float PI = 3.14159265358979;

bool flag(int bit) { return (uFlags & bit) != 0; }

// ---------------------------------------------------------------------------
// Blackbody radiance in linear sRGB, relative to a blackbody at uTpeak.
// The table stores chromaticity and ln(luminance) on a logarithmic T axis.
// ---------------------------------------------------------------------------
vec3 blackbody(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    vec4 s = nativeLookup(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5));
    return s.rgb * exp(s.a - uLogLumRef);
}

vec3 bbChroma(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    return nativeLookup(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5)).rgb;
}

// =============================================================================
//  stars.glsl — the procedural star catalogue, shared by the sky baker
//  (sky_gen.frag) and the ray tracer (raytrace.frag).
//
//  Stars live in cells of a cube-sphere grid, one candidate per cell per
//  layer; each layer is four times denser and (Euclidean number counts,
//  N(>S) ~ S^-3/2) 4^(2/3) times fainter than the one before.
//  The includer must define  vec3 bbChroma(float T).
// =============================================================================

uint pcg(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float u01(uint h) { return float(h) * (1.0 / 4294967296.0); }

const float STAR_LAYER0_CELLS = 26.0;   // cells per cube-face edge in layer 0
const float STAR_SIGMA0 = 6.0e-4;       // reference blob size [rad]: a star of unit
                                        // "flux" carries 2 pi STAR_SIGMA0^2 of flux

// Galactic frame: band tilted 62 degrees to the hole's equator, passing behind
// the hole as seen from the default camera.
const vec3 GAL_POLE   = vec3(0.0, 0.8829, 0.4695);
const vec3 GAL_CENTRE = vec3(-0.8192, -0.2693, 0.5064);

float galacticDensity(vec3 d, out float lat, out float lon) {
    vec3 e2 = cross(GAL_POLE, GAL_CENTRE);
    lat = asin(clamp(dot(d, GAL_POLE), -1.0, 1.0));
    lon = atan(dot(d, e2), dot(d, GAL_CENTRE));
    float thin  = exp(-lat * lat / (2.0 * 0.085 * 0.085));
    float thick = exp(-lat * lat / (2.0 * 0.26 * 0.26));
    float bulge = exp(-(lon * lon / (2.0 * 0.30 * 0.30) + lat * lat / (2.0 * 0.17 * 0.17)));
    float arm   = 0.75 + 0.25 * cos(lon);            // brighter towards the centre
    return (0.75 * thin + 0.25 * thick) * arm + 1.1 * bulge;
}

// The star (if any) of layer L in the cell containing direction d.
bool starInCell(vec3 d, int L, float density, out vec3 sd, out float flux, out float T) {
    vec3 ad = abs(d);
    int axis;
    float sgn;
    vec2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) { axis = 0; sgn = sign(d.x); uv = d.yz / ad.x; }
    else if (ad.y >= ad.z)            { axis = 1; sgn = sign(d.y); uv = d.zx / ad.y; }
    else                              { axis = 2; sgn = sign(d.z); uv = d.xy / ad.z; }
    int face = axis * 2 + (sgn > 0.0 ? 0 : 1);

    float N = STAR_LAYER0_CELLS * exp2(float(L));
    ivec2 cell = clamp(ivec2(floor((uv * 0.5 + 0.5) * N)), ivec2(0), ivec2(int(N) - 1));
    uint h = pcg(uint(cell.x) + pcg(uint(cell.y) + pcg(uint(face * 16 + L))));
    float r0 = u01(h); h = pcg(h);
    float r1 = u01(h); h = pcg(h);
    float r2 = u01(h); h = pcg(h);
    float r3 = u01(h); h = pcg(h);
    float r4 = u01(h);

    // Cube cells near face corners subtend less solid angle: thin them out so
    // stars are uniform per steradian, then follow the galactic density.
    vec2 cuv = (vec2(cell) + 0.5) / N * 2.0 - 1.0;
    if (r0 > pow(1.0 + dot(cuv, cuv), -1.5) * 0.9 * density) return false;

    // Kept clear of the cell border so no neighbour search is needed.
    vec2 suv = (vec2(cell) + 0.2 + 0.6 * vec2(r1, r2)) / N * 2.0 - 1.0;
    sd = axis == 0 ? vec3(sgn, suv.x, suv.y)
       : axis == 1 ? vec3(suv.y, sgn, suv.x)
                   : vec3(suv.x, suv.y, sgn);
    sd = normalize(sd);

    // Euclidean source counts within the layer: S = S_min u^(-2/3).
    flux = pow(0.397, float(L)) * min(pow(max(r3, 1e-4), -0.6667), 40.0);

    // Stellar temperatures: mostly cool-to-solar, a tail of hot blue stars.
    T = r4 < 0.86 ? mix(3000.0, 7500.0, pow(r4 / 0.86, 1.4))
                  : mix(8000.0, 28000.0, pow((r4 - 0.86) / 0.14, 2.0));
    return true;
}


// ---------------------------------------------------------------------------
// Bright stars as true point sources.
// A star is unresolved, so lensing cannot change its surface brightness the
// way it does for the disk - it changes its *flux*, by the magnification
//     mu = (solid angle of a pixel) / (solid angle of sky that pixel sees).
// gx, gy = d(dir)/d(pixel) is the lens map's Jacobian; inverting it turns the
// angular offset to the star into an offset in pixels, where the star is
// drawn with a fixed, small point-spread function.  Stars brighten into arcs
// near the Einstein ring and fade where the sky is squeezed towards the
// photon ring.
// ---------------------------------------------------------------------------
vec3 pointStars(vec3 d, vec3 gx, vec3 gy) {
    float A = dot(gx, gx), B = dot(gx, gy), C = dot(gy, gy);
    float pix2 = uPixelAngle * uPixelAngle;
    float det = max(A * C - B * B, pix2 * pix2 / 1600.0);     // caps mu at 40
    float mu = pix2 / sqrt(det);
    float foot = sqrt(max(A, C));                             // sky angle per pixel

    float lat, lon;
    float density = clamp(0.30 + 0.70 * min(galacticDensity(d, lat, lon), 1.0), 0.0, 1.0);

    const float s2 = 0.72 * 0.72;                             // PSF variance [pixel^2]
    vec3 col = vec3(0.0);
    for (int L = 0; L < 2; ++L) {
        vec3 sd;
        float flux, T;
        if (!starInCell(d, L, density, sd, flux, T)) continue;
        float cellAngle = 2.0 / (STAR_LAYER0_CELLS * exp2(float(L)));
        // drop out where the pixel sees more sky than a catalogue cell, and
        // on the critical curve itself, where the linearised lens map fails
        float fade = (1.0 - smoothstep(0.06 * cellAngle, 0.2 * cellAngle, foot))
                   * (1.0 - smoothstep(12.0, 36.0, mu));
        if (fade <= 0.0) continue;
        vec3 dl = sd - d;
        float u = dot(dl, gx), v = dot(dl, gy);
        float a = (C * u - B * v) / det, b = (A * v - B * u) / det;
        float q = a * a + b * b;
        col += bbChroma(T) * (flux * fade * mu * STAR_SIGMA0 * STAR_SIGMA0 / (s2 * pix2)) * exp(-q / (2.0 * s2));
    }
    return col;
}

// ---------------------------------------------------------------------------
// Value noise, periodic in x (the azimuthal lattice direction).
// ---------------------------------------------------------------------------
float hash3(int x, int y, int seed) {
    return float(pcg(uint(x) + pcg(uint(y) + pcg(uint(seed))))) * (1.0 / 4294967296.0);
}
int wrap(int x, int n) { return ((x % n) + n) % n; }

float vnoise(vec2 p, int period, int seed) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    int x0 = wrap(int(i.x), period), x1 = wrap(int(i.x) + 1, period);
    int y0 = int(i.y), y1 = y0 + 1;
    return mix(mix(hash3(x0, y0, seed), hash3(x1, y0, seed), u.x),
               mix(hash3(x0, y1, seed), hash3(x1, y1, seed), u.x), u.y);
}
float fbm(vec2 p, int period, int seed) {
    float a = 0.5, s = 0.0, n = 0.0;
    for (int o = 0; o < 5; ++o) {
        s += a * vnoise(p, period, seed + 17 * o);
        n += a;
        p *= 2.0;
        period *= 2;
        a *= 0.5;
    }
    return s / n;
}

// ---------------------------------------------------------------------------
// Turbulent emissivity pattern, advected with the Keplerian flow.
//
// Every annulus carries its pattern around at exactly Omega(r), so
// differential rotation shears blobs into trailing spirals.  Left alone the
// winding would grow without bound, so the pattern is regenerated every
// `turns` local orbits (about the coherence time of magnetorotational
// turbulence).  "Local" means per radial band: bands are half an e-fold wide,
// two interleaved sets of them and two half-cycle-offset generations per band
// are cross-faded, so nothing ever pops or shows a seam.
// Evaluated at the *emission* time, so light-travel-time delays across the
// disk are included.  Returns a zero-mean field of roughly constant variance.
// ---------------------------------------------------------------------------
float diskTurbulence(float r, float phi, float tEmit, float Omega) {
    const float turns = 0.6;
    const int   N = 12;                      // azimuthal lattice period
    const float bandWidth = 0.5;             // in ln r
    float lr = log(r);
    float acc = 0.0, w2 = 0.0;
    for (int s = 0; s < 2; ++s) {
        float bx = lr / bandWidth + 0.5 * float(s);
        float bi = floor(bx);
        float wb = 1.0 - abs(2.0 * (bx - bi) - 1.0);
        float rb = exp((bi + 0.5 - 0.5 * float(s)) * bandWidth);       // band centre
        float Tc = 2.0 * PI * turns * (rb * sqrt(rb) + uSpin);          // regeneration period
        float cyc = tEmit / Tc;
        for (int k = 0; k < 2; ++k) {
            float c  = cyc + 0.5 * float(k);
            float ci = floor(c);
            float fr = c - ci;
            float wt = 1.0 - abs(2.0 * fr - 1.0);
            float ang = phi - Omega * fr * Tc;           // co-rotating azimuth since regeneration
            int seed = int(bi) * 7919 + int(ci) * 104729 + s * 31 + k * 17 + 101;
            float n = fbm(vec2(ang * float(N) / (2.0 * PI), lr * 3.0), N, seed);
            float w = wb * wt;
            acc += w * (n - 0.5);
            w2  += w * w;
        }
    }
    return acc / sqrt(max(w2, 1.0e-6));
}

// Smooth blue-white-red map for the redshift diagnostic view.
vec3 redshiftMap(float g) {
    float x = clamp(log2(g) / 0.75, -1.0, 1.0);
    vec3 red = vec3(1.0, 0.16, 0.05), white = vec3(0.9), blue = vec3(0.08, 0.35, 1.0);
    return x < 0.0 ? mix(white, red, -x) : mix(white, blue, x);
}

// ---------------------------------------------------------------------------
// Appearance of the equatorial plane at a ray crossing (called by traceRay).
// ---------------------------------------------------------------------------
vec4 planeShade(vec3 x, vec3 p, float E, float t, float rc) {
    vec4 result = vec4(0.0);

    if (flag(8) && rc > uDiskIn && rc < uDiskOut) {
        // --- Novikov-Thorne disk: gas on circular geodesics, +phi direction ---
        float Omega = 1.0 / (rc * sqrt(rc) + uSpin);
        // In the time-reversed chart the gas orbits backwards.
        float g  = diskRedshift(x, p, E, -uSpin, rc, -Omega);
        // cosine of the emission angle from the disk normal, in the gas frame
        float mu = clamp(abs(p.z) * g, 0.0, 1.0);

        float u  = sqrt((rc - uDiskIn) / (uLutRmax - uDiskIn));
        float Tn = nativeLookup(uDiskLUT, vec2((u * 1023.0 + 0.5) / 1024.0, 0.5)).r;   // (F/Fmax)^(1/4)

        float emis = 1.0;
        if (flag(2)) {
            float phi = atan(x.y, x.x);
            float turb = diskTurbulence(rc, phi, uTime - t, Omega);
            emis = exp(uDiskContrast * turb);
        }
        float T = uTpeak * Tn * pow(emis, 0.25);        // flux ~ T^4

        // I_nu / nu^3 is invariant: the observer sees a blackbody at g*T.
        float gEff = flag(1) ? g : 1.0;
        vec3 rad = blackbody(max(T * gEff, 1.0));

        // Electron-scattering atmosphere (Chandrasekhar): I(mu) ~ 1 + 2.06 mu
        if (flag(4)) rad *= (3.0 / 7.0) * (1.0 + 2.0 * mu);

        if (flag(32)) rad = redshiftMap(g) * (0.25 + 0.75 * Tn) * 0.6;

        // Opaque (optically thick) body; the surface density is taken to
        // taper off over the outer fifth so the rim is not a hard edge.
        float alpha = smoothstep(uDiskIn, uDiskIn + 0.12, rc)
                    * (1.0 - smoothstep(uDiskOut * 0.78, uDiskOut, rc));
        result = vec4(rad * alpha, alpha);
    }

    if (flag(64)) {
        // --- coordinate grid painted on the equatorial plane, seen through
        //     the same curved light paths (drawn where the disk is not) ---
        float phi = atan(x.y, x.x);
        // footprint of a pixel on the plane ~ distance * pixel angle / cos(incidence)
        float foot = uPixelAngle * t * length(p) / max(abs(p.z), 0.02 * length(p));
        float wr = max(0.035, 1.2 * foot);
        float dr = abs(fract(rc * 0.5 + 0.5) - 0.5) * 2.0;               // rings every 2 M
        float dp = abs(fract(phi * 12.0 / PI + 0.5) - 0.5) * (PI / 12.0) * rc;   // spokes every 15 deg
        float line = max(1.0 - smoothstep(0.5 * wr, wr, dr), 1.0 - smoothstep(0.5 * wr, wr, dp));
        float fade = (1.0 - smoothstep(0.6 * uRfar, 0.95 * uRfar, rc)) * 0.035 / max(0.035, foot * 1.2);
        float a = 0.55 * line * clamp(fade, 0.0, 1.0) * (1.0 - result.a);
        result.rgb += vec3(0.10, 0.45, 1.0) * a * 0.9;
        result.a   += a * 0.5;
    }
    return result;
}

void main() {
    vec2 px  = gl_FragCoord.xy + uJitter;
    vec2 ndc = px / uResolution * 2.0 - 1.0;
    vec2 screen = vec2(ndc.x * uAspect, ndc.y) * uTanHalfFov;

    float aChart = -uSpin;                    // see the header of kerr_geodesic.glsl
    vec4 pe = ksCameraRay(uCamPos, uTetT, uTetR, uTetU, uTetF, screen, aChart);

    TraceParams P;
    P.a         = aChart;
    P.rHorizon  = uRh;
    P.rFar      = uRfar;
    P.eps       = uEps;
    P.maxSteps  = uMaxSteps;
    P.rPlaneMin = uRh;
    P.rPlaneMax = uRfar;

    TraceResult R = traceRay(uCamPos, pe.xyz, pe.w, P);

    // Screen-space footprint of the sky direction (taken outside any branch so
    // the derivatives are well defined): the Jacobian of the lens map.
    vec3 dir = R.dir;
    vec3 jx = dFdx(dir), jy = dFdy(dir);
    vec3 gx = jx, gy = jy;                    // clamped copy picks the cube-map mip level
    float gm = max(length(gx), length(gy));
    float lim = 0.08;
    if (gm > lim) { gx *= lim / gm; gy *= lim / gm; }

    vec3 col = R.color;
    if (R.status == 0 && flag(16)) {
        vec3 sky = textureGrad(uSky, dir, gx, gy).rgb + pointStars(dir, jx, jy);
        // A static observer deep in the potential sees the sky blueshifted by
        // g = 1/E (E = photon energy at infinity for unit local energy);
        // bolometric intensity scales as g^4.
        float g = 1.0 / pe.w;
        col += R.trans * sky * (uSkyGain * g * g * g * g);
    }

    float depth = R.depth;
    if (R.status == 1) depth = min(depth, length(uCamPos));   // the hole hides what lies behind it

    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    oColor = vec4(col, 1.0);
    oDepth = depth;
}
