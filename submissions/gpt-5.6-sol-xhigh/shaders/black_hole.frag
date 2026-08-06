#version 410 core

in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_resolution;
uniform float u_time;
uniform float u_yaw;
uniform float u_pitch;
uniform float u_cameraDistance;
uniform int u_showDisk;
uniform int u_showHalo;
uniform int u_showGrid;
uniform int u_quality;

// Geometrized units: G = c = 1 and Schwarzschild radius r_s = 2M = 1.
const float PI = 3.14159265358979323846;
const float TAU = 6.28318530717958647692;
const float RS = 1.0;
const float MASS = 0.5;
const float HORIZON = 1.0015;
const float PHOTON_SPHERE = 1.5;
const float ISCO = 3.0;
const float DISK_OUTER = 13.5;
const float ESCAPE_RADIUS = 55.0;
const int MAX_STEPS = 620;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float valueNoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),
        mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0)), f.x),
        f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rotation = mat2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < 5; ++i) {
        value += amplitude * valueNoise2(p);
        p = rotation * p * 2.03 + vec2(13.1, 7.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 acesToneMap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 displayEncode(vec3 hdr) {
    return pow(acesToneMap(max(hdr, vec3(0.0))), vec3(1.0 / 2.2));
}

// Three visible-band samples of Planck's law. This preserves black-body color
// changes while the overall flux is independently tone-mapped for a display.
float planckSample(float wavelengthNm, float temperatureK) {
    float exponent = 1.4387769e7 / (wavelengthNm * max(temperatureK, 1.0));
    return pow(550.0 / wavelengthNm, 5.0) / max(exp(min(exponent, 80.0)) - 1.0, 1e-8);
}

vec3 blackBody(float temperatureK) {
    vec3 spectrum = vec3(
        planckSample(650.0, temperatureK),
        planckSample(530.0, temperatureK),
        planckSample(450.0, temperatureK));
    return spectrum / max(max(spectrum.r, spectrum.g), max(spectrum.b, 1e-6));
}

vec3 skyRadiance(vec3 direction) {
    direction = normalize(direction);
    vec2 spherical = vec2(
        atan(direction.y, direction.x) / TAU + 0.5,
        asin(clamp(direction.z, -1.0, 1.0)) / PI + 0.5);

    vec3 galacticNormal = normalize(vec3(0.31, -0.72, 0.62));
    float latitude = abs(dot(direction, galacticNormal));
    float band = exp(-latitude * 10.0);
    float knots = fbm(spherical * vec2(9.0, 5.0) + vec2(2.7, -1.3));
    float dust = smoothstep(0.37, 0.72, fbm(spherical * vec2(23.0, 8.0)));

    vec3 sky = vec3(0.0015, 0.0025, 0.0060);
    sky += band * mix(vec3(0.018, 0.025, 0.055), vec3(0.11, 0.055, 0.018), knots);
    sky *= mix(1.0, 0.28, band * dust);

    vec2 starGrid = vec2(1050.0, 525.0);
    vec2 starCell = floor(spherical * starGrid);
    vec2 starLocal = fract(spherical * starGrid) - 0.5;
    float seed = hash12(starCell);
    vec2 offset = vec2(hash12(starCell + 19.7), hash12(starCell + 71.2)) - 0.5;
    float distanceToStar = length(starLocal - 0.72 * offset);
    float starMask = step(0.9915, seed);
    float star = starMask * exp(-distanceToStar * distanceToStar * 1250.0);
    float magnitude = 0.7 + 16.0 * pow(hash12(starCell + 7.1), 7.0);
    vec3 starColor = mix(
        vec3(1.0, 0.63, 0.36),
        vec3(0.62, 0.76, 1.0),
        hash12(starCell + 3.4));
    sky += star * magnitude * starColor;

    // A very faint celestial coordinate grid makes lensing distortions legible.
    float longitudeLine = 1.0 - smoothstep(0.012, 0.032, abs(sin(spherical.x * PI * 18.0)));
    float latitudeLine = 1.0 - smoothstep(0.010, 0.026, abs(sin(spherical.y * PI * 12.0)));
    sky += 0.008 * (longitudeLine + latitudeLine) * vec3(0.22, 0.35, 0.55);
    return sky;
}

// Thin, zero-torque disk with the Newtonian radial factor used by the
// Novikov-Thorne model away from the horizon. GR redshift is applied below.
vec3 shadeAccretionDisk(vec3 position, float photonLambda) {
    float radius = length(position.xy);
    float angle = atan(position.y, position.x);
    float omega = sqrt(MASS / (radius * radius * radius));

    float radialFlux = pow(ISCO / radius, 3.0)
        * max(1.0 - sqrt(ISCO / radius), 0.0);
    float normalizedFlux = clamp(radialFlux / 0.0572, 0.0, 1.4);
    float temperatureShape = pow(max(normalizedFlux, 0.0), 0.25);

    // The absolute temperature depends on M and accretion rate. This scale is a
    // visible-band supermassive-BH visualization; the radial law is physical.
    float emittedTemperature = 1200.0 + 9500.0 * temperatureShape;

    // g = nu_observer/nu_emitter for a circular Schwarzschild geodesic emitter:
    // g = sqrt(1-3M/r) / (1-Omega*lambda), lambda = L_z/E.
    float gFactor = sqrt(max(1.0 - 3.0 * MASS / radius, 0.001))
        / max(1.0 - omega * photonLambda, 0.12);
    gFactor = clamp(gFactor, 0.18, 2.8);

    float properPhase = angle - omega * u_time * 7.5;
    vec2 flowCoordinates = vec2(log(radius) * 5.8, properPhase * 4.0 - log(radius) * 8.0);
    float turbulence = mix(0.58, 1.42, fbm(flowCoordinates));
    float fineBands = 0.84 + 0.16 * sin(24.0 * log(radius) - 3.0 * properPhase
        + 4.0 * valueNoise2(flowCoordinates * 0.45));
    float edgeTaper = smoothstep(ISCO, ISCO + 0.22, radius)
        * (1.0 - smoothstep(DISK_OUTER - 1.4, DISK_OUTER, radius));

    vec3 color = blackBody(emittedTemperature * gFactor);
    // I_nu/nu^3 is invariant; g^4 is the bolometric intensity transform.
    float observedIntensity = normalizedFlux * pow(gFactor, 4.0)
        * turbulence * fineBands * edgeTaper;
    return color * observedIntensity * 4.2;
}

// Exact Flamm embedding z(r)=2*sqrt(r_s*(r-r_s)), translated vertically so
// the outer rim is at z=0. This inset is a spatial-geometry visualization, not
// an extra force or a claim that spacetime literally sits in Euclidean 3-space.
float flammHeight(float radius) {
    const float outerRadius = 12.0;
    return 2.0 * (sqrt(max(radius - RS, 0.0)) - sqrt(outerRadius - RS));
}

vec3 renderFlammInset(vec2 localUv) {
    vec2 uv = localUv * 2.0 - 1.0;
    uv.x *= 1.48;

    vec3 rayOrigin = vec3(10.5, -14.0, 9.0);
    vec3 target = vec3(0.0, 0.0, -2.7);
    vec3 forward = normalize(target - rayOrigin);
    vec3 right = normalize(cross(forward, vec3(0.0, 0.0, 1.0)));
    vec3 up = normalize(cross(right, forward));
    vec3 rayDirection = normalize(forward + 0.58 * uv.x * right + 0.58 * uv.y * up);

    vec3 background = mix(vec3(0.002, 0.006, 0.018), vec3(0.012, 0.025, 0.050), localUv.y);
    float previousF = 1e5;
    vec3 previousPosition = rayOrigin;
    bool hit = false;
    vec3 hitPosition = vec3(0.0);

    for (int i = 0; i < 210; ++i) {
        float t = float(i) * 0.14;
        vec3 position = rayOrigin + rayDirection * t;
        float radius = length(position.xy);
        float surface = flammHeight(clamp(radius, RS, 12.0));
        float f = position.z - surface;
        if (radius >= RS && radius <= 12.0 && f <= 0.0 && previousF > 0.0) {
            float fraction = previousF / max(previousF - f, 1e-5);
            hitPosition = mix(previousPosition, position, clamp(fraction, 0.0, 1.0));
            hit = true;
            break;
        }
        previousF = f;
        previousPosition = position;
    }

    if (!hit) {
        return background;
    }

    float radius = length(hitPosition.xy);
    float slope = 1.0 / sqrt(max(radius - RS, 0.006));
    vec3 normal = normalize(vec3(-slope * hitPosition.x / radius,
                                 -slope * hitPosition.y / radius, 1.0));
    vec3 lightDirection = normalize(vec3(-0.4, -0.5, 1.0));
    float diffuse = 0.18 + 0.82 * max(dot(normal, lightDirection), 0.0);

    float lineX = abs(sin(PI * hitPosition.x / 1.25));
    float lineY = abs(sin(PI * hitPosition.y / 1.25));
    float cartesianGrid = 1.0 - smoothstep(0.025, 0.105, min(lineX, lineY));
    float horizonProximity = 1.0 - smoothstep(RS, 5.0, radius);
    vec3 gridColor = mix(vec3(0.12, 0.72, 1.45), vec3(1.9, 0.22, 0.055), horizonProximity);
    vec3 surfaceColor = mix(vec3(0.006, 0.020, 0.045), vec3(0.055, 0.010, 0.006), horizonProximity);
    vec3 result = surfaceColor * diffuse + gridColor * cartesianGrid * (0.55 + diffuse);

    // The r=r_s throat is not part of this spatial slice; darken its limit.
    result *= smoothstep(RS, RS + 0.055, radius);
    return result;
}

void main() {
    // Bottom-right inset: an exact equatorial Schwarzschild spatial embedding.
    vec2 insetOrigin = vec2(0.665, 0.035);
    vec2 insetSize = vec2(0.315, 0.345);
    vec2 insetUv = (v_uv - insetOrigin) / insetSize;
    bool inInset = all(greaterThanEqual(insetUv, vec2(0.0)))
        && all(lessThanEqual(insetUv, vec2(1.0)));
    if (u_showGrid != 0 && inInset) {
        float pixelBorder = min(min(insetUv.x, 1.0 - insetUv.x),
                                min(insetUv.y, 1.0 - insetUv.y));
        vec3 insetColor = renderFlammInset(insetUv);
        insetColor += (1.0 - smoothstep(0.0, 0.011, pixelBorder)) * vec3(0.1, 0.55, 1.2);
        fragColor = vec4(displayEncode(insetColor), 1.0);
        return;
    }

    vec2 screen = (2.0 * gl_FragCoord.xy - u_resolution.xy) / u_resolution.y;

    vec3 rayOrigin = u_cameraDistance * vec3(
        cos(u_pitch) * cos(u_yaw),
        cos(u_pitch) * sin(u_yaw),
        sin(u_pitch));
    vec3 target = vec3(0.0, 0.0, -0.18);
    vec3 forward = normalize(target - rayOrigin);
    vec3 right = normalize(cross(forward, vec3(0.0, 0.0, 1.0)));
    vec3 up = normalize(cross(right, forward));
    float focalScale = 1.72;  // approximately a 60-degree vertical field of view
    vec3 rayDirection = normalize(forward * focalScale + screen.x * right + screen.y * up);

    // Every Schwarzschild geodesic lies in a plane through the origin. The
    // following basis reduces the exact 3D null geodesic to r(lambda), phi(lambda).
    float radius = length(rayOrigin);
    vec3 radialBasis = rayOrigin / radius;
    float localRadialDirection = dot(rayDirection, radialBasis);
    vec3 transverseDirection = rayDirection - localRadialDirection * radialBasis;
    float localTransverseDirection = length(transverseDirection);
    vec3 azimuthBasis;
    if (localTransverseDirection > 1e-6) {
        azimuthBasis = transverseDirection / localTransverseDirection;
    } else {
        azimuthBasis = normalize(cross(radialBasis, vec3(0.0, 0.0, 1.0)));
    }

    // Static-observer orthonormal tetrad -> Schwarzschild conserved quantities.
    float initialLapse = max(1.0 - RS / radius, 1e-5);
    float energy = sqrt(initialLapse);
    float angularMomentum = radius * localTransverseDirection;
    float radialVelocity = sqrt(initialLapse) * localRadialDirection;
    float phi = 0.0;

    // The physical photon runs from emitter to camera, opposite the traced path.
    float photonLambda = -cross(rayOrigin, rayDirection).z / energy;
    vec3 previousPosition = rayOrigin;
    vec3 baseRadiance = vec3(0.0);
    vec3 haloRadiance = vec3(0.0);
    float haloTransmittance = 1.0;
    vec3 escapedDirection = rayDirection;
    bool hasSurface = false;
    bool escaped = false;
    bool captured = false;
    float minimumRadius = radius;

    int stepLimit = (u_quality == 0) ? 235 : ((u_quality == 1) ? 385 : 615);
    float stepScale = (u_quality == 0) ? 1.38 : ((u_quality == 1) ? 1.0 : 0.72);

    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex) {
        if (stepIndex >= stepLimit) {
            break;
        }
        if (radius <= HORIZON) {
            captured = true;
            break;
        }
        if (radius >= ESCAPE_RADIUS && radialVelocity > 0.0 && stepIndex > 4) {
            escaped = true;
            break;
        }

        // Adaptive affine step. Smaller steps resolve near-critical photon orbits.
        float affineStep = clamp(0.024 * radius, 0.012, 0.34) * stepScale;
        float l2 = angularMomentum * angularMomentum;
        // Exact Schwarzschild null radial equation:
        // d2r/dlambda2 = L^2/r^3 * (1 - 3M/r).
        float acceleration0 = l2 / (radius * radius * radius)
            * (1.0 - 3.0 * MASS / radius);
        float nextRadius = radius + radialVelocity * affineStep
            + 0.5 * acceleration0 * affineStep * affineStep;

        if (nextRadius <= HORIZON) {
            captured = true;
            break;
        }

        float acceleration1 = l2 / (nextRadius * nextRadius * nextRadius)
            * (1.0 - 3.0 * MASS / nextRadius);
        float nextRadialVelocity = radialVelocity
            + 0.5 * (acceleration0 + acceleration1) * affineStep;
        float midpointRadius = max(0.5 * (radius + nextRadius), HORIZON);
        float nextPhi = phi + angularMomentum / (midpointRadius * midpointRadius) * affineStep;

        vec3 currentRadialBasis = cos(nextPhi) * radialBasis + sin(nextPhi) * azimuthBasis;
        vec3 currentAzimuthBasis = -sin(nextPhi) * radialBasis + cos(nextPhi) * azimuthBasis;
        vec3 currentPosition = nextRadius * currentRadialBasis;
        minimumRadius = min(minimumRadius, nextRadius);

        // Optically thin photon-shell and disk-corona emission. Near-critical
        // geodesics spend more affine time here and naturally form a bright ring.
        if (u_showHalo != 0) {
            float cylindricalRadius = length(currentPosition.xy);
            float shell = exp(-pow((nextRadius - PHOTON_SPHERE) / 0.20, 2.0));
            float coronaRadial = exp(-pow((cylindricalRadius - 5.0) / 4.8, 2.0));
            float coronaVertical = exp(-abs(currentPosition.z) / (0.30 + 0.075 * cylindricalRadius));
            float outsideHorizon = smoothstep(HORIZON, HORIZON + 0.35, nextRadius);
            float emissivity = outsideHorizon * (0.045 * shell + 0.016 * coronaRadial * coronaVertical);
            float lapse = max(1.0 - RS / nextRadius, 0.0);
            float observedEmission = emissivity * pow(lapse, 1.35);
            float opticalDepth = 1.0 - exp(-observedEmission * affineStep);
            vec3 emissionColor = mix(
                vec3(1.4, 0.20, 0.025), vec3(0.35, 0.65, 1.4),
                smoothstep(2.0, 8.0, nextRadius));
            haloRadiance += haloTransmittance * opticalDepth * emissionColor * 1.8;
            haloTransmittance *= 1.0 - 0.34 * opticalDepth;
        }

        // First crossing of the optically thick, geometrically thin disk.
        if (u_showDisk != 0 && previousPosition.z * currentPosition.z <= 0.0
            && abs(previousPosition.z - currentPosition.z) > 1e-7) {
            float crossing = previousPosition.z
                / (previousPosition.z - currentPosition.z);
            vec3 diskPosition = mix(previousPosition, currentPosition, clamp(crossing, 0.0, 1.0));
            float diskRadius = length(diskPosition.xy);
            if (diskRadius >= ISCO && diskRadius <= DISK_OUTER) {
                baseRadiance = shadeAccretionDisk(diskPosition, photonLambda);
                hasSurface = true;
                break;
            }
        }

        float lapse = max(1.0 - RS / nextRadius, 1e-5);
        escapedDirection = normalize(
            currentRadialBasis * (nextRadialVelocity / sqrt(lapse))
            + currentAzimuthBasis * (angularMomentum / nextRadius));

        previousPosition = currentPosition;
        radius = nextRadius;
        radialVelocity = nextRadialVelocity;
        phi = nextPhi;
    }

    if (!hasSurface) {
        if (escaped) {
            baseRadiance = skyRadiance(escapedDirection);
        } else if (captured || minimumRadius < PHOTON_SPHERE) {
            // The horizon has no outgoing classical radiance.
            baseRadiance = vec3(0.0);
        } else {
            // A quality-limited near-critical ray is treated as unresolved shadow.
            baseRadiance = vec3(0.0004, 0.0007, 0.0014);
        }
    }

    vec3 radiance = haloRadiance + haloTransmittance * baseRadiance;
    // Subtle vignette retains focus without changing the geodesic solution.
    float vignette = 1.0 - 0.16 * smoothstep(0.45, 1.45, length(screen));
    fragColor = vec4(displayEncode(radiance * vignette), 1.0);
}
