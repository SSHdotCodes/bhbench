#version 330 core
// Final composite: scene + bloom halo -> ACES filmic tonemap -> gamma.
precision highp float;

in  vec2 vUV;
out vec4 outColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;
uniform float uTime;

// Narkowicz ACES filmic approximation
vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    c += texture(uBloom, vUV).rgb * uBloomStrength;

    c *= uExposure;
    c = aces(c);

    float r = length(vUV - 0.5);
    c *= mix(1.0, smoothstep(0.95, 0.30, r), uVignette);

    // subtle dither to kill banding
    float d = fract(sin(dot(vUV * 1024.0 + uTime, vec2(12.9898, 78.233))) * 43758.5453);
    c += (d - 0.5) / 255.0;

    outColor = vec4(pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
