#version 410 core

in vec4 vColor;
in vec3 vWorld;
out vec4 fragColor;

uniform float uIntensity;
uniform int uRoundPoints;
uniform int uOpaque;

void main() {
    if (uOpaque == 1) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (uRoundPoints == 1) {
        vec2 d = gl_PointCoord - 0.5;
        float r = length(d);
        if (r > 0.5) discard;
        float a = smoothstep(0.5, 0.05, r);
        fragColor = vec4(vColor.rgb * vColor.a * a * uIntensity, 1.0);
    } else {
        fragColor = vec4(vColor.rgb * vColor.a * uIntensity, 1.0);
    }
}
