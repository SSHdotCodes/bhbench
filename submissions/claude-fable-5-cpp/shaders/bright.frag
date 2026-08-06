#version 410 core
// Bloom bright-pass: keep only the HDR highlights (photon ring, disk, stars).
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene;

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = smoothstep(1.0, 2.4, l);
    fragColor = vec4(c * knee, 1.0);
}
