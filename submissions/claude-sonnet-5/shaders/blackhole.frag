#version 410 core
// Per-pixel Schwarzschild null-geodesic ray tracer.
//
// Physics summary (Schwarzschild metric, G = c = 1, rs = 2M):
//   Every photon trajectory around a non-rotating black hole is planar
//   (the plane spanned by the black hole center, the ray origin and the
//   ray direction). Within that plane, using u = 1/r, the exact photon
//   orbit equation is:
//
//       d^2u/dphi^2 = -u + (3/2) * rs * u^2
//
//   This is integrated per-pixel with RK4, stepping in the angle phi.
//   Gravitational lensing, the photon ring and multiple (lensed) images
//   of the accretion disk all fall out of this single integration with
//   no special-casing.

in vec2 vUV;
out vec4 FragColor;

uniform vec3  uCamPos;
uniform vec3  uCamForward;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;

uniform float uRs;          // Schwarzschild radius (scene units)
uniform float uDiskInner;   // disk inner edge, scene units (ISCO = 3*rs for Schwarzschild)
uniform float uDiskOuter;   // disk outer edge, scene units
uniform float uTime;
uniform float uDiskBrightness;
uniform int   uMaxSteps;
uniform float uStepScale;
uniform int   uShowLensing; // 1 = curved geodesics, 0 = straight rays (for A/B comparison)

const float PI = 3.14159265359;

// ---------------------------------------------------------------- hashing
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Smoothly-interpolated value noise (bilinear hash lattice), used for the
// disk turbulence so it doesn't show hard per-cell block edges.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// ------------------------------------------------------------- starfield
vec3 starfield(vec3 dir) {
    // atan(y, x) is undefined when both arguments are exactly 0 (a ray
    // pointing exactly at the zenith/nadir); nudge x off zero so it never
    // hits that case.
    float u = atan(dir.z, dir.x + 1e-6) / (2.0 * PI) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    vec2 uv = vec2(u, v) * 900.0;
    vec2 cell = floor(uv);
    vec2 f = fract(uv);

    vec3 col = vec3(0.0);
    float n = hash21(cell);
    if (n > 0.9955) {
        float starBrightness = hash21(cell + 1.7);
        float d = length(f - 0.5);
        float core = smoothstep(0.5, 0.02, d);
        vec3 starColor = mix(vec3(0.65, 0.75, 1.0), vec3(1.0, 0.85, 0.55), hash21(cell + 3.1));
        col += core * (0.4 + 0.6 * starBrightness) * starColor * 1.6;
    }
    // Faint Milky-Way band plus a very dim isotropic sky glow so lensing
    // of the background is visible even off the dense star band.
    float band = exp(-pow((v - 0.5) * 4.2, 2.0)) * 0.05;
    col += band * vec3(0.55, 0.6, 0.85);
    col += vec3(0.01, 0.012, 0.018);
    return col;
}

// ------------------------------------------------- blackbody (Kelvin->RGB)
// Standard Planckian-locus polynomial fit (Tanner Helland's well-known
// public approximation), valid roughly over 1000K-40000K.
vec3 blackbodyColor(float tempK) {
    float t = clamp(tempK, 1000.0, 40000.0) / 100.0;
    float r, g, b;

    if (t <= 66.0) {
        r = 255.0;
    } else {
        r = 329.698727446 * pow(t - 60.0, -0.1332047592);
    }

    if (t <= 66.0) {
        g = 99.4708025861 * log(t) - 161.1195681661;
    } else {
        g = 288.1221695283 * pow(t - 60.0, -0.0755148492);
    }

    if (t >= 66.0) {
        b = 255.0;
    } else if (t <= 19.0) {
        b = 0.0;
    } else {
        b = 138.5177312231 * log(t - 10.0) - 305.0447927307;
    }

    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}

// -------------------------------------------------------- disk shading
// Shading at a single ray/disk-plane crossing point p, with the local
// photon travel direction photonDir (used for relativistic Doppler).
vec4 shadeDisk(vec3 p, float r, vec3 photonDir) {
    float M = 0.5 * uRs;

    // Exact circular-geodesic orbital speed relative to a local static
    // observer in Schwarzschild spacetime.
    float v = sqrt(M / max(r - uRs, 1e-4));
    v = clamp(v, 0.0, 0.999);
    float gammaL = 1.0 / sqrt(1.0 - v * v);

    float ang = atan(p.z, p.x);
    vec3 tangentDir = vec3(-sin(ang), 0.0, cos(ang)); // prograde orbital direction

    vec3 pd = vec3(photonDir.x, 0.0, photonDir.z);
    float pdLen = length(pd);
    float cosXi = pdLen > 1e-5 ? dot(pd / pdLen, tangentDir) : 0.0;

    float gravRedshift = sqrt(max(1.0 - uRs / r, 1e-6));
    float dopplerFactor = 1.0 / (gammaL * (1.0 + v * cosXi));
    float g = gravRedshift * dopplerFactor; // observed/emitted frequency ratio

    // Shakura-Sunyaev zero-torque thin-disk flux profile: zero at the
    // ISCO, peaking just outside it, falling off as r^-3 further out.
    float x = max(r / uDiskInner, 1.0001);
    float fluxProfile = pow(x, -3.0) * (1.0 - sqrt(1.0 / x));
    fluxProfile = max(fluxProfile, 0.0);
    // Analytic peak of x^-3*(1-sqrt(1/x)) occurs at x = (7/6)^2 = 49/36
    // and equals ~0.05667; normalize so the hottest annulus reaches
    // full brightness instead of the raw (small) flux value.
    const float kFluxPeak = 0.05667;
    float fluxNorm = clamp(fluxProfile / kFluxPeak, 0.0, 1.0);

    // Real disk temperatures (1e5-1e7 K) all sit far blue of the visible
    // Planckian locus, so we rescale into a visualizable 1000-20000K
    // range and let the Doppler factor g shift it further (approaching
    // side looks hotter/bluer, receding side cooler/redder).
    float temp = 1300.0 + 15500.0 * pow(fluxNorm, 1.3);
    temp *= g;

    vec3 color = blackbodyColor(temp);

    float intensity = uDiskBrightness * pow(fluxNorm, 0.7) * pow(max(g, 0.05), 4.0);

    // Subtle procedural turbulence so the disk doesn't look perfectly
    // smooth; purely decorative, not a physical claim. Two octaves,
    // sheared in angle over time to suggest differential rotation.
    float n1 = vnoise(vec2(ang * 5.0 + uTime * 0.12, r * 1.3));
    float n2 = vnoise(vec2(ang * 13.0 - uTime * 0.22, r * 3.1));
    float swirl = 0.88 + 0.09 * n1 + 0.05 * (n2 - 0.5);
    intensity *= swirl;

    float alpha = clamp(intensity, 0.0, 1.0);
    return vec4(color * intensity, alpha);
}

vec3 pos3D(float r, float phi, vec3 er, vec3 et) {
    return r * (cos(phi) * er + sin(phi) * et);
}

void rk4Step(inout float u, inout float up, float h, float rs) {
    float k1u = up;
    float k1up = -u + 1.5 * rs * u * u;

    float u2 = u + 0.5 * h * k1u;
    float up2 = up + 0.5 * h * k1up;
    float k2u = up2;
    float k2up = -u2 + 1.5 * rs * u2 * u2;

    float u3 = u + 0.5 * h * k2u;
    float up3 = up + 0.5 * h * k2up;
    float k3u = up3;
    float k3up = -u3 + 1.5 * rs * u3 * u3;

    float u4 = u + h * k3u;
    float up4 = up + h * k3up;
    float k4u = up4;
    float k4up = -u4 + 1.5 * rs * u4 * u4;

    u += (h / 6.0) * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
    up += (h / 6.0) * (k1up + 2.0 * k2up + 2.0 * k3up + k4up);
}

// Flat-space march, used for radial/degenerate rays and as the "lensing
// off" comparison mode.
vec4 straightMarch(vec3 ro, vec3 rd) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - uRs * uRs;
    float disc = b * b - c;
    float tHorizon = 1e9;
    if (disc > 0.0) {
        float t = -b - sqrt(disc);
        if (t > 0.0) tHorizon = t;
    }

    float tDisk = 1e9;
    if (abs(rd.y) > 1e-6) {
        float t = -ro.y / rd.y;
        if (t > 0.0) tDisk = t;
    }

    if (tHorizon < tDisk) return vec4(0.0, 0.0, 0.0, 1.0);

    vec3 sky = starfield(rd);
    if (tDisk < 1e8) {
        vec3 p = ro + rd * tDisk;
        float r = length(p);
        if (r >= uDiskInner && r <= uDiskOuter) {
            vec4 dc = shadeDisk(p, r, rd);
            return vec4(mix(sky, dc.rgb, dc.a), 1.0);
        }
    }
    return vec4(sky, 1.0);
}

vec4 traceRay(vec3 ro, vec3 rd) {
    float r0 = length(ro);
    vec3 planeNormal = cross(ro, rd);
    float nlen = length(planeNormal);

    if (uShowLensing == 0 || nlen < 1e-6) {
        return straightMarch(ro, rd);
    }
    planeNormal /= nlen;

    vec3 e_r = ro / r0;
    vec3 e_t = cross(planeNormal, e_r);

    float dir_r = dot(rd, e_r);
    float dir_t = dot(rd, e_t);
    if (abs(dir_t) < 1e-5) {
        return straightMarch(ro, rd);
    }

    float u = 1.0 / r0;
    float dudphi = -dir_r / dir_t * u;
    float phi = 0.0;

    vec3 accumColor = vec3(0.0);
    float accumAlpha = 0.0;

    vec3 prevPos = ro;
    float prevU = u;
    float prevPhi = 0.0;

    float farR = max(uDiskOuter * 6.0, 60.0 * uRs);

    for (int i = 0; i < uMaxSteps; i++) {
        float r = 1.0 / max(u, 1e-6);

        float closeness = clamp(1.0 - (r - uRs) / (10.0 * uRs), 0.0, 1.0);
        float h = uStepScale * mix(1.0, 0.10, closeness);

        prevU = u;
        prevPhi = phi;
        prevPos = pos3D(r, phi, e_r, e_t);

        rk4Step(u, dudphi, h, uRs);
        phi += h;

        r = 1.0 / max(u, 1e-6);

        if (r <= uRs) {
            accumColor += vec3(0.0) * (1.0 - accumAlpha);
            accumAlpha = 1.0;
            break;
        }

        vec3 currPos = pos3D(r, phi, e_r, e_t);

        if (sign(prevPos.y) != sign(currPos.y) && accumAlpha < 0.995) {
            float denom = (prevPos.y - currPos.y);
            float t = abs(denom) > 1e-8 ? prevPos.y / denom : 0.0;
            vec3 crossPos = mix(prevPos, currPos, clamp(t, 0.0, 1.0));
            float rc = length(crossPos);
            if (rc >= uDiskInner && rc <= uDiskOuter) {
                vec3 localDir = normalize(currPos - prevPos);
                vec4 dc = shadeDisk(crossPos, rc, localDir);
                accumColor += dc.rgb * dc.a * (1.0 - accumAlpha);
                accumAlpha += dc.a * (1.0 - accumAlpha);
            }
        }

        if (r > farR) {
            vec3 escapeDir = normalize(currPos - prevPos);
            vec3 sky = starfield(escapeDir);
            accumColor += sky * (1.0 - accumAlpha);
            accumAlpha = 1.0;
            break;
        }
    }

    if (accumAlpha < 0.999) {
        // Ran out of steps without escaping or being captured (can happen
        // very close to the critical impact parameter / photon sphere) -
        // these pixels belong to the photon ring's edge, so fade to black.
        accumColor += vec3(0.0) * (1.0 - accumAlpha);
    }

    return vec4(accumColor, 1.0);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 rd = normalize(uCamForward
        + ndc.x * uAspect * uTanHalfFov * uCamRight
        + ndc.y * uTanHalfFov * uCamUp);

    vec4 col = traceRay(uCamPos, rd);

    vec3 c = col.rgb;
    // Luminance-preserving Reinhard: compress brightness without
    // desaturating hot regions toward white (plain per-channel Reinhard
    // crushes all three channels toward 1 at high intensity, washing out
    // the disk's blackbody color).
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float lumTM = lum / (1.0 + lum);
    c = lum > 1e-5 ? c * (lumTM / lum) : c;
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)); // gamma

    FragColor = vec4(c, 1.0);
}
