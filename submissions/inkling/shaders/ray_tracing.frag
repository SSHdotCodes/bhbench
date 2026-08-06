#version 330 core
in vec2 vTexCoord;
uniform float uTime;
uniform vec3 uCameraPos;
uniform mat4 uViewInverse;
uniform float uSchwarzschildRadius;
uniform float uEventHorizonRadius;
uniform samplerCube uEnvironment;
uniform sampler2D uStarField;
out vec4 FragColor;

// Schwarzschild radius Rs = 2GM/c²
// Deflection angle (Einstein): alpha = 4GM/(c² * b) = 2 * Rs / b
float deflectionAngle(float impactParameter) {
    return 2.0 * uSchwarzschildRadius / max(impactParameter, 0.01);
}

vec3 computeLensedDirection(vec3 camDir, vec3 blackHoleCenter) {
    // Project black hole onto plane perpendicular to view direction
    float b = length(cross(camDir, blackHoleCenter - uCameraPos));
    float alpha = deflectionAngle(b);
    
    // Apply deflection: rotate direction around axis perpendicular to BH-ray plane
    if (b > 0.001) {
        vec3 axis = normalize(cross(camDir, normalize(blackHoleCenter - uCameraPos)));
        camDir = normalize(camDir * cos(alpha) + cross(axis, camDir) * sin(alpha));
    }
    return camDir;
}

void main() {
    // Get initial ray direction from camera
    vec3 rayDir = normalize(vec3(vTexCoord.x - 0.5, vTexCoord.y - 0.5, 0.5));
    vec3 blackHoleCenter = vec3(0.0, 0.0, 0.0);
    
    // Apply gravitational lensing
    float distToBH = length(blackHoleCenter - uCameraPos);
    float b = length(cross(rayDir, blackHoleCenter - uCameraPos));
    
    // Only apply lensing if ray passes near black hole
    if (b < 5.0 * uSchwarzschildRadius && b > 0.0) {
        float deflection = 2.0 * uSchwarzschildRadius / b;
        // Approximate geodesic: stronger bending for closer approach
        float factor = clamp(deflection * 0.3, 0.0, 2.0);
        
        // Vector from BH to camera projected onto perpendicular
        vec3 toBH = normalize(blackHoleCenter - uCameraPos);
        vec3 perp = normalize(cross(rayDir, toBH));
        
        rayDir = normalize(rayDir * (1.0 - factor) + perp * factor);
    }
    
    // Sample starfield with lensing
    vec4 starColor = texture(uStarField, vTexCoord * 2.0 + rayDir.xy * 0.1);
    
    // Check if ray hits event horizon
    float t = 0.0;
    vec3 rayOrigin = uCameraPos;
    bool hitHorizon = false;
    
    // Simple ray-sphere intersection for black hole
    float a = dot(rayDir, rayDir);
    float bVec = 2.0 * dot(rayDir, rayOrigin - blackHoleCenter);
    float c = dot(rayOrigin - blackHoleCenter, rayOrigin - blackHoleCenter) - uEventHorizonRadius * uEventHorizonRadius;
    float disc = bVec * bVec - 4.0 * a * c;
    
    if (disc >= 0.0) {
        float sqrtDisc = sqrt(disc);
        float t0 = (-bVec - sqrtDisc) / (2.0 * a);
        if (t0 > 0.0) {
            // Hit black hole: absolute black with slight gravitational glow
            vec3 hitPoint = rayOrigin + rayDir * t0;
            float distAfterHit = length(hitPoint - blackHoleCenter);
            float glow = exp(-pow(distAfterHit / uSchwarzschildRadius, 2.0));
            FragColor = vec4(vec3(0.0, 0.0, 0.0) + vec3(0.1, 0.05, 0.2) * glow * 0.5, 1.0);
            return;
        }
    }
    
    // Accretion disk contribution: compute if ray intersects disk plane y=0 near BH
    float planeDist = -rayOrigin.y / rayDir.y;
    if (abs(rayDir.y) > 0.001 && planeDist > 0.0) {
        vec3 planePoint = rayOrigin + rayDir * planeDist;
        float diskRadius = length(vec3(planePoint.x, 0.0, planePoint.z) - blackHoleCenter);
        
        if (diskRadius < 4.0 * uSchwarzschildRadius && diskRadius > 2.5 * uSchwarzschildRadius) {
            // Temperature-based emission
            float T = 8.0 * pow(max(diskRadius / (3.0 * uSchwarzschildRadius), 0.1), -0.75);
            float redshift = 1.0 - (2.0 * uSchwarzschildRadius) / max(diskRadius, 2.001 * uSchwarzschildRadius);
            float intensity = max(T * 0.2 * redshift, 0.1);
            
            // Inner hot (white-yellow), middle (orange-red)
            vec3 diskColor = vec3(1.0, 0.8, 0.3) * intensity;
            diskColor += vec3(0.5, 0.2, 0.0) * max(0.0, 1.0 - (diskRadius - 2.5 * uSchwarzschildRadius) / 1.5);
            
            // Halo/glow near inner radius
            float halo = exp(-pow((diskRadius - 2.7 * uSchwarzschildRadius) / 0.3, 2.0));
            diskColor += vec3(1.0, 0.9, 0.6) * halo * 1.5;
            
            FragColor = vec4(mix(starColor.rgb, diskColor, 0.95), 1.0);
            return;
        }
    }
    
    FragColor = starColor;
}
