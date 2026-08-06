#version 410 core
// Post pass: reads the HDR ray-traced texture, adds a cheap bloom from the
// bright (>1) regions (disk, photon ring, stars), tonemaps (ACES) and gammas.
out vec4 FragColor;

uniform sampler2D uTex;
uniform vec2  uTexel;       // 1.0 / texture size
uniform float uExposure;

// Narkowicz 2015 ACES filmic approximation.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    vec3 hdr = texture(uTex, uv).rgb;

    // Bloom: spiral taps, keep only the part above 1.0, blur softly.
    vec3 bloom = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 24; ++i) {
        float a = float(i) * 2.3998277;            // golden-angle spiral
        float rad = 1.5 + float(i) * 0.9;
        vec2 off = vec2(cos(a), sin(a)) * rad * uTexel;
        vec3 s = texture(uTex, uv + off).rgb;
        float w = 1.0 / (1.0 + float(i));
        bloom += max(s - 1.0, 0.0) * w;
        wsum += w;
    }
    bloom /= wsum;

    vec3 col = hdr + bloom * 0.6;
    col = aces(col * uExposure);
    // gentle saturation boost — ACES desaturates highlights; bring color back.
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = clamp(mix(vec3(luma), col, 1.45), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));               // gamma
    FragColor = vec4(col, 1.0);
}
