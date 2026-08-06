#define GL_SILENCE_DEPRECATION

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static const char *kVertexShader = R"GLSL(
#version 410 core

out vec2 vUV;

void main() {
    const vec2 vertices[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 p = vertices[gl_VertexID];
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char *kFragmentShader = R"GLSL(
#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uYaw;
uniform float uPitch;
uniform float uDistance;
uniform float uExposure;
uniform int uRaySteps;
uniform int uShowDisk;
uniform int uShowGrid;
uniform int uShowHalo;

const float PI = 3.141592653589793;
const float RS = 1.0;          // Schwarzschild radius, c = G = 1.
const float MASS = 0.5;        // M = r_s / 2.
const float RHO_H = 0.25;      // Isotropic-coordinate horizon radius.
const float FAR_RADIUS = 62.0;
const int MAX_STEPS = 620;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p) {
    float a = 0.5;
    float v = 0.0;
    for (int i = 0; i < 4; ++i) {
        v += a * valueNoise(p);
        p = p * 2.07 + vec3(17.1, 3.4, 9.2);
        a *= 0.5;
    }
    return v;
}

float arealRadiusFromRho(float rho) {
    float a = RS / (4.0 * max(rho, RHO_H + 1.0e-5));
    return rho * (1.0 + a) * (1.0 + a);
}

float dLogOpticalN_dRho(float rho) {
    float r = max(rho, RHO_H + 0.002);
    float a = RS / (4.0 * r);
    return (-a / r) * (3.0 / (1.0 + a) + 1.0 / max(0.02, 1.0 - a));
}

vec3 opticalAcceleration(vec3 p, vec3 dir) {
    float rho = max(length(p), RHO_H + 0.002);
    vec3 radial = p / rho;
    vec3 gradLogN = radial * dLogOpticalN_dRho(rho);
    return gradLogN - dir * dot(dir, gradLogN);
}

float planckSample(float wavelengthMeters, float kelvin) {
    float c2 = 1.4387769e-2; // h c / k, meters kelvin.
    float x = clamp(c2 / (wavelengthMeters * max(kelvin, 100.0)), 0.001, 80.0);
    return 1.0 / (pow(wavelengthMeters, 5.0) * (exp(x) - 1.0));
}

vec3 blackbody(float kelvin) {
    vec3 c = vec3(
        planckSample(700.0e-9, kelvin),
        planckSample(546.0e-9, kelvin),
        planckSample(435.0e-9, kelvin)
    );
    c /= max(max(c.r, c.g), max(c.b, 1.0e-20));
    return pow(c, vec3(0.55));
}

vec3 sampleBackground(vec3 d) {
    d = normalize(d);
    vec2 sphere = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5);
    float milkyWay = pow(saturate(1.0 - abs(d.y * 2.15 + 0.22 * sin(5.0 * d.x) + 0.08 * sin(9.0 * d.z))), 2.3);
    vec3 sky = mix(vec3(0.002, 0.004, 0.010), vec3(0.030, 0.026, 0.046), milkyWay);
    sky += milkyWay * vec3(0.035, 0.027, 0.018);

    vec2 gridA = sphere * vec2(1000.0, 500.0);
    vec2 cellA = floor(gridA);
    vec2 fA = fract(gridA) - 0.5;
    float hA = hash12(cellA);
    float starA = (1.0 - smoothstep(0.0, 0.055, length(fA))) * step(0.9965, hA);

    vec2 gridB = sphere * vec2(1850.0, 925.0);
    vec2 cellB = floor(gridB);
    vec2 fB = fract(gridB) - 0.5;
    float hB = hash12(cellB + 19.7);
    float starB = (1.0 - smoothstep(0.0, 0.035, length(fB))) * step(0.9983, hB);

    vec3 starColorA = mix(vec3(0.75, 0.82, 1.00), vec3(1.00, 0.82, 0.55), hash12(cellA + 8.2));
    vec3 starColorB = mix(vec3(0.72, 0.86, 1.00), vec3(1.00, 0.92, 0.72), hash12(cellB + 4.1));
    sky += starA * starColorA * (1.2 + 10.0 * pow(hA, 8.0));
    sky += starB * starColorB * (0.6 + 5.0 * pow(hB, 8.0));
    return sky;
}

float gridSurfaceY(vec3 p) {
    float rho = length(p.xz);
    if (rho <= RHO_H + 0.01) {
        return -6.0;
    }
    float R = arealRadiusFromRho(rho);
    float flamm = 2.0 * sqrt(max(RS * (R - RS), 0.0));
    return -2.95 + 0.42 * flamm;
}

float gridLineMask(vec3 p) {
    vec2 q = p.xz;
    float rho = length(q);
    float scale = 0.70;
    vec2 cart = abs(fract(q / scale) - 0.5);
    float cartLine = 1.0 - smoothstep(0.012, 0.032, min(cart.x, cart.y));

    float radialLine = 1.0 - smoothstep(0.012, 0.030, abs(fract(rho / scale) - 0.5));
    float angle = atan(q.y, q.x);
    float spoke = 1.0 - smoothstep(0.006, 0.020, abs(fract(angle / (PI / 12.0)) - 0.5) / max(rho, 0.6));

    float fade = smoothstep(RHO_H + 0.08, 1.4, rho) * (1.0 - smoothstep(16.0, 24.0, rho));
    return max(cartLine, max(0.75 * radialLine, 0.35 * spoke)) * fade;
}

vec3 gridNormal(vec3 p) {
    float e = 0.035;
    float y0 = gridSurfaceY(p);
    float dx = (gridSurfaceY(p + vec3(e, 0.0, 0.0)) - y0) / e;
    float dz = (gridSurfaceY(p + vec3(0.0, 0.0, e)) - y0) / e;
    return normalize(vec3(-dx, 1.0, -dz));
}

void blendUnder(inout vec3 color, inout float alpha, vec3 src, float srcAlpha) {
    float a = saturate(srcAlpha) * (1.0 - alpha);
    color += src * a;
    alpha += a;
}

vec3 diskEmission(vec3 hit, vec3 rayDir, float time, out float opacity) {
    float rho = length(hit);
    float R = arealRadiusFromRho(rho);
    float Rin = 3.0 * RS;   // Schwarzschild ISCO.
    float Rout = 15.5 * RS;
    float inFade = smoothstep(Rin, Rin + 0.28, R);
    float outFade = 1.0 - smoothstep(Rout - 2.5, Rout, R);
    float radialWindow = inFade * outFade;

    vec2 xz = hit.xz;
    float phi = atan(xz.y, xz.x);
    float flux = pow(Rin / max(R, Rin), 3.0) * max(0.0, 1.0 - sqrt(Rin / max(R, Rin + 0.001)));
    float temperature = 2300.0 + 12500.0 * pow(max(0.0, flux * 22.0), 0.25);

    vec3 radial = normalize(vec3(hit.x, 0.0, hit.z));
    vec3 orbitDir = normalize(cross(vec3(0.0, 1.0, 0.0), radial));
    float beta = sqrt(max(0.0, MASS / max(R - RS, 0.08)));
    beta = min(beta, 0.64);
    vec3 toObserver = normalize(-rayDir);
    float doppler = sqrt(max(0.0, 1.0 - beta * beta)) / max(0.10, 1.0 - beta * dot(orbitDir, toObserver));
    float gravRedshift = sqrt(max(0.018, 1.0 - RS / max(R, RS + 0.001)));
    float g = clamp(doppler * gravRedshift, 0.05, 3.8);

    float rings = 0.78 + 0.22 * sin(34.0 * log(R + 0.3) + 3.0 * phi);
    float turbulence = fbm(vec3(hit.xz * 1.25, time * 0.20 + phi));
    float shear = 0.82 + 0.32 * sin(9.0 * phi - time * (0.75 + 1.6 / pow(R, 1.5)) + 3.0 * turbulence);
    float density = radialWindow * clamp(rings * shear * (0.64 + 0.56 * turbulence), 0.0, 2.2);

    vec3 color = blackbody(temperature * clamp(g, 0.55, 2.2));
    float beaming = pow(g, 3.0);
    float luminosity = 9.0 * pow(max(flux, 0.0), 0.72) * beaming * density;
    color *= luminosity;

    opacity = clamp(0.18 + 0.68 * density, 0.0, 0.88);
    return color;
}

vec3 haloEmission(vec3 p, vec3 dir, float ds) {
    float rho = length(p);
    if (rho <= RHO_H + 0.01) {
        return vec3(0.0);
    }

    float R = arealRadiusFromRho(rho);
    float photonShell = exp(-pow((R - 1.5 * RS) / 0.15, 2.0));
    float corona = exp(-pow((R - 2.25 * RS) / 1.15, 2.0)) * exp(-abs(p.y) * 0.70);
    float diskWind = exp(-abs(p.y) * 1.7) * (1.0 - smoothstep(2.5, 12.0, R)) * smoothstep(1.20, 2.8, R);
    float lensCaustic = pow(saturate(1.0 - abs(dot(normalize(p), dir))), 6.0);

    float gravitationalFade = sqrt(max(0.0, 1.0 - RS / max(R, RS + 0.001)));
    vec3 photonColor = vec3(1.00, 0.56, 0.22) * photonShell * (0.34 + 1.4 * lensCaustic);
    vec3 coronaColor = vec3(0.22, 0.48, 1.00) * corona * 0.13;
    vec3 windColor = vec3(1.00, 0.34, 0.11) * diskWind * 0.10;
    return (photonColor + coronaColor + windColor) * gravitationalFade * ds;
}

vec3 tonemap(vec3 c) {
    c = max(c, vec3(0.0));
    c = vec3(1.0) - exp(-c * uExposure);
    return pow(c, vec3(1.0 / 2.2));
}

vec3 traceRay(vec3 camPos, vec3 rayDir) {
    vec3 p = camPos;
    vec3 dir = normalize(rayDir);
    vec3 radiance = vec3(0.0);
    float alpha = 0.0;
    bool captured = false;
    bool escaped = false;
    float previousGridD = p.y - gridSurfaceY(p);

    for (int i = 0; i < MAX_STEPS; ++i) {
        if (i >= uRaySteps) {
            break;
        }

        float rho = length(p);
        if (rho < RHO_H * 1.018) {
            captured = true;
            break;
        }

        if (rho > FAR_RADIUS && dot(p, dir) > 0.0) {
            escaped = true;
            break;
        }

        float R = arealRadiusFromRho(rho);
        float ds = clamp(0.0175 * rho + 0.0035, 0.0045, 0.135);
        if (R < 4.0) {
            ds *= 0.48;
        }
        if (abs(p.y) < 0.10 && R < 18.0) {
            ds = min(ds, 0.030);
        }

        if (uShowHalo == 1) {
            radiance += (1.0 - alpha) * haloEmission(p, dir, ds);
        }

        vec3 prevP = p;
        float prevY = p.y;
        float prevGridD = previousGridD;

        vec3 acc = opticalAcceleration(p, dir);
        dir = normalize(dir + acc * ds);
        p += dir * ds;

        if (uShowDisk == 1 && prevY * p.y <= 0.0 && abs(prevY - p.y) > 1.0e-5) {
            float t = clamp(prevY / (prevY - p.y), 0.0, 1.0);
            vec3 hit = mix(prevP, p, t);
            float hitR = arealRadiusFromRho(length(hit));
            if (hitR > 3.0 * RS && hitR < 15.5 * RS) {
                float opacity;
                vec3 disk = diskEmission(hit, dir, uTime, opacity);
                blendUnder(radiance, alpha, disk, opacity);
            }
        }

        previousGridD = p.y - gridSurfaceY(p);
        if (uShowGrid == 1 && prevGridD * previousGridD <= 0.0 && abs(prevGridD - previousGridD) > 1.0e-5) {
            float t = clamp(prevGridD / (prevGridD - previousGridD), 0.0, 1.0);
            vec3 hit = mix(prevP, p, t);
            float rhoGrid = length(hit.xz);
            if (rhoGrid > RHO_H + 0.10 && rhoGrid < 22.0) {
                float line = gridLineMask(hit);
                if (line > 0.01) {
                    vec3 n = gridNormal(hit);
                    float light = 0.23 + 0.77 * saturate(dot(n, normalize(vec3(0.25, 0.92, 0.31))));
                    float throat = 1.0 + 1.2 * exp(-pow((arealRadiusFromRho(rhoGrid) - RS) / 1.2, 2.0));
                    vec3 gridColor = vec3(0.16, 0.54, 1.00) * line * light * throat;
                    blendUnder(radiance, alpha, gridColor, 0.34 * line);
                }
            }
        }

        if (alpha > 0.985 && R > 4.0) {
            break;
        }
    }

    if (escaped) {
        radiance += (1.0 - alpha) * sampleBackground(dir);
    } else if (!captured) {
        radiance += (1.0 - alpha) * sampleBackground(dir) * 0.22;
    }

    if (captured) {
        float edge = exp(-22.0 * max(length(p) - RHO_H, 0.0));
        radiance += vec3(0.010, 0.005, 0.002) * edge;
    }

    return radiance;
}

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - uResolution.xy) / max(uResolution.y, 1.0);
    float fov = radians(48.0);

    float cp = cos(uPitch);
    vec3 camPos = uDistance * vec3(sin(uYaw) * cp, sin(uPitch), cos(uYaw) * cp);
    vec3 forward = normalize(-camPos);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = normalize(cross(forward, right));
    vec3 rayDir = normalize(forward + uv.x * right * tan(fov * 0.5) + uv.y * up * tan(fov * 0.5));

    vec3 color = traceRay(camPos, rayDir);

    float vignette = 1.0 - smoothstep(0.24, 1.36, length(uv * vec2(0.82, 1.0)));
    color *= mix(0.78, 1.0, vignette);
    fragColor = vec4(tonemap(color), 1.0);
}
)GLSL";

static const char *kLineVertexShader = R"GLSL(
#version 410 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uMVP;
uniform float uPointSize;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
}
)GLSL";

static const char *kLineFragmentShader = R"GLSL(
#version 410 core

out vec4 fragColor;

uniform vec4 uColor;
uniform int uUsePointFalloff;

void main() {
    float pointFade = 1.0;
    if (uUsePointFalloff == 1) {
        vec2 pointUV = gl_PointCoord * 2.0 - 1.0;
        pointFade = 1.0 - smoothstep(0.72, 1.0, length(pointUV));
    }
    fragColor = vec4(uColor.rgb, uColor.a * pointFade);
}
)GLSL";

static GLuint CompileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::fprintf(stderr, "Shader compile failed:\n%s\n", log.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint CreateProgram(const char *vertexSource, const char *fragmentSource) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs) {
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::fprintf(stderr, "Program link failed:\n%s\n", log.c_str());
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

struct Vec3f {
    float x;
    float y;
    float z;
};

struct Mat4f {
    float m[16];
};

static Vec3f operator+(Vec3f a, Vec3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3f operator-(Vec3f a, Vec3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3f operator*(Vec3f a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

static float Dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3f Cross(Vec3f a, Vec3f b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float Length(Vec3f v) {
    return std::sqrt(Dot(v, v));
}

static Vec3f Normalize(Vec3f v) {
    float len = std::max(Length(v), 1.0e-7f);
    return v * (1.0f / len);
}

static float ArealRadiusFromRho(float rho) {
    constexpr float rs = 1.0f;
    constexpr float rhoH = 0.25f;
    float r = std::max(rho, rhoH + 1.0e-5f);
    float a = rs / (4.0f * r);
    return r * (1.0f + a) * (1.0f + a);
}

static float DLogOpticalNDRho(float rho) {
    constexpr float rs = 1.0f;
    constexpr float rhoH = 0.25f;
    float r = std::max(rho, rhoH + 0.002f);
    float a = rs / (4.0f * r);
    return (-a / r) * (3.0f / (1.0f + a) + 1.0f / std::max(0.02f, 1.0f - a));
}

static Vec3f OpticalAcceleration(Vec3f p, Vec3f dir) {
    constexpr float rhoH = 0.25f;
    float rho = std::max(Length(p), rhoH + 0.002f);
    Vec3f radial = p * (1.0f / rho);
    Vec3f gradLogN = radial * DLogOpticalNDRho(rho);
    return gradLogN - dir * Dot(dir, gradLogN);
}

static Vec3f CameraPosition(float yaw, float pitch, float distance) {
    float cp = std::cos(pitch);
    return {
        distance * std::sin(yaw) * cp,
        distance * std::sin(pitch),
        distance * std::cos(yaw) * cp
    };
}

static Mat4f Perspective(float fovyRadians, float aspect, float nearPlane, float farPlane) {
    Mat4f out = {};
    float f = 1.0f / std::tan(fovyRadians * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return out;
}

static Mat4f LookAt(Vec3f eye, Vec3f center, Vec3f upHint) {
    Vec3f f = Normalize(center - eye);
    Vec3f s = Normalize(Cross(f, upHint));
    Vec3f u = Cross(s, f);

    Mat4f out = {};
    out.m[0] = s.x;
    out.m[4] = s.y;
    out.m[8] = s.z;
    out.m[12] = -Dot(s, eye);
    out.m[1] = u.x;
    out.m[5] = u.y;
    out.m[9] = u.z;
    out.m[13] = -Dot(u, eye);
    out.m[2] = -f.x;
    out.m[6] = -f.y;
    out.m[10] = -f.z;
    out.m[14] = Dot(f, eye);
    out.m[15] = 1.0f;
    return out;
}

static Mat4f Multiply(Mat4f a, Mat4f b) {
    Mat4f out = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = v;
        }
    }
    return out;
}

@interface BlackHoleView : NSOpenGLView
@end

@implementation BlackHoleView {
    GLuint _program;
    GLuint _lineProgram;
    GLuint _vao;
    GLuint _lightVao;
    GLuint _lightVbo;
    NSTimer *_timer;
    CFTimeInterval _startTime;
    float _pausedAt;
    BOOL _paused;
    float _yaw;
    float _pitch;
    float _distance;
    float _exposure;
    int _raySteps;
    BOOL _showLightRay;
    BOOL _slowMotion;
    float _lightPulseRate;
    BOOL _showDisk;
    BOOL _showGrid;
    BOOL _showHalo;
    std::vector<Vec3f> _lightPath;
    NSPoint _lastMouse;
}

- (instancetype)initWithFrame:(NSRect)frame {
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };

    NSOpenGLPixelFormat *pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    self = [super initWithFrame:frame pixelFormat:pixelFormat];
    if (self) {
        _yaw = 0.0f;
        _pitch = 0.22f;
        _distance = 18.5f;
        _exposure = 1.18f;
        _raySteps = 420;
        _showLightRay = YES;
        _slowMotion = YES;
        _lightPulseRate = 0.070f;
        _showDisk = NO;
        _showGrid = YES;
        _showHalo = YES;
        _paused = NO;
        _pausedAt = 0.0f;
        [self setWantsBestResolutionOpenGLSurface:YES];
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)rebuildLightPath {
    _lightPath.clear();

    Vec3f p = {-26.0f, 2.78f, 0.0f};
    Vec3f dir = Normalize({1.0f, 0.0f, 0.0f});

    for (int i = 0; i < 760; ++i) {
        _lightPath.push_back(p);

        float rho = Length(p);
        if (rho < 0.25f * 1.018f) {
            break;
        }
        if (rho > 27.0f && Dot(p, dir) > 0.0f && p.x > 0.0f) {
            break;
        }

        float arealR = ArealRadiusFromRho(rho);
        float ds = std::clamp(0.020f * rho + 0.010f, 0.012f, 0.18f);
        if (arealR < 4.0f) {
            ds *= 0.45f;
        }

        Vec3f acc = OpticalAcceleration(p, dir);
        dir = Normalize(dir + acc * ds);
        p = p + dir * ds;
    }

    glBindVertexArray(_lightVao);
    glBindBuffer(GL_ARRAY_BUFFER, _lightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(_lightPath.size() * sizeof(Vec3f)),
                 _lightPath.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3f), reinterpret_cast<const void *>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [[self window] makeFirstResponder:self];
}

- (void)prepareOpenGL {
    [super prepareOpenGL];
    [[self openGLContext] makeCurrentContext];

    GLint swapInterval = 1;
    [[self openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];

    _program = CreateProgram(kVertexShader, kFragmentShader);
    _lineProgram = CreateProgram(kLineVertexShader, kLineFragmentShader);
    glGenVertexArrays(1, &_vao);
    glGenVertexArrays(1, &_lightVao);
    glGenBuffers(1, &_lightVbo);
    glBindVertexArray(_vao);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);

    [self rebuildLightPath];

    _startTime = CACurrentMediaTime();
    __weak BlackHoleView *weakSelf = self;
    _timer = [NSTimer timerWithTimeInterval:(1.0 / 60.0) repeats:YES block:^(NSTimer *) {
        [weakSelf setNeedsDisplay:YES];
    }];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)dealloc {
    [_timer invalidate];
    if (_program || _lineProgram || _vao || _lightVao || _lightVbo) {
        [[self openGLContext] makeCurrentContext];
        if (_program) {
            glDeleteProgram(_program);
        }
        if (_lineProgram) {
            glDeleteProgram(_lineProgram);
        }
        if (_vao) {
            glDeleteVertexArrays(1, &_vao);
        }
        if (_lightVao) {
            glDeleteVertexArrays(1, &_lightVao);
        }
        if (_lightVbo) {
            glDeleteBuffers(1, &_lightVbo);
        }
    }
}

- (void)reshape {
    [super reshape];
    [self setNeedsDisplay:YES];
}

- (void)drawLightRayWithTime:(float)timeNow width:(GLint)width height:(GLint)height {
    if (!_showLightRay || _lightPath.size() < 2 || !_lineProgram) {
        return;
    }

    float aspect = static_cast<float>(width) / std::max(1.0f, static_cast<float>(height));
    Vec3f camPos = CameraPosition(_yaw, _pitch, _distance);
    Mat4f view = LookAt(camPos, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    Mat4f proj = Perspective(48.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.05f, 140.0f);
    Mat4f mvp = Multiply(proj, view);

    GLsizei pathCount = static_cast<GLsizei>(_lightPath.size());
    float phase = std::fmod(std::max(0.0f, timeNow) * _lightPulseRate, 1.0f);
    GLsizei head = std::clamp(static_cast<GLsizei>(phase * static_cast<float>(pathCount - 1)), static_cast<GLsizei>(1), pathCount - 1);
    GLsizei tail = std::max(static_cast<GLsizei>(0), head - static_cast<GLsizei>(52));

    glUseProgram(_lineProgram);
    glUniformMatrix4fv(glGetUniformLocation(_lineProgram, "uMVP"), 1, GL_FALSE, mvp.m);
    glBindVertexArray(_lightVao);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glUniform1i(glGetUniformLocation(_lineProgram, "uUsePointFalloff"), 0);
    glLineWidth(1.0f);
    glUniform1f(glGetUniformLocation(_lineProgram, "uPointSize"), 1.0f);
    glUniform4f(glGetUniformLocation(_lineProgram, "uColor"), 0.34f, 0.76f, 1.00f, 0.20f);
    glDrawArrays(GL_LINE_STRIP, 0, pathCount);

    glLineWidth(2.0f);
    glUniform4f(glGetUniformLocation(_lineProgram, "uColor"), 0.92f, 0.98f, 1.00f, 0.82f);
    glDrawArrays(GL_LINE_STRIP, tail, head - tail + 1);

    glUniform1i(glGetUniformLocation(_lineProgram, "uUsePointFalloff"), 1);
    glUniform1f(glGetUniformLocation(_lineProgram, "uPointSize"), 18.0f);
    glUniform4f(glGetUniformLocation(_lineProgram, "uColor"), 1.00f, 0.98f, 0.78f, 1.0f);
    glDrawArrays(GL_POINTS, head, 1);

    glUniform1f(glGetUniformLocation(_lineProgram, "uPointSize"), 9.0f);
    glUniform4f(glGetUniformLocation(_lineProgram, "uColor"), 0.30f, 0.80f, 1.00f, 0.82f);
    glDrawArrays(GL_POINTS, 0, 1);

    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[self openGLContext] makeCurrentContext];
    NSRect backingBounds = [self convertRectToBacking:[self bounds]];
    GLint width = static_cast<GLint>(std::max<CGFloat>(1.0, backingBounds.size.width));
    GLint height = static_cast<GLint>(std::max<CGFloat>(1.0, backingBounds.size.height));
    glViewport(0, 0, width, height);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_program);
    float timeNow = _paused ? _pausedAt : static_cast<float>(CACurrentMediaTime() - _startTime);
    glUniform2f(glGetUniformLocation(_program, "uResolution"), static_cast<float>(width), static_cast<float>(height));
    glUniform1f(glGetUniformLocation(_program, "uTime"), timeNow);
    glUniform1f(glGetUniformLocation(_program, "uYaw"), _yaw);
    glUniform1f(glGetUniformLocation(_program, "uPitch"), _pitch);
    glUniform1f(glGetUniformLocation(_program, "uDistance"), _distance);
    glUniform1f(glGetUniformLocation(_program, "uExposure"), _exposure);
    glUniform1i(glGetUniformLocation(_program, "uRaySteps"), _raySteps);
    glUniform1i(glGetUniformLocation(_program, "uShowDisk"), _showDisk ? 1 : 0);
    glUniform1i(glGetUniformLocation(_program, "uShowGrid"), _showGrid ? 1 : 0);
    glUniform1i(glGetUniformLocation(_program, "uShowHalo"), _showHalo ? 1 : 0);

    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    [self drawLightRayWithTime:timeNow width:width height:height];
    [[self openGLContext] flushBuffer];
}

- (void)mouseDown:(NSEvent *)event {
    _lastMouse = [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    CGFloat dx = p.x - _lastMouse.x;
    CGFloat dy = p.y - _lastMouse.y;
    _lastMouse = p;
    _yaw += static_cast<float>(dx) * 0.0065f;
    _pitch += static_cast<float>(dy) * 0.0065f;
    _pitch = std::max(-1.28f, std::min(1.28f, _pitch));
    [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent *)event {
    float scale = std::exp(static_cast<float>([event scrollingDeltaY]) * 0.018f);
    _distance = std::max(5.0f, std::min(38.0f, _distance / scale));
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)event {
    NSString *chars = [event charactersIgnoringModifiers];
    if ([chars length] == 0) {
        return;
    }
    unichar c = [chars characterAtIndex:0];
    switch (c) {
        case 27:
        case 'q':
        case 'Q':
            [NSApp terminate:nil];
            break;
        case ' ':
            if (_paused) {
                _startTime = CACurrentMediaTime() - _pausedAt;
                _paused = NO;
            } else {
                _pausedAt = static_cast<float>(CACurrentMediaTime() - _startTime);
                _paused = YES;
            }
            break;
        case 'd':
        case 'D':
            _showDisk = !_showDisk;
            break;
        case 'g':
        case 'G':
            _showGrid = !_showGrid;
            break;
        case 'h':
        case 'H':
            _showHalo = !_showHalo;
            break;
        case 'l':
        case 'L':
            _showLightRay = !_showLightRay;
            break;
        case 's':
        case 'S':
            _slowMotion = !_slowMotion;
            _lightPulseRate = _slowMotion ? 0.070f : 0.220f;
            break;
        case '[':
            _raySteps = std::max(160, _raySteps - 40);
            break;
        case ']':
            _raySteps = std::min(620, _raySteps + 40);
            break;
        case '-':
        case '_':
            _exposure = std::max(0.25f, _exposure * 0.88f);
            break;
        case '=':
        case '+':
            _exposure = std::min(4.0f, _exposure * 1.14f);
            break;
        case 'r':
        case 'R':
            _yaw = 0.0f;
            _pitch = 0.22f;
            _distance = 18.5f;
            _exposure = 1.18f;
            _raySteps = 420;
            _showLightRay = YES;
            _slowMotion = YES;
            _lightPulseRate = 0.070f;
            _showDisk = NO;
            _showGrid = YES;
            _showHalo = YES;
            break;
        default:
            [super keyDown:event];
            return;
    }
    [self setNeedsDisplay:YES];
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect frame = NSMakeRect(0, 0, 1280, 800);
    NSUInteger style = NSWindowStyleMaskTitled |
                       NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable |
                       NSWindowStyleMaskResizable;

    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [self.window center];
    [self.window setTitle:@"Black Hole - Schwarzschild Ray Tracing"];
    [self.window setMinSize:NSMakeSize(840, 520)];

    BlackHoleView *view = [[BlackHoleView alloc] initWithFrame:frame];
    [self.window setContentView:view];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
