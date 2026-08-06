#version 330 core
in vec3 vPos;
in float vDepth;
out vec4 fragColor;
uniform float uRs;
uniform float uTime;

void main() {
    float r = length(vPos.xz);
    float proximity = 1.0 - smoothstep(uRs, uRs + 8.0, r);

    vec3 col = mix(vec3(0.1, 0.4, 0.8), vec3(0.8, 0.2, 0.1), proximity);

    float pulse = 0.6 + 0.4 * sin(uTime * 1.5 - r * 0.5);
    col *= pulse;

    float alpha = mix(0.15, 0.6, proximity);
    alpha *= smoothstep(20.0, 15.0, r);

    fragColor = vec4(col, alpha);
}
