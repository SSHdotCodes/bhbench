#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform vec3 uCamPos;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uResolution;
uniform float uTime;
uniform float uRs;
uniform float uDiskInner;
uniform float uDiskOuter;
uniform int uShowGrid;

const int STEPS = 300;
const float STEP_SIZE = 0.15;
const float ESCAPE_R = 50.0;

vec3 geodesicAccel(vec3 pos, vec3 vel, float rs) {
    float r = length(pos);
    vec3 h = cross(pos, vel);
    float h2 = dot(h, h);
    return -1.5 * rs * h2 / (r * r * r * r * r) * pos;
}

void rk4Integrate(inout vec3 pos, inout vec3 vel, float dt, float rs) {
    vec3 a1 = geodesicAccel(pos, vel, rs);
    vec3 k1v = a1;
    vec3 k1x = vel;

    vec3 a2 = geodesicAccel(pos + 0.5*dt*k1x, vel + 0.5*dt*k1v, rs);
    vec3 k2v = a2;
    vec3 k2x = vel + 0.5*dt*k1v;

    vec3 a3 = geodesicAccel(pos + 0.5*dt*k2x, vel + 0.5*dt*k2v, rs);
    vec3 k3v = a3;
    vec3 k3x = vel + 0.5*dt*k2v;

    vec3 a4 = geodesicAccel(pos + dt*k3x, vel + dt*k3v, rs);
    vec3 k4v = a4;
    vec3 k4x = vel + dt*k3v;

    pos += (dt/6.0)*(k1x + 2.0*k2x + 2.0*k3x + k4x);
    vel += (dt/6.0)*(k1v + 2.0*k2v + 2.0*k3v + k4v);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 starfield(vec3 dir) {
    vec3 col = vec3(0.0);
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    float phi = atan(dir.z, dir.x);

    for (float i = 0.0; i < 3.0; i++) {
        vec2 uv = vec2(phi * (10.0 + i*7.0), theta * (10.0 + i*7.0));
        vec2 id = floor(uv);
        vec2 f = fract(uv) - 0.5;
        float h = hash(id + i * 100.0);
        if (h > 0.92) {
            float d = length(f - (vec2(hash(id+1.0), hash(id+2.0)) - 0.5) * 0.6);
            float brightness = smoothstep(0.08, 0.0, d) * (h - 0.92) * 12.0;
            float temp = hash(id + 50.0);
            vec3 starCol = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.9, 0.7), temp);
            col += starCol * brightness;
        }
    }

    float milky = exp(-abs(dir.y - 0.1) * 4.0) * 0.03;
    col += vec3(0.4, 0.35, 0.5) * milky;
    return col;
}

vec3 blackbody(float t) {
    float x = clamp(t / 6500.0, 0.0, 3.0);
    vec3 col;
    col.r = clamp(1.0 - exp(-x * 3.0), 0.0, 1.0);
    col.g = clamp(1.0 - exp(-x * 2.0), 0.0, 1.0) * clamp(x, 0.0, 1.0);
    col.b = clamp(x * x - 0.2, 0.0, 1.0);
    return col;
}

vec3 diskColor(float r, float phi, float rs) {
    float t = (r - uDiskInner) / (uDiskOuter - uDiskInner);

    float temp = 5500.0 * pow(uDiskInner / r, 0.75);
    vec3 col = blackbody(temp);

    float omega = sqrt(rs / (2.0 * r * r * r));
    float vOrbital = omega * r / sqrt(1.0 - rs / r);

    vec3 diskNormal = vec3(0.0, 1.0, 0.0);
    vec3 velDir = normalize(cross(diskNormal, vec3(cos(phi), 0.0, sin(phi))));
    vec3 toCam = normalize(uCamPos - vec3(r*cos(phi), 0.0, r*sin(phi)));

    float vDotN = dot(velDir * vOrbital, toCam);
    float lorentz = 1.0 / sqrt(1.0 - vOrbital * vOrbital);
    float doppler = 1.0 / (lorentz * (1.0 - vDotN));

    float gravRedshift = sqrt(1.0 - rs / r);
    float totalShift = doppler * gravRedshift;

    float intensity = pow(totalShift, 3.0);
    intensity *= smoothstep(0.0, 0.15, t) * smoothstep(1.0, 0.7, t);

    float turbulence = 0.7 + 0.3 * sin(phi * 8.0 + uTime * 2.0 + r * 3.0)
                     * sin(phi * 13.0 - uTime * 1.5 + r * 5.0);

    col *= intensity * turbulence * 2.5;

    float shiftedTemp = temp * totalShift;
    col = blackbody(shiftedTemp) * intensity * turbulence * 2.5;

    return col;
}

void main() {
    vec2 ndc = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    ndc.x *= uResolution.x / uResolution.y;

    mat4 invVP = inverse(uProj * uView);
    vec4 nearP = invVP * vec4(ndc, -1.0, 1.0);
    vec4 farP = invVP * vec4(ndc, 1.0, 1.0);
    nearP /= nearP.w;
    farP /= farP.w;

    vec3 rayOrigin = nearP.xyz;
    vec3 rayDir = normalize(farP.xyz - nearP.xyz);

    vec3 pos = rayOrigin;
    vec3 vel = rayDir;

    vec3 color = vec3(0.0);
    float diskGlow = 0.0;
    bool captured = false;
    bool hitDisk = false;

    float prevY = pos.y;

    for (int i = 0; i < STEPS; i++) {
        float r = length(pos);

        if (r < uRs * 1.0) {
            captured = true;
            break;
        }

        if (r > ESCAPE_R && dot(pos, vel) > 0.0) {
            break;
        }

        float adaptiveStep = STEP_SIZE * clamp(r * 0.15, 0.3, 3.0);

        vec3 oldPos = pos;
        rk4Integrate(pos, vel, adaptiveStep, uRs);

        float curY = pos.y;
        if (prevY * curY < 0.0 || abs(curY) < 0.01) {
            float tCross = abs(prevY) / (abs(prevY) + abs(curY) + 0.0001);
            vec3 crossPos = mix(oldPos, pos, tCross);
            float crossR = length(crossPos.xz);

            if (crossR > uDiskInner && crossR < uDiskOuter) {
                float phi = atan(crossPos.z, crossPos.x);
                vec3 dCol = diskColor(crossR, phi, uRs);

                float crossDist = length(crossPos - rayOrigin);
                float fog = exp(-crossDist * 0.01);
                color += dCol * fog;
                hitDisk = true;
            }
        }
        prevY = curY;

        float diskProximity = abs(pos.y);
        float diskR = length(pos.xz);
        if (diskR > uDiskInner * 0.8 && diskR < uDiskOuter * 1.1 && diskProximity < 0.5) {
            float glow = exp(-diskProximity * 4.0) * 0.02;
            glow *= smoothstep(uDiskInner * 0.8, uDiskInner, diskR) * smoothstep(uDiskOuter * 1.1, uDiskOuter, diskR);
            diskGlow += glow;
        }
    }

    if (captured) {
        float r = length(pos);
        float glow = exp(-(r - uRs) * 2.0) * 0.1;
        color += vec3(0.1, 0.05, 0.0) * glow;
    } else if (!hitDisk) {
        vec3 escDir = normalize(vel);
        color += starfield(escDir);
    }

    color += vec3(1.0, 0.6, 0.2) * diskGlow;

    float photonSphereR = 1.5 * uRs;
    float psDist = abs(length(pos) - photonSphereR);
    if (!captured) {
        float psGlow = exp(-psDist * 1.5) * 0.05;
        color += vec3(0.5, 0.4, 0.8) * psGlow;
    }

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(0.4545));

    fragColor = vec4(color, 1.0);
}
