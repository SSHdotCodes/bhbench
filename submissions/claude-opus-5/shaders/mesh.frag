#version 410 core
in vec3 vN;
in vec4 vC;
in vec3 vP;

layout(location = 0) out vec4 fragColor;

uniform vec3  uEye;
uniform int   uLit;        // 0 = emissive lines, 1 = shaded surface
uniform float uAlpha;

void main() {
    if (uLit == 0) {
        fragColor = vec4(vC.rgb, vC.a * uAlpha);
        return;
    }

    vec3 N = normalize(vN);
    vec3 V = normalize(uEye - vP);
    if (dot(N, V) < 0.0) N = -N;                 // the funnel is viewed from both sides

    vec3 L1 = normalize(vec3(0.45, 0.30, 0.85));
    vec3 L2 = normalize(vec3(-0.6, -0.5, 0.25));

    float d = 0.42 * max(dot(N, L1), 0.0) + 0.16 * max(dot(N, L2), 0.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.10;

    vec3 col = vC.rgb * (0.16 + d) + vec3(0.18, 0.34, 0.68) * rim * 0.22 + vec3(spec);
    fragColor = vec4(col, vC.a * uAlpha);
}
