#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uCameraRadius;
uniform float uAzimuth;
uniform float uElevation;
uniform int uShowDisk;
uniform int uShowHalo;

// Geometrized units: G = c = M = 1. Horizon r = 2, photon sphere r = 3,
// and the innermost stable circular orbit of a Schwarzschild hole r = 6.
const float PI = 3.141592653589793;
const float ROOT3 = 1.7320508075688772;
const float DISK_INNER = 6.0;
const float DISK_OUTER = 18.0;
const float CAPTURE_RADIUS = 2.10;
const float SKY_RADIUS = 115.0;

struct State { vec3 x; vec3 p; };

// Hamiltonian H = 1/2[-E^2/f + |p|^2 - 2M(x.p)^2/r^3], E = M = 1.
// These are exact Schwarzschild null geodesic equations in Cartesian
// Schwarzschild spatial coordinates. Integration error is numerical only.
State derivative(State s) {
    float r2 = dot(s.x, s.x);
    float r = sqrt(r2);
    float r3 = r2 * r;
    float f = max(1.0 - 2.0 / r, 0.025);
    float xp = dot(s.x, s.p);
    State d;
    d.x = s.p - 2.0 * xp * s.x / r3;
    d.p = -s.x / (r3 * f * f)
        + 2.0 * xp * s.p / r3
        - 3.0 * xp * xp * s.x / (r3 * r2);
    return d;
}

State addScaled(State s, State d, float h) {
    State v;
    v.x = s.x + h * d.x;
    v.p = s.p + h * d.p;
    return v;
}

State rk4(State s, float h) {
    State a = derivative(s);
    State b = derivative(addScaled(s, a, 0.5 * h));
    State c = derivative(addScaled(s, b, 0.5 * h));
    State d = derivative(addScaled(s, c, h));
    State outState;
    outState.x = s.x + (h / 6.0) * (a.x + 2.0 * b.x + 2.0 * c.x + d.x);
    outState.p = s.p + (h / 6.0) * (a.p + 2.0 * b.p + 2.0 * c.p + d.p);
    return outState;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + 1.0), f.x), f.y);
}

float diskFlux(float r) {
    // Page-Thorne zero-torque thin-disk flux for a=0, up to a constant.
    // F(r) proportional to [-Omega'(r)]/[r(E-Omega L)^2]
    //                   * integral_6^r (E-Omega L)L' dr.
    float y = sqrt(r);
    float primitive = y - 0.5 * ROOT3 * log((y - ROOT3) / (y + ROOT3));
    const float p6 = 3.976073575950256;
    return max(0.0, (primitive - p6) / (pow(r, 2.5) * (r - 3.0))) * 8780.0;
}

float orbitalOmega(float r) { return inversesqrt(r * r * r); }

vec3 diskColor(vec3 x, vec3 p, float cameraF, out float luminance) {
    float r = length(x.xz);
    float flux = diskFlux(r);
    float omega = orbitalOmega(r);
    // p follows the backwards spatial ray. The future-directed photon's
    // axial angular momentum is minus (x cross p)_y.
    float backwardLy = x.z * p.x - x.x * p.z;
    float g = sqrt(1.0 - 3.0 / r) /
              (sqrt(cameraF) * max(0.2, 1.0 + omega * backwardLy));

    // Differential rotation advects a modest, deterministic brightness
    // structure. The temperature/color scale is illustrative; F and g are GR.
    // Positive angular velocity about +Y reduces atan(z,x), so a material
    // pattern is constant along atan(z,x) + Omega * t.
    float phase = atan(x.z, x.x) + uTime * omega;
    float turbulence = noise(vec2(r * 0.60, phase * 4.0 + r * 0.9));
    float filaments = sin(phase * 17.0 + r * 3.4 + 2.0 * turbulence);
    float texture = 0.48 + 0.72 * turbulence + 0.15 * filaments;
    float temperature = pow(max(flux, 0.0), 0.25) * g;
    vec3 warm = vec3(1.0, 0.11, 0.025);
    vec3 amber = vec3(1.0, 0.48, 0.13);
    vec3 hot = vec3(1.0, 0.94, 0.77);
    vec3 color = mix(warm, amber, smoothstep(0.58, 1.02, temperature));
    color = mix(color, hot, smoothstep(1.12, 1.55, temperature));
    luminance = 2.6 * flux * pow(g, 4.0) * texture;
    return color * luminance;
}

vec3 sky(vec3 position) {
    vec3 d = normalize(position);
    vec2 vUV = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5,
                   asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5);
    // A fixed celestial sphere makes multiple lensed images apparent.
    float lat = dot(d, normalize(vec3(0.17, 0.87, 0.46)));
    float band = exp(-37.0 * lat * lat);
    float cloud = noise(vUV * vec2(17.0, 8.0)) * 0.6 +
                  noise(vUV * vec2(39.0, 19.0)) * 0.4;
    vec3 color = vec3(0.0015, 0.0030, 0.0080) +
                 band * cloud * vec3(0.018, 0.024, 0.047);

    vec2 cell = floor(vUV * vec2(560.0, 280.0));
    vec2 local = fract(vUV * vec2(560.0, 280.0));
    float chance = hash12(cell);
    if (chance > 0.975) {
        vec2 starPos = vec2(hash12(cell + 13.7), hash12(cell + 91.3));
        float radius2 = dot(local - starPos, local - starPos);
        float star = exp(-radius2 * 95.0) * pow((chance - 0.975) / 0.025, 2.0);
        color += star * mix(vec3(0.5, 0.72, 1.0), vec3(1.0, 0.86, 0.62),
                            hash12(cell + 31.5)) * 1.8;
    }
    return color;
}

vec3 tonemap(vec3 linearColor) {
    vec3 c = linearColor / (linearColor + vec3(1.0));
    return pow(max(c, 0.0), vec3(1.0 / 2.2));
}

void main() {
    vec2 vUV = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    float ca = cos(uAzimuth), sa = sin(uAzimuth);
    float ce = cos(uElevation), se = sin(uElevation);
    vec3 camera = uCameraRadius * vec3(sa * ce, se, ca * ce);
    vec3 forward = normalize(-camera);
    vec3 right = normalize(cross(forward, vec3(0, 1, 0)));
    vec3 up = cross(right, forward);
    vec3 direction = normalize(forward + 1.04 * (vUV.x * right + vUV.y * up));

    float cameraF = 1.0 - 2.0 / uCameraRadius;
    vec3 radial = normalize(camera);
    float dRadial = dot(direction, radial);
    State ray;
    ray.x = camera;
    ray.p = (direction - dRadial * radial) / sqrt(cameraF)
          + dRadial * radial / cameraF;

    vec3 halo = vec3(0.0);
    vec3 result = vec3(0.0);
    for (int i = 0; i < 500; ++i) {
        float r = length(ray.x);
        if (r <= CAPTURE_RADIUS) break;
        if (r >= SKY_RADIUS && dot(ray.x, derivative(ray).x) > 0.0) {
            result = sky(ray.x);
            break;
        }

        float f = 1.0 - 2.0 / r;
        float h = min(2.3, 0.075 * r) * clamp(3.0 * f, 0.18, 1.0);
        State next = rk4(ray, h);
        if (length(next.x) < 1.9) break;

        if (uShowHalo == 1 && r > DISK_INNER && r < 23.0) {
            float cylindrical = length(ray.x.xz);
            if (cylindrical > 6.0 && cylindrical < 21.0) {
                float scaleHeight = 0.42 + 0.018 * cylindrical;
                float density = exp(-abs(ray.x.y) / scaleHeight);
                float taper = smoothstep(6.0, 8.0, cylindrical) *
                              (1.0 - smoothstep(17.0, 21.0, cylindrical));
                float emissivity = 0.016 * density * taper *
                                   diskFlux(clamp(cylindrical, 6.001, 24.0));
                halo += vec3(1.0, 0.34, 0.10) * emissivity *
                        length(next.x - ray.x);
            }
        }

        if (uShowDisk == 1 && ray.x.y * next.x.y <= 0.0) {
            float t = ray.x.y / (ray.x.y - next.x.y);
            vec3 hit = mix(ray.x, next.x, t);
            float diskR = length(hit.xz);
            if (diskR >= DISK_INNER && diskR <= DISK_OUTER) {
                float lum;
                result = diskColor(hit, mix(ray.p, next.p, t), cameraF, lum);
                break;
            }
        }
        ray = next;
    }

    // A ray that neither escaped nor hit the disk after 500 steps is close
    // to the unstable photon orbit; displaying it dark is conservative.
    result += halo;
    vec3 mapped = tonemap(result);
    float vignette = 1.0 - 0.20 * smoothstep(0.28, 0.9, length(vUV));
    FragColor = vec4(mapped * vignette, 1.0);
}
