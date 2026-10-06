#pragma once

// ---------------------------------------------------------------------------
// Fullscreen triangle / quad
// ---------------------------------------------------------------------------
static const char* QUAD_VERT = R"GLSL(
#version 330 core
in vec2 aPos;
out vec2 vUv;   // in [-1, 1]
void main() {
    vUv = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Relativistic ray tracer: one geodesic per pixel.
// ---------------------------------------------------------------------------
static const char* LENSING_FRAG = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform vec3  uCamPos;
uniform vec3  uFwd;     // camera forward (unit)
uniform vec3  uRight;   // camera right * tan(hfov) * aspect
uniform vec3  uUp;      // camera up    * tan(hfov)
uniform int   uMaxIter;
uniform float uStepBase;
uniform float uTpeak;   // disk peak temperature (K)

const float RS  = 2.0;      // Schwarzschild radius (M = 1)
const float ESC = 55.0;     // escape radius
const float DISK_IN  = 6.0; // ISCO
const float DISK_OUT = 22.0;

// ---------------------------------------------------------------- sky ------
float h1(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }

vec3 skyColor(vec3 d) {
    vec3 col = vec3(0.0);
    // star field
    vec3 q = d * 24.0;
    vec3 cell = floor(q);
    vec3 f = fract(q);
    for (int i = -1; i <= 1; ++i)
    for (int j = -1; j <= 1; ++j)
    for (int k = -1; k <= 1; ++k) {
        vec3 c = cell + vec3(float(i), float(j), float(k));
        float h = h1(c);
        if (h > 0.972) {
            vec3 sp = c + vec3(h1(c + 7.3), h1(c + 13.9), h1(c + 23.7)) - 0.5;
            vec3 dd = sp - f;
            float g = exp(-dot(dd, dd) * 260.0);
            float sz = 0.5 + h1(c + 3.1);
            vec3 tint = mix(vec3(0.75, 0.80, 1.0), vec3(1.0, 0.88, 0.65), h1(c + 1.7));
            col += g * tint * (0.5 + 1.7 * sz);
        }
    }
    // Milky Way band
    float bd = exp(-pow(abs(d.y * 0.85 + d.x * 0.35 + 0.12), 1.3) * 9.0);
    col += bd * vec3(0.55, 0.66, 0.95) * 0.14;
    // faint nebulosity
    float nb = exp(-pow(abs(d.x * 0.6 - d.z * 0.3 + 0.25), 2.2) * 5.0)
             * exp(-pow(abs(d.y + 0.35), 2.0) * 6.0);
    col += nb * vec3(0.35, 0.20, 0.42) * 0.10;
    // the sun: a bright disc at infinity, gets gravitationally lensed
    vec3 sun = normalize(vec3(0.40, 0.48, -0.78));
    float sdot = dot(d, sun);
    float disc = exp2(-(1.0 - sdot) * 3.5e5);
    col += disc * vec3(1.0, 0.97, 0.90) * 70.0;
    return col;
}

// ------------------------------------------------------- Planck emission ---
// Specific intensity sampled at R/G/B wavelengths, white-balanced at 5500 K.
// Includes the relativistic Doppler/gravitational redshift factor g:
//   I_obs(lambda_c) = g^3 * B(lambda_c * g, T_em)
const float HC_K = 1.4388e-2; // h c / k_B (m K)
const float TREF = 5500.0;

vec3 planckRGB(float T, float g) {
    float lamR = 650.0e-9, lamG = 530.0e-9, lamB = 470.0e-9;
    vec3 col;
    float lam[3];
    lam[0] = lamR; lam[1] = lamG; lam[2] = lamB;
    for (int i = 0; i < 3; ++i) {
        float le = lam[i] * g;                       // emitted wavelength
        float x = HC_K / max(le * T, 1e-12);
        x = min(x, 80.0);
        float xr = HC_K / (lam[i] * TREF);
        float b = pow(lam[i] / le, 5.0) * (exp(xr) - 1.0) / (exp(x) - 1.0);
        col[i] = b;
    }
    return col * pow(g, 3.0);                        // I_obs = g^3 I_em
}

// ------------------------------------------------------------- disk --------
vec4 diskColor(vec3 x, vec3 p, float rc) {
    float r6 = max(rc, 6.02);
    // Novikov-Thorne temperature profile, peak at r = 49/6 M
    float rp = 49.0 / 6.0;
    float T = uTpeak * pow(rp / r6, 0.75)
            * pow((1.0 - sqrt(6.0 / r6)) / (1.0 - sqrt(6.0 / rp)), 0.25);
    float b = x.x * p.y - x.y * p.x;                 // signed L_z / E
    float om = pow(rc, -1.5);                        // Keplerian Omega
    float th = inversesqrt(max(1.0 - 3.0 / rc, 1e-4));
    float g = 1.0 / (th * max(1.0 - b * om, 1e-4));
    vec3 col = planckRGB(T, g);
    float I = pow(T / TREF, 4.0);                    // bolometric radiance
    return vec4(col * I * 2.2, 1.0);
}

// --------------------------------------------------------- geodesics --------
void deriv(vec3 x, vec3 p, float E, out vec3 dx, out vec3 dp) {
    float r = length(x);
    float r3 = r * r * r;
    float r5 = r3 * r * r;
    float px = dot(p, x);
    float a = 1.0 - RS / r;
    float k = RS * px / r3;                          // rs (p.x) / r^3
    float l = 1.5 * RS * px * px / r5;               // 1.5 rs (p.x)^2 / r^5
    float m0 = 0.5 * E * E * RS / (r3 * a * a);      // E^2 rs / (2 r^3 A^2)
    dx = p - (RS * px / r3) * x;
    dp = -m0 * x + k * p - l * x;
}

void project(inout vec3 p, vec3 x, float E) {
    float r = length(x);
    float a = 1.0 - RS / r;
    float pxn = dot(p, x);
    float target = E * E / a + RS * pxn * pxn / (r * r * r);
    float pp = dot(p, p);
    float s = (target > 0.0 && pp > 0.0) ? sqrt(target / pp) : 0.0;
    p *= s;
}

// ------------------------------------------------------------- main --------
void main() {
    vec3 dir = normalize(uFwd + vUv.x * uRight + vUv.y * uUp);
    vec3 x = uCamPos;
    vec3 p = dir;
    float E = 1.0;
    project(p, x, E);

    vec3 col = vec3(0.0);
    bool done = false;
    float zPrev = x.z;
    vec3 xPrev = x;

    for (int i = 0; i < uMaxIter && !done; ++i) {
        float r = length(x);
        if (r <= RS * 1.0005) { done = true; break; }        // inside horizon
        if (r > ESC) { col = skyColor(normalize(p)); done = true; break; }

        float h = clamp(uStepBase * pow(r, 1.3), uStepBase * 0.25, 0.16);

        vec3 k1x, k1p, k2x, k2p, k3x, k3p, k4x, k4p;
        deriv(x, p, E, k1x, k1p);
        vec3 x2 = x + 0.5 * h * k1x;
        vec3 p2 = p + 0.5 * h * k1p;
        project(p2, x2, E);
        deriv(x2, p2, E, k2x, k2p);
        x2 = x + 0.5 * h * k2x;
        p2 = p + 0.5 * h * k2p;
        project(p2, x2, E);
        deriv(x2, p2, E, k3x, k3p);
        x2 = x + h * k3x;
        p2 = p + h * k3p;
        project(p2, x2, E);
        deriv(x2, p2, E, k4x, k4p);

        xPrev = x;
        x += h * (k1x + 2.0 * k2x + 2.0 * k3x + k4x) / 6.0;
        p += h * (k1p + 2.0 * k2p + 2.0 * k3p + k4p) / 6.0;
        project(p, x, E);

        float zn = x.z;
        if (zPrev * zn < 0.0) {
            float t = zPrev / (zPrev - zn);
            vec3 xc = xPrev + t * (x - xPrev);
            float rc = length(xc);
            if (rc > DISK_IN && rc < DISK_OUT) {
                col = diskColor(xc, p, rc).rgb;
                done = true;
                break;
            }
        }
        zPrev = zn;
    }
    if (!done) col = skyColor(normalize(p)) * 0.45;   // step budget exhausted
    fragColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Bloom: bright pass, blur, composite
// ---------------------------------------------------------------------------
static const char* BRIGHT_FRAG = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    vec3 c = texture(uTex, vUv * 0.5 + 0.5).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(l - 1.1, 0.0);
    fragColor = vec4(c * k / max(l, 1e-4) * 1.5, 1.0);
}
)GLSL";

static const char* BLUR_FRAG = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uDir;
uniform float uRes;
void main() {
    vec2 uv = vUv * 0.5 + 0.5;
    vec2 step = uDir / uRes;
    float w[5];
    w[0] = 0.227027; w[1] = 0.1945946; w[2] = 0.1216216; w[3] = 0.054054; w[4] = 0.016216;
    vec3 c = texture(uTex, uv).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, uv + step * float(i)).rgb * w[i];
        c += texture(uTex, uv - step * float(i)).rgb * w[i];
    }
    fragColor = vec4(c, 1.0);
}
)GLSL";

static const char* COMPOSE_FRAG = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform float uExposure;
vec3 aces(vec3 x) { return x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14); }
void main() {
    vec2 uv = vUv * 0.5 + 0.5;
    vec3 hdr = texture(uHDR, uv).rgb;
    vec3 bloom = texture(uBloom, uv).rgb;
    vec3 col = (hdr + bloom * 1.1) * uExposure;
    col = aces(max(col, 0.0));
    col = pow(col, vec3(1.0 / 2.2));
    float vig = 1.0 - 0.30 * dot(vUv, vUv) * 0.5;
    fragColor = vec4(col * vig, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Spacetime grid mode
// ---------------------------------------------------------------------------
static const char* GRID_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uMVP;
uniform float uRotZ;
out vec3 vCol;
void main() {
    float c = cos(uRotZ), s = sin(uRotZ);
    vec3 rp = vec3(c * aPos.x - s * aPos.y, s * aPos.x + c * aPos.y, aPos.z);
    gl_Position = uMVP * vec4(rp, 1.0);
    vCol = aCol;
}
)GLSL";

static const char* GRID_FRAG = R"GLSL(
#version 330 core
in vec3 vCol;
out vec4 fragColor;
void main() { fragColor = vec4(vCol, 1.0); }
)GLSL";

static const char* SPHERE_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uCamPos;
uniform float uRotZ;
out vec3 vN;
out vec3 vV;
void main() {
    float c = cos(uRotZ), s = sin(uRotZ);
    vec3 rp = vec3(c * aPos.x - s * aPos.y, s * aPos.x + c * aPos.y, aPos.z);
    vec3 rn = vec3(c * aNorm.x - s * aNorm.y, s * aNorm.x + c * aNorm.y, aNorm.z);
    gl_Position = uMVP * vec4(rp, 1.0);
    vN = rn;
    vV = uCamPos - rp;
}
)GLSL";

static const char* SPHERE_FRAG = R"GLSL(
#version 330 core
in vec3 vN;
in vec3 vV;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vN);
    vec3 v = normalize(vV);
    float rim = pow(1.0 - abs(dot(n, v)), 2.5);
    vec3 col = vec3(0.012, 0.016, 0.030) + rim * vec3(0.10, 0.16, 0.30);
    fragColor = vec4(col, 1.0);
}
)GLSL";

static const char* STAR_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = 2.0;
}
)GLSL";

static const char* STAR_FRAG = R"GLSL(
#version 330 core
out vec4 fragColor;
void main() { fragColor = vec4(0.55, 0.62, 0.85, 1.0); }
)GLSL";
