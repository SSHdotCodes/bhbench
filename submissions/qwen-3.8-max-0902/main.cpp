// ============================================================================
//  SCHWARZSCHILD BLACK HOLE — real-time GPU geodesic ray tracer
//
//  Physics implemented (G = c = 1 units, r_s = 2M = 1 world unit):
//
//   1. Gravitational lensing — every pixel traces a light ray *backwards*
//      along an exact null geodesic of the Schwarzschild metric. The ray
//      path obeys the relativistic Binet equation
//
//          d²u/dφ² = −u + (3/2) r_s u²,      u = 1/r
//
//      integrated with adaptive-step RK4. This reproduces the photon
//      sphere (r = 1.5 r_s), the shadow (b_crit = 3√3/2 r_s), multiple
//      winding, Einstein-ring-like higher order images, and the lensed
//      "halo" images of the far side of the disk bent over/under the hole
//      (the Interstellar look) — all emergent, no fakery.
//
//   2. Accretion disk — razor-thin equatorial disk from the ISCO
//      (r = 3 r_s = 6M, exact Schwarzschild ISCO) to 13 r_s.
//        * Shakura–Sunyaev effective temperature
//              T(r) = T_d (r/r_in)^-3/4 (1 − √(r_in/r))^1/4
//        * Exact observed redshift factor for a circular Keplerian
//          emitter and an observer at infinity, from the conserved
//          photon energy E and axial angular momentum L_z:
//              g = ν_obs/ν_em = 1 / [ u^t (1 − Ω L_z/E) ]
//              u^t = (1 − 3M/r)^-1/2,   Ω = √(M/r³)
//          (combines gravitational redshift, transverse & radial
//           Doppler, and beaming exactly)
//        * Observed blackbody colour at temperature T_obs = g·T and
//          bolometric intensity ∝ g⁴T⁴ (relativistic beaming: the
//          approaching side is dramatically brighter/bluer).
//        * Front-to-back compositing of multiple ray/disk crossings
//          (the disk is semi-transparent → lensed secondary images).
//
//   3. Spacetime curvature — Flamm's paraboloid, the exact isometric
//      embedding of a t = const equatorial slice of Schwarzschild:
//              z(r) = −2 √( r_s (r − r_s) )
//      drawn as a "trapdoor" grid, with test particles inspiralling on
//      Keplerian orbits mapped onto the curved surface.
//
//  Runs at 60 fps on Apple Silicon; internal render scale adapts.
// ============================================================================

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static const float RS = 1.0f;          // Schwarzschild radius (world units)
static const float DISK_IN  = 3.0f;    // ISCO = 3 r_s = 6M
static const float DISK_OUT = 13.0f;

// ----------------------------------------------------------------------------
// Tiny mat4 math (column-major, OpenGL convention)
// ----------------------------------------------------------------------------
struct Mat4 { float m[16]; };
struct Vec3 {
    float x, y, z;
    Vec3() {}
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x+o.x, y+o.y, z+o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x-o.x, y-o.y, z-o.z); }
    Vec3 operator*(float s)     const { return Vec3(x*s, y*s, z*s); }
    float dot(const Vec3& o)    const { return x*o.x + y*o.y + z*o.z; }
    Vec3 cross(const Vec3& o)   const {
        return Vec3(y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x);
    }
    float len() const { return sqrtf(x*x + y*y + z*z); }
    Vec3 norm() const { float l = len(); return l > 1e-9f ? (*this)*(1.0f/l) : Vec3(0,0,0); }
};

static Mat4 mat4Identity() {
    Mat4 r; memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r;
}
static Mat4 mat4Mul(const Mat4& a, const Mat4& b) { // a * b
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k*4+rw] * b.m[c*4+k];
            r.m[c*4+rw] = s;
        }
    return r;
}
static Mat4 mat4Perspective(float fovyDeg, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovyDeg * 0.5f * (float)M_PI / 180.0f);
    Mat4 r; memset(r.m, 0, sizeof(r.m));
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
static Mat4 mat4LookAt(Vec3 eye, Vec3 ctr, Vec3 upv) {
    Vec3 f = (ctr - eye).norm();
    Vec3 s = f.cross(upv).norm();
    Vec3 u = s.cross(f);
    Mat4 r = mat4Identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -s.dot(eye);
    r.m[13] = -u.dot(eye);
    r.m[14] = f.dot(eye);
    return r;
}

// ----------------------------------------------------------------------------
// App state
// ----------------------------------------------------------------------------
struct Camera {
    float yaw   = 0.55f;
    float pitch = 0.16f;
};
struct App {
    int   mode      = 1;      // 1 = ray traced, 2 = spacetime grid, 3 = split
    float dist[4]   = {0, 20.0f, 34.0f, 26.0f};   // per-mode camera distance
    bool  paused    = false;
    bool  diskOn    = true;
    float simTime   = 0.0f;
    float exposure  = 1.0f;
    float diskBright= 2.00f;
    int   maxSteps  = 380;
    Camera cam;
    // adaptive resolution
    float scales[5] = {0.4f, 0.55f, 0.7f, 0.85f, 1.0f};
    int   scaleIdx  = 2;
    float frameMsEMA= 16.0f;
    int   frameCount= 0;
    // input
    bool  dragging  = false;
    double lastX = 0, lastY = 0;
    // shot mode
    const char* shotPath = nullptr;
    int   shotFrames = 90;
} g_app;

static GLFWwindow* g_win = nullptr;
static int g_fbW = 1600, g_fbH = 900;

// ----------------------------------------------------------------------------
// Shader sources
// ----------------------------------------------------------------------------
static const char* VERT_FULLSCREEN = R"GLSL(
#version 410
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

static const char* FRAG_RAYTRACE = R"GLSL(
#version 410
precision highp float;
out vec4 fragColor;

uniform vec2  uRes;
uniform float uTime;
uniform float uTanHalf;
uniform float uRs;
uniform float uEscR;
uniform float uExposure;
uniform float uDiskBright;
uniform int   uMaxSteps;
uniform int   uDiskOn;
uniform vec3  uCamPos;
uniform vec3  uFwd;
uniform vec3  uRight;
uniform vec3  uUp;

const float PI = 3.14159265359;
const float DISK_IN  = 3.0;    // ISCO = 6M = 3 rs
const float DISK_OUT = 13.0;
const float T_DISK   = 12000.0; // Shakura-Sunyaev normalization (K), set by Mdot

// ---------------- procedural noise ----------------
float h21(vec2 p) {
    p = fract(p * vec2(127.31, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = h21(i), b = h21(i + vec2(1, 0));
    float c = h21(i + vec2(0, 1)), d = h21(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; }
    return s;
}

// ---------------- blackbody radiator -> sRGB (Tanner Helland / Bartlett fit) -------------
vec3 blackbody(float kelvin) {
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float r, g, b;
    if (t <= 66.0) r = 255.0;
    else r = 329.698727446 * pow(max(t - 60.0, 0.0), -0.1332047592);
    if (t <= 66.0) g = 99.4708025861 * log(max(t, 0.001)) - 161.1195681661;
    else g = 288.1221695283 * pow(max(t - 60.0, 0.0), -0.0755148492);
    if (t >= 66.0) b = 255.0;
    else if (t <= 19.0) b = 0.0;
    else b = 138.5177312231 * log(max(t - 10.0, 0.001)) - 305.0447927307;
    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}

// ---------------- procedural starfield + milky way ----------------
vec3 stars(vec3 d) {
    float ph = acos(clamp(d.y, -1.0, 1.0));
    float th = atan(d.z, d.x);
    vec2 sph = vec2(th / (2.0 * PI) + 0.5, ph / PI);
    vec3 c = vec3(0.0);
    for (int L = 0; L < 3; L++) {
        float fl = float(L);
        float scale = (L == 0) ? 110.0 : ((L == 1) ? 260.0 : 520.0);
        float thr   = (L == 0) ? 0.86 : ((L == 1) ? 0.93 : 0.965);
        float bri   = (L == 0) ? 3.4 : ((L == 1) ? 1.7 : 0.8);
        vec2 p = sph * vec2(scale * 2.0, scale) + fl * 41.7;
        vec2 i = floor(p), f = fract(p);
        float h1 = h21(i + fl * 17.3);
        if (h1 > thr) {
            vec2 sp = vec2(h21(i + vec2(3.1, 7.7) + fl * 5.0),
                           h21(i + vec2(9.3, 2.9) + fl * 3.0));
            float dd = length(f - sp);
            float mag = pow((h1 - thr) / (1.0 - thr), 0.6) * h21(i + vec2(4.4, 8.8));
            float I = bri * mag * (smoothstep(0.10, 0.0, dd) * 2.2 +
                                   smoothstep(0.26, 0.04, dd) * 0.16);
            c += blackbody(mix(2700.0, 9800.0, h21(i + vec2(1.7, 5.3)))) * I;
        }
    }
    vec3 bn = normalize(vec3(0.38, 0.82, -0.42));
    float band = exp(-pow(dot(d, bn), 2.0) * 13.0);
    float n    = fbm(sph * vec2(15.0, 8.5));
    float dust = fbm(sph * vec2(27.0, 15.0) + 7.0);
    c += band * (0.028 + 0.052 * n) * (0.35 + 0.65 * dust) * vec3(0.72, 0.76, 0.98) * 1.15;
    c += vec3(0.0016, 0.0018, 0.0028);
    return c;
}

// ---------------- disk emission at a ray/disk-plane intersection ----------------
// pc  : intersection point (world), rc : |pc|, by : signed L_z/E of the photon
// (conserved, computed at the camera), returns emitted radiance, alpha out.
vec3 diskEmission(vec3 pc, float rc, float by, out float alpha) {
    float rs = uRs;
    float az = atan(pc.z, pc.x);

    // Keplerian orbital frequency (M = rs/2), exact 4-velocity factor
    float Om = sqrt(rs / (2.0 * rc * rc * rc));
    float ut = 1.0 / sqrt(max(1.0 - 1.5 * rs / rc, 1e-4));

    // g = nu_obs / nu_em = 1 / [u^t (1 - Omega * b)]   (exact for circular orbit)
    float g = 1.0 / (ut * (1.0 - Om * by));
    g = clamp(g, 0.05, 30.0);

    // Shakura-Sunyaev thin-disk temperature profile (zero torque at ISCO)
    float T = T_DISK * pow(rc / DISK_IN, -0.75) *
              pow(max(1.0 - sqrt(DISK_IN / rc), 0.0), 0.25);
    float Tobs = clamp(T * g, 900.0, 40000.0);

    // turbulent gas pattern co-rotating at Omega (frozen into the flow)
    float azp  = az + Om * uTime * 8.0;
    float dens = 0.80 + 0.38 * fbm(vec2(rc * 1.1, azp * 2.3))
                      + 0.16 * fbm(vec2(rc * 3.9, azp * 6.1));
    dens = clamp(dens, 0.25, 1.7);

    float fade = smoothstep(DISK_IN - 0.02, DISK_IN + 0.7, rc) *
                 (1.0 - smoothstep(DISK_OUT - 2.5, DISK_OUT, rc));
    alpha = clamp(0.85 * fade * clamp(dens, 0.3, 1.4), 0.0, 1.0);

    // observed specific intensity: blackbody at T_obs, I ~ g^4 (beaming)
    vec3 emis = blackbody(Tobs) * pow(clamp(Tobs / 5772.0, 0.02, 12.0), 4.0)
                * uDiskBright * clamp(dens, 0.3, 1.7);
    return emis;
}

vec3 aces(vec3 x) {
    x *= 0.62;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 ndc = (gl_FragCoord.xy / uRes) * 2.0 - 1.0;
    float aspect = uRes.x / uRes.y;
    vec3 d = normalize(uFwd + uRight * (ndc.x * uTanHalf * aspect)
                            + uUp    * (ndc.y * uTanHalf));
    vec3 x0 = uCamPos;
    float r0 = length(x0);
    float rs = uRs;

    vec3 col = vec3(0.0);
    float trans = 1.0;

    vec3 cr = cross(x0, d);
    float crossLen = length(cr);

    // ---- degenerate radial ray: no angular momentum, path is a straight line ----
    if (crossLen < 1e-5) {
        if (abs(d.y) > 1e-6) {
            float tHit = -x0.y / d.y;
            if (tHit > 0.0 && uDiskOn == 1) {
                vec3 p = x0 + d * tHit;
                float r = length(p);
                if (r > DISK_IN && r < DISK_OUT) {
                    float a; vec3 e = diskEmission(p, r, 0.0, a);
                    col += trans * e * a; trans *= (1.0 - a);
                }
            }
        }
        col += trans * stars(d);
    } else {
        // ---- orbit plane basis: e1 toward camera, e2 along transverse motion ----
        vec3 e1 = normalize(x0);
        vec3 perp = d - e1 * dot(d, e1);
        vec3 e2 = normalize(perp);
        float A = e1.y, B = e2.y;             // y(φ) ∝ A cosφ + B sinφ
        float C = length(vec2(A, B));         // 0 ⇒ ray lies in the disk plane
        bool equatorial = (C < 4e-3);

        // conserved quantities (exact): axial impact parameter L_z/E
        float by = cr.y / (1.0 - rs / r0);

        // Binet state: u = 1/r, w = du/dφ
        float u = 1.0 / r0;
        float w = -dot(d, e1) / (r0 * max(dot(d, e2), 1e-6));
        float phi = 0.0;

        float hBase = 0.030;
        bool escaped = false, captured = false;
        vec3 escDir = d;
        float sPrev = A;

        for (int i = 0; i < uMaxSteps; i++) {
            float rr = 1.0 / max(u, 1e-9);
            float hs = hBase * clamp(0.25 * rr, 0.08, 1.0);
            hs = min(hs, 0.6 / max(abs(w), 1e-4));

            // ---- RK4 on  d²u/dφ² = −u + 1.5 rs u² ----
            vec2 y0 = vec2(u, w);
            vec2 k1 = vec2(y0.y, -y0.x + 1.5 * rs * y0.x * y0.x);
            vec2 y1 = y0 + 0.5 * hs * k1;
            vec2 k2 = vec2(y1.y, -y1.x + 1.5 * rs * y1.x * y1.x);
            vec2 y2 = y0 + 0.5 * hs * k2;
            vec2 k3 = vec2(y2.y, -y2.x + 1.5 * rs * y2.x * y2.x);
            vec2 y3 = y0 + hs * k3;
            vec2 k4 = vec2(y3.y, -y3.x + 1.5 * rs * y3.x * y3.x);
            vec2 yN = y0 + (hs / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
            float phiNew = phi + hs;
            float sNew = A * cos(phiNew) + B * sin(phiNew);

            if (uDiskOn == 1) {
                if (!equatorial) {
                    // analytic equatorial-plane crossing inside this step
                    if (sPrev * sNew < 0.0) {
                        float tc = sPrev / (sPrev - sNew);
                        float uc = mix(u, yN.x, clamp(tc, 0.0, 1.0));
                        float phic = phi + hs * tc;
                        float rc = 1.0 / max(uc, 1e-9);
                        if (rc > DISK_IN && rc < DISK_OUT) {
                            vec3 pc = (e1 * cos(phic) + e2 * sin(phic)) * rc;
                            float a; vec3 e = diskEmission(pc, rc, by, a);
                            col += trans * e * a;
                            trans *= (1.0 - a);
                        }
                    }
                } else {
                    // ray trapped in the disk plane: integrate emission along path
                    if (rr > DISK_IN && rr < DISK_OUT) {
                        vec3 pc = (e1 * cos(phi) + e2 * sin(phi)) * rr;
                        float a; vec3 e = diskEmission(pc, rr, by, a);
                        float aa = clamp(a * hs * 1.6, 0.0, 1.0);
                        col += trans * e * aa;
                        trans *= (1.0 - aa);
                    }
                }
            }

            sPrev = sNew; u = yN.x; w = yN.y; phi = phiNew;

            if (u * rs >= 1.0) { captured = true; break; }   // crossed the horizon
            if (u < 1.0 / uEscR && w < 0.0) {                // escaped to infinity
                float drd = -w / (u * u);
                vec3 er = e1 * cos(phi) + e2 * sin(phi);
                vec3 ef = -e1 * sin(phi) + e2 * cos(phi);
                escDir = normalize(drd * er + (1.0 / u) * ef);
                escaped = true;
                break;
            }
            if (phi > 44.0) break;                           // ~7 windings: ring photon
        }

        if (escaped) {
            col += trans * stars(escDir);
        } else if (!captured) {
            float rEnd = 1.0 / max(u, 1e-9);
            if (rEnd > r0 * 0.85) {                          // ran out of steps, far away
                float drd = -w / (u * u);
                vec3 er = e1 * cos(phi) + e2 * sin(phi);
                vec3 ef = -e1 * sin(phi) + e2 * cos(phi);
                escDir = normalize(drd * er + (1.0 / u) * ef);
                col += trans * stars(escDir);
            }
        }
        // captured rays: col already holds any disk emission picked up en route
    }

    col *= uExposure;
    col = aces(col);
    fragColor = vec4(col, 1.0);
}
)GLSL";

static const char* VERT_LINE = R"GLSL(
#version 410
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uMVP;
out vec3 vCol;
void main() {
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FRAG_LINE = R"GLSL(
#version 410
precision highp float;
in vec3 vCol;
out vec4 fragColor;
void main() { fragColor = vec4(vCol, 1.0); }
)GLSL";

static const char* VERT_POINT = R"GLSL(
#version 410
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
layout(location = 2) in float aSize;
uniform mat4 uMVP;
uniform float uPointScale;
out vec3 vCol;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = clamp(aSize * uPointScale / max(gl_Position.w, 0.001), 1.0, 48.0);
    vCol = aCol;
}
)GLSL";

static const char* FRAG_POINT = R"GLSL(
#version 410
precision highp float;
in vec3 vCol;
out vec4 fragColor;
void main() {
    float d = length(gl_PointCoord - vec2(0.5));
    float a = smoothstep(0.5, 0.04, d);
    fragColor = vec4(vCol * a, a);
}
)GLSL";

// ----------------------------------------------------------------------------
// GL helpers
// ----------------------------------------------------------------------------
static GLuint compileShader(GLenum type, const char* src, const char* what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "SHADER COMPILE ERROR (%s):\n%s\n", what, log);
        exit(1);
    }
    return s;
}
static GLuint makeProgram(const char* vs, const char* fs, const char* what) {
    GLuint p = glCreateProgram();
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, what);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, what);
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "PROGRAM LINK ERROR (%s):\n%s\n", what, log);
        exit(1);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ----------------------------------------------------------------------------
// Ray-trace render target (adaptive resolution FBO)
// ----------------------------------------------------------------------------
static GLuint g_fbo = 0, g_fboTex = 0;
static int g_rtW = 0, g_rtH = 0;

static void ensureFBO(int w, int h) {
    if (g_rtW == w && g_rtH == h && g_fbo) return;
    if (g_fboTex) glDeleteTextures(1, &g_fboTex);
    if (g_fbo) glDeleteFramebuffers(1, &g_fbo);
    g_rtW = w; g_rtH = h;
    glGenFramebuffers(1, &g_fbo);
    glGenTextures(1, &g_fboTex);
    glBindTexture(GL_TEXTURE_2D, g_fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fboTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete!\n"); exit(1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ----------------------------------------------------------------------------
// Spacetime grid: Flamm paraboloid  z(r) = -2 sqrt(rs (r - rs))
// ----------------------------------------------------------------------------
static GLuint g_gridVAO = 0, g_gridVBO = 0;
static int g_gridVerts = 0;
static const int GRID_SPOKES = 128;
static const int GRID_RINGS  = 72;
static const float GRID_RMAX = 18.0f;

// Flamm paraboloid: throat (horizon) at the bottom, surface rising to the
// asymptotically flat plane — the classic "trapdoor" depression.
static float embedY(float r) { return 2.0f * sqrtf(RS * std::max(r - RS, 0.0f)); }

static Vec3 gridColor(float r) {
    float t = std::clamp((r - RS) / (GRID_RMAX - RS), 0.0f, 1.0f);
    Vec3 nearC(1.00f, 0.55f, 0.16f), midC(0.16f, 0.80f, 1.00f), farC(0.10f, 0.22f, 0.65f);
    float s1 = std::clamp(t / 0.32f, 0.0f, 1.0f);
    float s2 = std::clamp((t - 0.25f) / 0.75f, 0.0f, 1.0f);
    s1 = s1 * s1 * (3 - 2 * s1); s2 = s2 * s2 * (3 - 2 * s2);
    Vec3 c(nearC.x + (midC.x - nearC.x) * s1,
           nearC.y + (midC.y - nearC.y) * s1,
           nearC.z + (midC.z - nearC.z) * s1);
    return Vec3(c.x + (farC.x - c.x) * s2, c.y + (farC.y - c.y) * s2, c.z + (farC.z - c.z) * s2);
}

static void buildGrid() {
    std::vector<float> v; // pos3 col3
    auto pushPt = [&](float r, float ang, float boost) {
        float y = embedY(r);
        Vec3 c = gridColor(r);
        v.push_back(r * cosf(ang)); v.push_back(y); v.push_back(r * sinf(ang));
        v.push_back(c.x * boost);   v.push_back(c.y * boost); v.push_back(c.z * boost);
    };
    // ring radii: denser near the horizon (r ∝ 1 + t^1.8)
    std::vector<float> rr(GRID_RINGS);
    for (int i = 0; i < GRID_RINGS; i++) {
        float t = (float)i / (GRID_RINGS - 1);
        rr[i] = RS * 1.0008f + (GRID_RMAX - RS) * powf(t, 1.8f);
    }
    // radial spokes
    for (int j = 0; j < GRID_SPOKES; j++) {
        float a = 2.0f * (float)M_PI * j / GRID_SPOKES;
        for (int i = 0; i < GRID_RINGS - 1; i++) {
            pushPt(rr[i], a, 1.0f); pushPt(rr[i + 1], a, 1.0f);
        }
    }
    // concentric rings
    for (int i = 0; i < GRID_RINGS; i++) {
        float dim = 0.75f;
        for (int j = 0; j < GRID_SPOKES; j++) {
            float a0 = 2.0f * (float)M_PI * j / GRID_SPOKES;
            float a1 = 2.0f * (float)M_PI * (j + 1) / GRID_SPOKES;
            pushPt(rr[i], a0, dim); pushPt(rr[i], a1, dim);
        }
    }
    // horizon ring (r = rs) and photon sphere ring (r = 1.5 rs)
    for (int j = 0; j < 256; j++) {
        float a0 = 2.0f * (float)M_PI * j / 256, a1 = 2.0f * (float)M_PI * (j + 1) / 256;
        pushPt(RS * 1.0008f, a0, 2.6f); pushPt(RS * 1.0008f, a1, 2.6f);
        pushPt(RS * 1.5f, a0, 1.5f);    pushPt(RS * 1.5f, a1, 1.5f);
    }
    g_gridVerts = (int)v.size() / 6;
    glGenVertexArrays(1, &g_gridVAO);
    glGenBuffers(1, &g_gridVBO);
    glBindVertexArray(g_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

// ----------------------------------------------------------------------------
// Test particles inspiralling on the embedding surface
// ----------------------------------------------------------------------------
static const int NUM_PARTS = 160;
struct Particle { float r, ph, size, c[3]; };
static Particle g_parts[NUM_PARTS];
static GLuint g_partVAO = 0, g_partVBO = 0;
static std::vector<float> g_partData;

static void respawn(Particle& p) {
    p.r  = 4.0f + (float)rand() / RAND_MAX * 12.0f;
    p.ph = (float)rand() / RAND_MAX * 2.0f * (float)M_PI;
    p.size = 0.10f + (float)rand() / RAND_MAX * 0.10f;
}
static void initParticles() {
    srand(1234);
    for (int i = 0; i < NUM_PARTS; i++) {
        respawn(g_parts[i]);
        g_parts[i].r = 1.05f + (float)rand() / RAND_MAX * 16.0f;
    }
    glGenVertexArrays(1, &g_partVAO);
    glGenBuffers(1, &g_partVBO);
    g_partData.resize(NUM_PARTS * 7);
    glBindVertexArray(g_partVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_partVBO);
    glBufferData(GL_ARRAY_BUFFER, g_partData.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
}
static void updateParticles(float dt) {
    for (int i = 0; i < NUM_PARTS; i++) {
        Particle& p = g_parts[i];
        float r = std::max(p.r, 1.02f);
        float Om = sqrtf(RS / (2.0f * r * r * r));       // Keplerian, exact
        p.ph -= Om * dt * 8.0f;                          // right-handed about +y
        p.r  -= dt * (0.055f * powf(4.0f / r, 2.0f) + 0.010f); // viscous inspiral
        if (p.r < 1.03f) respawn(p);
        float t = std::clamp((p.r - 1.0f) / 8.0f, 0.0f, 1.0f);
        p.c[0] = 1.00f + (0.35f - 1.00f) * t;
        p.c[1] = 0.85f + (0.55f - 0.85f) * t;
        p.c[2] = 0.55f + (1.00f - 0.55f) * t;
        float* d = &g_partData[i * 7];
        d[0] = p.r * cosf(p.ph);
        d[1] = embedY(p.r);
        d[2] = p.r * sinf(p.ph);
        d[3] = p.c[0]; d[4] = p.c[1]; d[5] = p.c[2];
        d[6] = p.size;
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_partVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, g_partData.size() * sizeof(float), g_partData.data());
}

// ----------------------------------------------------------------------------
// Programs & uniforms
// ----------------------------------------------------------------------------
static GLuint g_progRT = 0, g_progLine = 0, g_progPoint = 0;
static GLuint g_emptyVAO = 0;
struct RTU {
    int res, time, tanHalf, rs, escR, exposure, diskBright, maxSteps, diskOn;
    int camPos, fwd, right, up;
} g_u;

static void initGL() {
    g_progRT    = makeProgram(VERT_FULLSCREEN, FRAG_RAYTRACE, "raytrace");
    g_progLine  = makeProgram(VERT_LINE,  FRAG_LINE,  "lines");
    g_progPoint = makeProgram(VERT_POINT, FRAG_POINT, "points");
    glGenVertexArrays(1, &g_emptyVAO);

    g_u.res        = glGetUniformLocation(g_progRT, "uRes");
    g_u.time       = glGetUniformLocation(g_progRT, "uTime");
    g_u.tanHalf    = glGetUniformLocation(g_progRT, "uTanHalf");
    g_u.rs         = glGetUniformLocation(g_progRT, "uRs");
    g_u.escR       = glGetUniformLocation(g_progRT, "uEscR");
    g_u.exposure   = glGetUniformLocation(g_progRT, "uExposure");
    g_u.diskBright = glGetUniformLocation(g_progRT, "uDiskBright");
    g_u.maxSteps   = glGetUniformLocation(g_progRT, "uMaxSteps");
    g_u.diskOn     = glGetUniformLocation(g_progRT, "uDiskOn");
    g_u.camPos     = glGetUniformLocation(g_progRT, "uCamPos");
    g_u.fwd        = glGetUniformLocation(g_progRT, "uFwd");
    g_u.right      = glGetUniformLocation(g_progRT, "uRight");
    g_u.up         = glGetUniformLocation(g_progRT, "uUp");

    buildGrid();
    initParticles();
    glEnable(GL_PROGRAM_POINT_SIZE);
}

static float g_lastDt = 0.0f;

static void renderRayTrace(int vpX, int vpY, int vpW, int vpH, const Vec3& eye) {
    float scale = g_app.scales[g_app.scaleIdx];
    int rtW = std::max(64, (int)(vpW * scale));
    int rtH = std::max(64, (int)(vpH * scale));
    rtW = std::min(rtW, 2560); rtH = std::min(rtH, 1600);
    ensureFBO(rtW, rtH);

    Vec3 fwd = (Vec3(0, 0, 0) - eye).norm();
    Vec3 right = fwd.cross(Vec3(0, 1, 0)).norm();
    Vec3 upv = right.cross(fwd);
    float fov = 42.0f, tanHalf = tanf(fov * 0.5f * (float)M_PI / 180.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, rtW, rtH);
    glUseProgram(g_progRT);
    glUniform2f(g_u.res, (float)rtW, (float)rtH);
    glUniform1f(g_u.time, g_app.simTime);
    glUniform1f(g_u.tanHalf, tanHalf);
    glUniform1f(g_u.rs, RS);
    glUniform1f(g_u.escR, std::max(45.0f, eye.len() * 1.8f));
    glUniform1f(g_u.exposure, g_app.exposure);
    glUniform1f(g_u.diskBright, g_app.diskBright);
    glUniform1i(g_u.maxSteps, g_app.maxSteps);
    glUniform1i(g_u.diskOn, g_app.diskOn ? 1 : 0);
    glUniform3f(g_u.camPos, eye.x, eye.y, eye.z);
    glUniform3f(g_u.fwd, fwd.x, fwd.y, fwd.z);
    glUniform3f(g_u.right, right.x, right.y, right.z);
    glUniform3f(g_u.up, upv.x, upv.y, upv.z);
    glBindVertexArray(g_emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // upscale-blit into the destination viewport
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rtW, rtH, vpX, vpY, vpX + vpW, vpY + vpH,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static void renderGrid(int vpX, int vpY, int vpW, int vpH, const Vec3& eye, Vec3 center) {
    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClearColor(0.010f, 0.012f, 0.022f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glEnable(GL_DEPTH_TEST);
    float aspect = (float)vpW / (float)vpH;
    Mat4 P = mat4Perspective(45.0f, aspect, 0.1f, 400.0f);
    Mat4 V = mat4LookAt(eye, center, Vec3(0, 1, 0));
    Mat4 MVP = mat4Mul(P, V);

    glUseProgram(g_progLine);
    glUniformMatrix4fv(glGetUniformLocation(g_progLine, "uMVP"), 1, GL_FALSE, MVP.m);
    glBindVertexArray(g_gridVAO);
    glDrawArrays(GL_LINES, 0, g_gridVerts);

    updateParticles(g_app.paused ? 0.0f : g_lastDt);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glUseProgram(g_progPoint);
    glUniformMatrix4fv(glGetUniformLocation(g_progPoint, "uMVP"), 1, GL_FALSE, MVP.m);
    float pointScale = (0.5f * vpH) / tanf(45.0f * 0.5f * (float)M_PI / 180.0f);
    glUniform1f(glGetUniformLocation(g_progPoint, "uPointScale"), pointScale);
    glBindVertexArray(g_partVAO);
    glDrawArrays(GL_POINTS, 0, NUM_PARTS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

static Vec3 cameraEye(Vec3 center) {
    float d = g_app.dist[g_app.mode];
    float cp = cosf(g_app.cam.pitch), sp = sinf(g_app.cam.pitch);
    return center + Vec3(d * cp * cosf(g_app.cam.yaw), d * sp, d * cp * sinf(g_app.cam.yaw));
}

static void drawFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (g_app.mode == 1) {
        Vec3 eye = cameraEye(Vec3(0, 0, 0));
        renderRayTrace(0, 0, g_fbW, g_fbH, eye);
    } else if (g_app.mode == 2) {
        Vec3 ctr(0, 4.0f, 0);
        Vec3 eye = cameraEye(ctr);
        renderGrid(0, 0, g_fbW, g_fbH, eye, ctr);
    } else {
        int halfW = g_fbW / 2;
        Vec3 ctr(0, 4.0f, 0);
        Vec3 eye = cameraEye(ctr);
        renderGrid(halfW, 0, g_fbW - halfW, g_fbH, eye, ctr);
        Vec3 eyeRT = cameraEye(Vec3(0, 0, 0));
        renderRayTrace(0, 0, halfW, g_fbH, eyeRT);
        // divider
        glEnable(GL_SCISSOR_TEST);
        glScissor(halfW - 1, 0, 2, g_fbH);
        glClearColor(0.25f, 0.28f, 0.35f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
}

// ----------------------------------------------------------------------------
// Screenshot (PPM)
// ----------------------------------------------------------------------------
static void savePPM(const char* path) {
    std::vector<unsigned char> px((size_t)g_fbW * g_fbH * 4);
    glReadPixels(0, 0, g_fbW, g_fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", g_fbW, g_fbH);
    std::vector<unsigned char> row((size_t)g_fbW * 3);
    for (int y = g_fbH - 1; y >= 0; y--) {
        for (int x = 0; x < g_fbW; x++) {
            row[(size_t)x * 3 + 0] = px[((size_t)y * g_fbW + x) * 4 + 0];
            row[(size_t)x * 3 + 1] = px[((size_t)y * g_fbW + x) * 4 + 1];
            row[(size_t)x * 3 + 2] = px[((size_t)y * g_fbW + x) * 4 + 2];
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    printf("saved %s\n", path);
}

// ----------------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------------
static void keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(g_win, true); break;
    case GLFW_KEY_1: g_app.mode = 1; break;
    case GLFW_KEY_2: g_app.mode = 2; break;
    case GLFW_KEY_3: g_app.mode = 3; break;
    case GLFW_KEY_G: g_app.mode = g_app.mode % 3 + 1; break;
    case GLFW_KEY_SPACE: g_app.paused = !g_app.paused; break;
    case GLFW_KEY_D: g_app.diskOn = !g_app.diskOn; break;
    case GLFW_KEY_R: g_app.cam.yaw = 0.55f; g_app.cam.pitch = 0.16f;
                     g_app.dist[1] = 16; g_app.dist[2] = 34; g_app.dist[3] = 24; break;
    case GLFW_KEY_EQUAL: g_app.exposure = std::min(g_app.exposure * 1.25f, 8.0f); break;
    case GLFW_KEY_MINUS: g_app.exposure = std::max(g_app.exposure / 1.25f, 0.1f); break;
    case GLFW_KEY_LEFT_BRACKET:  g_app.maxSteps = std::max(120, g_app.maxSteps - 40); break;
    case GLFW_KEY_RIGHT_BRACKET: g_app.maxSteps = std::min(900, g_app.maxSteps + 40); break;
    default: break;
    }
}
static void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        g_app.dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(g_win, &g_app.lastX, &g_app.lastY);
    }
}
static void cursorCallback(GLFWwindow*, double x, double y) {
    if (!g_app.dragging) return;
    float dx = (float)(x - g_app.lastX), dy = (float)(y - g_app.lastY);
    g_app.lastX = x; g_app.lastY = y;
    g_app.cam.yaw   -= dx * 0.005f;
    g_app.cam.pitch += dy * 0.005f;
    g_app.cam.pitch = std::clamp(g_app.cam.pitch, -1.45f, 1.45f);
}
static void scrollCallback(GLFWwindow*, double, double yoff) {
    float& d = g_app.dist[g_app.mode];
    float minD = (g_app.mode == 2) ? 8.0f : 3.0f;
    d = std::clamp(d * (float)pow(0.9, yoff), minD, 150.0f);
}

// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) g_app.shotPath = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_app.shotFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) g_app.mode = std::clamp(atoi(argv[++i]), 1, 3);
    }

    glfwSetErrorCallback([](int, const char* d) { fprintf(stderr, "GLFW: %s\n", d); });
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    g_win = glfwCreateWindow(1600, 900, "Schwarzschild Black Hole — geodesic ray tracer", nullptr, nullptr);
    if (!g_win) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);

    glfwSetKeyCallback(g_win, keyCallback);
    glfwSetMouseButtonCallback(g_win, mouseButtonCallback);
    glfwSetCursorPosCallback(g_win, cursorCallback);
    glfwSetScrollCallback(g_win, scrollCallback);

    printf("OpenGL: %s\n", (const char*)glGetString(GL_VERSION));
    initGL();

    glfwGetFramebufferSize(g_win, &g_fbW, &g_fbH);
    glfwSetFramebufferSizeCallback(g_win, [](GLFWwindow*, int w, int h) { g_fbW = w; g_fbH = h; });

    if (!g_app.shotPath) {
        printf(
        "\n================ BLACK HOLE SIMULATION ================\n"
        "  drag         orbit camera\n"
        "  scroll       zoom\n"
        "  1 / 2 / 3    ray-traced view | spacetime grid | split\n"
        "  G            cycle views\n"
        "  SPACE        pause time    D  toggle accretion disk\n"
        "  = / -        exposure      [ / ]  ray-trace steps (accuracy)\n"
        "  R            reset camera  ESC quit\n"
        "========================================================\n\n");
    }

    double lastT = glfwGetTime();
    double fpsTimer = lastT; int fpsCount = 0; double fps = 0;
    int frame = 0;

    while (!glfwWindowShouldClose(g_win)) {
        double now = glfwGetTime();
        float dt = (float)std::min(now - lastT, 0.1);
        lastT = now;
        g_lastDt = dt;
        if (!g_app.paused) g_app.simTime += dt;

        glfwGetFramebufferSize(g_win, &g_fbW, &g_fbH);
        drawFrame();

        // adaptive resolution: keep ~60 fps
        g_app.frameMsEMA = g_app.frameMsEMA * 0.93f + (dt * 1000.0f) * 0.07f;
        if (++g_app.frameCount % 90 == 0 && !g_app.shotPath) {
            if (g_app.frameMsEMA > 20.0f && g_app.scaleIdx > 0) g_app.scaleIdx--;
            else if (g_app.frameMsEMA < 12.5f && g_app.scaleIdx < 4) g_app.scaleIdx++;
        }

        fpsCount++;
        if (now - fpsTimer >= 1.0) {
            fps = fpsCount / (now - fpsTimer);
            fpsCount = 0; fpsTimer = now;
            char title[256];
            snprintf(title, sizeof(title),
                     "Schwarzschild Black Hole — %.0f fps — scale %.2f — steps %d — %s",
                     fps, g_app.scales[g_app.scaleIdx], g_app.maxSteps,
                     g_app.mode == 1 ? "ray-traced" : (g_app.mode == 2 ? "spacetime grid" : "split"));
            glfwSetWindowTitle(g_win, title);
        }

        if (g_app.shotPath) {
            if (++frame >= g_app.shotFrames) {
                drawFrame();
                savePPM(g_app.shotPath);
                break;
            }
        }

        glfwSwapBuffers(g_win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
