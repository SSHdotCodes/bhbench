#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
out vec4 fragColor;

uniform vec3  uCamPos;
uniform vec3  uFwd;     // camera forward (unit)
uniform vec3  uRight;   // camera right * tan(hfov) * aspect
uniform vec3  uUp;      // camera up    * tan(hfov)
uniform int   uMaxIter;
uniform float uStepBase;
uniform float uTpeak;   // disk peak temperature (K)

const float RS  = 2.0;      // Schwarzschild radius (M = 1)
const float ESC = 55.0;     // escape radius
const float DISK_IN  = 6.0; // ISCO
const float DISK_OUT = 22.0;

// ---------------------------------------------------------------- sky ------
float h1(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }

vec3 skyColor(vec3 d) {
    vec3 col = vec3(0.0);
    // star field
    vec3 q = d * 24.0;
    vec3 cell = floor(q);
    vec3 f = fract(q);
    for (int i = -1; i <= 1; ++i)
    for (int j = -1; j <= 1; ++j)
    for (int k = -1; k <= 1; ++k) {
        vec3 c = cell + vec3(float(i), float(j), float(k));
        float h = h1(c);
        if (h > 0.972) {
            vec3 sp = c + vec3(h1(c + 7.3), h1(c + 13.9), h1(c + 23.7)) - 0.5;
            vec3 dd = sp - f;
            float g = exp(-dot(dd, dd) * 260.0);
            float sz = 0.5 + h1(c + 3.1);
            vec3 tint = mix(vec3(0.75, 0.80, 1.0), vec3(1.0, 0.88, 0.65), h1(c + 1.7));
            col += g * tint * (0.5 + 1.7 * sz);
        }
    }
    // Milky Way band
    float bd = exp(-pow(abs(d.y * 0.85 + d.x * 0.35 + 0.12), 1.3) * 9.0);
    col += bd * vec3(0.55, 0.66, 0.95) * 0.14;
    // faint nebulosity
    float nb = exp(-pow(abs(d.x * 0.6 - d.z * 0.3 + 0.25), 2.2) * 5.0)
             * exp(-pow(abs(d.y + 0.35), 2.0) * 6.0);
    col += nb * vec3(0.35, 0.20, 0.42) * 0.10;
    // the sun: a bright disc at infinity, gets gravitationally lensed
    vec3 sun = normalize(vec3(0.40, 0.48, -0.78));
    float sdot = dot(d, sun);
    float disc = exp2(-(1.0 - sdot) * 3.5e5);
    col += disc * vec3(1.0, 0.97, 0.90) * 70.0;
    return col;
}

// ------------------------------------------------------- Planck emission ---
// Specific intensity sampled at R/G/B wavelengths, white-balanced at 5500 K.
// Includes the relativistic Doppler/gravitational redshift factor g:
//   I_obs(lambda_c) = g^3 * B(lambda_c * g, T_em)
const float HC_K = 1.4388e-2; // h c / k_B (m K)
const float TREF = 5500.0;

vec3 planckRGB(float T, float g) {
    float lamR = 650.0e-9, lamG = 530.0e-9, lamB = 470.0e-9;
    vec3 col;
    float lam[3];
    lam[0] = lamR; lam[1] = lamG; lam[2] = lamB;
    for (int i = 0; i < 3; ++i) {
        float le = lam[i] * g;                       // emitted wavelength
        float x = HC_K / max(le * T, 1e-12);
        x = min(x, 80.0);
        float xr = HC_K / (lam[i] * TREF);
        float b = pow(lam[i] / le, 5.0) * (exp(xr) - 1.0) / (exp(x) - 1.0);
        col[i] = b;
    }
    return col * pow(g, 3.0);                        // I_obs = g^3 I_em
}

// ------------------------------------------------------------- disk --------
vec4 diskColor(vec3 x, vec3 p, float rc) {
    float r6 = max(rc, 6.02);
    // Novikov-Thorne temperature profile, peak at r = 49/6 M
    float rp = 49.0 / 6.0;
    float T = uTpeak * pow(rp / r6, 0.75)
            * pow((1.0 - sqrt(6.0 / r6)) / (1.0 - sqrt(6.0 / rp)), 0.25);
    float b = x.x * p.y - x.y * p.x;                 // signed L_z / E
    float om = pow(rc, -1.5);                        // Keplerian Omega
    float th = inversesqrt(max(1.0 - 3.0 / rc, 1e-4));
    float g = 1.0 / (th * max(1.0 - b * om, 1e-4));
    vec3 col = planckRGB(T, g);
    float I = pow(T / TREF, 4.0);                    // bolometric radiance
    return vec4(col * I * 2.2, 1.0);
}

// --------------------------------------------------------- geodesics --------
void deriv(vec3 x, vec3 p, float E, out vec3 dx, out vec3 dp) {
    float r = length(x);
    float r3 = r * r * r;
    float r5 = r3 * r * r;
    float px = dot(p, x);
    float a = 1.0 - RS / r;
    float k = RS * px / r3;                          // rs (p.x) / r^3
    float l = 1.5 * RS * px * px / r5;               // 1.5 rs (p.x)^2 / r^5
    float m0 = 0.5 * E * E * RS / (r3 * a * a);      // E^2 rs / (2 r^3 A^2)
    dx = p - (RS * px / r3) * x;
    dp = -m0 * x + k * p - l * x;
}

void project(inout vec3 p, vec3 x, float E) {
    float r = length(x);
    float a = 1.0 - RS / r;
    float pxn = dot(p, x);
    float target = E * E / a + RS * pxn * pxn / (r * r * r);
    float pp = dot(p, p);
    float s = (target > 0.0 && pp > 0.0) ? sqrt(target / pp) : 0.0;
    p *= s;
}

// ------------------------------------------------------------- main --------
void main() {
    vec3 dir = normalize(uFwd + (vUV.x * 2.0 - 1.0) * uRight + (vUV.y * 2.0 - 1.0) * uUp);
    vec3 x = uCamPos;
    vec3 p = dir;
    float E = 1.0;
    project(p, x, E);

    vec3 col = vec3(0.0);
    bool done = false;
    float zPrev = x.z;
    vec3 xPrev = x;

    for (int i = 0; i < uMaxIter && !done; ++i) {
        float r = length(x);
        if (r <= RS * 1.0005) { done = true; break; }        // inside horizon
        if (r > ESC) { col = skyColor(normalize(p)); done = true; break; }

        float h = clamp(uStepBase * pow(r, 1.3), uStepBase * 0.25, 0.16);

        vec3 k1x, k1p, k2x, k2p, k3x, k3p, k4x, k4p;
        deriv(x, p, E, k1x, k1p);
        vec3 x2 = x + 0.5 * h * k1x;
        vec3 p2 = p + 0.5 * h * k1p;
        project(p2, x2, E);
        deriv(x2, p2, E, k2x, k2p);
        x2 = x + 0.5 * h * k2x;
        p2 = p + 0.5 * h * k2p;
        project(p2, x2, E);
        deriv(x2, p2, E, k3x, k3p);
        x2 = x + h * k3x;
        p2 = p + h * k3p;
        project(p2, x2, E);
        deriv(x2, p2, E, k4x, k4p);

        xPrev = x;
        x += h * (k1x + 2.0 * k2x + 2.0 * k3x + k4x) / 6.0;
        p += h * (k1p + 2.0 * k2p + 2.0 * k3p + k4p) / 6.0;
        project(p, x, E);

        float zn = x.z;
        if (zPrev * zn < 0.0) {
            float t = zPrev / (zPrev - zn);
            vec3 xc = xPrev + t * (x - xPrev);
            float rc = length(xc);
            if (rc > DISK_IN && rc < DISK_OUT) {
                col = diskColor(xc, p, rc).rgb;
                done = true;
                break;
            }
        }
        zPrev = zn;
    }
    if (!done) col = skyColor(normalize(p)) * 0.45;   // step budget exhausted
    fragColor = vec4(col, 1.0);
}
