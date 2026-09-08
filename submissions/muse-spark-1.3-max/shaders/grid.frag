#version 410 core
// Modes: lit surface (funnel), unlit (lines/disk/points), fresnel glow shell.
in vec4 vColor;
in vec3 vNormalW;
in vec3 vPosW;

uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform int uUnlit;
uniform int uGlow;
uniform float uAlpha;
uniform float uPointSize;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormalW);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(uCamPos - vPosW);
    vec4 c = vColor;
    if (uGlow == 1) {
        float f = pow(1.0 - abs(dot(N, V)), 2.0);
        c.a *= f;
        c.rgb *= (0.5 + 1.8 * f);
    } else if (uUnlit == 0) {
        float dif = max(dot(N, normalize(uLightDir)), 0.0);
        float rim = pow(1.0 - abs(dot(N, V)), 3.0);
        c.rgb *= (0.22 + 0.78 * dif);
        c.rgb += rim * vec3(0.15, 0.35, 0.60);
    }
    if (uPointSize > 0.0) {
        float m = smoothstep(0.5, 0.08, length(gl_PointCoord - 0.5));
        c.a *= m;
    }
    c.a *= uAlpha;
    FragColor = c;
}
