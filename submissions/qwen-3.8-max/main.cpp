#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

static const char *RAY_VS = R"(
#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char *RAY_FS = R"(
#version 330 core

uniform vec2 uRes;
uniform float uTime;
uniform vec3 uCamPos;
uniform mat3 uCamBasis;
uniform float uFocal;
uniform int uShowDisk;
uniform vec3 uDiskN;
uniform float uExposure;
uniform float uDiskIn;
uniform float uDiskOut;
uniform int uFlatBG;

out vec4 fragColor;

const float M = 0.5;
const int MAX_STEPS = 2500;

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.y + p3.z * p3.x);
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), u.x),
            mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), u.x), u.y),
        mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), u.x),
            mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), u.x), u.y),
        u.z);
}

float fbm(vec3 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        s += a * vnoise(p);
        p = p * 2.03 + 7.1;
        a *= 0.5;
    }
    return s;
}

vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = (T <= 66.0) ? 1.0 : clamp(1.292936 * pow(T - 60.0, -0.1332047), 0.0, 1.0);
    c.g = (T <= 66.0)
        ? clamp(0.390082 * log(max(T, 1.0)) - 0.631841, 0.0, 1.0)
        : clamp(1.129891 * pow(T - 60.0, -0.0755148), 0.0, 1.0);
    c.b = (T >= 66.0) ? 1.0
        : ((T <= 19.0) ? 0.0 : clamp(0.543207 * log(T - 10.0) - 1.196254, 0.0, 1.0));
    return c;
}

vec3 background(vec3 d) {
    if (uFlatBG == 1) return vec3(0.9);
    vec3 gn = normalize(vec3(0.32, 0.94, 0.28));
    float band = exp(-pow(dot(d, gn) / 0.24, 2.0));
    float n = fbm(d * 3.5);
    vec3 col = band * (vec3(0.14, 0.18, 0.34) * (0.65 + 1.4 * n) +
                       vec3(0.30, 0.16, 0.10) * n * n * n) * 1.6;
    for (int L = 0; L < 3; L++) {
        float sc = 60.0 * float(L + 1);
        vec3 q = d * sc;
        vec3 id = floor(q);
        vec3 f = fract(q) - 0.5;
        float h = hash13(id + 77.7 * float(L));
        if (h > 0.986) {
            vec3 off = vec3(hash13(id + 13.1), hash13(id + 27.7), hash13(id + 91.3)) - 0.5;
            float dist = length(f - off * 0.8);
            float bri = smoothstep(0.30, 0.0, dist) * pow((h - 0.986) / 0.014, 4.0);
            float T = 2600.0 + 24000.0 * hash13(id + 5.5);
            col += bri * blackbody(T) * (0.6 + 1.8 * hash13(id + 2.2)) * 0.6;
        }
    }
    return col;
}

vec3 diskEmission(float r, vec3 pc, vec3 p3, out float opacity) {
    float rin = uDiskIn;
    float F = max(0.0, 1.0 - sqrt(rin / r)) / (r * r * r);
    float rp = rin * 49.0 / 36.0;
    float Fmax = (1.0 - sqrt(rin / rp)) / (rp * rp * rp);
    float Inorm = F / Fmax;
    float Omega = sqrt(M / (r * r * r));
    float ut = inversesqrt(max(1e-4, 1.0 - 3.0 * M / r));
    float lam = dot(cross(pc, p3), uDiskN);
    float g = clamp(1.0 / (ut * (1.0 - Omega * lam)), 0.0, 6.0);
    float x = clamp((r - rin) / (uDiskOut - rin), 0.0, 1.0);
    float fade = smoothstep(0.0, 0.03, x) * (1.0 - smoothstep(0.80, 1.0, x));
    float I = Inorm * fade;
    opacity = clamp((0.93 - 0.60 * x) * clamp(0.4 + I, 0.0, 1.0), 0.05, 0.97);
    float Tobs = clamp(11500.0 * pow(max(Inorm, 0.0), 0.25) * g, 900.0, 30000.0);
    return blackbody(Tobs) * I * pow(g, 4.0) * 0.9;
}

vec3 deriv(float u, float s, float b) {
    float u2 = u * u;
    float ds = b * b * u2 * u2 * u * (1.5 * u - 1.0) + 2.0 * s * s / max(u, 1e-9);
    return vec3(s, ds, b * u2);
}

float stepH(float u) {
    float r = 1.0 / max(u, 1e-7);
    if (r < 2.0) return 0.02;
    if (r < 6.0) return 0.02 + (r - 2.0) * 0.0225;
    if (r < 20.0) return 0.11 + (r - 6.0) * 0.02;
    return min(0.25 * r, 100.0);
}

void main() {
    vec2 uv = (2.0 * gl_FragCoord.xy - uRes) / uRes.y;
    vec3 rd = normalize(uCamBasis * vec3(uv, uFocal));
    vec3 ro = uCamPos;
    float r0 = length(ro);
    vec3 e1 = ro / r0;
    float vr = dot(rd, e1);
    vec3 tgv = rd - vr * e1;
    float tl = length(tgv);

    vec3 acc = vec3(0.0);
    float trans = 1.0;
    vec3 outDir = rd;
    bool escaped = false;

    if (tl < 1e-6) {
        escaped = (vr > 0.0);
    } else {
        vec3 e2 = tgv / tl;
        float f0 = 1.0 - 1.0 / max(r0, 1.001);
        float b = r0 * tl / sqrt(f0);
        float u = 1.0 / r0;
        float s = -vr * u * u;
        float phi = 0.0;
        float hPrev = dot(ro, uDiskN);

        for (int i = 0; i < MAX_STEPS; i++) {
            if (u >= 0.9995) break;
            if (s < 0.0 && u < 1.0 / max(400.0, 3.0 * r0)) {
                escaped = true;
                float drdl = -s / (u * u);
                float ang = phi + atan(b * u / drdl);
                outDir = cos(ang) * e1 + sin(ang) * e2;
                break;
            }
            float h = stepH(u);
            vec3 k1 = deriv(u, s, b);
            vec3 k2 = deriv(u + 0.5 * h * k1.x, s + 0.5 * h * k1.y, b);
            vec3 k3 = deriv(u + 0.5 * h * k2.x, s + 0.5 * h * k2.y, b);
            vec3 k4 = deriv(u + h * k3.x, s + h * k3.y, b);
            float uPrev = u;
            float sPrev = s;
            float phiPrev = phi;
            u += h * (k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x) / 6.0;
            s += h * (k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y) / 6.0;
            phi += h * (k1.z + 2.0 * k2.z + 2.0 * k3.z + k4.z) / 6.0;
            u = max(u, 0.0);
            float r = 1.0 / max(u, 1e-9);
            vec3 pos = r * (cos(phi) * e1 + sin(phi) * e2);
            float hh = dot(pos, uDiskN);
            if (uShowDisk == 1 && hPrev * hh < 0.0 && trans > 0.02) {
                float tt = hPrev / (hPrev - hh);
                float uC = mix(uPrev, u, tt);
                float sC = mix(sPrev, s, tt);
                float pC = mix(phiPrev, phi, tt);
                float rC = 1.0 / max(uC, 1e-9);
                if (rC >= uDiskIn && rC <= uDiskOut) {
                    vec3 rhat = cos(pC) * e1 + sin(pC) * e2;
                    vec3 phat = -sin(pC) * e1 + cos(pC) * e2;
                    vec3 pc = rC * rhat;
                    vec3 p3 = (-sC / (uC * uC)) * rhat + (b * uC) * phat;
                    float op;
                    vec3 em = diskEmission(rC, pc, p3, op);
                    acc += trans * em;
                    trans *= (1.0 - op);
                }
            }
            hPrev = hh;
        }
    }

    if (escaped) acc += trans * background(outDir);

    float dith = (hash13(vec3(gl_FragCoord.xy, uTime)) - 0.5) / 64.0;
    vec3 col = 1.0 - exp(-(acc * uExposure + dith));
    vec2 q = gl_FragCoord.xy / uRes;
    col *= 0.68 + 0.32 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.18);
    fragColor = vec4(pow(max(col, 0.0), vec3(0.4545)), 1.0);
}
)";

static const char *BLIT_VS = R"(
#version 330 core
layout(location = 0) in vec2 p;
out vec2 vUv;
void main() {
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char *BLIT_FS = R"(
#version 330 core
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUv);
}
)";

static const char *GRID_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
layout(location = 2) in float aT;

uniform mat4 uMVP;
uniform float uTime;
uniform vec3 uCamPos;

out vec4 vCol;

void main() {
    float pulse = 0.72 + 0.28 * sin(uTime * 2.2 + aT * 26.0);
    vec3 toV = aPos - uCamPos;
    vec3 toBH = -uCamPos;
    float dl = length(toBH);
    vec3 bhDir = toBH / max(dl, 1e-5);
    float tb = dot(toV, bhDir);
    float perp = length(toV - bhDir * tb);
    float fade = (tb > dl) ? smoothstep(2.1, 3.3, perp) : 1.0;
    vCol = vec4(aCol * pulse * fade, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char *GRID_FS = R"(
#version 330 core
in vec4 vCol;
out vec4 fragColor;
void main() {
    fragColor = vCol;
}
)";

struct V3 {
    float x = 0, y = 0, z = 0;
};

static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 1}; }

struct Camera {
    float az = 0.6f;
    float lat = 0.30f;
    float dist = 34.0f;
    float exposure = 1.3f;
    bool autoOrbit = true;
};

struct State {
    Camera cam;
    bool showGrid = true;
    bool showDisk = true;
    bool flatBG = false;
    bool dragging = false;
    double lastX = 0, lastY = 0;
};

static void errCb(int err, const char *desc) {
    fprintf(stderr, "GLFW error %d: %s\n", err, desc);
}

static void keyCb(GLFWwindow *w, int key, int, int action, int) {
    State *s = (State *)glfwGetWindowUserPointer(w);
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_G: s->showGrid = !s->showGrid; break;
        case GLFW_KEY_D: s->showDisk = !s->showDisk; break;
        case GLFW_KEY_SPACE: s->cam.autoOrbit = !s->cam.autoOrbit; break;
        case GLFW_KEY_R: s->cam = Camera(); break;
        case GLFW_KEY_1: s->cam.lat = 0.30f; s->cam.dist = 34.0f; break;
        case GLFW_KEY_2: s->cam.lat = 1.05f; s->cam.dist = 44.0f; break;
        case GLFW_KEY_3: s->cam.lat = 0.05f; s->cam.dist = 30.0f; break;
        case GLFW_KEY_MINUS: s->cam.exposure = fmaxf(0.15f, s->cam.exposure * 0.8f); break;
        case GLFW_KEY_EQUAL: s->cam.exposure = fminf(8.0f, s->cam.exposure * 1.25f); break;
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, true); break;
        default: break;
    }
}

static void mouseCb(GLFWwindow *w, int button, int action, int) {
    State *s = (State *)glfwGetWindowUserPointer(w);
    glfwGetCursorPos(w, &s->lastX, &s->lastY);
    if (button == GLFW_MOUSE_BUTTON_LEFT) s->dragging = (action == GLFW_PRESS);
}

static void cursorCb(GLFWwindow *w, double x, double y) {
    State *s = (State *)glfwGetWindowUserPointer(w);
    if (s->dragging) {
        float dx = (float)(x - s->lastX);
        float dy = (float)(y - s->lastY);
        s->cam.az -= dx * 0.005f;
        s->cam.lat += dy * 0.005f;
        s->cam.lat = fminf(1.52f, fmaxf(-1.52f, s->cam.lat));
        s->cam.autoOrbit = false;
    }
    s->lastX = x;
    s->lastY = y;
}

static void scrollCb(GLFWwindow *w, double, double yoff) {
    State *s = (State *)glfwGetWindowUserPointer(w);
    s->cam.dist *= expf((float)(-yoff) * 0.10f);
    s->cam.dist = fminf(150.0f, fmaxf(4.0f, s->cam.dist));
}

static GLuint compileShader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        exit(1);
    }
    return sh;
}

static GLuint linkProgram(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compileShader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compileShader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        exit(1);
    }
    return p;
}

struct RT {
    GLuint fbo = 0;
    GLuint tex = 0;
    int w = 0;
    int h = 0;
};

static void rtResize(RT &rt, int w, int h) {
    if (rt.tex) glDeleteTextures(1, &rt.tex);
    if (rt.fbo) glDeleteFramebuffers(1, &rt.fbo);
    rt.w = w;
    rt.h = h;
    glGenTextures(1, &rt.tex);
    glBindTexture(GL_TEXTURE_2D, rt.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &rt.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer incomplete\n");
        exit(1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void savePPM(const char *path, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    fclose(f);
    fprintf(stderr, "Saved frame to %s\n", path);
}

static void perspective(float fovy, float aspect, float zn, float zf, float *m) {
    float f = 1.0f / tanf(fovy * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = 2.0f * zf * zn / (zn - zf);
}

static void lookAt(V3 eye, V3 fwd, V3 right, V3 up, float *m) {
    m[0] = right.x; m[4] = right.y; m[8] = right.z;   m[12] = -dot(right, eye);
    m[1] = up.x;    m[5] = up.y;    m[9] = up.z;      m[13] = -dot(up, eye);
    m[2] = -fwd.x;  m[6] = -fwd.y;  m[10] = -fwd.z;   m[14] = dot(fwd, eye);
    m[3] = 0;       m[7] = 0;       m[11] = 0;        m[15] = 1;
}

static void mat4Mul(const float *a, const float *b, float *out) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = v;
        }
    memcpy(out, r, sizeof(r));
}

struct GridMesh {
    std::vector<float> v;
};

static void pushVert(GridMesh &g, float x, float y, float z, float r, float gc, float b, float t) {
    float d[7] = {x, y, z, r, gc, b, t};
    for (int k = 0; k < 7; k++) g.v.push_back(d[k]);
}

static void gridColor(float z, float *c) {
    float d = fminf(fmaxf(-z / 16.0f, 0.0f), 1.0f);
    float k = powf(d, 1.3f);
    float amp = 0.38f + 0.80f * d;
    c[0] = (0.18f + (1.00f - 0.18f) * k) * amp;
    c[1] = (0.55f + (0.34f - 0.55f) * k) * amp;
    c[2] = (0.95f + (0.10f - 0.95f) * k) * amp;
}

static GridMesh buildGrid() {
    GridMesh g;
    const float rMin = 1.0015f, rMax = 90.0f;
    const int Nr = 48, Np = 72;
    auto Z = [](float r) { return -2.0f * sqrtf(fmaxf(r - 1.0f, 0.0f)); };
    std::vector<float> radii(Nr);
    for (int i = 0; i < Nr; i++) {
        float t = (float)i / (Nr - 1);
        radii[i] = rMin + (rMax - rMin) * powf(t, 2.6f);
    }
    for (int i = 0; i < Nr; i++) {
        float t = (float)i / (Nr - 1);
        float z = Z(radii[i]);
        float c[3];
        gridColor(z, c);
        for (int j = 0; j < Np; j++) {
            float p0 = 2.0f * (float)M_PI * j / Np;
            float p1 = 2.0f * (float)M_PI * (j + 1) / Np;
            pushVert(g, radii[i] * cosf(p0), radii[i] * sinf(p0), z, c[0], c[1], c[2], t);
            pushVert(g, radii[i] * cosf(p1), radii[i] * sinf(p1), z, c[0], c[1], c[2], t);
        }
    }
    for (int j = 0; j < Np; j++) {
        float p = 2.0f * (float)M_PI * j / Np;
        for (int i = 0; i < Nr - 1; i++) {
            float z0 = Z(radii[i]);
            float z1 = Z(radii[i + 1]);
            float c0[3], c1[3];
            gridColor(z0, c0);
            gridColor(z1, c1);
            float t0 = (float)i / (Nr - 1);
            float t1 = (float)(i + 1) / (Nr - 1);
            pushVert(g, radii[i] * cosf(p), radii[i] * sinf(p), z0, c0[0], c0[1], c0[2], t0);
            pushVert(g, radii[i + 1] * cosf(p), radii[i + 1] * sinf(p), z1, c1[0], c1[1], c1[2], t1);
        }
    }
    const float hr = 1.0002f;
    for (int j = 0; j < Np; j++) {
        float p0 = 2.0f * (float)M_PI * j / Np;
        float p1 = 2.0f * (float)M_PI * (j + 1) / Np;
        pushVert(g, hr * cosf(p0), hr * sinf(p0), 0.0f, 1.3f, 0.70f, 0.25f, 0.0f);
        pushVert(g, hr * cosf(p1), hr * sinf(p1), 0.0f, 1.3f, 0.70f, 0.25f, 0.0f);
    }
    return g;
}

static void printHelp() {
    printf("=====================================================================\n");
    printf(" Schwarzschild Black Hole - GPU Geodesic Ray Tracer\n");
    printf("---------------------------------------------------------------------\n");
    printf(" Physics: exact null geodesics (u'' = -u + 3M u^2, RK4), G = c = 1,\n");
    printf(" r_s = 1. Shadow, photon sphere (r = 1.5), ISCO (r = 3), Keplerian\n");
    printf(" disk with gravitational redshift + Doppler beaming (I ~ g^4),\n");
    printf(" Novikov-Thorne flux profile, higher-order (n >= 1) lensed images.\n");
    printf(" Grid: Flamm paraboloid z = -2 sqrt(r_s (r - r_s)) embedding.\n");
    printf("---------------------------------------------------------------------\n");
    printf(" Drag    orbit camera        Scroll   zoom\n");
    printf(" G       spacetime grid      D        accretion disk\n");
    printf(" Space   auto-orbit          R        reset camera\n");
    printf(" 1/2/3   view presets        -/+      exposure\n");
    printf(" Esc     quit\n");
    printf("=====================================================================\n");
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    const char *stillPath = nullptr;
    bool noDisk = false;
    bool noGrid = false;
    bool flatBG = false;
    int preset = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--still") && i + 1 < argc) stillPath = argv[++i];
        else if (!strcmp(argv[i], "--nodisk")) noDisk = true;
        else if (!strcmp(argv[i], "--nogrid")) noGrid = true;
        else if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--flatbg")) flatBG = true;
    }

    glfwSetErrorCallback(errCb);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow *win = glfwCreateWindow(1280, 800, "Schwarzschild Black Hole - Geodesic Ray Tracer", nullptr, nullptr);
    if (!win) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    State state;
    state.showDisk = !noDisk;
    state.showGrid = !noGrid;
    state.flatBG = flatBG;
    if (preset == 2) {
        state.cam.lat = 1.05f;
        state.cam.dist = 44.0f;
    } else if (preset == 3) {
        state.cam.lat = 0.05f;
        state.cam.dist = 30.0f;
    }
    state.cam.autoOrbit = maxFrames == 0;
    glfwSetWindowUserPointer(win, &state);
    glfwSetKeyCallback(win, keyCb);
    glfwSetMouseButtonCallback(win, mouseCb);
    glfwSetCursorPosCallback(win, cursorCb);
    glfwSetScrollCallback(win, scrollCb);

    GLuint progRay = linkProgram(RAY_VS, RAY_FS);
    GLuint progBlit = linkProgram(BLIT_VS, BLIT_FS);
    GLuint progGrid = linkProgram(GRID_VS, GRID_FS);

    GLuint quadVao, quadVbo;
    float quad[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    glGenVertexArrays(1, &quadVao);
    glGenBuffers(1, &quadVbo);
    glBindVertexArray(quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    GridMesh grid = buildGrid();
    GLuint gridVao, gridVbo;
    glGenVertexArrays(1, &gridVao);
    glGenBuffers(1, &gridVbo);
    glBindVertexArray(gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo);
    glBufferData(GL_ARRAY_BUFFER, grid.v.size() * sizeof(float), grid.v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)(6 * sizeof(float)));
    int gridVerts = (int)grid.v.size() / 7;
    glBindVertexArray(0);

    printHelp();

    RT rt;
    float scale = 1.0f;
    float ema = 1.0f / 60.0f;
    double lastAdjust = glfwGetTime();
    int frame = 0;
    int stillFrame = maxFrames > 0 ? std::max(20, maxFrames - 5) : 80;
    double fpsClock = glfwGetTime();
    int fpsFrames = 0;
    bool stillSaved = false;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double tNow = glfwGetTime();
        float dt = 1.0f / 60.0f;
        static double tPrev = 0;
        if (tPrev > 0) dt = (float)std::min(tNow - tPrev, 0.1);
        tPrev = tNow;

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw < 8 || fbh < 8) {
            glfwWaitEvents();
            continue;
        }

        if (state.cam.autoOrbit && !state.dragging) state.cam.az += dt * 0.05f;

        V3 eye = {state.cam.dist * cosf(state.cam.lat) * cosf(state.cam.az),
                  state.cam.dist * cosf(state.cam.lat) * sinf(state.cam.az),
                  state.cam.dist * sinf(state.cam.lat)};
        V3 fwd = norm(eye * -1.0f);
        V3 right = norm(cross(fwd, {0, 0, 1}));
        V3 up = cross(right, fwd);

        float basis[9] = {right.x, right.y, right.z, up.x, up.y, up.z, fwd.x, fwd.y, fwd.z};

        int rw = std::max(64, (int)(fbw * scale));
        int rh = std::max(64, (int)(fbh * scale));
        if (rt.w != rw || rt.h != rh) rtResize(rt, rw, rh);

        float focal = 1.0f / tanf(0.4363f);

        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        glViewport(0, 0, rw, rh);
        glUseProgram(progRay);
        glUniform2f(glGetUniformLocation(progRay, "uRes"), (float)rw, (float)rh);
        glUniform1f(glGetUniformLocation(progRay, "uTime"), (float)tNow);
        glUniform3f(glGetUniformLocation(progRay, "uCamPos"), eye.x, eye.y, eye.z);
        glUniformMatrix3fv(glGetUniformLocation(progRay, "uCamBasis"), 1, GL_FALSE, basis);
        glUniform1f(glGetUniformLocation(progRay, "uFocal"), focal);
        glUniform1i(glGetUniformLocation(progRay, "uShowDisk"), state.showDisk ? 1 : 0);
        glUniform1i(glGetUniformLocation(progRay, "uFlatBG"), state.flatBG ? 1 : 0);
        glUniform3f(glGetUniformLocation(progRay, "uDiskN"), 0.0f, 0.0f, 1.0f);
        glUniform1f(glGetUniformLocation(progRay, "uExposure"), state.cam.exposure);
        glUniform1f(glGetUniformLocation(progRay, "uDiskIn"), 3.0f);
        glUniform1f(glGetUniformLocation(progRay, "uDiskOut"), 15.0f);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rt.tex);
        glUseProgram(progBlit);
        glUniform1i(glGetUniformLocation(progBlit, "uTex"), 0);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (state.showGrid) {
            float proj[16], view[16], mvp[16];
            perspective(2.0f * atanf(1.0f / focal), (float)fbw / (float)fbh, 0.1f, 600.0f, proj);
            lookAt(eye, fwd, right, up, view);
            mat4Mul(proj, view, mvp);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glUseProgram(progGrid);
            glUniformMatrix4fv(glGetUniformLocation(progGrid, "uMVP"), 1, GL_FALSE, mvp);
            glUniform1f(glGetUniformLocation(progGrid, "uTime"), (float)tNow);
            glUniform3f(glGetUniformLocation(progGrid, "uCamPos"), eye.x, eye.y, eye.z);
            glBindVertexArray(gridVao);
            glDrawArrays(GL_LINES, 0, gridVerts);
            glDisable(GL_BLEND);
        }

        if (stillPath && !stillSaved && frame == stillFrame) {
            glFinish();
            savePPM(stillPath, fbw, fbh);
            stillSaved = true;
        }

        glfwSwapBuffers(win);
        frame++;

        ema = ema * 0.95f + dt * 0.05f;
        if (frame > 120 && tNow - lastAdjust > 1.5) {
            if (ema > 0.033f && scale > 0.4f) {
                scale *= 0.85f;
                lastAdjust = tNow;
            } else if (ema < 0.015f && scale < 1.0f) {
                scale = fminf(1.0f, scale * 1.08f);
                lastAdjust = tNow;
            }
        }

        fpsFrames++;
        if (tNow - fpsClock > 0.5) {
            char title[256];
            snprintf(title, sizeof(title),
                     "Schwarzschild Black Hole - %.0f fps | res %.0f%% | G:grid %s | D:disk %s",
                     fpsFrames / (tNow - fpsClock), scale * 100.0f,
                     state.showGrid ? "on" : "off", state.showDisk ? "on" : "off");
            glfwSetWindowTitle(win, title);
            fpsClock = tNow;
            fpsFrames = 0;
        }

        if (maxFrames > 0 && frame >= maxFrames) break;
    }

    glDeleteProgram(progRay);
    glDeleteProgram(progBlit);
    glDeleteProgram(progGrid);
    glfwTerminate();
    return 0;
}
