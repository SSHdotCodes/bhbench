#version 300 es
precision highp float;
precision highp int;
in vec4 vColor;
in vec2 vMeta;
in vec3 vWorldPos;
layout(location = 0) out vec4 FragColor;

uniform float uTime;
uniform float uHorizonR;
uniform float uMass;
uniform vec3  uCamPos;

void main() {
    float rOverM = vMeta.x;
    float lineType = vMeta.y;

    vec3 col = vColor.rgb;
    float alpha = vColor.a;

    // Smoothly attenuate far-side grid lines occluded behind the black hole shadow sphere
    if (dot(vWorldPos.xz, uCamPos.xz) < 0.0 && vWorldPos.y > -2.5 * uMass) {
        vec3 toCam = uCamPos - vWorldPos;
        float distToCam = length(toCam);
        vec3 rayDir = toCam / max(distToCam, 1e-4);
        float proj = dot(-vWorldPos, rayDir);
        if (proj > 0.0 && proj < distToCam) {
            vec3 closestPt = vWorldPos + proj * rayDir;
            float impactR = length(closestPt);
            alpha *= smoothstep(3.6 * uMass, 5.2 * uMass, impactR);
        }
    }

    if (lineType < 0.5) {
        // Gullstrand-Painleve "River Model of Spacetime" infalling pulse waves
        // Spacetime flows inward at v_river = sqrt(2M / r), accelerating into the trapdoor
        float riverPhase = 2.4 * sqrt(max(0.1, rOverM)) + uTime * 1.75;
        float pulse = 0.65 + 0.35 * sin(riverPhase * 3.14159265);
        // Brighten curvature glow inside the trapdoor throat
        float throatGlow = 1.0 + 1.25 * exp(-max(0.0, rOverM - uHorizonR / uMass) * 0.55);
        col *= pulse * throatGlow;
    } else if (lineType > 4.5) {
        // Animated photon packet traveling along 3D null geodesic beam
        float packet = 0.75 + 0.55 * pow(0.5 + 0.5 * sin(rOverM * 7.5 - uTime * 4.5), 3.0);
        col *= packet;
    }

    FragColor = vec4(col, alpha);
}
