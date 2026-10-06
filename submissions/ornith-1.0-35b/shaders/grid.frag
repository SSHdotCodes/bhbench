#version 330 core
in vec2 texCoord;
in vec3 worldPos;

uniform float time;
uniform vec3 cameraPos;

out vec4 FragColor;

vec3 palette(float t) {
    // Deep blue to cyan to white color palette for spacetime grid
    vec3 a = vec3(0.02, 0.02, 0.15);
    vec3 b = vec3(0.05, 0.15, 0.60);
    vec3 c = vec3(0.50, 0.85, 1.00);
    vec3 d = vec3(0.90, 0.95, 1.00);
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    float r = length(worldPos.xz);
    float SchwarzschildRadius = 2.0;

    // Compute spatial curvature indicator for coloring
    float curvScale = SchwarzschildRadius / max(r, 0.5);
    float gridDistort = pow(curvScale * 0.3, 1.5);

    // Grid line intensity - stronger near the black hole
    vec2 gridUV = texCoord * 40.0;
    vec2 gridFrac = fract(gridUV) - 0.5;
    float lineX = smoothstep(0.03, 0.0, abs(gridFrac.x));
    float lineZ = smoothstep(0.03, 0.0, abs(gridFrac.y));
    float lineWidth = max(lineX, lineZ);

    // Distance-based fade for grid lines (dimmer near center)
    float distFade = 1.0 - smoothstep(SchwarzschildRadius * 2.0, SchwarzschildRadius * 8.0, r);

    // Ambient grid color from palette based on curvature strength
    vec3 baseColor = palette(gridDistort + time * 0.05) * 0.15;

    // Grid line glow - bright cyan/white near event horizon
    float lineGlow = lineWidth * distFade * (0.4 + gridDistort * 2.5);

    // Near-horizon intense blue-white emission
    vec3 horizonColor = vec3(0.6, 0.8, 1.0) * smoothstep(0.0, SchwarzschildRadius * 1.5, r) * gridDistort;

    // Combine: base grid + lines + horizon glow
    vec3 color = baseColor + horizonColor;
    color += vec3(0.2, 0.6, 1.0) * lineGlow;
    color += vec3(0.8, 0.9, 1.0) * lineWidth * distFade * 0.3;

    // Below event horizon: very dark
    if (r < SchwarzschildRadius) {
        color *= 0.05;
    }

    FragColor = vec4(color, 1.0);
}
