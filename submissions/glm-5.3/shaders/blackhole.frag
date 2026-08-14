#version 410 core
// ============================================================================
//  Schwarzschild black hole — real-time null-geodesic ray tracer
//
//  Units: Schwarzschild radius rs = 1, G = c = 1  =>  M = rs/2 = 1/2.
//
//  Photon trajectories: exact shape of null geodesics follows from the Binet
//  equation  u'' + u = (3/2) rs u^2  (u = 1/r), which in Cartesian form is
//      d2x/dl2 = -(3/2) rs h^2 x / r^5 ,   h = |x x v| = const.
//  This reproduces the photon sphere at 1.5 rs, the capture shadow with
//  critical impact parameter b = 3*sqrt(3) M ~ 2.598 rs, Einstein rings and
//  all higher-order lensed images.  Integrated with velocity-Verlet.
//
//  Accretion disk: geometrically thin Keplerian (Shakura-Sunyaev) disk,
//  r_in = ISCO = 3 rs, T(r) ~ r^-3/4 (1 - sqrt(r_in/r))^1/4.
//  Orbital speed beta = sqrt(M/(r-2M)) => 0.5 c at the ISCO.
//  Emission uses I_nu/nu^3 invariance: observed temperature g*T, bolometric
//  intensity ~ g^4 T^4, with g = sqrt(1-rs/r) * sqrt(1-b^2)/(1-b.n) including
//  the coordinate -> local-static-frame aberration.  Planck colors.
//
//  Spacetime grid: exact Flamm paraboloid embedding of the equatorial slice,
//      z(r) = 2 sqrt(rs (r - rs)),  r >= rs   (the "trapdoor in spacetime"),
//  with ISCO and photon-sphere reference circles, rendered through the same
//  geodesic tracer so the grid itself is gravitationally lensed.
// ============================================================================
out vec4 outColor;

uniform vec2  uRes;
uniform float uAnim;         // simulation time (disk rotation, infall)
uniform vec3  uCamPos;
uniform mat3  uCamBasis;     // columns: right, up, forward
uniform float uTanHalfFov;
uniform int   uMode;         // 0 = realistic, 1 = spacetime grid, 2 = combined
uniform int   uDiskOn;
uniform int   uStarsOn;
uniform int   uSteps;
uniform float uDtScale;
uniform float uExposure;
uniform float uDiskTemp;     // peak disk temperature [K]

const float PI    = 3.14159265358979;
const float RS    = 1.0;
const float M     = 0.5;
const float RIN   = 3.0;      // ISCO = 6M = 3 rs
const float ROUT  = 14.0;
const float RESC  = 70.0;     // escape radius
const float BCRIT = 2.59807621135332;  // 3*sqrt(3)*M  (shadow impact parameter)
const float GRIDY = -2.7;     // vertical offset of the embedding funnel

// ---------------------------------------------------------------- utilities
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.yzx + 33.33);
                      return fract((p.x + p.y)*p.z); }
vec3  hash33(vec3 p){ p = fract(p*vec3(0.1031, 0.1030, 0.0973));
                      p += dot(p, p.yxz + 33.33);
                      return fract((p.xxy + p.yxx)*p.zyx); }

float vnoise(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1,0,0));
    float n010 = hash13(i + vec3(0,1,0));
    float n110 = hash13(i + vec3(1,1,0));
    float n001 = hash13(i + vec3(0,0,1));
    float n101 = hash13(i + vec3(1,0,1));
    float n011 = hash13(i + vec3(0,1,1));
    float n111 = hash13(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm(vec3 p){
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++){ s += a*vnoise(p); p = p*2.03 + 17.13; a *= 0.5; }
    return s/0.9375;
}

// ------------------------------------------------- Planck blackbody -> RGB
// Tanner Helland's multi-lobe fit of the Planck spectrum (1000..40000 K),
// normalized to unit max component (chromaticity only; luminance is physical
// and handled separately as ~ T^4).
vec3 blackbody(float K){
    K = clamp(K, 1000.0, 40000.0);
    float t = K*0.01;
    float r = (t <= 66.0) ? 1.0 : 1.29293618606*pow(t - 60.0, -0.1332047592);
    float g = (t <= 66.0) ? 0.39008157916*log(t) - 0.63184344224
                          : 1.12989084509*pow(t - 60.0, -0.0755148492);
    float b = (t >= 66.0) ? 1.0 :
              ((t <= 19.0) ? 0.0 : 0.54320678027*log(t - 10.0) - 1.19625408914);
    vec3 c = clamp(vec3(r, g, b), 0.0, 1.0);
    return c / max(max(c.r, c.g), max(c.b, 1e-4));
}

// ------------------------------------------------------------- star field
vec3 stars(vec3 d){
    vec3 col = vec3(0.0);
    for (int L = 0; L < 2; L++){
        float s  = (L == 0) ? 26.0 : 95.0;
        float k  = (L == 0) ? 230.0 : 90.0;
        float br = (L == 0) ? 3.2 : 2.2;
        vec3 q   = d*s;
        vec3 id  = floor(q);
        vec3 f   = q - id;
        vec3 sp  = 0.15 + 0.7*hash33(id);
        float dd = length(f - sp);
        float mag = pow(hash13(id + 7.31), 14.0);
        vec3 tint = mix(vec3(0.72, 0.82, 1.0), vec3(1.0, 0.86, 0.72),
                        hash13(id + 3.17));
        col += tint*(br*mag)*exp(-dd*dd*k);
    }
    // faint galactic band
    vec3 ax = normalize(vec3(0.32, 1.0, 0.24));
    float bandv = dot(d, ax);
    float band  = exp(-bandv*bandv*16.0);
    float neb   = fbm(d*5.0)*0.55 + fbm(d*14.0)*0.30;
    col += vec3(0.55, 0.68, 1.0)*band*neb*0.16;
    col += vec3(1.0, 0.82, 0.60)*band*band*neb*neb*0.35;
    return col;
}

// ------------------------------ accretion disk emission at a crossing point
vec3 diskShade(vec3 hp, vec3 dirCamToScene, out float alpha){
    float r = length(hp.xz);

    // Keplerian circular geodesic (Schwarzschild)
    float beta = sqrt(M/max(r - 2.0*M, 0.08));   // local orbital speed (c=1)
    float Om   = sqrt(M/(r*r*r));                // angular velocity dphi/dt
    vec3  rh   = normalize(vec3(hp.x, 0.0, hp.z));
    vec3  phih = vec3(-rh.z, 0.0, rh.x);         // prograde orbital direction

    // photon direction toward camera, in the local static observer's frame
    // (radial coordinate distances are stretched by 1/g0)
    vec3  n  = -dirCamToScene;
    float g0 = sqrt(max(1.0 - RS/r, 0.0));
    float nr = dot(n, rh);
    vec3  nt = n - nr*rh;
    vec3  nloc = normalize(nt + rh*(nr/g0));

    // special-relativistic Doppler x gravitational redshift
    float dop = sqrt(1.0 - beta*beta)/max(1.0 - beta*dot(nloc, phih), 0.05);
    float g   = g0*dop;

    // Shakura-Sunyaev temperature profile, normalized to peak = uDiskTemp
    float prof = pow(r/RIN, -0.75)*pow(max(1.0 - sqrt(RIN/r), 0.0), 0.25)
               * 1.6206;
    float Tem = uDiskTemp*prof;

    // turbulent density, differentially rotated by the Keplerian Omega(r)
    float ang = Om*uAnim;
    float ca = cos(ang), sa = sin(ang);
    vec3 q = vec3(hp.x*ca + hp.z*sa, 0.0, -hp.x*sa + hp.z*ca);
    float n1 = fbm(vec3(q.x*0.85, r*1.8, q.z*0.85));
    float edge = smoothstep(RIN, RIN + 0.4, r)
               * (1.0 - smoothstep(ROUT - 3.5, ROUT, r));
    float dens = edge*(0.35 + 0.9*n1);

    alpha = clamp(dens*2.0, 0.0, 1.0);

    float TL = g*Tem;                       // observed temperature
    float L  = pow(max(TL, 0.0)/6500.0, 4.0)*2.2*dens;  // g^4 T^4
    return blackbody(TL)*L;
}

// --------------------------- spacetime grid (Flamm paraboloid) shading
float funnelY(float r){ return GRIDY - 2.0*sqrt(max(r - RS, 0.0)); }

vec3 gridShade(vec3 hp, vec3 ro){
    float r    = length(hp.xz);
    float phi  = atan(hp.z, hp.x);
    float dist = length(hp - ro);
    float w    = max(0.014, 0.0045*dist);       // world-space line half-width
    float g    = sqrt(max(1.0 - RS/r, 0.03));   // gravitational dimming

    // meridians (constant phi), every 15 degrees
    float dphi   = abs(fract(phi*(24.0/(2.0*PI)) + 0.5) - 0.5)*(2.0*PI/24.0);
    float linePhi = 1.0 - smoothstep(w*0.35, w, dphi*max(r, 1.3));

    // circles (constant r), 1 rs apart, arc length measured along surface
    float slope  = sqrt(1.0 + 1.0/max(r - 1.0, 0.03));   // |dz/dr| of funnel
    float dr     = abs(fract(r + 0.5) - 0.5);
    float lineR  = 1.0 - smoothstep(w*0.35, w, dr*slope);

    float grid = max(linePhi, lineR);

    // matter streaks streaming down the funnel (free-fall-like speed)
    float vff = 0.55/sqrt(max(r, 1.05));
    float fall = fract(r*0.9 + uAnim*vff);
    grid *= 0.75 + 0.25*smoothstep(0.35, 0.95, fall);

    // reference circles: ISCO (3 rs) and photon sphere (1.5 rs), hot lip
    float risco = 1.0 - smoothstep(w*1.2, w*2.4, abs(r - 3.0));
    float rps   = 1.0 - smoothstep(w*1.0, w*2.2, abs(r - 1.5));
    float lip   = exp(-abs(r - 1.18)*3.0);

    vec3 col = vec3(0.30, 1.0, 1.0)*grid*0.85;
    col += vec3(0.20, 0.90, 1.0)*risco*1.2;
    col += vec3(1.0, 0.80, 0.35)*rps*1.2;
    col += vec3(1.0, 0.90, 0.80)*lip*1.4;

    float depth = smoothstep(-10.0, -3.0, hp.y);   // dim with depth
    col *= mix(0.25, 1.0, depth);
    col *= g;
    return col;
}

// ------------------------------------------------------------- integrator
vec3 render(vec3 ro, vec3 rd){
    vec3 p = ro;
    vec3 v = rd;
    vec3 hv = cross(p, v);
    float h2 = dot(hv, hv);       // conserved square of the impact parameter
    float b  = sqrt(h2);

    vec3  col = vec3(0.0);
    float T   = 1.0;              // transmittance
    bool  escaped = false;

    bool  gridOn = (uMode == 1 || uMode == 2);
    bool  diskOn = (uDiskOn == 1);
    float diskGhost = (uMode == 1) ? 0.35 : 1.0;
    float diskAlpha = (uMode == 1) ? 0.45 : 1.0;
    float fPrev = gridOn ? (p.y - funnelY(length(p.xz))) : 1.0;

    for (int i = 0; i < 1024; i++){
        if (i >= uSteps) break;
        float r2 = dot(p, p);
        float r  = sqrt(r2);
        if (r < 1.035) break;                                 // horizon
        if (r > RESC && dot(p, v) > 0.0){ escaped = true; break; }

        float dt = uDtScale*clamp(0.15*(r - 0.7), 0.02, 1.5);

        // velocity-Verlet step of  a = -(3/2) rs h^2 x / r^5
        vec3 a1 = (-1.5*h2)*p/(r2*r2*r);
        vec3 pn = p + v*dt + a1*(0.5*dt*dt);
        float rn2 = dot(pn, pn);
        vec3 a2 = (-1.5*h2)*pn/(rn2*rn2*sqrt(rn2));
        vec3 vn = v + (a1 + a2)*(0.5*dt);

        // --- equatorial-plane crossing: accretion disk (front and all
        //     higher-order lensed images alike)
        if (diskOn && (p.y*pn.y) < 0.0){
            float f = p.y/(p.y - pn.y);
            vec3 hp = mix(p, pn, f);
            float hr = length(hp.xz);
            if (hr > RIN && hr < ROUT){
                float alpha;
                vec3 e = diskShade(hp, normalize(mix(v, vn, f)), alpha);
                col += T*e*diskGhost;
                T   *= 1.0 - clamp(alpha*diskAlpha, 0.0, 1.0);
                if (T < 0.02) break;
            }
        }

        // --- embedding-funnel crossing: lensed spacetime grid
        if (gridOn){
            float fn = pn.y - funnelY(length(pn.xz));
            if (fPrev*fn < 0.0){
                float f = fPrev/(fPrev - fn);
                vec3 hp = mix(p, pn, f);
                float hr = length(hp.xz);
                if (hr > 1.12 && hr < 46.0) col += T*gridShade(hp, ro);
            }
            fPrev = fn;
        }

        p = pn; v = vn;
    }

    if (escaped){
        float dim = (uMode == 1) ? 0.45 : 1.0;
        if (uStarsOn == 1) col += T*stars(normalize(v))*dim;
    } else {
        // rays inside b_crit are captured: physically the shadow interior is
        // black; keep only a faint rim near b_crit (light skimming the
        // photon sphere before plunging) to mark the edge
        float ring = exp(-abs(b - BCRIT)*2.2);
        col += T*blackbody(uDiskTemp*0.8)*ring*0.10;
    }
    return col;
}

vec3 aces(vec3 x){
    return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0);
}

void main(){
    vec2 uv = (2.0*gl_FragCoord.xy - uRes)/uRes.y;
    vec3 rd = normalize(uCamBasis*vec3(uv*uTanHalfFov, 1.0));
    vec3 col = render(uCamPos, rd);
    col = aces(col*uExposure);
    col = pow(col, vec3(1.0/2.2));
    col += (hash13(vec3(gl_FragCoord.xy, uAnim)) - 0.5)/255.0;  // dither
    outColor = vec4(col, 1.0);
}
