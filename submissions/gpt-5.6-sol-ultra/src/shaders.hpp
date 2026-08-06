#pragma once

namespace shaders {

inline constexpr char kFullscreenVertex[] = R"GLSL(
#version 410 core

out vec2 vUV;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

inline constexpr char kBlackHoleFragment[] = R"GLSL(
#version 410 core

layout(location = 0) out vec4 outColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uYaw;
uniform float uPitch;
uniform float uDistance;
uniform float uStepScale;
uniform int uMaxSteps;
uniform int uEmbeddingSteps;
uniform int uViewMode;       // 0 observer, 1 curvature, 2 split
uniform int uShowDisk;
uniform int uShowHalo;
uniform int uShowGrid;
uniform int uLensing;

const float PI = 3.14159265358979323846;
const float HORIZON = 2.0;   // 2 GM/c^2
const float PHOTON_SPHERE = 3.0;
const float ISCO = 6.0;
const float DISK_OUTER = 22.0;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
    float n = hash21(p);
    return vec2(n, hash21(p + n + 19.19));
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)),
                   hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}

float starLayer(vec2 uv, float scale, float threshold) {
    vec2 p = uv * vec2(scale, scale * 0.5);
    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;
    vec2 offset = (hash22(id) - 0.5) * 0.78;
    float seed = hash21(id + 31.7);
    float existence = smoothstep(threshold, 1.0, seed);
    float size = mix(0.035, 0.15, pow(seed, 14.0));
    float core = 1.0 - smoothstep(size * 0.22, size, length(gv - offset));
    return core * existence * mix(0.45, 3.2, pow(seed, 10.0));
}

vec3 skyRadiance(vec3 d, vec3 directlyBehind) {
    d = normalize(d);
    vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5,
                   asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5);

    vec3 bandNormal = normalize(vec3(0.23, 0.91, 0.34));
    float latitude = abs(dot(d, bandNormal));
    float band = exp(-42.0 * latitude * latitude);
    float clouds = 0.42 + 0.58 * valueNoise(uv * vec2(34.0, 17.0));
    clouds *= 0.65 + 0.35 * valueNoise(uv * vec2(93.0, 41.0) + 7.0);

    vec3 sky = vec3(0.0012, 0.0020, 0.0045);
    sky += band * clouds * vec3(0.018, 0.026, 0.052);

    float starsA = starLayer(uv, 520.0, 0.965);
    float starsB = starLayer(uv + vec2(0.173, 0.291), 910.0, 0.985);
    float temperatureSeed = hash21(floor(uv * vec2(520.0, 260.0)) + 8.0);
    vec3 starTint = mix(vec3(1.0, 0.72, 0.48), vec3(0.62, 0.78, 1.0),
                        temperatureSeed);
    sky += starTint * starsA + vec3(0.72, 0.82, 1.0) * starsB;

    // A small procedural source is kept behind the hole to make the
    // Einstein-ring mapping visually legible. It is part of the demo sky.
    float sourceAngle = acos(clamp(dot(d, directlyBehind), -1.0, 1.0));
    float source = exp(-pow(sourceAngle / 0.052, 2.0));
    float sourceCore = exp(-pow(sourceAngle / 0.014, 2.0));
    sky += source * vec3(0.10, 0.22, 0.42) + sourceCore * vec3(1.8, 1.45, 0.9);

    return sky;
}

vec3 thermalPalette(float t) {
    vec3 deepRed = vec3(0.55, 0.025, 0.006);
    vec3 orange = vec3(1.0, 0.22, 0.025);
    vec3 warmWhite = vec3(1.0, 0.78, 0.42);
    vec3 hotWhite = vec3(0.72, 0.86, 1.0);
    vec3 c = mix(deepRed, orange, smoothstep(0.12, 0.52, t));
    c = mix(c, warmWhite, smoothstep(0.45, 0.95, t));
    c = mix(c, hotWhite, smoothstep(0.95, 1.8, t));
    return c;
}

vec3 diskRadiance(float r, vec3 p, float b, vec3 planeNormal,
                  float observerLapse) {
    float flux = pow(r, -3.0) * max(0.0, 1.0 - sqrt(ISCO / r));

    // Exact Schwarzschild circular-orbit frequency shift for a static observer:
    // g = 1 / [sqrt(f_obs) u^t (1 - Omega xi)].
    float omega = pow(r, -1.5);
    float uTimeComponent = inversesqrt(max(1.0e-5, 1.0 - 3.0 / r));
    // The integrated tangent is past-directed. Reversing it to the physical,
    // future-directed photon gives xi = -b N dot axis.
    float xi = -b * dot(planeNormal, vec3(0.0, 1.0, 0.0));
    float denominator = observerLapse * uTimeComponent * (1.0 - omega * xi);
    float g = clamp(1.0 / max(0.06, denominator), 0.12, 4.0);

    // atan(-z,x) increases for right-handed rotation about +Y. The m=17
    // pattern therefore advances at exactly the local circular Omega.
    float azimuth = atan(-p.z, p.x);
    float orbitPattern = 0.82 + 0.18 * sin(17.0 * azimuth
                                         - 5.0 * log(max(r, ISCO))
                                         - uTime * omega * 17.0);
    orbitPattern *= 0.92 + 0.08 * sin(43.0 * azimuth + r * 2.7);

    float scaledFlux = 1900.0 * flux;
    float observedTemperature = pow(max(scaledFlux, 0.0), 0.25) * g;
    float bolometric = 2.7 * scaledFlux * pow(g, 4.0);
    float edge = smoothstep(ISCO, ISCO + 0.15, r)
               * (1.0 - smoothstep(DISK_OUTER - 1.4, DISK_OUTER, r));
    return thermalPalette(observedTemperature) * bolometric * orbitPattern * edge;
}

vec3 haloIncrement(vec3 p, float r, float affineStep, float observerLapse) {
    float latitude = abs(p.y) / max(r, 1.0e-4);
    float equatorial = exp(-0.5 * pow(latitude / 0.105, 2.0));
    float corona = exp(-0.5 * pow(latitude / 0.34, 2.0));
    float radial = exp(-0.20 * max(r - PHOTON_SPHERE, 0.0));
    radial *= smoothstep(HORIZON + 0.03, 2.45, r);
    radial *= 1.0 - smoothstep(16.0, 24.0, r);
    float photonRegion = exp(-pow((r - PHOTON_SPHERE) / 0.72, 2.0));
    float emissivity = radial * (0.0055 * equatorial
                               + 0.0010 * corona
                               + 0.0025 * photonRegion * equatorial);

    // Static-emitter gravitational shift for the simplified optically-thin halo.
    float localLapse = sqrt(max(1.0e-5, 1.0 - HORIZON / r));
    float g = clamp(localLapse / observerLapse, 0.0, 2.0);
    vec3 tint = mix(vec3(1.0, 0.10, 0.012), vec3(1.0, 0.58, 0.12),
                    smoothstep(2.2, 8.0, r));
    // With E=1, a static local emitter measures d ell = d lambda / lapse.
    float properPathLength = affineStep / max(localLapse, 1.0e-3);
    return tint * emissivity * pow(g, 4.0) * properPathLength;
}

vec3 geodesicDerivative(vec3 q, float b, float massSwitch) {
    float r = max(q.x, 1.0e-4);
    return vec3(q.y,
                b * b / (r * r * r) * (1.0 - 3.0 * massSwitch / r),
                b / (r * r));
}

vec3 rk4Geodesic(vec3 q, float h, float b, float massSwitch) {
    vec3 k1 = geodesicDerivative(q, b, massSwitch);
    vec3 k2 = geodesicDerivative(q + 0.5 * h * k1, b, massSwitch);
    vec3 k3 = geodesicDerivative(q + 0.5 * h * k2, b, massSwitch);
    vec3 k4 = geodesicDerivative(q + h * k3, b, massSwitch);
    return q + (h / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

vec3 orbitPosition(vec3 radialBasis, vec3 tangentBasis, vec3 q) {
    return q.x * (radialBasis * cos(q.z) + tangentBasis * sin(q.z));
}

vec3 orbitTangent(vec3 radialBasis, vec3 tangentBasis, vec3 q, float b) {
    vec3 er = radialBasis * cos(q.z) + tangentBasis * sin(q.z);
    vec3 ephi = -radialBasis * sin(q.z) + tangentBasis * cos(q.z);
    return normalize(q.y * er + (b / max(q.x, 1.0e-4)) * ephi);
}

void observerCamera(vec2 p, out vec3 cameraPosition, out vec3 rayDirection) {
    float cp = cos(uPitch);
    cameraPosition = uDistance * vec3(sin(uYaw) * cp,
                                      sin(uPitch),
                                      cos(uYaw) * cp);
    vec3 forward = normalize(-cameraPosition);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = normalize(cross(right, forward));
    float tanHalfFov = 0.383864035; // tan(42 degrees / 2)
    rayDirection = normalize(forward + tanHalfFov * (p.x * right + p.y * up));
}

vec3 renderObserver(vec2 p) {
    vec3 cameraPosition;
    vec3 rayDirection;
    observerCamera(p, cameraPosition, rayDirection);

    float r0 = length(cameraPosition);
    vec3 radialBasis = cameraPosition / r0;
    float mu = clamp(dot(rayDirection, radialBasis), -1.0, 1.0);
    vec3 transverse = rayDirection - mu * radialBasis;
    float transverseLength = length(transverse);
    vec3 tangentBasis;
    if (transverseLength > 1.0e-6) {
        tangentBasis = transverse / transverseLength;
    } else {
        tangentBasis = normalize(cross(vec3(0.0, 1.0, 0.0), radialBasis));
    }
    vec3 planeNormal = normalize(cross(radialBasis, tangentBasis));

    float massSwitch = uLensing != 0 ? 1.0 : 0.0;
    float fObserverForPath = max(1.0e-5, 1.0 - 2.0 * massSwitch / r0);
    float observerLapse = sqrt(max(1.0e-5, 1.0 - HORIZON / r0));
    float b = r0 * transverseLength / sqrt(fObserverForPath);
    vec3 q = vec3(r0, mu, 0.0); // (r, dr/dlambda, phi)

    float skyRadius = max(58.0, r0 + 34.0);
    vec3 halo = vec3(0.0);
    vec3 disk = vec3(0.0);
    bool hitDisk = false;
    bool captured = false;
    bool escaped = false;

    for (int i = 0; i < 1400; ++i) {
        if (i >= uMaxSteps) break;
        if (q.x <= HORIZON + 0.0025) {
            captured = true;
            break;
        }
        if (i > 2 && q.x >= skyRadius && q.y > 0.0) {
            escaped = true;
            break;
        }

        float radialStep = uStepScale * q.x;
        float angularStep = 0.045 * q.x * q.x / max(b, 0.20);
        float h = clamp(min(radialStep, angularStep), 0.012, 0.90);

        vec3 previousQ = q;
        vec3 previousPosition = orbitPosition(radialBasis, tangentBasis, previousQ);
        q = rk4Geodesic(q, h, b, massSwitch);

        // Project small RK drift back onto the exact null first integral.
        if (q.x > HORIZON + 0.01 && abs(q.y) > 0.002) {
            float f = 1.0 - 2.0 * massSwitch / q.x;
            float expected = sqrt(max(0.0, 1.0 - f * b * b / (q.x * q.x)));
            q.y = (q.y < 0.0 ? -1.0 : 1.0) * expected;
        }

        vec3 position = orbitPosition(radialBasis, tangentBasis, q);

        if (uShowDisk != 0 && previousPosition.y * position.y <= 0.0
            && abs(previousPosition.y - position.y) > 1.0e-6) {
            float crossing = clamp(previousPosition.y
                                   / (previousPosition.y - position.y), 0.0, 1.0);
            vec3 hitQ = mix(previousQ, q, crossing);
            vec3 hitPosition = orbitPosition(radialBasis, tangentBasis, hitQ);
            float hitRadius = hitQ.x;
            if (hitRadius >= ISCO && hitRadius <= DISK_OUTER) {
                disk = diskRadiance(hitRadius, hitPosition, b,
                                    planeNormal, observerLapse);
                hitDisk = true;
                break; // The analytic thin disk is optically thick.
            }
        }

        if (uShowHalo != 0 && q.x > HORIZON + 0.01 && q.x < 24.0) {
            halo += haloIncrement(position, q.x, h, observerLapse);
        }
    }

    vec3 background = vec3(0.0);
    if (hitDisk) {
        background = disk;
    } else if (escaped) {
        vec3 escapedDirection = orbitTangent(radialBasis, tangentBasis, q, b);
        background = skyRadiance(escapedDirection, -radialBasis);
    } else if (!captured) {
        // Near-critical rays that exhaust the realtime step budget should not
        // flash a false sky color; a faint residual exposes convergence limits.
        background = vec3(0.0005, 0.0002, 0.0001);
    }

    return background + min(halo, vec3(8.0));
}

float embeddingHeight(float r) {
    const float outerRadius = 20.0;
    float z = 2.0 * sqrt(2.0 * max(r - HORIZON, 0.0));
    float outerZ = 2.0 * sqrt(2.0 * (outerRadius - HORIZON));
    return z - outerZ;
}

vec3 renderEmbedding(vec2 p) {
    float distanceScale = mix(22.0, 32.0,
                              clamp((uDistance - 12.0) / 48.0, 0.0, 1.0));
    vec3 cameraPosition = vec3(sin(uYaw + 0.34) * distanceScale,
                               9.0 + 8.0 * uPitch,
                               cos(uYaw + 0.34) * distanceScale);
    vec3 target = vec3(0.0, -5.2, 0.0);
    vec3 forward = normalize(target - cameraPosition);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = normalize(cross(right, forward));
    vec3 rayDirection = normalize(forward + 0.4663 * (p.x * right + p.y * up));

    vec3 background = mix(vec3(0.0015, 0.003, 0.008),
                          vec3(0.009, 0.018, 0.038),
                          clamp(0.5 + 0.5 * rayDirection.y, 0.0, 1.0));

    float t = 0.5;
    float previousT = t;
    float previousField = 1.0e6;
    bool previousValid = false;
    bool hit = false;
    vec3 hitPosition = vec3(0.0);

    for (int i = 0; i < 320; ++i) {
        if (i >= uEmbeddingSteps) break;
        vec3 samplePosition = cameraPosition + rayDirection * t;
        float radius = length(samplePosition.xz);
        bool valid = radius >= HORIZON && radius <= 20.0;
        float field = samplePosition.y - embeddingHeight(max(radius, HORIZON));

        if (valid && previousValid && field <= 0.0 && previousField > 0.0) {
            float lo = previousT;
            float hi = t;
            for (int j = 0; j < 8; ++j) {
                float mid = 0.5 * (lo + hi);
                vec3 mp = cameraPosition + rayDirection * mid;
                float mr = length(mp.xz);
                float mf = mp.y - embeddingHeight(max(mr, HORIZON));
                if (mf > 0.0) lo = mid; else hi = mid;
            }
            hitPosition = cameraPosition + rayDirection * (0.5 * (lo + hi));
            hit = true;
            break;
        }

        if (valid) {
            previousT = t;
            previousField = field;
            previousValid = true;
        } else if (radius < HORIZON) {
            previousValid = false;
        }
        t += 0.28;
    }

    if (!hit) return background;

    float r = length(hitPosition.xz);
    float slope = sqrt(2.0 / max(r - HORIZON, 0.001));
    vec3 normal = normalize(vec3(-slope * hitPosition.x / r,
                                 1.0,
                                -slope * hitPosition.z / r));
    vec3 lightDirection = normalize(vec3(-0.4, 0.85, 0.25));
    float diffuse = 0.24 + 0.76 * max(dot(normal, lightDirection), 0.0);
    float lapse = sqrt(max(0.0, 1.0 - HORIZON / r));
    vec3 surface = mix(vec3(0.018, 0.028, 0.075),
                       vec3(0.025, 0.13, 0.19), lapse) * diffuse;

    if (uShowGrid != 0) {
        float angle = atan(hitPosition.z, hitPosition.x);
        float radialWave = abs(sin(12.0 * angle));
        float circularWave = abs(sin(PI * (r - HORIZON)));
        float radialLine = 1.0 - smoothstep(0.0, 0.075, radialWave);
        float circularLine = 1.0 - smoothstep(0.0, 0.11, circularWave);
        float grid = max(radialLine, circularLine);
        surface = mix(surface, vec3(0.08, 0.72, 0.93) * (0.7 + 0.3 * diffuse),
                      0.78 * grid);
    }

    // Reference rings: cyan horizon boundary, gold photon sphere, green ISCO.
    float throatRing = exp(-pow((r - HORIZON) / 0.080, 2.0));
    float photonRing = exp(-pow((r - PHOTON_SPHERE) / 0.070, 2.0));
    float iscoRing = exp(-pow((r - ISCO) / 0.090, 2.0));
    surface += throatRing * vec3(0.15, 1.2, 1.7);
    surface += photonRing * vec3(1.8, 0.62, 0.07);
    surface += iscoRing * vec3(0.18, 1.15, 0.52);

    float fresnel = pow(1.0 - max(dot(normal, -rayDirection), 0.0), 3.0);
    surface += fresnel * vec3(0.025, 0.08, 0.13);
    float outerFade = 1.0 - smoothstep(19.60, 20.0, r);
    return mix(background, surface, outerFade);
}

vec2 viewportCoordinates(vec2 fragment, vec2 origin, vec2 size) {
    vec2 p = 2.0 * (fragment - origin) / size - 1.0;
    p.x *= size.x / size.y;
    return p;
}

void main() {
    vec2 fragment = gl_FragCoord.xy;
    vec3 color;

    if (uViewMode == 0) {
        color = renderObserver(viewportCoordinates(fragment, vec2(0.0), uResolution));
    } else if (uViewMode == 1) {
        color = renderEmbedding(viewportCoordinates(fragment, vec2(0.0), uResolution));
    } else {
        float halfWidth = floor(0.5 * uResolution.x);
        if (fragment.x < halfWidth) {
            color = renderObserver(viewportCoordinates(
                fragment, vec2(0.0), vec2(halfWidth, uResolution.y)));
        } else {
            color = renderEmbedding(viewportCoordinates(
                fragment, vec2(halfWidth, 0.0),
                vec2(uResolution.x - halfWidth, uResolution.y)));
        }
        float divider = 1.0 - smoothstep(0.0, 2.5, abs(fragment.x - halfWidth));
        color = mix(color, vec3(0.08, 0.42, 0.58), 0.65 * divider);
    }

    outColor = vec4(color, 1.0);
}
)GLSL";

inline constexpr char kPresentFragment[] = R"GLSL(
#version 410 core

in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uScene;
uniform vec2 uTextureSize;
uniform float uExposure;
uniform int uBloom;

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;

    if (uBloom != 0) {
        vec2 texel = 1.0 / uTextureSize;
        vec3 glow = vec3(0.0);
        const float radius = 4.5;
        glow += max(texture(uScene, vUV + texel * vec2( radius, 0.0)).rgb - 0.8, 0.0);
        glow += max(texture(uScene, vUV + texel * vec2(-radius, 0.0)).rgb - 0.8, 0.0);
        glow += max(texture(uScene, vUV + texel * vec2(0.0,  radius)).rgb - 0.8, 0.0);
        glow += max(texture(uScene, vUV + texel * vec2(0.0, -radius)).rgb - 0.8, 0.0);
        glow += 0.7 * max(texture(uScene, vUV + texel * vec2( radius,  radius)).rgb - 0.8, 0.0);
        glow += 0.7 * max(texture(uScene, vUV + texel * vec2(-radius,  radius)).rgb - 0.8, 0.0);
        glow += 0.7 * max(texture(uScene, vUV + texel * vec2( radius, -radius)).rgb - 0.8, 0.0);
        glow += 0.7 * max(texture(uScene, vUV + texel * vec2(-radius, -radius)).rgb - 0.8, 0.0);
        hdr += 0.055 * glow;
    }

    vec3 mapped = vec3(1.0) - exp(-hdr * uExposure);
    mapped = pow(max(mapped, 0.0), vec3(1.0 / 2.2));
    vec2 centered = vUV * 2.0 - 1.0;
    float vignette = 1.0 - 0.12 * dot(centered, centered);
    mapped *= clamp(vignette, 0.72, 1.0);
    outColor = vec4(mapped, 1.0);
}
)GLSL";

} // namespace shaders
