#include <metal_stdlib>
using namespace metal;

constant float PI = 3.14159265358979323846f;
constant float TWO_PI = 6.28318530717958647692f;

struct Uniforms {
    uint2 traceSize;
    uint2 outputSize;
    float4 camera;   // time, a/M, inclination, observer radius
    float4 image;    // vertical FOV, exposure, EDR headroom, camera azimuth
    uint4 sampling;  // frame index, maximum steps, flags, reserved
    float4 disk;     // horizon, ISCO, outer radius, temperature scale
};

inline float hash21(float2 p) {
    float3 p3 = fract(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.x + p3.y) * p3.z);
}

inline float2 hash22(float2 p) {
    float n = hash21(p);
    return fract(float2(n, n * 1.2154f + 0.3719f) * float2(43758.55f, 22578.15f));
}

inline float hash31(float3 p3) {
    p3 = fract(p3 * 0.1031f);
    p3 += dot(p3, p3.zyx + 31.32f);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    f = f * f * (3.0f - 2.0f * f);
    float n000 = hash31(i + float3(0, 0, 0));
    float n100 = hash31(i + float3(1, 0, 0));
    float n010 = hash31(i + float3(0, 1, 0));
    float n110 = hash31(i + float3(1, 1, 0));
    float n001 = hash31(i + float3(0, 0, 1));
    float n101 = hash31(i + float3(1, 0, 1));
    float n011 = hash31(i + float3(0, 1, 1));
    float n111 = hash31(i + float3(1, 1, 1));
    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z
    );
}

inline float radialPotential(float r, float lambda, float carter, float spin) {
    float delta = r * r - 2.0f * r + spin * spin;
    float p = r * r + spin * spin - spin * lambda;
    float shiftedL = lambda - spin;
    return p * p - delta * (carter + shiftedL * shiftedL);
}

inline float polarPotential(float theta, float lambda, float carter, float spin) {
    float s = max(abs(sin(theta)), 1.0e-4f);
    float c = cos(theta);
    return carter + spin * spin * c * c - lambda * lambda * c * c / (s * s);
}

float radialTurningPoint(float allowed, float forbidden, float lambda, float carter, float spin) {
    for (uint iteration = 0; iteration < 5; ++iteration) {
        float midpoint = (allowed + forbidden) * 0.5f;
        if (radialPotential(midpoint, lambda, carter, spin) >= 0.0f) {
            allowed = midpoint;
        } else {
            forbidden = midpoint;
        }
    }
    return (allowed + forbidden) * 0.5f;
}

float polarTurningPoint(float allowed, float forbidden, float lambda, float carter, float spin) {
    for (uint iteration = 0; iteration < 5; ++iteration) {
        float midpoint = (allowed + forbidden) * 0.5f;
        if (polarPotential(midpoint, lambda, carter, spin) >= 0.0f) {
            allowed = midpoint;
        } else {
            forbidden = midpoint;
        }
    }
    return (allowed + forbidden) * 0.5f;
}

float3 planckRGB(float temperature) {
    float t = clamp(temperature, 1'000.0f, 20'000.0f);
    float3 wavelengthNM = float3(680.0f, 550.0f, 440.0f);
    float3 x = 1.4387769e7f / (wavelengthNM * t);
    float greenNumerator = exp(clamp(x.y, 0.0f, 20.0f)) - 1.0f;
    float3 spectrum = pow(float3(550.0f) / wavelengthNM, float3(5.0f))
        * greenNumerator / max(exp(clamp(x, float3(0.0f), float3(20.0f))) - 1.0f, float3(1.0e-5f));
    spectrum /= max(max(spectrum.r, spectrum.g), spectrum.b);
    return max(spectrum, float3(0.0f));
}

float3 diskEmission(
    float radius,
    float phi,
    float lambda,
    float time,
    float spin,
    float innerRadius,
    float outerRadius,
    float temperatureScale
) {
    float omega = 1.0f / (pow(radius, 1.5f) + spin);
    float gTT = -(1.0f - 2.0f / radius);
    float gTPhi = -2.0f * spin / radius;
    float gPhiPhi = radius * radius + spin * spin + 2.0f * spin * spin / radius;
    float uT = rsqrt(max(-(gTT + 2.0f * gTPhi * omega + gPhiPhi * omega * omega), 1.0e-4f));
    float frequencyShift = clamp(1.0f / max(uT * (1.0f - omega * lambda), 0.08f), 0.16f, 2.4f);

    // The zero-torque thin-disk profile vanishes at the ISCO and falls as r^-3/4.
    float boundary = pow(max(1.0f - sqrt(innerRadius / radius), 0.0f), 0.25f);
    float emittedTemperature = temperatureScale * pow(radius, -0.75f) * boundary;
    float observedTemperature = emittedTemperature * frequencyShift;
    float bolometricShift = pow(frequencyShift, 4.0f);

    float spiral = phi - omega * time * 7.0f;
    float2 orbitalPhase = float2(cos(spiral), sin(spiral));
    float turbulence = valueNoise(float3(
        radius * 1.8f + orbitalPhase.x * 1.7f,
        orbitalPhase.y * 2.8f,
        time * 0.08f
    ));
    float fineStructure = 0.5f + 0.5f * sin(
        radius * 29.0f - orbitalPhase.y * 6.0f + orbitalPhase.x * 3.0f + turbulence * 8.0f
    );
    float density = mix(0.55f, 1.35f, turbulence) * mix(0.82f, 1.18f, fineStructure);
    float edge = smoothstep(innerRadius, innerRadius + 0.42f, radius)
        * (1.0f - smoothstep(outerRadius - 1.4f, outerRadius, radius));
    float emittedPower = pow(max(emittedTemperature, 0.0f) / 6'500.0f, 4.0f);

    return planckRGB(observedTemperature) * emittedPower * bolometricShift * density * edge * 5.5f;
}

float3 skyRadiance(float theta, float phi) {
    float s = sin(theta);
    float3 direction = normalize(float3(s * cos(phi), cos(theta), s * sin(phi)));
    float3 galacticNorth = normalize(float3(0.13f, 0.78f, 0.61f));
    float latitude = asin(clamp(dot(direction, galacticNorth), -1.0f, 1.0f));
    float cloud = valueNoise(direction * 4.2f) * 0.65f + valueNoise(direction * 11.0f) * 0.35f;
    float band = exp(-abs(latitude) * 7.5f);
    float dustLane = 1.0f - 0.72f * exp(-abs(latitude) * 36.0f) * smoothstep(0.34f, 0.72f, cloud);
    float3 milkyWay = mix(float3(0.018f, 0.026f, 0.055f), float3(0.16f, 0.11f, 0.075f), cloud)
        * band * dustLane * 0.36f;

    float wrappedPhi = fract(phi / TWO_PI + 1.0f);
    float2 starGrid = float2(wrappedPhi * 1'280.0f, theta / PI * 640.0f);
    float2 cell = floor(starGrid);
    float2 center = hash22(cell) * 0.72f + 0.14f;
    float distanceToStar = length(fract(starGrid) - center);
    float seed = hash21(cell + 17.31f);
    float starMask = smoothstep(0.11f, 0.015f, distanceToStar) * step(0.986f, seed);
    float magnitude = min(pow(max((seed - 0.986f) / 0.014f, 0.0f), 4.0f) * 22.0f + 1.2f, 24.0f);
    float colorSeed = hash21(cell + 93.7f);
    float3 starColor = mix(
        float3(1.0f, 0.62f, 0.34f),
        float3(0.62f, 0.77f, 1.0f),
        smoothstep(0.25f, 0.78f, colorSeed)
    );

    return float3(0.00045f, 0.0007f, 0.0015f) + milkyWay + starColor * starMask * magnitude;
}

float4 traceKerrMapping(float2 pixel, constant Uniforms &u) {
    float2 size = float2(u.traceSize);
    float2 p = (pixel + 0.5f) / size * 2.0f - 1.0f;
    p.y = -p.y;
    p.x *= size.x / size.y;

    float spin = u.camera.y;
    float observerTheta = u.camera.z;
    float observerRadius = u.camera.w;
    float imagePlaneScale = tan(u.image.x * 0.5f);
    float3 localDirection = normalize(float3(-1.0f, -p.y * imagePlaneScale, p.x * imagePlaneScale));
    float sinObserver = max(abs(sin(observerTheta)), 1.0e-5f);
    float cosObserver = cos(observerTheta);

    // Convert the camera ray from a finite-distance ZAMO tetrad to Kerr constants of motion.
    float sigmaObserver = observerRadius * observerRadius + spin * spin * cosObserver * cosObserver;
    float deltaObserver = observerRadius * observerRadius - 2.0f * observerRadius + spin * spin;
    float aMetric = pow(observerRadius * observerRadius + spin * spin, 2.0f)
        - spin * spin * deltaObserver * sinObserver * sinObserver;
    float lapse = sqrt(max(sigmaObserver * deltaObserver / aMetric, 1.0e-8f));
    float frameDragging = 2.0f * spin * observerRadius / aMetric;
    float pT = 1.0f / lapse;
    float pPhi = frameDragging / lapse
        + localDirection.z * sqrt(sigmaObserver / aMetric) / sinObserver;
    float gTTObserver = -(1.0f - 2.0f * observerRadius / sigmaObserver);
    float gTPhiObserver = -2.0f * spin * observerRadius * sinObserver * sinObserver / sigmaObserver;
    float gPhiPhiObserver = aMetric * sinObserver * sinObserver / sigmaObserver;
    float energy = -(gTTObserver * pT + gTPhiObserver * pPhi);
    float angularMomentum = gTPhiObserver * pT + gPhiPhiObserver * pPhi;
    float covariantTheta = sqrt(sigmaObserver) * localDirection.y;
    float lambda = angularMomentum / energy;
    float carter = covariantTheta * covariantTheta / (energy * energy)
        + cosObserver * cosObserver * (lambda * lambda / (sinObserver * sinObserver) - spin * spin);
    float radialSign = localDirection.x < 0.0f ? -1.0f : 1.0f;
    float polarSign = localDirection.y < 0.0f ? -1.0f : 1.0f;
    float radius = observerRadius;
    float theta = observerTheta;
    float phi = 0.0f;
    float tenuousPlasma = 0.0f;

    uint maximumSteps = u.sampling.y;
    for (uint step = 0; step < maximumSteps; ++step) {
        float previousRadius = radius;
        float previousTheta = theta;
        float sinTheta = max(abs(sin(theta)), 1.0e-4f);
        float cosTheta = cos(theta);
        float sigma = max(radius * radius + spin * spin * cosTheta * cosTheta, 1.0e-4f);
        float delta = max(radius * radius - 2.0f * radius + spin * spin, 1.0e-4f);
        float pKerr = radius * radius + spin * spin - spin * lambda;
        float radialSpeed = sqrt(max(radialPotential(radius, lambda, carter, spin), 0.0f)) / sigma;
        float polarSpeed = sqrt(max(polarPotential(theta, lambda, carter, spin), 0.0f)) / sigma;
        float azimuthRate = (lambda / (sinTheta * sinTheta) - spin + spin * pKerr / delta) / sigma;
        float stepSlope = radius < 7.0f ? 0.020f : 0.040f;
        float affineStep = clamp(stepSlope * radius, 0.022f, 0.84f);

        float nextRadius = radius + radialSign * radialSpeed * affineStep;
        if (nextRadius > u.disk.x && radialPotential(nextRadius, lambda, carter, spin) < 0.0f) {
            float turningRadius = radialTurningPoint(radius, nextRadius, lambda, carter, spin);
            nextRadius = 2.0f * turningRadius - nextRadius;
            radialSign = -radialSign;
        }

        float nextTheta = theta + polarSign * polarSpeed * affineStep;
        if (polarPotential(nextTheta, lambda, carter, spin) < 0.0f) {
            float turningTheta = polarTurningPoint(theta, nextTheta, lambda, carter, spin);
            nextTheta = 2.0f * turningTheta - nextTheta;
            polarSign = -polarSign;
        }

        radius = nextRadius;
        theta = nextTheta;
        phi += azimuthRate * affineStep;

        if (theta < 0.0f) {
            theta = -theta;
            polarSign = -polarSign;
        } else if (theta > PI) {
            theta = TWO_PI - theta;
            polarSign = -polarSign;
        }

        if (radius <= u.disk.x + 0.012f) {
            return float4(0.0f, 0.0f, tenuousPlasma, 0.0f);
        }

        float sideBefore = previousTheta - PI * 0.5f;
        float sideAfter = theta - PI * 0.5f;
        if (sideBefore * sideAfter <= 0.0f && step > 0) {
            float crossing = clamp(abs(sideBefore) / max(abs(sideBefore) + abs(sideAfter), 1.0e-5f), 0.0f, 1.0f);
            float diskRadius = mix(previousRadius, radius, crossing);
            if (diskRadius >= u.disk.y && diskRadius <= u.disk.z) {
                float diskPhi = phi - azimuthRate * affineStep * (1.0f - crossing);
                return float4(diskRadius, diskPhi, lambda, 1.0f);
            }
        }

        // A very thin, optically light atmosphere keeps higher-order disk images visible.
        if (radius > u.disk.y && radius < u.disk.z) {
            float height = abs(theta - PI * 0.5f) * radius;
            float atmosphere = exp(-height * 42.0f) * exp(-0.15f * (radius - u.disk.y));
            tenuousPlasma += atmosphere * affineStep * 0.004f;
        }

        if (radialSign > 0.0f && radius > observerRadius * 1.06f && step > 6) {
            return float4(theta, phi, tenuousPlasma, 2.0f);
        }
    }

    return radius > u.disk.x + 0.1f
        ? float4(theta, phi, tenuousPlasma, 3.0f)
        : float4(0.0f, 0.0f, tenuousPlasma, 0.0f);
}

kernel void traceLensMap(
    texture2d<float, access::write> lensMap [[texture(0)]],
    constant Uniforms &uniforms [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    uint phase = uniforms.sampling.z & 7u;
    uint2 phaseOffset = uint2(phase & 3u, phase >> 2u);
    uint2 pixel = gid * uint2(4u, 2u) + phaseOffset;
    if (any(pixel >= uniforms.traceSize)) { return; }
    lensMap.write(traceKerrMapping(float2(pixel), uniforms), pixel);
}

kernel void shadeLensMap(
    texture2d<float, access::read> lensMap [[texture(0)]],
    texture2d<half, access::write> radiance [[texture(1)]],
    constant Uniforms &uniforms [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    if (any(gid >= uniforms.traceSize)) { return; }
    float4 mapping = lensMap.read(gid);
    float kind = mapping.w;
    float3 plasma = float3(1.0f, 0.27f, 0.035f) * mapping.z;
    float3 color;
    if (kind < 0.5f) {
        color = plasma;
    } else if (kind < 1.5f) {
        color = diskEmission(
            mapping.x,
            mapping.y + uniforms.image.w,
            mapping.z,
            uniforms.camera.x,
            uniforms.camera.y,
            uniforms.disk.y,
            uniforms.disk.z,
            uniforms.disk.w
        );
    } else {
        float skyScale = kind < 2.5f ? 1.0f : 0.55f;
        color = skyRadiance(mapping.x, mapping.y + uniforms.image.w) * skyScale + plasma;
    }
    radiance.write(half4(half3(max(color, float3(0.0f))), half(1.0f)), gid);
}

inline float3 acesFitted(float3 value) {
    float3 a = value * (2.51f * value + 0.03f);
    float3 b = value * (2.43f * value + 0.59f) + 0.14f;
    return clamp(a / b, 0.0f, 1.0f);
}

inline float3 linearSRGBToDisplayP3(float3 color) {
    return float3(
        dot(color, float3(0.8225929f, 0.1775340f, 0.0f)),
        dot(color, float3(0.0331995f, 0.9667835f, 0.0f)),
        dot(color, float3(0.0170854f, 0.0723957f, 0.9103015f))
    );
}

kernel void presentHDR(
    texture2d<half, access::sample> source [[texture(0)]],
    texture2d<half, access::write> drawable [[texture(1)]],
    constant Uniforms &uniforms [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    if (any(gid >= uniforms.outputSize)) { return; }
    constexpr sampler linearSampler(coord::normalized, address::clamp_to_edge, filter::linear);
    float2 uv = (float2(gid) + 0.5f) / float2(uniforms.outputSize);
    float2 texel = 1.0f / float2(uniforms.outputSize);
    float3 center = float3(source.sample(linearSampler, uv).rgb);
    float3 neighborhood = (
        float3(source.sample(linearSampler, uv + float2(texel.x, 0.0f)).rgb)
        + float3(source.sample(linearSampler, uv - float2(texel.x, 0.0f)).rgb)
        + float3(source.sample(linearSampler, uv + float2(0.0f, texel.y)).rgb)
        + float3(source.sample(linearSampler, uv - float2(0.0f, texel.y)).rgb)
    ) * 0.25f;
    float3 scene = max(center + (center - neighborhood) * 0.18f, float3(0.0f)) * uniforms.image.y;
    float luminance = max(dot(scene, float3(0.2126f, 0.7152f, 0.0722f)), 1.0e-6f);
    float headroom = max(uniforms.image.z, 1.0f);
    float sdrWhite = acesFitted(float3(1.0f)).x;
    float mappedLuminance = luminance <= 1.0f
        ? acesFitted(float3(luminance)).x
        : sdrWhite + (headroom - sdrWhite) * (1.0f - exp(-(luminance - 1.0f) * 0.18f));
    float3 chroma = scene / luminance;
    float3 displayLinear = chroma * mappedLuminance;
    float peak = max(max(displayLinear.r, displayLinear.g), displayLinear.b);
    displayLinear *= peak > headroom ? headroom / peak : 1.0f;
    drawable.write(half4(half3(linearSRGBToDisplayP3(displayLinear)), half(1.0f)), gid);
}
