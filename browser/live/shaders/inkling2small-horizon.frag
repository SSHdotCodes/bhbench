#version 300 es
precision highp float;
precision highp int;
uniform vec3 uCamPos;
in vec3 vNormal;
out vec4 fragColor;
void main() {
    vec3 N = normalize(vNormal);
    // Pure black occluder with a whisper of warm Fresnel at the limb;
    // the horizon itself emits nothing — we cheat only to seat it visually.
    vec3 viewDir = normalize(uCamPos);   // sphere centred on origin
    float fres = pow(1.0 - abs(dot(viewDir, N)), 8.0);
    vec3 col = vec3(0.004, 0.003, 0.002) + vec3(0.20, 0.085, 0.030) * fres * 0.14;
    fragColor = vec4(col, 1.0);
}
