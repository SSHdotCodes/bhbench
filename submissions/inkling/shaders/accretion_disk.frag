#version 330 core
in vec3 vPosition;
in vec3 vNormal;
uniform float uTime;
uniform vec3 uBlackHolePos;
uniform float uSchwarzschildRadius;
uniform float uCameraPos;
out vec4 FragColor;

// Highly accurate blackbody-like emission for accretion disk
vec3 accretionColor(float r, float temperature) {
    // Temperature profile: T ~ r^(-3/4) for thin disk
    float T = temperature * pow(max(r / (3.0 * uSchwarzschildRadius), 0.1), -0.75);
    // Wien approximation for thermal emission: peak shifts with T
    // Simplified Planck-like emission for visualization
    float red = exp(-1.0 / (T * 0.3 + 0.05));
    float green = exp(-1.0 / (T * 0.2 + 0.1));
    float blue = exp(-2.0 / (T * 0.15 + 0.01));
    return vec3(red, green, blue) * T * 2.0;
}

void main() {
    float r = length(vec3(vPosition.x, 0.0, vPosition.z) - vec3(uBlackHolePos.x, 0.0, uBlackHolePos.z));
    float verticalDist = abs(vPosition.y);
    
    // Thin disk: only render near plane y=0 within radius range
    if (r < 1.2 * uSchwarzschildRadius || verticalDist > 0.2) discard;
    
    // Inner hot region (white-blue), middle (yellow-orange), outer (red-purple)
    float T = 8.0; // Base temperature scaling
    float normalizedR = clamp((r - 2.5 * uSchwarzschildRadius) / (2.0 * uSchwarzschildRadius), 0.0, 1.0);
    
    vec3 color = accretionColor(r, T);
    
    // Gravitational redshift near event horizon: light loses energy
    float redshift = 1.0 - (2.0 * uSchwarzschildRadius) / max(r, 2.001 * uSchwarzschildRadius);
    color *= max(redshift, 0.1);
    
    // Doppler beaming approximation based on viewing angle
    vec3 viewDir = normalize(uCameraPos - vPosition);
    float doppler = 1.0 + 0.4 * clamp(dot(normalize(vec3(1.0, 0.0, 0.0)), viewDir), -1.0, 1.0);
    color *= doppler;
    
    // Halo/glow effect for the inner region
    float halo = exp(-pow(r / (3.0 * uSchwarzschildRadius), 2.0)) * 2.0;
    color += vec3(1.0, 0.9, 0.8) * halo * 0.8;
    
    FragColor = vec4(color, 1.0);
}
