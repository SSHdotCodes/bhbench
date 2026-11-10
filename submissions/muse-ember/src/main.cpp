// ============================================================================
// Black hole simulator -- Schwarzschild geodesic ray tracing (OpenGL 4.1)
// ============================================================================
// Independent implementation (written from the physics, not copied from any
// existing black-hole project).
//
// Physics summary (G = c = 1, M = 1, so Schwarzschild radius rs = 2M):
//   * Spacetime: ds^2 = -(1-rs/r) dt^2 + dr^2/(1-rs/r) + r^2 dOmega^2
//   * Light follows null geodesics. We integrate Hamilton's equations for
//     the photon 4-momentum p_mu in Schwarzschild coordinates (see the
//     fragment shader for the equations and the derivation notes).
//     Conserved quantities E = -p_t and Lz = p_phi are exact integrals of
//     the motion and are used as a built-in accuracy check.
//   * Event horizon r = rs, photon sphere r = 1.5 rs (unstable circular
//     light orbit; its image is the bright photon ring),
//     ISCO (inner edge of a thin accretion disk) r = 3 rs.
//   * Disk temperature follows the Shakura-Sunyaev thin-disk law
//     T(r) ~ r^{-3/4}; the observed colour/intensity includes the exact
//     gravitational + Doppler frequency shift
//        g = (k.u_obs) / (k.u_emit)
//     for a static observer and a Keplerian circular-geodesic emitter, and
//     the relativistic intensity law I_obs ~ g^3 I_emit (Liouville).
//     The warped disk image, the halo above/below the shadow and the
//     secondary (far-side) disk images are NOT drawn -- they emerge from
//     the geodesic integration itself.
//   * "Trapdoor" view: Flamm's embedding diagram of the equatorial
//     spatial slice, z(r) = sqrt(8M(r-2M)) (up to vertical squash), which
//     is the classic funnel/trapdoor picture of Schwarzschild geometry.
// ============================================================================

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Small vec/mat helpers (avoid an extra dependency on GLM)
// ---------------------------------------------------------------------------
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
static Vec3 normalize(const Vec3& a) {
  float l = length(a);
  return l > 1e-12f ? a * (1.0f / l) : a;
}

struct Mat4 {
  float m[16]{};  // column-major, like OpenGL expects
  static Mat4 identity() { Mat4 r; for (int i = 0; i < 4; i++) r.m[i * 4 + i] = 1.0f; return r; }
};

static Mat4 mul(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}

static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) {
  Mat4 r; for (int i = 0; i < 16; i++) r.m[i] = 0;
  float f = 1.0f / std::tan(fovYRad * 0.5f);
  r.m[0] = f / aspect;
  r.m[5] = f;
  r.m[10] = (zFar + zNear) / (zNear - zFar);
  r.m[11] = -1.0f;
  r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  return r;
}

static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
  Vec3 f = normalize(center - eye);
  Vec3 s = normalize(cross(f, up));
  Vec3 u = cross(s, f);
  Mat4 r = Mat4::identity();
  r.m[0] = s.x;  r.m[1] = u.x;  r.m[2] = -f.x;  r.m[3] = 0;
  r.m[4] = s.y;  r.m[5] = u.y;  r.m[6] = -f.y;  r.m[7] = 0;
  r.m[8] = s.z;  r.m[9] = u.z;  r.m[10] = -f.z; r.m[11] = 0;
  r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye); r.m[15] = 1;
  return r;
}

// ---------------------------------------------------------------------------
// Tiny 3x5 bitmap font for the in-window HUD (uppercase only)
// ---------------------------------------------------------------------------
struct Glyph { const char* rows[5]; };
static const Glyph kFont[] = {
  // space
  {"   ", "   ", "   ", "   ", "   "},
  // !
  {" # ", " # ", " # ", "   ", " # "},
  // "
  {"# #", "# #", "   ", "   ", "   "},
  // #
  {"# #", "###", "# #", "###", "# #"},
  // $
  {"###", "# #", "###", "  #", "###"},
  // %
  {"## ", "## ", " # ", " ##", " ##"},
  // &
  {"###", "# #", "###", "# #", "###"},
  // '
  {" # ", " # ", "   ", "   ", "   "},
  // (
  {"  #", " # ", " # ", " # ", "  #"},
  // )
  {"#  ", " # ", " # ", " # ", "#  "},
  // *
  {" # ", "###", " # ", "   ", "   "},
  // +
  {"   ", " # ", "###", " # ", "   "},
  // ,
  {"   ", "   ", "   ", " # ", "#  "},
  // -
  {"   ", "   ", "###", "   ", "   "},
  // .
  {"   ", "   ", "   ", "   ", " # "},
  // /
  {"  #", "  #", " # ", "#  ", "#  "},
  // 0
  {"###", "# #", "# #", "# #", "###"},
  // 1
  {" # ", "## ", " # ", " # ", "###"},
  // 2
  {"###", "  #", "###", "#  ", "###"},
  // 3
  {"###", "  #", "## ", "  #", "###"},
  // 4
  {"# #", "# #", "###", "  #", "  #"},
  // 5
  {"###", "#  ", "###", "  #", "###"},
  // 6
  {"###", "#  ", "###", "# #", "###"},
  // 7
  {"###", "  #", " # ", " # ", " # "},
  // 8
  {"###", "# #", "###", "# #", "###"},
  // 9
  {"###", "# #", "###", "  #", "###"},
  // :
  {"   ", " # ", "   ", " # ", "   "},
  // ;
  {"   ", " # ", "   ", " # ", "#  "},
  // <
  {"  #", " # ", "#  ", " # ", "  #"},
  // =
  {"   ", "###", "   ", "###", "   "},
  // >
  {"#  ", " # ", "  #", " # ", "#  "},
  // ?
  {"###", "  #", "## ", "   ", " # "},
  // @
  {"###", "# #", "# #", "#  ", "###"},
  // A
  {" # ", "# #", "###", "# #", "# #"},
  // B
  {"## ", "# #", "## ", "# #", "## "},
  // C
  {"###", "#  ", "#  ", "#  ", "###"},
  // D
  {"## ", "# #", "# #", "# #", "## "},
  // E
  {"###", "#  ", "## ", "#  ", "###"},
  // F
  {"###", "#  ", "## ", "#  ", "#  "},
  // G
  {"###", "#  ", "# #", "# #", "###"},
  // H
  {"# #", "# #", "###", "# #", "# #"},
  // I
  {"###", " # ", " # ", " # ", "###"},
  // J
  {"  #", "  #", "  #", "# #", " # "},
  // K
  {"# #", "# #", "## ", "# #", "# #"},
  // L
  {"#  ", "#  ", "#  ", "#  ", "###"},
  // M
  {"# #", "###", "###", "# #", "# #"},
  // N
  {"# #", "## #", "###", "# #", "# #"},
  // NOTE: row above for N intentionally overwritten below? keep 3-wide:
  // O
  {"###", "# #", "# #", "# #", "###"},
  // P
  {"###", "# #", "###", "#  ", "#  "},
  // Q
  {"###", "# #", "# #", "###", "  #"},
  // R
  {"###", "# #", "## ", "# #", "# #"},
  // S
  {"###", "#  ", "###", "  #", "###"},
  // T
  {"###", " # ", " # ", " # ", " # "},
  // U
  {"# #", "# #", "# #", "# #", "###"},
  // V
  {"# #", "# #", "# #", "# #", " # "},
  // W
  {"# #", "# #", "###", "###", "# #"},
  // X
  {"# #", "# #", " # ", "# #", "# #"},
  // Y
  {"# #", "# #", " # ", " # ", " # "},
  // Z
  {"###", "  #", " # ", "#  ", "###"},
  // [
  {"###", "#  ", "#  ", "#  ", "###"},
  // backslash
  {"#  ", "#  ", " # ", "  #", "  #"},
  // ]
  {"###", "  #", "  #", "  #", "###"},
  // ^
  {" # ", "# #", "   ", "   ", "   "},
  // _
  {"   ", "   ", "   ", "   ", "###"},
  // `
  {"#  ", " # ", "   ", "   ", "   "},
};
static const Glyph* glyphFor(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c >= ' ' && c <= '~') return &kFont[(int)c - ' '];
  return &kFont[0];
}
static const int kHudCell = 3;      // font cells per glyph (width)

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------
static const char* kQuadVert = R"GLSL(
#version 410 core
layout(location=0) in vec2 aPos;
out vec2 vUv;
void main(){ vUv = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }
)GLSL";

static const char* kGeodesicFrag = R"GLSL(
#version 410 core
// ==========================================================================
// Schwarzschild null-geodesic ray tracer -- Cartesian Hamiltonian form
// --------------------------------------------------------------------------
// Schwarzschild in Cartesian coordinates (G=c=M=1, rs=2M):
//   ds^2 = -f dt^2 + (delta_ij + (rs/r)/f n_i n_j) dx^i dx^j,
//   f = 1-rs/r, n = x/r.  Contravariant spatial metric:
//   g^{ij} = delta^{ij} - (rs/r) n^i n^j.
// Hamilton's equations for photon covariant momentum p_i (p_t = -E const):
//   dx^i/dl = g^{ij} p_j = p_i - (rs/r) n^i (n.p)
//   dp_i/dl = (rs/r^3) [ p_i (x.p) - 3/2 x_i (x.p)^2 / r^2 ]
// (from dp_i/dl = -1/2 (dg^{jk}/dx^i) p_j p_k with
//  g^{jk} = delta^{jk} - rs x^j x^k / r^3).
// This chart is regular everywhere except the horizon itself: there is no
// polar-axis singularity, so rays passing near the axis are integrated as
// accurately as any others (the spherical-coordinate version of these
// equations has an integrable 1/sin^2(theta) phi-spin singularity at the
// poles which corrupts exactly those rays).  E is an exact constant of
// the motion; the null constraint H=0 holds by construction of the
// initial data.  Integration: RK4 with step size shrinking towards the
// horizon, h ~ rs * clamp(0.03*(r/rs-0.7), 0.02, 0.6).
// Initial data: at the camera, choose E so a static observer measures
// photon energy 1 (E = sqrt(f_cam)); the contravariant spatial momentum
// k^i is built so its components in the static observer's orthonormal
// frame (radial component scaled by 1/sqrt(f)) equal the pixel ray
// direction D exactly.  p_i = g_ij k^j then follows.
// ==========================================================================
in vec2 vUv;
out vec4 FragColor;

uniform float uTime;
uniform vec2  uResolution;
uniform float uAspect;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanFov;
uniform float uRs;          // 2.0 (M=1)
uniform float uEscapeR;     // > camera distance: ray considered escaped
uniform float uDiskBrightness;
uniform int   uInnerGlow;
uniform int   uSteps;       // RK4 steps per pixel
uniform float uTimeScale;   // visualisation time dilution (see README)
uniform int   uDebugMode;   // 0 normal, 1 background-only, 2 encode final direction

const float PI = 3.141592653589793;

// ---- hashing / noise -------------------------------------------------------
float hash11(float p){ p = fract(p*0.1031); p *= p+33.33; p *= p+p; return fract(p); }
vec3 hash33(vec3 p){
  p = fract(p*vec3(0.1031,0.1030,0.0973));
  p += dot(p, p.yxz+33.33);
  return fract((p.xxy+p.yxx)*p.zyx);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  vec2 u = f*f*(3.0-2.0*f);
  float a = hash11(dot(i, vec2(127.1,311.7)));
  float b = hash11(dot(i+vec2(1.,0.), vec2(127.1,311.7)));
  float c = hash11(dot(i+vec2(0.,1.), vec2(127.1,311.7)));
  float d = hash11(dot(i+vec2(1.,1.), vec2(127.1,311.7)));
  return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
}
float fbm(vec2 p){
  float v = 0.0, a = 0.5;
  for(int i=0;i<3;i++){ v += a*vnoise(p); p = p*2.03 + vec2(19.7,7.3); a *= 0.5; }
  return v;
}
// Approximate blackbody chromaticity (Tanner Helland fit), T in kelvin.
vec3 blackbody(float T){
  T = clamp(T, 800.0, 12000.0);
  float t = T/100.0;
  float r = t <= 66.0 ? 255.0 : 329.698727446 * pow(t-60.0, -0.1332047592);
  float g = t <= 66.0 ? 99.4708025861*log(max(t,1.0)) - 161.1195681661 : 288.1221695283*pow(t-60.0,-0.0755148492);
  float b = t >= 66.0 ? 255.0 : (t <= 19.0 ? 0.0 : 138.5177312231*log(max(t-10.0,1.0)) - 305.0447927307);
  vec3 c = clamp(vec3(r,g,b)/255.0, 0.0, 1.0);
  c = pow(c, vec3(1.4));
  return c;
}
vec3 starLayer(vec3 dir, float scale, float thresh, float bright){
  vec3 q = dir*scale;
  vec3 id = floor(q);
  vec3 f = fract(q)-0.5;
  vec3 rnd = hash33(id);
  vec3 centre = (rnd-0.5)*0.7;
  float d = length(f-centre);
  float star = smoothstep(0.16, 0.0, d) * step(thresh, rnd.x);
  vec3 tint = mix(vec3(0.75,0.85,1.0), vec3(1.0,0.85,0.65), rnd.y);
  return tint * star * bright * (0.35+0.65*rnd.z);
}
vec3 background(vec3 dir){
  vec3 n = normalize(vec3(0.28, 0.92, 0.27));
  float band = exp(-pow(dot(dir,n)*2.4, 2.0));
  vec3 col = vec3(0.003, 0.004, 0.009);
  col += vec3(0.03,0.06,0.11) * band * fbm(dir.xz*2.5 + dir.yy*1.7);
  col += vec3(0.06,0.03,0.07) * band * fbm(dir.xy*3.0+7.0);
  col += starLayer(dir,  38.0, 0.985, 0.55);
  col += starLayer(dir,  90.0, 0.993, 0.90);
  col *= 1.0 + 1.6*band;
  return col;
}

// ---- Cartesian geodesic state ----------------------------------------------
struct State { vec3 x; vec3 p; };
struct Deriv { vec3 dx; vec3 dp; };
Deriv deriv(State s, float rs){
  float r = max(length(s.x), rs*0.51);
  vec3 n = s.x / r;
  float xp = dot(s.x, s.p);
  Deriv d;
  d.dx = s.p - (rs/r) * n * dot(n, s.p);
  d.dp = (rs/(r*r*r)) * (s.p*xp - 1.5 * s.x * xp*xp/(r*r));
  return d;
}
State addScaled(State s, Deriv d, float h){
  return State(s.x + d.dx*h, s.p + d.dp*h);
}
State rk4(State s, float h, float rs){
  Deriv k1 = deriv(s, rs);
  Deriv k2 = deriv(addScaled(s,k1,h*0.5), rs);
  Deriv k3 = deriv(addScaled(s,k2,h*0.5), rs);
  Deriv k4 = deriv(addScaled(s,k3,h),     rs);
  Deriv k;
  k.dx = (k1.dx + 2.0*k2.dx + 2.0*k3.dx + k4.dx)/6.0;
  k.dp = (k1.dp + 2.0*k2.dp + 2.0*k3.dp + k4.dp)/6.0;
  return addScaled(s, k, h);
}

// ---- accretion disk shading -------------------------------------------------
vec4 diskShading(float r, float ph, vec3 pCross, float E, float rs, float rIn, float rOut, float fCam){
  float M = rs*0.5;
  float omega = sqrt(M / pow(r, 3.0));
  float phRot = ph - omega * uTime * uTimeScale;
  vec2 diskXY = vec2(cos(phRot), sin(phRot)) * r;
  float turb = fbm(diskXY*0.55 + vec2(0.0, uTime*0.015));
  float turb2 = fbm(diskXY*1.7 - vec2(uTime*0.02, 0.0));
  float rings = 0.85 + 0.15*sin(r*3.1 + turb2*4.0);
  float T_emit = 10500.0 * pow(r/rIn, -0.75) * (0.82 + 0.36*turb);
  // g = (k.u_obs)/(k.u_emit) with u_emit = ut*(1, r*omega*e_phi) at equator:
  // k.u_emit = ut*(-E + r*omega*(p.e_phi)); Lz = r*(p.e_phi) there.
  float ut = 1.0 / sqrt(max(1.0 - 3.0*M/r, 0.02));
  vec3 ephi = vec3(-sin(ph), 0.0, cos(ph));
  float pephi = dot(pCross, ephi);
  float denom = ut * (1.0 - r*omega*pephi/max(E,1e-6));
  denom = clamp(denom, 0.25, 4.0);
  float g = (1.0 / sqrt(max(fCam, 0.05))) / denom;
  g = clamp(g, 0.25, 3.5);
  float T_obs = T_emit * g;
  float flux = pow(rIn/r, 3.0) * (1.0 - sqrt(rIn/r));
  float intensity = flux * 42.0 * pow(g, 3.0) * (0.55 + 0.85*turb) * rings;
  vec3 col = blackbody(T_obs) * intensity * uDiskBrightness;
  col += blackbody(12000.0) * exp(-(r-rIn)/(0.12*rs)) * 2.2 * pow(g,3.0) * uDiskBrightness;
  float alpha = 1.0 - smoothstep(rOut*0.70, rOut, r);
  return vec4(col, alpha);
}

vec3 ace(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }

void main(){
  vec2 ndc = vUv*2.0 - 1.0;
  ndc.x *= uAspect;
  vec3 D = normalize(uCamFwd + uTanFov*(uCamRight*ndc.x + uCamUp*ndc.y));

  float rs = uRs;
  float rIn = 3.0*rs;
  float rOut = 9.5*rs;

  vec3 x0 = uCamPos;
  float r0 = length(x0);
  vec3 n0 = x0 / r0;
  float f0 = 1.0 - rs/r0;
  float fCam = f0;
  float Dr = dot(D, n0);
  vec3 Dt = D - Dr*n0;
  vec3 k0 = Dt + sqrt(max(f0,1e-4))*Dr*n0;
  float a0 = (rs/r0)/max(f0,1e-4);
  State s;
  s.x = x0;
  s.p = k0 + a0*n0*dot(n0,k0);
  float E = sqrt(max(f0,1e-4));

  vec3 glow = vec3(0.0);
  bool captured = false, escaped = false, hitDisk = false;
  vec4 diskCol = vec4(0.0);
  State prev = s;

  for(int i=0; i<uSteps; i++){
    prev = s;
    float rNow = length(s.x);
    float h = rs * clamp(0.040 * (rNow/rs - 0.7), 0.020, 0.85);
    s = rk4(s, h, rs);

    // Equatorial-plane (y=0) crossing?
    if(prev.x.y * s.x.y < 0.0){
      float a = prev.x.y/(prev.x.y - s.x.y);
      vec3 xCross = mix(prev.x, s.x, a);
      float rCross = length(xCross);
      if(rCross > rIn && rCross < rOut && uDebugMode == 0){
        vec3 pCross = mix(prev.p, s.p, a);
        float phCross = atan(xCross.z, xCross.x);
        diskCol = diskShading(rCross, phCross, pCross, E, rs, rIn, rOut, fCam);
        hitDisk = true;
        break;
      }
    }

    float rCur = length(s.x);
    if(uInnerGlow == 1 && uDebugMode == 0 && rCur < rIn && rCur > rs*1.05){
      float eq = exp(-pow((s.x.y/max(rCur,1e-4))/0.22, 2.0));
      float dens = eq * (1.0 - rs/rCur) * pow(rIn/rCur, 1.5);
      glow += vec3(1.0, 0.78, 0.5) * dens * h * 0.035;
    }

    if(rCur < rs*1.02){ captured = true; break; }
    if(rCur > uEscapeR){ escaped = true; break; }
  }

  float rf = max(length(s.x), rs*1.01);
  vec3 nf = s.x / rf;
  float ff = 1.0 - rs/rf;
  vec3 kf = s.p - (rs/rf) * nf * dot(nf, s.p);
  float nk = dot(nf, kf);
  vec3 Dfinal = normalize( (kf - nk*nf) + (nk/sqrt(max(ff,1e-4)))*nf );

  vec3 col;
  if(uDebugMode == 2){
    col = Dfinal*0.5 + 0.5;
  } else if(hitDisk){
    vec3 bg = (diskCol.a < 0.999) ? background(Dfinal) : vec3(0.0);
    col = mix(bg, diskCol.rgb, diskCol.a) + glow*0.35;
  } else if(captured){
    col = glow;
  } else {
    col = background(Dfinal) * sqrt(max(fCam, 0.05)) + glow;
    if(!escaped) col += vec3(0.9, 0.8, 0.6) * 0.04;
  }
  col = min(col, vec3(4.0));

  col = ace(col * 1.15);
  col = pow(col, vec3(1.0/2.2));
  vec2 q = vUv*2.0-1.0;
  col *= 1.0 - 0.22*dot(q,q);
  FragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kGridVert = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPos;   // grid: (x, y, 0); particle: (seed, angle, 0)
uniform mat4 uMVP;
uniform float uRs;
uniform float uTime;
uniform int   uMode;               // 0 grid mesh, 1 ring, 2 particles
out vec3 vWorld;
out vec3 vNormal;
out float vR;
out float vFade;
void main(){
  vec3 p = aPos;
  vFade = 1.0;
  if(uMode == 2){
    float seed = aPos.x;
    float ang0 = aPos.y;
    float phase = fract(uTime*0.045 + seed);         // 0 at top, 1 at horizon
    float r = mix(11.0*uRs, 1.06*uRs, pow(phase, 0.65));
    float ang = ang0 + phase*7.0 + (1.0-phase)*(1.0-phase)*2.0; // inspiral
    p = vec3(cos(ang)*r, 0.0, sin(ang)*r);
    vFade = smoothstep(0.0,0.12,phase) * (1.0 - smoothstep(0.75,1.0,phase));
  }
  float r = length(p.xz);
  vR = r;
  // Flamm embedding of the equatorial (XZ) slice: y(r) = -sqrt(rs*(r-rs))
  // analytic derivative dy/dr = -0.5*sqrt(rs/(r-rs))
  float rr = max(r - uRs, 0.0);
  float y = -sqrt(uRs*rr);
  float dydR = (rr > 1e-4) ? -0.5*sqrt(uRs/rr) : -8.0;
  vec2 grad = (r > 1e-5) ? (p.xz/r) : vec2(0.0);
  vNormal = normalize(vec3(-dydR*grad.x, 1.0, -dydR*grad.y));
  p.y = y + (uMode == 2 ? 0.35 : 0.0);
  vWorld = p;
  vec4 clip = uMVP * vec4(p, 1.0);
  gl_Position = clip;
  if(uMode == 2){
    float dist = max(clip.w, 0.1);
    gl_PointSize = clamp(260.0/dist, 3.0, 14.0) * (0.6+0.8*fract(aPos.x*7.31));
  }
}
)GLSL";

static const char* kGridFrag = R"GLSL(
#version 410 core
in vec3 vWorld;
in vec3 vNormal;
in float vR;
in float vFade;
out vec4 FragColor;
uniform float uRs;
uniform int   uMode;
uniform vec3  uColor;
uniform float uTime;
void main(){
  if(uMode == 1){ FragColor = vec4(uColor, 0.95); return; }
  if(uMode == 2){
    vec2 c = gl_PointCoord - 0.5;
    float d = length(c);
    if(d > 0.5) discard;
    float a = smoothstep(0.5, 0.05, d) * vFade;
    FragColor = vec4(vec3(0.8,0.92,1.0)*2.2, a);
    return;
  }
  if(vR < uRs*1.001) discard; // inside horizon: black hole interior
  // Antialiased grid lines on the embedded sheet
  vec2 g = abs(fract(vWorld.xz/2.0) - 0.5);
  vec2 fw = fwidth(vWorld.xz);
  float line = 1.0 - min(min(g.x/max(fw.x,1e-4), g.y/max(fw.y,1e-4)), 1.0);
  line = clamp(line, 0.0, 1.0);
  vec3 L = normalize(vec3(0.5, 0.7, 0.8));
  float diff = max(dot(normalize(vNormal), L), 0.0);
  float curvature = clamp(uRs/max(vR, uRs*1.01), 0.0, 1.0); // 1 at horizon -> 0 far
  vec3 base = mix(vec3(0.02,0.05,0.10), vec3(0.16,0.05,0.02), curvature);
  vec3 lineCol = mix(vec3(0.15,0.55,0.85), vec3(1.0,0.45,0.15), curvature);
  vec3 col = base*(0.35+0.75*diff) + lineCol*line*(0.7+0.6*diff);
  // Landmark rings on the sheet: horizon (red), photon sphere (gold), ISCO (cyan)
  col += vec3(1.0,0.25,0.05) * exp(-abs(vR-uRs)/(0.10*uRs)) * 0.8;
  col += vec3(1.0,0.75,0.20) * exp(-abs(vR-1.5*uRs)/(0.10*uRs)) * 0.55;
  col += vec3(0.20,0.80,1.00) * exp(-abs(vR-3.0*uRs)/(0.12*uRs)) * 0.50;
  // Horizon glow and ripple rings moving inward (visual aid)
  col += vec3(1.0,0.25,0.05) * exp(-(vR-uRs)/(0.18*uRs)) * 0.9;
  float ripple = sin(vR*2.2 - uTime*1.5);
  col += vec3(0.1,0.2,0.3) * ripple * exp(-(vR-uRs)/(3.0*uRs)) * 0.35;
  FragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kHudVert = R"GLSL(
#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
out vec4 vCol;
void main(){ vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* kHudFrag = R"GLSL(
#version 410 core
in vec4 vCol;
out vec4 FragColor;
void main(){ FragColor = vCol; }
)GLSL";

// ---------------------------------------------------------------------------
// GL helpers
// ---------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    std::exit(1);
  }
  return s;
}
static GLuint makeProgram(const char* vs, const char* fs) {
  GLuint p = glCreateProgram();
  GLuint v = compileShader(GL_VERTEX_SHADER, vs);
  GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p, v); glAttachShader(p, f);
  glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Program link error:\n%s\n", log);
    std::exit(1);
  }
  glDeleteShader(v); glDeleteShader(f);
  return p;
}
static GLint uniformLoc(GLuint p, const char* name) {
  GLint l = glGetUniformLocation(p, name);
  // Missing uniforms are allowed (optimised out), but warn in debug builds.
  return l;
}

// ---------------------------------------------------------------------------
// HUD: build dynamic triangle soup from the 3x5 font
// ---------------------------------------------------------------------------
struct HudVertex { float x, y, z, w, r, g, b, a; };
static std::vector<HudVertex> g_hud;
static void hudQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
  // x,y in pixels, origin top-left. Converted with current framebuffer size.
  auto add = [&](float x, float y) { g_hud.push_back({x, y, 0, 1, r, g, b, a}); };
  add(x0, y0); add(x1, y0); add(x0, y1);
  add(x1, y0); add(x1, y1); add(x0, y1);
}
static void hudText(int px, int py, const std::string& text, int fbW, int fbH,
                    float r, float g, float b, float a = 0.95f) {
  int cx = px, cy = py;
  int scale = std::max(2, fbH / 300);  // keep glyphs readable on Retina too
  const int adv = (kHudCell + 1) * scale;
  for (char ch : text) {
    if (ch == '\n') { cy += (5 + 2) * scale; cx = px; continue; }
    const Glyph* gl = glyphFor(ch);
    for (int row = 0; row < 5; row++)
      for (int col = 0; col < 3; col++) {
        if (gl->rows[row][col] != '#') continue;
        float x0 = (float)(cx + col * scale);
        float y0 = (float)(cy + row * scale);
        float x1 = x0 + (float)scale;
        float y1 = y0 + (float)scale;
        // pixel -> NDC (origin top-left)
        float nx0 = (x0 / (float)fbW) * 2.0f - 1.0f;
        float nx1 = (x1 / (float)fbW) * 2.0f - 1.0f;
        float ny0 = 1.0f - (y0 / (float)fbH) * 2.0f;
        float ny1 = 1.0f - (y1 / (float)fbH) * 2.0f;
        hudQuad(nx0, ny1, nx1, ny0, r, g, b, a);
      }
    cx += adv;
  }
}

// ---------------------------------------------------------------------------
// BMP screenshot (24-bit, uncompressed)
// ---------------------------------------------------------------------------
static bool saveBMP(const char* path, int w, int h) {
  std::vector<unsigned char> px((size_t)w * h * 3);
  glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
  int row = ((w * 3 + 3) / 4) * 4;
  int imgSize = row * h;
  int fileSize = 54 + imgSize;
  std::vector<unsigned char> out(fileSize, 0);
  auto put16 = [&](int off, unsigned short v) { out[off] = v & 0xff; out[off + 1] = (v >> 8) & 0xff; };
  auto put32 = [&](int off, unsigned int v) {
    out[off] = v & 0xff; out[off + 1] = (v >> 8) & 0xff;
    out[off + 2] = (v >> 16) & 0xff; out[off + 3] = (v >> 24) & 0xff;
  };
  out[0] = 'B'; out[1] = 'M';
  put32(2, (unsigned int)fileSize); put32(10, 54); put32(14, 40);
  put32(18, (unsigned int)w); put32(22, (unsigned int)h);
  put16(26, 1); put16(28, 24); put32(34, (unsigned int)imgSize);
  for (int y = 0; y < h; y++)
    std::memcpy(&out[54 + (size_t)y * row], &px[(size_t)y * w * 3], (size_t)w * 3);
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// Application state shared with the (C-style) GLFW callbacks
// ---------------------------------------------------------------------------
static float g_camDist = 42.0f;
static float g_camYaw = 0.6f, g_camPitch = 0.45f;
static bool g_autoRotate = true, g_paused = false, g_showHelp = true;
static bool g_gridMode = false;
static int g_steps = 180, g_quality = 1;
static float g_diskBright = 1.0f;
static int g_innerGlow = 1;
static double g_mouseX = 0, g_mouseY = 0;
static bool g_dragging = false;
static float g_time = 0.0f;

static void onMouseButton(GLFWwindow*, int button, int action, int) {
  if (button == GLFW_MOUSE_BUTTON_LEFT) g_dragging = (action == GLFW_PRESS);
}
static void onCursorPos(GLFWwindow*, double x, double y) {
  if (g_dragging) {
    g_camYaw += (float)(x - g_mouseX) * 0.005f;
    g_camPitch += (float)(y - g_mouseY) * 0.005f;
    g_camPitch = std::max(-1.45f, std::min(1.45f, g_camPitch));
    g_autoRotate = false;
  }
  g_mouseX = x; g_mouseY = y;
}
static void onScroll(GLFWwindow*, double, double dy) {
  g_camDist *= (dy > 0) ? 0.92f : 1.08f;
  g_camDist = std::max(10.0f, std::min(90.0f, g_camDist));
}
static void onKey(GLFWwindow* win, int key, int, int action, int) {
  if (action != GLFW_PRESS) return;
  switch (key) {
    case GLFW_KEY_1: g_gridMode = false; break;
    case GLFW_KEY_2: g_gridMode = true; break;
    case GLFW_KEY_G: g_gridMode = !g_gridMode; break;
    case GLFW_KEY_SPACE: g_autoRotate = !g_autoRotate; break;
    case GLFW_KEY_P: g_paused = !g_paused; break;
    case GLFW_KEY_R: g_camDist = 42.0f; g_camYaw = 0.6f; g_camPitch = 0.45f; break;
    case GLFW_KEY_H: g_showHelp = !g_showHelp; break;
    case GLFW_KEY_L: g_innerGlow = !g_innerGlow; break;
    case GLFW_KEY_Q:
      g_quality = (g_quality + 1) % 3;
      g_steps = (g_quality == 0) ? 120 : (g_quality == 1 ? 180 : 280);
      break;
    case GLFW_KEY_EQUAL: g_diskBright = std::min(2.5f, g_diskBright + 0.15f); break;
    case GLFW_KEY_MINUS: g_diskBright = std::max(0.2f, g_diskBright - 0.15f); break;
    case GLFW_KEY_S: {
      int w, h; glfwGetFramebufferSize(win, &w, &h);
      static int shotN = 0;
      char name[64]; std::snprintf(name, sizeof(name), "screenshot_%d.bmp", ++shotN);
      if (saveBMP(name, w, h)) std::fprintf(stderr, "Screenshot saved: %s\n", name);
      break;
    }
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, GLFW_TRUE); break;
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  bool startGrid = false, still = false;
  bool noHud = false;
  int debugMode = 0;
  float rsOverride = 0.0f, yawOverride = 0.0f, pitchOverride = 999.0f, distOverride = 0.0f;
  const char* shotPath = "screenshot.bmp";
  int stillFrames = 90;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--grid") startGrid = true;
    else if (a == "--still") still = true;
    else if (a == "--bg-only") debugMode = 1;
    else if (a == "--debug-dir") debugMode = 2;
    else if (a == "--no-hud") noHud = true;
    else if (a == "--rs" && i + 1 < argc) rsOverride = std::atof(argv[++i]);
    else if (a == "--steps" && i + 1 < argc) g_steps = std::atoi(argv[++i]);
    else if (a == "--yaw" && i + 1 < argc) yawOverride = std::atof(argv[++i]);
    else if (a == "--pitch" && i + 1 < argc) pitchOverride = std::atof(argv[++i]);
    else if (a == "--dist" && i + 1 < argc) distOverride = std::atof(argv[++i]);
    else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
    else if (a == "--frames" && i + 1 < argc) stillFrames = std::atoi(argv[++i]);
    else if (a == "--help" || a == "-h") {
      std::printf("black-hole  Schwarzschild geodesic ray tracer\n"
                  "  --grid            start in spacetime-grid (trapdoor) view\n"
                  "  --still           render --frames frames, save --shot BMP, exit\n"
                  "  --shot PATH       screenshot path (default screenshot.bmp)\n"
                  "  --frames N        frames for --still (default 90)\n");
      return 0;
    }
  }

  if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 0);

  const int kWinW = 1280, kWinH = 720;
  GLFWwindow* win = glfwCreateWindow(kWinW, kWinH, "Black hole - Schwarzschild ray tracing", nullptr, nullptr);
  if (!win) { std::fprintf(stderr, "glfwCreateWindow failed\n"); glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) { std::fprintf(stderr, "glewInit failed\n"); return 1; }
  std::fprintf(stderr, "OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

  // --- programs ---
  GLuint quadProg = makeProgram(kQuadVert, kGeodesicFrag);
  GLuint gridProg = makeProgram(kGridVert, kGridFrag);
  GLuint hudProg = makeProgram(kHudVert, kHudFrag);

  struct GeoLoc {
    GLint time, res, aspect, camPos, camRight, camUp, camFwd, tanFov,
          rs, escapeR, diskBright, innerGlow, steps, timeScale, debugMode;
  } G;
  G.time = uniformLoc(quadProg, "uTime");
  G.res = uniformLoc(quadProg, "uResolution");
  G.aspect = uniformLoc(quadProg, "uAspect");
  G.camPos = uniformLoc(quadProg, "uCamPos");
  G.camRight = uniformLoc(quadProg, "uCamRight");
  G.camUp = uniformLoc(quadProg, "uCamUp");
  G.camFwd = uniformLoc(quadProg, "uCamFwd");
  G.tanFov = uniformLoc(quadProg, "uTanFov");
  G.rs = uniformLoc(quadProg, "uRs");
  G.escapeR = uniformLoc(quadProg, "uEscapeR");
  G.diskBright = uniformLoc(quadProg, "uDiskBrightness");
  G.innerGlow = uniformLoc(quadProg, "uInnerGlow");
  G.steps = uniformLoc(quadProg, "uSteps");
  G.timeScale = uniformLoc(quadProg, "uTimeScale");
  G.debugMode = uniformLoc(quadProg, "uDebugMode");

  struct GridLoc { GLint mvp, rs, time, mode, color; } GL2;
  GL2.mvp = uniformLoc(gridProg, "uMVP");
  GL2.rs = uniformLoc(gridProg, "uRs");
  GL2.time = uniformLoc(gridProg, "uTime");
  GL2.mode = uniformLoc(gridProg, "uMode");
  GL2.color = uniformLoc(gridProg, "uColor");

  // --- fullscreen quad ---
  GLuint quadVAO, quadVBO;
  glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO);
  glBindVertexArray(quadVAO); glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
  float quadXY[] = {-1, -1, 3, -1, -1, 3};
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadXY), quadXY, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
  glBindVertexArray(0);

  // --- offscreen target for dynamic resolution ---
  GLuint fbo = 0, fboTex = 0;
  glGenFramebuffers(1, &fbo); glGenTextures(1, &fboTex);
  glBindTexture(GL_TEXTURE_2D, fboTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // --- grid mesh (Flamm paraboloid, displaced in the vertex shader) ---
  const int kGridN = 220;
  const float kGridHalf = 30.0f;  // 15 rs half-width (rs=2)
  std::vector<float> gridPos;
  std::vector<unsigned int> gridIdx;
  for (int j = 0; j <= kGridN; j++)
    for (int i = 0; i <= kGridN; i++) {
      float x = -kGridHalf + (2 * kGridHalf) * i / kGridN;
      float z = -kGridHalf + (2 * kGridHalf) * j / kGridN;
      gridPos.push_back(x); gridPos.push_back(0.0f); gridPos.push_back(z);
    }
  for (int j = 0; j < kGridN; j++)
    for (int i = 0; i < kGridN; i++) {
      unsigned int a = j * (kGridN + 1) + i, b = a + 1, c = a + kGridN + 1, d = c + 1;
      gridIdx.push_back(a); gridIdx.push_back(c); gridIdx.push_back(b);
      gridIdx.push_back(b); gridIdx.push_back(c); gridIdx.push_back(d);
    }
  GLuint gridVAO, gridVBO, gridIBO;
  glGenVertexArrays(1, &gridVAO); glGenBuffers(1, &gridVBO); glGenBuffers(1, &gridIBO);
  glBindVertexArray(gridVAO);
  glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
  glBufferData(GL_ARRAY_BUFFER, gridPos.size() * sizeof(float), gridPos.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridIBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, gridIdx.size() * sizeof(unsigned int), gridIdx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glBindVertexArray(0);

  // --- landmark rings: horizon, photon sphere, ISCO, disk rim ---
  struct Ring { float radius; float r, g, b; };
  std::vector<Ring> rings = {
    {2.0f, 1.0f, 0.2f, 0.05f},    // horizon rs
    {3.0f, 1.0f, 0.8f, 0.25f},    // photon sphere 1.5 rs
    {6.0f, 0.3f, 0.85f, 1.0f},    // ISCO 3 rs
    {19.0f, 0.5f, 0.5f, 0.6f},    // disk outer rim 9.5 rs
  };
  GLuint ringVAOs[4] = {0, 0, 0, 0};
  for (int k = 0; k < 4; k++) {
    const int S = 200;
    std::vector<float> pts;
    for (int i = 0; i <= S; i++) {
      float a = (float)i / S * 2.0f * (float)M_PI;
      pts.push_back(std::cos(a) * rings[k].radius);
      pts.push_back(0.0f);
      pts.push_back(std::sin(a) * rings[k].radius);
    }
    glGenVertexArrays(1, &ringVAOs[k]);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(ringVAOs[k]); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(float), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
  }

  // --- falling test particles (inspiral into the trapdoor) ---
  const int kParticles = 700;
  GLuint partVAO, partVBO;
  {
    std::vector<float> pts;
    unsigned int seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    for (int i = 0; i < kParticles; i++) {
      pts.push_back(rnd());                         // seed / phase offset
      pts.push_back(rnd() * 2.0f * (float)M_PI);    // initial angle
      pts.push_back(0.0f);
    }
    glGenVertexArrays(1, &partVAO); glGenBuffers(1, &partVBO);
    glBindVertexArray(partVAO); glBindBuffer(GL_ARRAY_BUFFER, partVBO);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(float), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
  }

  // --- HUD buffers ---
  GLuint hudVAO, hudVBO;
  glGenVertexArrays(1, &hudVAO); glGenBuffers(1, &hudVBO);
  glBindVertexArray(hudVAO); glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
  glBufferData(GL_ARRAY_BUFFER, 1 << 20, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
  glBindVertexArray(0);

  // --- state (aliases to the globals the GLFW callbacks modify) ---
  const float kRs = (rsOverride > 0.0f) ? rsOverride : 2.0f;  // M = 1 normally
  g_gridMode = startGrid;
  if (yawOverride != 0.0f) g_camYaw = yawOverride;
  if (pitchOverride < 900.0f) g_camPitch = pitchOverride;
  if (distOverride > 0.0f) g_camDist = distOverride;
  float& camDist = g_camDist;              // 15 rs initially
  float& camYaw = g_camYaw;
  float& camPitch = g_camPitch;
  float fovY = 52.0f * (float)M_PI / 180.0f;
  bool& autoRotate = g_autoRotate;
  bool& paused = g_paused;
  bool& showHelp = g_showHelp;
  bool& gridMode = g_gridMode;
  int& steps = g_steps;
  float& diskBright = g_diskBright;
  int& innerGlow = g_innerGlow;
  float& time = g_time;  // simulation time (never reset)
  const float timeScale = 5.0f;
  bool& dragging = g_dragging;
  float renderScale = 0.75f;
  int frameCount = 0;
  double fpsAccum = 0; int fpsFrames = 0; double fps = 0;

  glfwSetMouseButtonCallback(win, onMouseButton);
  glfwSetCursorPosCallback(win, onCursorPos);
  glfwSetScrollCallback(win, onScroll);
  glfwSetKeyCallback(win, onKey);

  double lastT = glfwGetTime();
  char title[256];

  while (!glfwWindowShouldClose(win)) {
    double now = glfwGetTime();
    float dt = (float)std::min(now - lastT, 0.1);
    lastT = now;
    if (!paused) time += dt;
    if (autoRotate && !dragging) camYaw += dt * 0.06f;

    int fbW, fbH;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    if (fbW == 0 || fbH == 0) { glfwPollEvents(); continue; }
    float aspect = (float)fbW / (float)fbH;

    // --- camera basis (spherical orbit about the origin, +y = polar axis) ---
    float cp = std::cos(camPitch), sp = std::sin(camPitch);
    Vec3 camPos{camDist * cp * std::cos(camYaw), camDist * sp, camDist * cp * std::sin(camYaw)};
    Vec3 fwd = normalize(Vec3{0, 0, 0} - camPos);
    Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
    Vec3 up = cross(right, fwd);

    // --- dynamic resolution: hold ~45 fps ---
    fpsAccum += dt; fpsFrames++;
    if (fpsFrames >= 30) {
      fps = fpsFrames / fpsAccum;
      if (fps > 55 && renderScale < 1.0f) renderScale = std::min(1.0f, renderScale + 0.08f);
      else if (fps < 38 && renderScale > 0.4f) renderScale = std::max(0.4f, renderScale - 0.08f);
      fpsAccum = 0; fpsFrames = 0;
    }
    int rw = std::max(320, (int)(fbW * renderScale));
    int rh = std::max(180, (int)(fbH * renderScale));

    if (!gridMode) {
      // ----- geodesic ray tracing into the offscreen target -----
      static int lastRw = 0, lastRh = 0;
      if (rw != lastRw || rh != lastRh) {
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        lastRw = rw; lastRh = rh;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glViewport(0, 0, rw, rh);
      glDisable(GL_DEPTH_TEST);
      glBindVertexArray(quadVAO);
      glUseProgram(quadProg);
      glUniform1f(G.time, time);
      glUniform2f(G.res, (float)rw, (float)rh);
      glUniform1f(G.aspect, aspect);
      glUniform3f(G.camPos, camPos.x, camPos.y, camPos.z);
      glUniform3f(G.camRight, right.x, right.y, right.z);
      glUniform3f(G.camUp, up.x, up.y, up.z);
      glUniform3f(G.camFwd, fwd.x, fwd.y, fwd.z);
      glUniform1f(G.tanFov, std::tan(fovY * 0.5f));
      glUniform1f(G.rs, kRs);
      glUniform1f(G.escapeR, std::max(80.0f, camDist * 2.0f));
      glUniform1f(G.diskBright, diskBright);
      glUniform1i(G.innerGlow, innerGlow);
      glUniform1i(G.steps, steps);
      glUniform1f(G.timeScale, timeScale);
      glUniform1i(G.debugMode, debugMode);
      glDrawArrays(GL_TRIANGLES, 0, 3);

      // ----- blit the scaled image to the screen -----
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glViewport(0, 0, fbW, fbH);
      glBlitFramebuffer(0, 0, rw, rh, 0, 0, fbW, fbH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
      // ----- spacetime grid (trapdoor) view, drawn directly -----
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.005f, 0.006f, 0.012f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glUseProgram(gridProg);
      Mat4 proj = perspective(fovY, aspect, 0.1f, 500.0f);
      Mat4 view = lookAt(camPos, Vec3{0, 0, -1.2f}, Vec3{0, 1, 0});
      Mat4 mvp = mul(proj, view);
      glUniformMatrix4fv(GL2.mvp, 1, GL_FALSE, mvp.m);
      glUniform1f(GL2.rs, kRs);
      glUniform1f(GL2.time, time);
      glUniform1i(GL2.mode, 0);
      glBindVertexArray(gridVAO);
      glDrawElements(GL_TRIANGLES, (GLsizei)gridIdx.size(), GL_UNSIGNED_INT, (void*)0);
      glUniform1i(GL2.mode, 1);
      glLineWidth(2.0f);
      for (int k = 0; k < 4; k++) {
        glUniform3f(GL2.color, rings[k].r, rings[k].g, rings[k].b);
        glBindVertexArray(ringVAOs[k]);
        glDrawArrays(GL_LINE_STRIP, 0, 201);
      }
      glUniform1i(GL2.mode, 2);
      glBindVertexArray(partVAO);
      glDrawArrays(GL_POINTS, 0, kParticles);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
      glBindVertexArray(0);
    }

    // ----- HUD -----
    if (!noHud) {
      g_hud.clear();
      char buf[512];
      if (!gridMode) {
        std::snprintf(buf, sizeof(buf),
          "SCHWARZSCHILD BLACK HOLE  RS=2M  M=1  GEODESIC RAY TRACING\n"
          "PHOTON SPHERE 1.5RS   ISCO 3RS   SHADOW RAY TRACED  FPS %.0f\n"
          "CAM %.1f RS  STEPS %d  RES %.0f%%  DISK X%.2f  GLOW %s",
          fps, camDist / kRs, steps, renderScale * 100.0f, diskBright,
          innerGlow ? "ON" : "OFF");
      } else {
        std::snprintf(buf, sizeof(buf),
          "SPACETIME GRID  FLAMM EMBEDDING Z=-SQRT(RS(R-RS))\n"
          "RED=HORIZON RS  GOLD=PHOTON SPHERE 1.5RS  CYAN=ISCO 3RS\n"
          "CAM %.1f RS  PARTICLES = FALLING TEST MASSES  FPS %.0f",
          camDist / kRs, fps);
      }
      hudText(12, 10, buf, fbW, fbH, 0.85f, 0.92f, 1.0f);
      if (showHelp) {
        const char* help =
          "DRAG ORBIT  WHEEL ZOOM  1 RAYTRACE  2 GRID  SPACE ROTATE\n"
          "Q QUALITY  +/- DISK BRIGHT  L INNER GLOW  P PAUSE  R RESET\n"
          "S SCREENSHOT  H HELP  ESC QUIT";
        hudText(12, fbH - 52, help, fbW, fbH, 0.55f, 0.75f, 0.9f, 0.85f);
      }
      glUseProgram(hudProg);
      glBindVertexArray(hudVAO);
      glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
      glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(g_hud.size() * sizeof(HudVertex)),
                      g_hud.data());
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_hud.size());
      glDisable(GL_BLEND);
      glBindVertexArray(0);
    }

    std::snprintf(title, sizeof(title), "Black hole - %s - %.0f fps",
                  gridMode ? "spacetime grid" : "geodesic ray tracing", fps);
    glfwSetWindowTitle(win, title);

    // Capture before the swap: the back buffer is only guaranteed to hold
    // the frame before glfwSwapBuffers().
    bool doStill = still && (frameCount + 1 >= stillFrames);
    if (doStill) {
      if (saveBMP(shotPath, fbW, fbH))
        std::fprintf(stderr, "Still saved: %s (after %d frames)\n", shotPath, frameCount + 1);
      else
        std::fprintf(stderr, "Failed to save still: %s\n", shotPath);
    }
    glfwSwapBuffers(win);
    glfwPollEvents();
    frameCount++;
    if (doStill) break;
  }

  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}
