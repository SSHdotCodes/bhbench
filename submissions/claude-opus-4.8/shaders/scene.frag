#version 410 core
in vec3 vColor;
out vec4 FragColor;

uniform float uAlpha;
uniform int   uRound;     // 1 = treat points as soft round dots

void main() {
    float a = uAlpha;
    if (uRound == 1) {
        vec2 d = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(d, d);
        if (r2 > 1.0) discard;
        a *= smoothstep(1.0, 0.1, r2);     // soft glow falloff
    }
    FragColor = vec4(vColor, a);
}
