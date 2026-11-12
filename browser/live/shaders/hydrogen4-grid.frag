#version 300 es
precision highp float;
precision highp int;
uniform vec4 camPosTime, camForwardFov, camRightAspect, camUpDummy, camBL;
uniform vec4 bhParams, diskParams, gridParams, visualParams, screenParams;
uniform mat4 viewProj;
in vec3 worldPos;
in float radius, twist, camDist;
uniform highp sampler2D depthTex;
out vec4 fragColor;
void main(){
    if (gridParams.x < 0.5) {
        discard;
    }

    float sceneHitDist = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;

    float occlusion = 1.0 - smoothstep(sceneHitDist - 0.35, sceneHitDist + 0.35, camDist);
    if (occlusion < 0.01) {
        discard;
    }

    float rp = bhParams.y;
    float rISCO = bhParams.z;
    float rOuter = gridParams.z;
    float simTime = camPosTime.w;

    float phi = atan(worldPos.y, worldPos.x);
    float twistedPhi = phi - twist;
    vec2 gridUV = vec2(radius * cos(twistedPhi), radius * sin(twistedPhi));

    // Anti-aliased Cartesian square grid lines using screen-space derivatives
    float gridSpacing = 2.0;
    vec2 coord = gridUV / gridSpacing;
    vec2 gridDeriv = fwidth(coord);
    vec2 gridDist = abs(fract(coord - 0.5) - 0.5) / max(gridDeriv, vec2(1e-4));
    float cartesianLine = 1.0 - min(min(gridDist.x, gridDist.y), 1.0);

    // Anti-aliased concentric radial curvature rings
    float rCoord = radius / 2.0;
    float rDist = abs(fract(rCoord - 0.5) - 0.5) / max(fwidth(rCoord), 1e-4);
    float radialRing = 1.0 - min(rDist, 1.0);

    // Physical GR radius markers on the trapdoor surface
    float horizonRing = exp(-pow((radius - rp) / 0.09, 2.0));
    float iscoRing    = exp(-pow((radius - rISCO) / 0.08, 2.0));
    float ergoRing    = (bhParams.x > 0.05) ? exp(-pow((radius - 2.0) / 0.08, 2.0)) : 0.0;

    float curvatureFactor = clamp(pow(rp / max(radius, rp * 0.85), 0.72), 0.0, 1.0);
    vec3 outerCol  = vec3(0.10, 0.62, 1.00);
    vec3 midCol    = vec3(0.95, 0.52, 0.14);
    vec3 throatCol = vec3(1.00, 0.18, 0.05);
    vec3 baseLineCol = (curvatureFactor < 0.5)
        ? mix(outerCol, midCol, curvatureFactor * 2.0)
        : mix(midCol, throatCol, (curvatureFactor - 0.5) * 2.0);

    float infallPulse = 0.0;
    if (visualParams.z > 0.5 && radius > rp * 0.72) {
        float wavePhase = fract(radius * 0.28 + simTime * 0.65);
        float pulse = exp(-pow((wavePhase - 0.5) / 0.065, 2.0));
        infallPulse = pulse * 0.35 * smoothstep(rOuter * 0.85, rp * 1.1, radius);
    }

    float edgeFade = 1.0 - smoothstep(rOuter * 0.62, rOuter * 0.96, radius);
    float horizonInteriorFade = smoothstep(rp * 0.68, rp * 0.94, radius);

    float lineStrength = max(cartesianLine * 0.90, radialRing * 0.34);
    vec3 color = baseLineCol * lineStrength * (0.65 + 1.05 * curvatureFactor);
    color += baseLineCol * (0.018 + 0.075 * curvatureFactor);

    color += vec3(1.0, 0.28, 0.08) * horizonRing * 2.5;
    color += vec3(0.22, 0.95, 0.88) * iscoRing * 0.75;
    color += vec3(0.80, 0.42, 1.00) * ergoRing * 0.55;
    color += mix(vec3(0.28, 0.82, 1.0), vec3(1.0, 0.52, 0.18), curvatureFactor) * infallPulse;

    float alpha = (lineStrength * 0.86 + 0.025 + horizonRing * 0.85 + iscoRing * 0.45 + infallPulse * 0.45)
                * edgeFade * horizonInteriorFade * gridParams.w * occlusion;

    fragColor = vec4(color * alpha, clamp(alpha, 0.0, 1.0));
}
