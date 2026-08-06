#version 410 core
// Dual-purpose bloom pass: 13-tap Karis downsample (with an optional soft
// threshold on the first level) and a 3x3 tent upsample.

in vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uSrc;
uniform vec2  uTexel;      // 1 / source resolution
uniform int   uMode;       // 0 prefilter, 1 downsample, 2 upsample
uniform float uThreshold;
uniform float uRadius;

vec3 tap(vec2 uv) { return texture(uSrc, uv).rgb; }

void main() {
    vec2 uv = vUV;

    if (uMode == 2) {
        vec2 d = uTexel * uRadius;
        vec3 s = tap(uv + vec2(-d.x,  d.y)) + tap(uv + vec2(0.0,  d.y)) * 2.0 + tap(uv + vec2(d.x,  d.y))
               + tap(uv + vec2(-d.x, 0.0)) * 2.0 + tap(uv) * 4.0 + tap(uv + vec2(d.x, 0.0)) * 2.0
               + tap(uv + vec2(-d.x, -d.y)) + tap(uv + vec2(0.0, -d.y)) * 2.0 + tap(uv + vec2(d.x, -d.y));
        fragColor = vec4(s / 16.0, 1.0);
        return;
    }

    vec2 t = uTexel;
    vec3 a = tap(uv + vec2(-2.0*t.x,  2.0*t.y));
    vec3 b = tap(uv + vec2( 0.0,      2.0*t.y));
    vec3 c = tap(uv + vec2( 2.0*t.x,  2.0*t.y));
    vec3 d = tap(uv + vec2(-2.0*t.x,  0.0));
    vec3 e = tap(uv);
    vec3 f = tap(uv + vec2( 2.0*t.x,  0.0));
    vec3 g = tap(uv + vec2(-2.0*t.x, -2.0*t.y));
    vec3 h = tap(uv + vec2( 0.0,     -2.0*t.y));
    vec3 i = tap(uv + vec2( 2.0*t.x, -2.0*t.y));
    vec3 j = tap(uv + vec2(-t.x,  t.y));
    vec3 k = tap(uv + vec2( t.x,  t.y));
    vec3 l = tap(uv + vec2(-t.x, -t.y));
    vec3 m = tap(uv + vec2( t.x, -t.y));

    vec3 s = e * 0.125
           + (a + c + g + i) * 0.03125
           + (b + d + f + h) * 0.0625
           + (j + k + l + m) * 0.125;

    if (uMode == 0) {
        // Soft-knee threshold so bright disk pixels bloom without the whole
        // star field turning to mush.
        float lum = dot(s, vec3(0.2126, 0.7152, 0.0722));
        float knee = uThreshold * 0.6;
        float soft = clamp((lum - uThreshold + knee) / max(2.0 * knee, 1e-4), 0.0, 1.0);
        float w = max(lum - uThreshold, lum * soft * soft * 0.5) / max(lum, 1e-5);
        s *= w;
    }

    fragColor = vec4(s, 1.0);
}
