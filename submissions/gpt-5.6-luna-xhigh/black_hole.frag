#version 410 core

out vec4 FragColor;

uniform vec2  uResolution;
uniform float uTime;
uniform float uCameraDistance;
uniform float uInclination;
uniform float uYaw;
uniform float uMass;
uniform bool  uShowGrid;
uniform bool  uShowDisk;
uniform bool  uShowHalo;
uniform float uExposure;

const float PI = 3.14159265358979323846;
const int MAX_STEPS = 360;
const float FAR_RADIUS = 58.0;

struct TraceResult {
    vec3  color;
    float minRho;
    bool  captured;
    bool  diskHit;
};

float arealRadius(float rho) {
    float q = uMass / max(2.0 * rho, 0.0001);
    return rho * (1.0 + q) * (1.0 + q);
}

// Exact optical index of the Schwarzschild metric in isotropic radius rho.
// ds^2 = -A(rho)^2 dt^2 + B(rho)^2 d rho^2, so n = B/A.
float opticalIndex(float rho) {
    float q = uMass / max(2.0 * rho, 0.0001);
    return pow(1.0 + q, 3.0) / max(1.0 - q, 0.001);
}

float opticalIndexGradient(float rho) {
    float safeRho = max(rho, 0.55 * uMass);
    float q = uMass / (2.0 * safeRho);
    float n = opticalIndex(safeRho);
    float dqdr = -q / safeRho;
    float dLogNdr = dqdr * (3.0 / (1.0 + q) + 1.0 / max(1.0 - q, 0.001));
    return n * dLogNdr;
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 starField(vec3 direction) {
    vec3 cell = floor(direction * 160.0);
    float star = step(0.9977, hash31(cell));
    float starSize = pow(hash31(cell + 7.17), 16.0);
    vec3 starTint = mix(vec3(0.65, 0.78, 1.0), vec3(1.0, 0.72, 0.45), hash31(cell + 2.3));
    return star * starSize * starTint * 2.0;
}

vec3 background(vec3 direction) {
    float horizon = clamp(0.5 + 0.5 * direction.z, 0.0, 1.0);
    vec3 sky = mix(vec3(0.0015, 0.0025, 0.007), vec3(0.010, 0.017, 0.040), horizon);
    float milkyWay = pow(max(0.0, 1.0 - abs(direction.y)), 7.0);
    sky += vec3(0.018, 0.014, 0.010) * milkyWay;
    return sky + starField(direction);
}

// A compact RGB approximation to a thermal spectrum. The radial temperature
// profile below follows the standard thin-disk F(r) ~ r^-3 profile with the
// zero-torque ISCO boundary factor.
vec3 thermalColor(float temperature) {
    float cold = smoothstep(0.05, 0.48, temperature);
    float hot = smoothstep(0.34, 1.0, temperature);
    vec3 red = vec3(1.0, 0.10, 0.015);
    vec3 amber = vec3(1.0, 0.42, 0.035);
    vec3 white = vec3(1.0, 0.92, 0.70);
    vec3 blueWhite = vec3(0.52, 0.76, 1.0);
    vec3 color = mix(red, amber, cold);
    color = mix(color, white, hot * 0.72);
    color = mix(color, blueWhite, max(0.0, hot - 0.7) * 0.8);
    return color;
}

vec3 accretionEmission(vec3 hitPoint, vec3 rayDirection, float rho) {
    float r = arealRadius(rho);
    float rISCO = 6.0 * uMass;
    float outerRadius = 34.0 * uMass;
    float radial = clamp((outerRadius - r) / (outerRadius - rISCO), 0.0, 1.0);
    float temperature = pow(max(0.0, rISCO / max(r, rISCO)), 0.75);
    float flux = pow(max(0.0, rISCO / max(r, rISCO)), 3.0)
               * (1.0 - sqrt(clamp(rISCO / max(r, rISCO), 0.0, 1.0)));

    // Keplerian circular-orbit speed measured by a static Schwarzschild observer.
    float orbitalSpeed = sqrt(clamp(uMass / max(r - 2.0 * uMass, 0.001), 0.0, 0.86));
    vec3 tangent = normalize(vec3(-hitPoint.y, hitPoint.x, 0.0));
    // The traced ray travels observer -> disk, while emitted photons travel
    // disk -> observer. Use the physical emission direction here.
    vec3 emissionDirection = -rayDirection;
    float emissionCosine = clamp(dot(tangent, emissionDirection), -0.98, 0.98);
    float gamma = inversesqrt(max(1.0 - orbitalSpeed * orbitalSpeed, 0.05));
    float doppler = 1.0 / (gamma * (1.0 - orbitalSpeed * emissionCosine));
    float gravitational = sqrt(max(0.025, 1.0 - 2.0 * uMass / max(r, 2.01 * uMass)));
    float redshift = gravitational * doppler;

    // I_nu / nu^3 is invariant along a null geodesic, hence the redshift^3.
    vec3 spectrum = thermalColor(temperature * (0.82 + 0.20 * redshift));
    float beaming = 0.25 + 0.75 * max(0.0, dot(normalize(vec3(0.0, 0.0, 1.0)), emissionDirection));
    float azimuthalVariation = 0.90 + 0.10 * sin(16.0 * atan(hitPoint.y, hitPoint.x) - 0.18 * uTime);
    return spectrum * flux * pow(max(redshift, 0.02), 3.0) * (4.5 + 2.0 * radial)
         * beaming * azimuthalVariation;
}

TraceResult tracePhoton(vec3 origin, vec3 initialDirection) {
    TraceResult result;
    result.color = background(initialDirection);
    result.minRho = length(origin);
    result.captured = false;
    result.diskHit = false;

    vec3 p = origin;
    vec3 v = normalize(initialDirection);

    for (int i = 0; i < MAX_STEPS; ++i) {
        float rho = length(p);
        result.minRho = min(result.minRho, rho);

        // In isotropic coordinates the event horizon is rho = M/2.
        if (rho <= 0.535 * uMass) {
            result.captured = true;
            result.color = vec3(0.0);
            break;
        }

        if (rho >= FAR_RADIUS * uMass && i > 8) {
            break;
        }

        // Smaller steps near the photon sphere and the horizon, larger steps far away.
        float stepSize = clamp(0.045 * rho, 0.055 * uMass, 0.55 * uMass);
        vec3 oldP = p;
        float oldZ = p.z;

        float n = opticalIndex(rho);
        vec3 radial = p / max(rho, 0.0001);
        vec3 gradient = opticalIndexGradient(rho) * radial;
        vec3 transverseGradient = gradient - v * dot(v, gradient);
        v = normalize(v + (transverseGradient / max(n, 0.001)) * stepSize);
        p += v * stepSize;

        if (uShowDisk && oldZ * p.z <= 0.0 && abs(v.z) > 0.012) {
            float denominator = oldZ - p.z;
            float fraction = oldZ / denominator;
            vec3 hit = mix(oldP, p, clamp(fraction, 0.0, 1.0));
            float hitRho = length(hit.xy);
            float hitR = arealRadius(hitRho);
            if (hitR >= 6.0 * uMass && hitR <= 34.0 * uMass) {
                result.color = accretionEmission(hit, v, hitRho);
                result.diskHit = true;
                break;
            }
        }
    }

    if (!result.captured) {
        float photonSphereRho = (1.0 + 0.8660254) * uMass;
        float ringWidth = 0.15 * uMass;
        float ring = exp(-pow((result.minRho - photonSphereRho) / ringWidth, 2.0));
        float nearHole = exp(-result.minRho / (5.0 * uMass));
        if (uShowHalo) {
            result.color += vec3(1.0, 0.34, 0.055) * ring * 1.8;
            result.color += vec3(0.10, 0.16, 0.34) * nearHole * 0.08;
        }
    }

    return result;
}

float warpedGridZ(float rho) {
    float depth = 6.5 * uMass;
    return -depth * exp(-rho / (4.5 * uMass));
}

vec3 addSpacetimeGrid(vec3 eye, vec3 direction, vec3 color, bool occluded) {
    if (!uShowGrid || occluded) {
        return color;
    }

    // Intersect the camera ray with a visual embedding diagram z(rho). This is
    // a coordinate visualization of a spatial slice, not an extra physical force.
    float maxT = 2.25 * uCameraDistance;
    float previousT = 0.0;
    float previousRho = length(eye.xy);
    float previousF = eye.z - warpedGridZ(previousRho);
    bool found = false;
    float hitT = 0.0;

    for (int i = 1; i <= 80; ++i) {
        float t = maxT * float(i) / 80.0;
        vec3 samplePoint = eye + direction * t;
        float sampleRho = length(samplePoint.xy);
        float f = samplePoint.z - warpedGridZ(sampleRho);
        if (previousF * f <= 0.0) {
            float lo = previousT;
            float hi = t;
            for (int j = 0; j < 8; ++j) {
                float mid = 0.5 * (lo + hi);
                vec3 q = eye + direction * mid;
                float qRho = length(q.xy);
                float qF = q.z - warpedGridZ(qRho);
                if (previousF * qF <= 0.0) {
                    hi = mid;
                } else {
                    lo = mid;
                    previousF = qF;
                }
            }
            hitT = 0.5 * (lo + hi);
            found = true;
            break;
        }
        previousT = t;
        previousF = f;
    }

    if (!found) {
        return color;
    }

    vec3 hit = eye + direction * hitT;
    float rho = length(hit.xy);
    if (rho < 0.75 * uMass || rho > 48.0 * uMass) {
        return color;
    }

    float cell = 1.65 * uMass;
    float dx = abs(fract(hit.x / cell + 0.5) - 0.5) * cell;
    float dy = abs(fract(hit.y / cell + 0.5) - 0.5) * cell;
    float line = 1.0 - smoothstep(0.0, 0.105 * uMass, min(dx, dy));
    float rings = 1.0 - smoothstep(0.0, 0.075 * uMass,
                                   abs(fract(log(max(rho, 0.8 * uMass)) / log(1.55)) - 0.5) * 1.55 * uMass);
    float radialFade = smoothstep(48.0 * uMass, 1.0 * uMass, rho);
    float trapdoor = 0.35 + 0.65 * exp(-rho / (8.0 * uMass));
    vec3 gridColor = mix(vec3(0.03, 0.16, 0.42), vec3(0.10, 0.64, 1.0), trapdoor);
    float gridAlpha = (0.18 * line + 0.07 * rings) * radialFade;
    vec3 warpedSurfaceTint = vec3(0.004, 0.008, 0.020) * trapdoor;
    return mix(color + warpedSurfaceTint, color + gridColor * trapdoor, clamp(gridAlpha, 0.0, 0.42));
}

void main() {
    vec2 pixel = gl_FragCoord.xy;
    vec2 ndc = (2.0 * pixel - uResolution) / uResolution.y;

    float sinI = sin(uInclination);
    float cosI = cos(uInclination);
    float sinY = sin(uYaw);
    float cosY = cos(uYaw);
    vec3 eye = uCameraDistance * vec3(cosY * sinI, sinY * sinI, cosI);
    vec3 forward = normalize(-eye);
    vec3 referenceUp = abs(forward.z) > 0.92 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(forward, referenceUp));
    vec3 up = normalize(cross(right, forward));
    vec3 rayDirection = normalize(forward + 0.75 * ndc.x * right + 0.75 * ndc.y * up);

    TraceResult traced = tracePhoton(eye, rayDirection);
    vec3 color = addSpacetimeGrid(eye, rayDirection, traced.color, traced.captured);

    // Filmic compression keeps the disk readable while retaining the photon ring.
    color *= uExposure;
    color = vec3(1.0) - exp(-color);
    color = pow(max(color, 0.0), vec3(0.4545));
    FragColor = vec4(color, 1.0);
}
