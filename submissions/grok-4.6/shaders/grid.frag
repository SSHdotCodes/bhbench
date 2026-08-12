#version 410 core
in vec3 vColor;
in vec3 vWorld;
uniform vec3 uCamPos;
uniform float uMass;
out vec4 fragColor;
void main() {
    float dist = length(uCamPos - vWorld);
    float fade = exp(-dist * 0.018);
    float rs = 2.0 * uMass;
    float rho = length(vWorld.xz);
    float throat = smoothstep(rs * 1.02, 10.0 * uMass, rho);
    vec3 c = vColor * (0.55 + 0.45 * throat);
    float alpha = fade * (0.18 + 0.42 * throat);
    fragColor = vec4(c, clamp(alpha, 0.04, 0.72));
}
