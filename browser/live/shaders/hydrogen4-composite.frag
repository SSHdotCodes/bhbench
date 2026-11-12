#version 300 es
precision highp float;
precision highp int;
uniform vec4 camPosTime, camForwardFov, camRightAspect, camUpDummy, camBL;
uniform vec4 bhParams, diskParams, gridParams, visualParams, screenParams;
uniform mat4 viewProj;
in vec2 vUV;
uniform sampler2D sceneHDR, bloomTight, bloomWide;
out vec4 fragColor;
vec3 acesFilmic(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main(){
    vec3 hdr = texture(sceneHDR, vUV).rgb;
    vec3 b1  = texture(bloomTight, vUV).rgb;
    vec3 b2  = texture(bloomWide, vUV).rgb;

    float bloomAmt = visualParams.y;
    vec3 combined = hdr + bloomAmt * (0.60 * b1 + 0.45 * b2);

    vec2 centered = vUV - 0.5;
    float vignette = 1.0 - dot(centered, centered) * 0.25;
    combined *= vignette * visualParams.w;

    vec3 mapped = acesFilmic(combined);
    vec3 srgb = pow(mapped, vec3(1.0 / 2.2));
    fragColor=vec4(srgb,1.0);
}
