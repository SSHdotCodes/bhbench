#version 410 core
// =============================================================================
//  composite.frag — camera model: glare (bloom), exposure, filmic tone curve,
//  sRGB encoding.  Everything upstream is linear scene radiance.
// =============================================================================
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2  uSceneSize;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uBloomNorm;     // 1 / number of bloom levels summed in uBloom
uniform int   uTonemap;     // 0 ACES (Hill), 1 ACES (Narkowicz), 2 hue-preserving sigmoid, 3 clip
uniform float uContrast;    // exponent n of the sigmoid

// Catmull-Rom reconstruction when the scene was rendered below display size.
vec3 sampleSharp(sampler2D tex, vec2 uv, vec2 size) {
    vec2 p = uv * size;
    vec2 c = floor(p - 0.5) + 0.5;
    vec2 f = p - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (c - 1.0) / size, t3 = (c + 2.0) / size, t12 = (c + w2 / w12) / size;
    vec3 r = texture(tex, vec2(t0.x, t0.y)).rgb * w0.x * w0.y + texture(tex, vec2(t12.x, t0.y)).rgb * w12.x * w0.y +
             texture(tex, vec2(t3.x, t0.y)).rgb * w3.x * w0.y + texture(tex, vec2(t0.x, t12.y)).rgb * w0.x * w12.y +
             texture(tex, vec2(t12.x, t12.y)).rgb * w12.x * w12.y + texture(tex, vec2(t3.x, t12.y)).rgb * w3.x * w12.y +
             texture(tex, vec2(t0.x, t3.y)).rgb * w0.x * w3.y + texture(tex, vec2(t12.x, t3.y)).rgb * w12.x * w3.y +
             texture(tex, vec2(t3.x, t3.y)).rgb * w3.x * w3.y;
    return max(r, vec3(0.0));
}

// ACES filmic fit (S. Hill): sRGB -> ACEScg-ish -> RRT+ODT curve -> sRGB.
vec3 aces(vec3 c) {
    const mat3 inM = mat3(0.59719, 0.07600, 0.02840,
                          0.35458, 0.90834, 0.13383,
                          0.04823, 0.01566, 0.83777);
    const mat3 outM = mat3( 1.60475, -0.10208, -0.00327,
                           -0.53108,  1.10813, -0.07276,
                           -0.07367, -0.00605,  1.07602);
    vec3 v = inM * c;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outM * (a / b), 0.0, 1.0);
}

// Default tone curve: the Naka-Rushton response of photoreceptors,
//     t(m) = m^n / (m^n + 1),      m = largest colour channel,
// which is a sigmoid in log-brightness - a gentle toe, a long shoulder, and
// room for the orders of magnitude an accretion disk spans (the Wien tail
// makes cool gas exponentially dimmer).  Colour ratios are preserved, so a
// blackbody keeps its hue as it dims; only very intense light rolls off to
// white, as it does on film and in the eye.
vec3 hueKeeping(vec3 c) {
    float m = max(max(c.r, c.g), max(c.b, 1e-6));
    float mn = pow(m, uContrast);
    float t = mn / (mn + 1.0);
    vec3 o = c * (t / m);
    float white = smoothstep(1.5, 24.0, m);
    return clamp(mix(o, vec3(1.0), white * white), 0.0, 1.0);
}

vec3 srgbEncode(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

float ign(vec2 p) {          // interleaved gradient noise, for dithering
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 scene = sampleSharp(uScene, vUV, uSceneSize);
    vec3 bloom = texture(uBloom, vUV).rgb * uBloomNorm;
    vec3 c = mix(scene, bloom, uBloomStrength) * uExposure;
    if (uTonemap == 0) c = aces(c);
    else if (uTonemap == 1) c = clamp(c * (2.51 * c + 0.03) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    else if (uTonemap == 2) c = hueKeeping(c);
    else c = clamp(c, 0.0, 1.0);
    c = srgbEncode(c);
    c += (ign(gl_FragCoord.xy) - 0.5) / 255.0;
    oColor = vec4(c, 1.0);
}
