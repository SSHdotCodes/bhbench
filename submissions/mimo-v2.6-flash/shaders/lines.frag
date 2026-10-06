#version 410 core
in vec4 vCol;
layout(location = 0) out vec4 outColor;

uniform float uRoundPoints;

void main() {
    if (uRoundPoints > 0.5) {
        vec2 d = gl_PointCoord - vec2(0.5);
        float r2 = dot(d, d);
        if (r2 > 0.25) discard;
        float a = 1.0 - smoothstep(0.16, 0.25, r2);
        outColor = vec4(vCol.rgb * vCol.a * a, vCol.a * a);
        return;
    }
    outColor = vCol;
}
