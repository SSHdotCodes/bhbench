#version 330 core

in vec2 v_texCoord;
out vec4 FragColor;

// Uniforms
uniform vec2 u_resolution;
uniform vec3 u_camPos;
uniform vec3 u_camForward;
uniform vec3 u_camRight;
uniform vec3 u_camUp;

uniform float u_rs;             // Schwarzschild radius
uniform float u_time;
uniform int u_showGrid;
uniform int u_showDisk;
uniform int u_dopplerEffect;
uniform int u_showStars;
uniform int u_trapdoorMode;
uniform float u_diskBrightness;
uniform int u_maxSteps;

// Constants
const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

// Pseudo-random noise functions
float hash(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 3D Simplex-like Value Noise
float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);

    return mix(mix(mix(hash(p + vec3(0,0,0)), hash(p + vec3(1,0,0)), f.x),
                   mix(hash(p + vec3(0,1,0)), hash(p + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(p + vec3(0,0,1)), hash(p + vec3(1,0,1)), f.x),
                   mix(hash(p + vec3(0,1,1)), hash(p + vec3(1,1,1)), f.x), f.y), f.z);
}

// Fractional Brownian Motion (fBm)
float fbm(vec3 p) {
    float val = 0.0;
    float amp = 0.5;
    vec3 q = p;
    for (int i = 0; i < 4; i++) {
        val += amp * noise(q);
        q = q * 2.02 + vec3(0.12, 0.34, 0.56);
        amp *= 0.5;
    }
    return val;
}

// Planck Blackbody color approximation based on relativistic temperature shift factor g
vec3 blackbodyColor(float tempK) {
    // Temperature in Kelvin range roughly 1000K to 30000K
    float t = tempK / 1000.0;
    vec3 col;

    if (t < 6.0) {
        col.r = 1.0;
        col.g = clamp(0.15 * t - 0.1, 0.0, 1.0);
        col.b = clamp(0.005 * t * t - 0.05, 0.0, 1.0);
    } else if (t < 12.0) {
        float factor = (t - 6.0) / 6.0;
        col.r = mix(1.0, 0.7, factor);
        col.g = mix(0.8, 0.85, factor);
        col.b = mix(0.2, 1.0, factor);
    } else {
        float factor = clamp((t - 12.0) / 20.0, 0.0, 1.0);
        col.r = mix(0.7, 0.4, factor);
        col.g = mix(0.85, 0.6, factor);
        col.b = 1.0;
    }
    return col;
}

// Background starfield and galaxy backdrop
vec3 getStarfield(vec3 dir) {
    if (u_showStars == 0) return vec3(0.0);

    // Galactic coordinate projection
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    float phi = atan(dir.z, dir.x);

    // Star distribution
    vec2 starUv = vec2(phi / TWO_PI + 0.5, theta / PI);
    float n = hash2(floor(starUv * 800.0));

    vec3 color = vec3(0.0);

    if (n > 0.985) {
        float brightness = pow((n - 0.985) / 0.015, 6.0) * 2.5;
        vec3 starCol = mix(vec3(0.8, 0.9, 1.0), vec3(1.0, 0.7, 0.5), hash2(floor(starUv * 800.0) + 17.1));
        color += starCol * brightness;
    }

    // Milky Way galactic dust band
    float galacticBand = exp(-pow(dir.y * 3.5, 2.0));
    if (galacticBand > 0.01) {
        vec3 gPos = dir * 4.0;
        float dust1 = fbm(gPos + vec3(1.2, 3.4, 5.6));
        float dust2 = fbm(gPos * 2.0 + vec3(7.8, 9.1, 2.3));

        vec3 dustCol = mix(vec3(0.15, 0.08, 0.25), vec3(0.4, 0.25, 0.15), dust1);
        color += dustCol * galacticBand * (0.3 + 0.7 * dust2);
    }

    return color;
}

// Geodesic acceleration in Schwarzschild curved spacetime
// d2x/dtau2 = -1.5 * r_s * (x x v) x v / r^5
vec3 geodesicAccel(vec3 pos, vec3 vel) {
    float r = length(pos);
    if (r < 0.0001) return vec3(0.0);

    vec3 L = cross(pos, vel); // Orbital angular momentum vector
    vec3 accel = -1.5 * u_rs * cross(L, vel) / (pow(r, 5.0));
    return accel;
}

// RK4 Step Integrator for General Relativity Light Geodesic
void rk4Step(inout vec3 pos, inout vec3 vel, float dt) {
    vec3 k1_v = geodesicAccel(pos, vel);
    vec3 k1_p = vel;

    vec3 p2 = pos + 0.5 * dt * k1_p;
    vec3 v2 = vel + 0.5 * dt * k1_v;
    vec3 k2_v = geodesicAccel(p2, v2);
    vec3 k2_p = v2;

    vec3 p3 = pos + 0.5 * dt * k2_p;
    vec3 v3 = vel + 0.5 * dt * k2_v;
    vec3 k3_v = geodesicAccel(p3, v3);
    vec3 k3_p = v3;

    vec3 p4 = pos + dt * k3_p;
    vec3 v4 = vel + dt * k3_v;
    vec3 k4_v = geodesicAccel(p4, v4);
    vec3 k4_p = v4;

    pos += (dt / 6.0) * (k1_p + 2.0 * k2_p + 2.0 * k3_p + k4_p);
    vel += (dt / 6.0) * (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v);

    // Keep light speed normalized
    vel = normalize(vel);
}

// Flamm's Paraboloid embedding surface z(r) = 2 * sqrt(r_s * (r - r_s))
// Evaluates distance to 3D trapdoor surface for Spacetime Grid visualization
float flammSurfaceZ(float r) {
    if (r <= u_rs) return -10.0;
    return -2.0 * sqrt(u_rs * (r - u_rs));
}

// Grid lines calculation
vec4 getSpacetimeGrid(vec3 pos, vec3 prevPos) {
    if (u_showGrid == 0) return vec4(0.0);

    float r = length(pos.xz);
    float prevR = length(prevPos.xz);

    // Grid on equatorial plane or on Flamm's paraboloid trapdoor surface
    float planeY = 0.0;
    if (u_trapdoorMode == 1) {
        planeY = flammSurfaceZ(r);
    }

    // Check if step crossed planeY
    if ((prevPos.y - planeY) * (pos.y - planeY) <= 0.0 && r >= u_rs) {
        // Radial and azimuthal grid parameters
        float phi = atan(pos.z, pos.x);

        float radialSpacing = u_rs * 1.0;
        float radialGrid = abs(fract(r / radialSpacing + 0.5) - 0.5) * radialSpacing;

        float numAngularDivisions = 24.0;
        float angularGrid = abs(fract(phi / (TWO_PI / numAngularDivisions) + 0.5) - 0.5) * (TWO_PI / numAngularDivisions) * r;

        float lineWidth = 0.06;
        float gridPattern = min(radialGrid, angularGrid);

        if (gridPattern < lineWidth) {
            float alpha = smoothstep(lineWidth, 0.0, gridPattern);
            // Color shifts towards deep cyan/purple near horizon showing gravitational time dilation
            float shift = clamp((r - u_rs) / (10.0 * u_rs), 0.0, 1.0);
            vec3 gridColor = mix(vec3(0.0, 1.0, 0.8), vec3(0.3, 0.4, 1.0), shift);

            // Enhance grid contrast on trapdoor
            if (u_trapdoorMode == 1) {
                gridColor = mix(vec3(1.0, 0.3, 0.1), gridColor, shift);
            }

            return vec4(gridColor * 1.8, alpha * 0.85);
        }
    }
    return vec4(0.0);
}

// Sample Accretion Disk density & emission at position
vec4 sampleAccretionDisk(vec3 pos, vec3 rayDir) {
    if (u_showDisk == 0) return vec4(0.0);

    // Disk lies in equatorial plane Y = 0 (or slight Gaussian thickness)
    float r = length(pos.xz);
    float rInner = 3.0 * u_rs;  // ISCO (Innermost Stable Circular Orbit) = 3 r_s
    float rOuter = 11.0 * u_rs; // Outer edge

    if (r < rInner || r > rOuter) return vec4(0.0);

    // Thin disk height profile h(r)
    float h = 0.15 * u_rs * (1.0 + (r - rInner) / rOuter);
    float heightAlpha = exp(-pow(pos.y / h, 2.0));
    if (heightAlpha < 0.01) return vec4(0.0);

    // Radial intensity profile
    // Temperature drops as T(r) ~ r^(-3/4)
    float radialProfile = pow(rInner / r, 0.75) * (1.0 - sqrt(rInner / r));
    radialProfile = max(radialProfile, 0.0);

    // Dynamic spiral turbulence structure
    float phi = atan(pos.z, pos.x);
    float orbitalPeriod = sqrt(pow(r / u_rs, 3.0));
    float rotAngle = u_time * (3.0 / orbitalPeriod);

    vec3 noisePos = vec3(r * 0.8, (phi + rotAngle) * 3.0, pos.y * 5.0);
    float turb = fbm(noisePos);
    float density = radialProfile * (0.5 + 0.8 * turb) * heightAlpha;

    if (density <= 0.001) return vec4(0.0);

    // Base temperature (Kelvin)
    float baseTemp = 12000.0 * pow(rInner / r, 0.75);

    // Relativistic Doppler & Gravitational Redshift Calculation
    float gFactor = 1.0;
    if (u_dopplerEffect == 1) {
        // Keplerian orbital velocity v/c = sqrt(r_s / (2*r))
        float v_mag = sqrt(u_rs / (2.0 * r));
        v_mag = min(v_mag, 0.65); // Cap near ISCO for numerical safety

        // Tangential velocity direction in disk plane
        vec3 v_dir = vec3(-sin(phi), 0.0, cos(phi));
        vec3 v_vec = v_mag * v_dir;

        // Lorentz factor gamma = 1 / sqrt(1 - v^2)
        float gamma = 1.0 / sqrt(1.0 - v_mag * v_mag);

        // Doppler shift factor delta = 1 / (gamma * (1 - v . k))
        // rayDir is pointing towards black hole from camera, so photon direction k = -rayDir
        vec3 k = -rayDir;
        float cosTheta = dot(v_vec, k);
        float deltaDoppler = 1.0 / (gamma * (1.0 - cosTheta));

        // Gravitational redshift sqrt(1 - r_s / r)
        float gravRedshift = sqrt(1.0 - u_rs / r);

        // Combined relativistic shift g = delta * gravRedshift
        gFactor = deltaDoppler * gravRedshift;
    }

    // Shift temperature and total radiance (I = g^4 * I_0)
    float shiftTemp = baseTemp * gFactor;
    vec3 col = blackbodyColor(shiftTemp);

    // Relativistic beaming intensity amplification I_obs = g^4 * I_em
    float gIntensity = pow(gFactor, 4.0);
    gIntensity = clamp(gIntensity, 0.05, 8.0);

    col *= density * gIntensity * u_diskBrightness * 2.5;
    float alpha = clamp(density * 0.4 * gIntensity, 0.0, 1.0);

    return vec4(col, alpha);
}

void main() {
    // Screen coordinates normalized to [-1, 1]
    vec2 st = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

    // Ray setup from pinhole camera
    vec3 rayDir = normalize(u_camForward + st.x * u_camRight + st.y * u_camUp);
    vec3 rayPos = u_camPos;
    vec3 rayVel = rayDir; // |v| = c = 1

    vec3 accumulatedColor = vec3(0.0);
    float transmittance = 1.0;

    float eventHorizonRadius = u_rs;
    float stepCount = float(u_maxSteps);

    // Adaptive Integration Loop
    for (int i = 0; i < u_maxSteps; i++) {
        float r = length(rayPos);

        // Captured inside Event Horizon (Trapdoor black hole throat)
        if (r <= eventHorizonRadius * 1.01) {
            // Event horizon is pure shadow
            accumulatedColor += vec3(0.0) * transmittance;
            transmittance = 0.0;
            break;
        }

        // Escape to distant background
        if (r > 60.0 * u_rs) {
            vec3 starCol = getStarfield(rayVel);
            accumulatedColor += starCol * transmittance;
            transmittance = 0.0;
            break;
        }

        // Adaptive step size based on distance and curvature gradient
        float dt = max(0.02 * r, 0.005 * u_rs);
        dt = min(dt, 0.4);

        vec3 prevPos = rayPos;

        // Advance photon on geodesic using RK4
        rk4Step(rayPos, rayVel, dt);

        // Sample Accretion Disk volumetric halo along geodesic
        vec4 diskSample = sampleAccretionDisk(rayPos, rayVel);
        if (diskSample.a > 0.0) {
            accumulatedColor += diskSample.rgb * diskSample.a * transmittance;
            transmittance *= (1.0 - diskSample.a);
        }

        // Sample Spacetime Curvature Grid plane
        vec4 gridSample = getSpacetimeGrid(rayPos, prevPos);
        if (gridSample.a > 0.0) {
            accumulatedColor += gridSample.rgb * gridSample.a * transmittance;
            transmittance *= (1.0 - gridSample.a * 0.5);
        }

        if (transmittance < 0.01) break;
    }

    // Tone mapping (ACES filmic) & Gamma correction
    vec3 col = accumulatedColor;

    // Filmic ACES Tone Mapping
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);

    // Gamma correction
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));

    FragColor = vec4(col, 1.0);
}
