#version 330 core

in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uImage;
uniform float uExposure;

vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uImage, vUV).rgb * uExposure;
    vec3 color = aces(hdr);
    color = pow(color, vec3(1.0 / 2.2));
    float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    fragColor = vec4(color + (dither - 0.5) / 255.0, 1.0);
}
