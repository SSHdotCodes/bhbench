#version 330 core
out vec4 FragColor;

in vec3 ParticleColor;

void main() {
    // gl_PointCoord is the texture coordinate within the point [0.0, 1.0]^2
    float dist = length(gl_PointCoord - vec2(0.5));
    if (dist > 0.5) {
        discard;
    }
    
    // Smooth radial falloff to create a glowing particle appearance
    float alpha = smoothstep(0.5, 0.0, dist);
    float glow = pow(alpha, 2.0); // sharp core, soft outer envelope
    
    // We multiply color to make it look emissive (HDR style)
    FragColor = vec4(ParticleColor * 1.8, glow);
}
