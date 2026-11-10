#version 300 es
precision highp float;
precision highp int;

// Backward null-geodesic ray tracing in the exterior Schwarzschild geometry.
// Units: G = c = M = 1. The camera is a static observer outside the horizon.

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uResolution;
uniform vec3 uCameraPos;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform float uTanHalfFov;
uniform float uObserverLapse;
uniform float uDiskOpacity;
uniform int uShowDisk;
uniform int uShowStars;
uniform int uMaxSteps;

const float PI = 3.14159265358979323846;
const float R_INNER = 6.0;  // Schwarzschild ISCO
const float R_OUTER = 16.0;
const float DISK_FLUX_MAX = 46656.0 / 823543.0;
const vec3 DISK_NORMAL = vec3(0.0, 1.0, 0.0);

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 starLayer(vec3 d, float density, float threshold) {
    // A cube-sphere avoids the pole singularities and stretched stars of an
    // equirectangular procedural map. Star positions remain fixed at infinity.
    vec3 absoluteDirection = abs(d);
    vec2 uv;
    float face;
    if (absoluteDirection.x >= absoluteDirection.y &&
        absoluteDirection.x >= absoluteDirection.z) {
        uv = vec2(d.z, d.y) / absoluteDirection.x * 0.5 + 0.5;
        face = d.x > 0.0 ? 0.0 : 1.0;
    } else if (absoluteDirection.y >= absoluteDirection.z) {
        uv = vec2(d.x, d.z) / absoluteDirection.y * 0.5 + 0.5;
        face = d.y > 0.0 ? 2.0 : 3.0;
    } else {
        uv = vec2(d.x, d.y) / absoluteDirection.z * 0.5 + 0.5;
        face = d.z > 0.0 ? 4.0 : 5.0;
    }
    vec2 grid = uv * density;
    vec2 cell = floor(grid) + face * 113.17;
    vec2 f = fract(grid);
    float seed = hash12(cell);
    vec2 starPos = 0.15 + 0.7 * hash22(cell + 17.3);
    float dist = length(f - starPos);
    float aa = max(fwidth(grid.x), fwidth(grid.y)) * 0.7;
    float core = 1.0 - smoothstep(0.025, 0.09 + aa, dist);
    float exists = step(threshold, seed);
    float magnitude = pow(hash12(cell + 91.7), 12.0);
    vec3 warmCool = mix(vec3(1.0, 0.72, 0.48), vec3(0.65, 0.80, 1.0),
                        hash12(cell + 37.1));
    return warmCool * core * exists * (0.35 + 8.0 * magnitude);
}

vec3 sky(vec3 d) {
    // Procedural background, positioned at infinity. All apparent distortion
    // comes from the geodesic; the sky itself is never screen-space warped.
    vec3 galacticNormal = normalize(vec3(0.28, 0.84, 0.46));
    float latitude = abs(dot(d, galacticNormal));
    float band = exp(-latitude * latitude * 52.0);
    float clouds = 0.55 + 0.20 * sin(17.0 * d.x + 9.0 * d.z)
                        + 0.15 * sin(31.0 * d.y - 13.0 * d.x);
    vec3 color = vec3(0.0012, 0.0018, 0.0040);
    color += band * clouds * vec3(0.028, 0.021, 0.034);
    color += band * band * vec3(0.015, 0.010, 0.008);
    if (uShowStars != 0) {
        color += starLayer(d, 190.0, 0.982);
        color += starLayer(d, 320.0, 0.993) * 0.65;
    }
    return color;
}

vec2 acceleration(vec2 s) {
    // s = (u, du/dphi); u'' = 3 M u^2 - u.
    return vec2(s.y, s.x * (3.0 * s.x - 1.0));
}

vec2 rk4(vec2 s, float h) {
    vec2 k1 = acceleration(s);
    vec2 k2 = acceleration(s + 0.5 * h * k1);
    vec2 k3 = acceleration(s + 0.5 * h * k2);
    vec2 k4 = acceleration(s + h * k3);
    return s + (h / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

float planck(float temperature, float wavelengthMicrons) {
    // Spectral radiance up to a common constant; h c / k = 14387.76877 um K.
    float x = 14387.76877 / max(wavelengthMicrons * temperature, 1.0);
    return 1.0 / (pow(wavelengthMicrons, 5.0) * (exp(x) - 1.0));
}

vec3 planckColor(float temperature) {
    // Compact three-primary visualization of the shifted Planck spectrum.
    // White-balanced at 6500 K; radiometric brightness is applied separately.
    temperature = clamp(temperature, 1200.0, 24000.0);
    vec3 wavelengths = vec3(0.610, 0.550, 0.465);
    vec3 sampleColor = vec3(planck(temperature, wavelengths.r),
                            planck(temperature, wavelengths.g),
                            planck(temperature, wavelengths.b));
    vec3 white = vec3(planck(6500.0, wavelengths.r),
                      planck(6500.0, wavelengths.g),
                      planck(6500.0, wavelengths.b));
    vec3 color = sampleColor / white;
    return color / max(max(color.r, color.g), max(color.b, 0.001));
}

vec3 diskEmission(float radius, vec2 state,
                  vec3 radial, vec3 orbitalTangent) {
    float f = 1.0 - 2.0 / radius;

    // Convert the backward ray to a unit direction in the emitter's local
    // static orthonormal frame. The physical photon travels toward the camera,
    // opposite to the backward integration direction.
    vec3 nBackward = normalize((-state.y / (state.x * sqrt(f))) * radial
                               + orbitalTangent);
    vec3 nToObserver = -nBackward;

    // Circular Schwarzschild geodesic: Omega = r^(-3/2), local v = 1/sqrt(r-2).
    vec3 velocityDirection = normalize(cross(DISK_NORMAL, radial));
    float speed = 1.0 / sqrt(radius - 2.0);
    float gamma = sqrt((radius - 2.0) / (radius - 3.0));
    float lorentzDenominator = gamma * (1.0 - speed *
                                        dot(velocityDirection, nToObserver));

    // g = nu_observed / nu_emitted. uObserverLapse is 1-2M/r_obs.
    float g = sqrt(f / uObserverLapse) / lorentzDenominator;
    g = clamp(g, 0.03, 3.0);

    float flux = pow(R_INNER / radius, 3.0) * (1.0 - sqrt(R_INNER / radius));
    float normalizedFlux = max(flux / DISK_FLUX_MAX, 0.0);
    float emittedTemperature = 7000.0 * pow(normalizedFlux, 0.25);
    float observedTemperature = g * emittedTemperature;

    // I_nu/nu^3 is invariant; integrated blackbody intensity transforms as g^4.
    float observedFlux = normalizedFlux * pow(g, 4.0);
    float edge = smoothstep(R_INNER, R_INNER + 0.06, radius)
               * (1.0 - smoothstep(R_OUTER - 2.0, R_OUTER, radius));
    return planckColor(observedTemperature) * observedFlux * edge * 0.25;
}

void main() {
    vec2 ndc = (2.0 * gl_FragCoord.xy - uResolution.xy) / uResolution.y;
    vec3 viewRay = normalize(uCameraForward + uTanHalfFov *
                   (ndc.x * uCameraRight + ndc.y * uCameraUp));

    vec3 er0 = normalize(uCameraPos);
    float observerRadius = length(uCameraPos);
    float nRadial = dot(viewRay, er0);
    vec3 transverse = viewRay - nRadial * er0;
    float nTangential = length(transverse);

    // A perfectly radial ray has no orbital-plane azimuth; handle its limiting
    // behavior directly to avoid a 0/0 impact parameter.
    if (nTangential < 1e-7) {
        if (nRadial < 0.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        } else {
            fragColor = vec4(sky(viewRay), 1.0);
        }
        return;
    }

    vec3 ephi = transverse / nTangential;
    float impact = observerRadius * nTangential / sqrt(uObserverLapse);
    vec2 state = vec2(1.0 / observerRadius,
                      -nRadial / impact);

    // The disk crossings satisfy A cos(phi) + B sin(phi) = 0.
    float A = dot(er0, DISK_NORMAL);
    float B = dot(ephi, DISK_NORMAL);
    float previousQ = A;
    float phi = 0.0;
    float escapeRadius = max(120.0, observerRadius * 1.5);

    vec3 radiance = vec3(0.0);
    float transmission = 1.0;
    bool escaped = false;

    for (int step = 0; step < 512; ++step) {
        if (step >= uMaxSteps) break;
        float radius = 1.0 / max(state.x, 1e-8);

        if (state.x >= 0.5 || radius <= 2.00001) {
            // Captured: no celestial-sphere contribution.
            break;
        }
        if (state.x <= 0.0 && state.y < 0.0) {
            escaped = true;
            break;
        }
        if (phi > 0.05 && state.y < 0.0 && radius > escapeRadius) {
            escaped = true;
            break;
        }

        // Smaller steps in the strong-field region, larger steps at infinity.
        float h = clamp(0.105 * sqrt(max(radius - 2.0, 0.015) / 4.0),
                        0.010, 0.105);
        h = min(h, (8.0 * PI - phi) + 0.001);
        if (h <= 0.0) break;

        vec2 nextState = rk4(state, h);
        float nextPhi = phi + h;
        float nextQ = A * cos(nextPhi) + B * sin(nextPhi);

        if (uShowDisk != 0 && previousQ * nextQ < 0.0 && transmission > 0.015) {
            float fraction = previousQ / (previousQ - nextQ);
            float hitPhi = mix(phi, nextPhi, fraction);
            vec2 hitState = mix(state, nextState, fraction);
            float hitRadius = 1.0 / hitState.x;
            if (hitRadius >= R_INNER && hitRadius <= R_OUTER) {
                vec3 hitRadial = cos(hitPhi) * er0 + sin(hitPhi) * ephi;
                vec3 emission = diskEmission(hitRadius, hitState,
                                             hitRadial, ephi);
                float opacity = clamp(uDiskOpacity, 0.0, 0.995);
                radiance += transmission * opacity * emission;
                transmission *= 1.0 - opacity;
            }
        }

        state = nextState;
        phi = nextPhi;
        previousQ = nextQ;

        if (isnan(state.x) || isinf(state.x) || state.x > 0.5) {
            break;
        }
        if (transmission <= 0.015) break;
    }

    if (escaped && transmission > 0.015) {
        float radius = 1.0 / max(state.x, 1e-8);
        float f = 1.0 - 2.0 / radius;
        vec3 radial = cos(phi) * er0 + sin(phi) * ephi;
        vec3 skyDirection = normalize((-state.y / (state.x * sqrt(f))) * radial
                                      + ephi);
        radiance += transmission * sky(skyDirection);
    }

    // Rays still winding near the photon sphere at the integration limit are
    // left with any disk radiance already accumulated, without an artificial
    // photon-ring glow or background fill.
    fragColor = vec4(radiance, 1.0);
}
