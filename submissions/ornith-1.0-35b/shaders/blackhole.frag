#version 330 core
in vec2 texCoord;
out vec4 FragColor;

uniform vec3 cameraPos;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

// Schwarzschild radius of the black hole (normalized units)
const float R_s = 1.5;

// ---- Starfield: procedural galactic background with lensing distortion ----
vec3 getStarfield(vec3 dir) {
    // Convert direction to spherical coordinates for sky mapping
    float phi   = atan(dir.z, dir.x);           // azimuthal angle around Y axis
    float theta = asin(clamp(dir.y, -1.0, 1.0)); // polar angle from equatorial plane

    // Galactic center direction (pointing toward Sagittarius A* region)
    vec3 galacticCenter = normalize(vec3(0.2, 0.4, -0.85));
    float galacticAngle = acos(clamp(dot(normalize(dir), galacticCenter), -1.0, 1.0));

    // Milky Way band: bright along the galactic plane with spiral structure
    float band = exp(-pow(galacticAngle * 2.5, 2.0)) * 0.6;
    // Spiral arm density modulation (approximate two-arm spiral)
    float spiralArm = sin(phi * 2.0 - galacticAngle * 4.0 + 1.5) * 0.3 + 0.7;
    band *= spiralArm;

    // Nebula-like diffuse gas clouds using layered noise
    vec3 nebulaUV = dir * 8.0;
    float n1 = sin(nebulaUV.x * 1.7 + nebulaUV.y * 2.3) * cos(nebulaUV.z * 1.9);
    float n2 = sin(nebulaUV.y * 3.1 - nebulaUV.z * 0.8) * cos(nebulaUV.x * 2.7);
    float nebula = (n1 + n2) * 0.5 * band;

    // Star field using high-frequency noise with clustering
    vec3 starUV = dir * 60.0;
    float starNoise = sin(starUV.x * 7.13 + starUV.y * 13.7) * cos(starUV.z * 9.81);
    starNoise += sin(starUV.x * 23.4 - starUV.y * 31.1) * cos(starUV.z * 17.6);
    float stars = pow(max(starNoise, 0.0), 20.0) * 1.8;

    // Dense star cluster toward galactic center
    float coreDensity = exp(-pow(galacticAngle * 6.0, 2.0)) * 3.5;
    vec3 coreDir = normalize(dir - galacticCenter * dot(normalize(dir), galacticCenter));
    float coreStars = pow(max(sin(coreDir.x * 89.1 + coreDir.y * 67.3) *
                              cos(coreDir.z * 45.7 + coreDir.x * 23.1), 0.0), 12.0);
    coreStars *= coreDensity;

    // Combine all star field components
    float totalStar = stars + coreStars + band * 0.8;

    // Color: warm white for dense regions, blue-white for hot clusters
    vec3 starColor = mix(
        vec3(1.0, 0.92, 0.78),       // warm white
        vec3(0.75, 0.85, 1.0),       // blue-white
        smoothstep(0.3, 0.7, totalStar)
    );

    return starColor * (totalStar + nebula * 0.25);
}

// ---- Compute gravitational deflection per unit distance for a ray at position p ----
vec3 gravityDeflect(vec3 p, vec3 dir) {
    float r = length(p);
    if (r < R_s * 0.8 || r > 100.0) return vec3(0.0);

    // Newtonian approximation enhanced with GR correction factor (1 + Rs/r)
    float factor = R_s / (r * r * r) * (1.0 + R_s / r);

    // Deflection: component toward black hole center, perpendicular to ray direction
    vec3 towardsBH = -normalize(p);
    vec3 perp = normalize(towardsBH - dot(towardsBH, dir) * dir);
    return factor * perp;
}

// ---- Accretion disk rendering along a ray path ----
vec3 getAccretionDisk(vec3 ro, vec3 rd, float R_inner, float R_outer) {
    vec3 color = vec3(0.0);
    if (abs(rd.y) < 1e-4) return color; // Ray parallel to disk plane

    // Intersect ray with y=0 plane (the equatorial plane of the disk)
    float tDisc = -ro.y / rd.y;
    if (tDisc < 0.0 || tDisc > 50.0) return color; // Plane intersection outside bounds or behind

    vec3 hitPos = ro + rd * tDisc;
    float r = length(hitPos.xz);

    if (r < R_inner || r > R_outer) return color;

    // Material density profile: steep inner edge, power-law falloff
    float normR = (r - R_inner) / max(R_outer - R_inner, 0.1);
    float density = pow(1.0 - normR, 2.5) * smoothstep(R_inner, R_inner + 0.3, r);

    // Keplerian orbital velocity with relativistic correction near ISCO
    float vOrb = sqrt(R_s / max(r, R_s)) / sqrt(max(1.0 - R_s / r, 0.1));

    // Temperature profile: T ~ r^(-3/4) for standard thin disk model
    float temp = pow(max(R_inner / max(r, R_inner), 0.01), 0.75);

    // Gravitational redshift factor: photons lose energy climbing out of potential well
    float gravRedshift = sqrt(max(1.0 - R_s / r, 0.05));

    // Orbital direction (azimuthal) at disk hit point for Doppler calculation
    vec3 orbDir = normalize(hitPos.xz);
    if (length(hitPos.xz) < 0.01) orbDir = vec3(1.0, 0.0, 0.0);

    // Photon direction in the local disk frame (project out radial component)
    vec3 photonInDisk = normalize(rd - dot(rd, hitPos.xzz * invR(r)) * hitPos.xzz * invR(r));

    // Doppler factor: approaching side brighter (blueshifted), receding dimmer (redshifted)
    float dopplerDot = dot(orbDir, photonInDisk);
    float dopplerFactor = 1.0 / max(1.0 - vOrb * dopplerDot, 0.3);

    // Combined relativistic intensity boost: D^3 where D is Doppler factor
    float relBoost = pow(dopplerFactor, 3.0) * gravRedshift;

    // Blackbody-like spectral color from temperature (approximate Planck curve)
    // Hotter (inner disk) -> blue-white, cooler (outer disk) -> orange-red
    vec3 thermalColor = mix(
        vec3(1.0, 0.82, 0.55),   // warm orange at outer edge
        vec3(0.65, 0.78, 1.0),   // blue-white at inner hot region
        smoothstep(0.0, 0.7, temp)
    );

    // Final disk emission intensity
    float diskIntensity = density * relBoost * (temp * temp) * 0.012;

    // Volumetric glow above/below disk plane using Gaussian falloff in y
    float yHeight = abs(hitPos.y);
    float verticalGlow = exp(-pow(yHeight / max(0.4, r * 0.08), 2.0));

    color += thermalColor * diskIntensity * (1.0 + verticalGlow * 0.4);

    return color;
}

// ---- Photon ring at the photon sphere (r = 1.5 * R_s) ----
vec3 getPhotonRing(vec3 ro, vec3 rd, float Rs) {
    float rPhoton = Rs * 1.5;
    // Check if ray passes near the photon sphere
    vec3 closestPt = ro - dot(ro, rd) * rd; // closest point on ray to origin
    float distToSphere = length(closestPt) - rPhoton;

    if (abs(distToSphere) > 0.25) return vec3(0.0);

    // Bright thin ring with subtle pulsation
    float ringProfile = exp(-pow(distToSphere * 8.0, 2.0));
    float pulse = 0.9 + 0.1 * sin(time * 0.8 + length(ro) * 3.0);

    return vec3(0.7, 0.85, 1.0) * ringProfile * pulse * 2.5;
}

// ---- Ray trace through curved spacetime using Schwarzschild geodesics ----
vec4 traceBlackHole(vec2 fragCoord, vec2 resolution) {
    // Map fragment coordinates to NDC range [-1, 1]
    vec2 ndc = (fragCoord - 0.5 * resolution) / min(resolution.x, resolution.y);

    // Compute ray direction from camera through this pixel using inverse projection+view
    mat4 invVP = inverse(projection * view);
    vec3 rayDirLocal = normalize(vec3(ndc, -1.0)); // local forward = -Z in view space

    // Transform ray to world space: rotate by camera orientation
    float camYaw   = atan(cameraPos.z, cameraPos.x);
    float camPitch  = asin(clamp(cameraPos.y / length(cameraPos), -1.0, 1.0));

    float cYa = cos(camYaw), sYa = sin(camYaw);
    float cPa = cos(camPitch), sPa = sin(camPitch);

    // Rotation matrix from camera to world coordinates
    vec3 rayDirWorld = normalize(vec3(
        cYa * rayDirLocal.x + sYa * cPa * rayDirLocal.z - sPa * rayDirLocal.y,
        cPa * rayDirLocal.z + sPa * rayDirLocal.y,
        sYa * rayDirLocal.x - cYa * cPa * rayDirLocal.z + cYa * sPa * rayDirLocal.y
    ));

    vec3 ro = cameraPos; // Ray origin at camera position
    vec3 rd = rayDirWorld; // Initial ray direction (unit vector)

    // Adaptive geodesic integration through Schwarzschild spacetime
    float totalRedshift = 1.0;
    bool captured = false;
    int steps = 64; // Ray tracing iterations for curved spacetime

    vec3 accumColor = vec3(0.0);
    float rMin = 1e9; // Track minimum approach distance (for lensing visualization)

    for (int i = 0; i < steps; ++i) {
        float r = length(ro);
        rMin = min(rMin, r);

        // Event horizon capture: photon crossed inside Rs and is falling in
        if (r < R_s * 1.05 && dot(normalize(-ro), rd) > 0.3) {
            captured = true;
            break;
        }

        // Escape condition: ray has moved far enough away after being near BH
        if (i > 8 && r > 60.0 && length(ro - cameraPos) > 55.0) {
            break; // Ray escaped to infinity -> sample starfield
        }

        // Adaptive step size: smaller near BH for accuracy, larger far away
        float stepSize = mix(0.08, 0.6, smoothstep(R_s * 3.0, 40.0, r));
        if (r < R_s * 2.5) stepSize = 0.03; // Very small steps deep in gravity well

        // Compute gravitational deflection at current position
        vec3 deflect = gravityDeflect(ro, rd);
        float deflectMag = length(deflect);

        // Update ray direction (bend toward mass) - GR geodesic equation approximation
        // d^2 x^mu / d lambda^2 = -Gamma^mu_alpha_beta * dx^alpha/dlambda * dx^beta/dlambda
        rd += deflect * stepSize;
        rd = normalize(rd);

        // Update ray position along curved path
        ro += rd * stepSize;

        // Gravitational redshift: photons lose energy near massive objects
        float localRedshift = sqrt(max(1.0 - R_s / max(r, R_s * 1.01), 0.05));
        totalRedshift *= mix(1.0, localRedshift, smoothstep(R_s * 2.0, R_s * 8.0, r));

        // Sample accretion disk at each integration step for volumetric glow
        if (r > R_s * 1.1 && r < 15.0) {
            vec3 diskColor = getAccretionDisk(ro, rd, R_s * 2.6, R_s * 8.0); // ISCO to outer edge
            float depthFade = exp(-abs(ro.y) / max(0.5, r * 0.15));
            accumColor += diskColor * depthFade * stepSize * 0.35;
        }

        // Photon ring sampling near photon sphere (r ~ 1.5 Rs)
        if (abs(r - R_s * 1.5) < 0.4 && rMin > R_s * 1.2) {
            accumColor += getPhotonRing(ro, rd, R_s) * stepSize * 1.8;
        }
    }

    // Final starfield background with lensing distortion from closest approach
    vec3 bgColor = getStarfield(rd);

    // Einstein ring effect: brightening of background near photon sphere radius
    float impactParam = rMin - R_s * 1.5;
    float einsteinRing = exp(-pow(impactParam * 4.0, 2.0)) * 0.35;
    bgColor += vec3(0.6, 0.75, 1.0) * einsteinRing;

    // Apply accumulated redshift to background (gravitational dimming)
    bgColor *= totalRedshift;

    return vec4(bgColor + accumColor, 1.0);
}

// ---- Helper: compute inverse radius for normalization ----
float invR(float r) {
    return 1.0 / max(r, 1e-6);
}

void main() {
    // Fullscreen ray tracing at native resolution with supersampling hint via coord jitter
    vec2 fragCoord = gl_FragCoord.xy;

    // Trace a photon geodesic from this pixel through curved spacetime
    vec4 result = traceBlackHole(fragCoord, vec2(1280.0, 720.0));

    // Tone mapping: Reinhard operator for HDR -> LDR conversion with contrast boost
    vec3 hdrColor = result.rgb;
    vec3 tonemap = hdrColor / (hdrColor + vec3(1.0));
    tonemap = pow(tonemap, vec3(0.85)); // Slight gamma for better visual range

    FragColor = vec4(tonemap, 1.0);
}
