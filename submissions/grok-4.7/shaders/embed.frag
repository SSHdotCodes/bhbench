#version 410 core
in vec3 vNormal;
in vec3 vWorld;
in float vRadius;
in float vPhi;
in float vKind;

uniform vec3 uEye;

layout(location = 0) out vec4 fragColor;

void main() {
    if (vKind > 1.5) {
        vec2 pc = gl_PointCoord * 2.0 - 1.0;
        float d = dot(pc, pc);
        if (d > 1.0) discard;
        float core = pow(1.0 - d, 1.4);
        vec3 col = mix(vec3(1.0, 0.55, 0.15), vec3(1.0, 0.95, 0.75), core);
        fragColor = vec4(col, core);
        return;
    }
    if (vKind > 0.5) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(uEye - vWorld);
    float upL = max(dot(N, normalize(vec3(0.32, 0.48, 0.82))), 0.0);
    float fill = max(dot(N, normalize(vec3(-0.55, -0.15, 0.25))), 0.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.2);

    float depthT = smoothstep(2.1, 13.0, vRadius);
    vec3 base = mix(vec3(0.004, 0.006, 0.012), vec3(0.05, 0.13, 0.26), depthT);
    vec3 lit = base * (0.16 + 1.05 * upL + 0.28 * fill);
    lit += rim * vec3(0.18, 0.32, 0.55) * depthT;

    float sector = vPhi * (24.0 / 6.28318530718);
    float fd = min(fract(sector), 1.0 - fract(sector));
    float radial = 1.0 - smoothstep(fwidth(sector) * 0.7, fwidth(sector) * 2.1, fd);

    float ring = vRadius;
    float fc = min(fract(ring), 1.0 - fract(ring));
    float circ = 1.0 - smoothstep(fwidth(ring) * 0.7, fwidth(ring) * 2.1, fc);
    float grid = clamp(max(radial, circ), 0.0, 1.0);
    lit += grid * vec3(0.28, 0.48, 0.72) * mix(0.35, 1.0, depthT);

    float w = max(fwidth(vRadius) * 2.0, 0.025);
    float lip = 1.0 - smoothstep(0.0, 0.34, vRadius - 2.04);
    float photon = 1.0 - smoothstep(w * 0.35, w, abs(vRadius - 3.0));
    float isco = 1.0 - smoothstep(w * 0.45, w, abs(vRadius - 6.0));
    lit += lip * vec3(1.0, 0.74, 0.28);
    lit += photon * vec3(0.25, 0.85, 1.0);
    lit += isco * vec3(1.0, 0.46, 0.12);

    fragColor = vec4(lit, 1.0);
}
