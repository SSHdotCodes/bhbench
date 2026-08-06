#version 330 core
// =============================================================================
//  Schwarzschild black-hole geodesic ray-tracer.
//
//  Photons are integrated through curved spacetime using the exact Binet
//  equation for null geodesics in the Schwarzschild metric,
//
//      d^2 u / dphi^2 + u = (3/2) Rs u^2 ,   u = 1/r ,  Rs = 2GM/c^2
//
//  which is rewritten in Cartesian form as a transverse acceleration
//
//      a = -(3/2) Rs  h^2 / r^5  *  r_vec ,    h = |r x v|  (conserved)
//
//  integrated along the affine parameter (= path length, since |v|=1 for
//  light).  This reproduces the correct first-order light deflection
//  4GM/(c^2 b) and the photon sphere at r = 1.5 Rs.
//
//  Effects modelled:
//    * gravitational lensing of a procedural star field
//    * capture by the event horizon (r < Rs)
//    * thin accretion disk (ISCO r_in = 3 Rs .. r_out) with
//        - Shakura-Sunyaev temperature profile  T ~ r^(-3/4)
//        - Keplerian orbital velocity  v = sqrt(Rs/(2r)) c
//        - relativistic Doppler beaming  I ~ D^4
//        - gravitational redshift        g = sqrt(1 - Rs/r)
//    * photon-ring / Einstein-ring halo (emerges naturally from lensing)
//    * lensed 3D spacetime lattice (visualises curvature from the observer)
// =============================================================================
out vec4 FragColor;
in vec2 vUV;

uniform vec3  uCamPos;
uniform mat3  uCamRot;      // camera basis (columns = right, up, -forward)
uniform float uFocal;       // focal length for perspective
uniform float uRs;          // Schwarzschild radius (event-horizon radius)
uniform vec2  uRes;
uniform float uTime;
uniform int   uMode;        // 0 = scene, 1 = lensed grid, 2 = Flamm (drawn in C++)
uniform bool  uShowDisk;
uniform bool  uShowGrid;
uniform float uDiskInner;
uniform float uDiskOuter;

#define PI 3.14159265358979

// ---------------------------------------------------------------------------
//  Hash / noise helpers
// ---------------------------------------------------------------------------
float hash13(vec3 p3){
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
float hash11(float n){ return fract(sin(n) * 43758.5453123); }

float vnoise(vec3 p){
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f*f*(3.0-2.0*f);
    float n000 = hash13(i+vec3(0,0,0));
    float n100 = hash13(i+vec3(1,0,0));
    float n010 = hash13(i+vec3(0,1,0));
    float n110 = hash13(i+vec3(1,1,0));
    float n001 = hash13(i+vec3(0,0,1));
    float n101 = hash13(i+vec3(1,0,1));
    float n011 = hash13(i+vec3(0,1,1));
    float n111 = hash13(i+vec3(1,1,1));
    float nx00 = mix(n000,n100,f.x);
    float nx10 = mix(n010,n110,f.x);
    float nx01 = mix(n001,n101,f.x);
    float nx11 = mix(n011,n111,f.x);
    return mix(mix(nx00,nx10,f.y), mix(nx01,nx11,f.y), f.z);
}
float fbm(vec3 p){
    float v=0.0, a=0.5;
    for(int i=0;i<5;i++){ v += a*vnoise(p); p*=2.03; a*=0.5; }
    return v;
}

// ---------------------------------------------------------------------------
//  Procedural star field (sampled in the escaped photon direction)
// ---------------------------------------------------------------------------
vec3 starField(vec3 dir){
    vec3 col = vec3(0.0);
    // faint galactic band: glow modulated by fbm around an inclined plane
    float band = exp(-pow(dir.y*2.2, 2.0));
    vec3 n = dir*3.0 + vec3(0.0, uTime*0.005, 0.0);
    float g = fbm(n*1.5);
    col += band * mix(vec3(0.03,0.02,0.06), vec3(0.10,0.09,0.16), g) * 0.6;
    col += fbm(dir*8.0)*0.015;

    // stars at several densities
    for(int k=0;k<3;k++){
        float scale = 60.0 * pow(2.0, float(k));
        vec3 g3 = floor(dir*scale);
        vec3 f  = fract(dir*scale) - 0.5;
        float h = hash13(g3 + float(k)*17.3);
        if(h > 0.965){
            float d = length(f);
            float bright = (h - 0.965)/0.035;
            float star = smoothstep(0.30, 0.0, d) * bright;
            // twinkle
            star *= 0.75 + 0.25*sin(uTime*2.0 + h*60.0);
            vec3 sc = mix(vec3(0.65,0.78,1.0), vec3(1.0,0.82,0.65), hash13(g3+5.0));
            col += sc * star;
        }
    }
    return col;
}

// ---------------------------------------------------------------------------
//  Accretion-disk emission at radius r (Shakura-Sunyaev profile)
//  Returns RGB radiance (pre-beaming) and local temperature.
// ---------------------------------------------------------------------------
vec3 diskColor(float r){
    // T ~ r^(-3/4). Normalise so T=1 at r=r_in.
    float T = pow(uDiskInner / r, 0.75);

    // Blackbody-ish colour ramp from deep red -> orange -> white -> blue-white.
    vec3 c;
    if(T < 0.45f){
        c = mix(vec3(0.35,0.04,0.01), vec3(1.0,0.25,0.05), smoothstep(0.0,0.45,T));
    } else if(T < 0.9f){
        c = mix(vec3(1.0,0.25,0.05), vec3(1.0,0.75,0.35), smoothstep(0.45,0.9,T));
    } else if(T < 1.4f){
        c = mix(vec3(1.0,0.75,0.35), vec3(1.0,0.95,0.85), smoothstep(0.9,1.4,T));
    } else {
        c = mix(vec3(1.0,0.95,0.85), vec3(0.8,0.9,1.0), smoothstep(1.4,2.2,T));
    }

    // Radial intensity (brighter inside, soft edges)
    float edgeIn  = smoothstep(uDiskInner, uDiskInner*1.15, r);
    float edgeOut = 1.0 - smoothstep(uDiskOuter*0.8, uDiskOuter, r);
    // turbulent filamentary structure (orbiting blobs)
    float phi = atan(r); // placeholder, real phi computed by caller via angle
    float I = edgeIn * edgeOut * pow(uDiskInner/r, 2.2f);
    return c * I;
}

// filamentary turbulence based on full position (sheared with Keplerian flow)
float diskTurb(vec3 p, float r){
    float orbT = pow(r/uDiskInner, 1.5);          // Keplerian period ~ r^1.5
    float ang = uTime*0.6 / max(orbT,0.05);
    // rotate p around y by -ang
    float ca=cos(ang), sa=sin(ang);
    vec3 q = vec3(ca*p.x - sa*p.z, p.y, sa*p.x + ca*p.z);
    float t = fbm(q*1.3 + vec3(0.0,uTime*0.2,0.0));
    return 0.6 + 0.9*t;
}

// ---------------------------------------------------------------------------
//  Spacetime-lattice glow: distance to nearest line of a 3D grid (spacing S).
//  Lines are drawn parallel to each axis; the lensing bends them visually.
// ---------------------------------------------------------------------------
float latticeGlow(vec3 p, float S){
    vec3 q = p / S;
    vec3 d = abs(fract(q + 0.5) - 0.5) * S;  // dist to nearest plane per axis
    // distance to nearest line parallel to x = min(d.y,d.z); etc.
    float lx = min(d.y, d.z);
    float ly = min(d.x, d.z);
    float lz = min(d.x, d.y);
    float k = 6.0 / S;
    float gx = exp(-lx*k);
    float gy = exp(-ly*k);
    float gz = exp(-lz*k);
    return gx + gy + gz;
}

// ---------------------------------------------------------------------------
void main(){
    vec2 px = (vUV * 2.0 - 1.0);
    px.x *= uRes.x / uRes.y;

    vec3 ro = uCamPos;
    vec3 rd = normalize(uCamRot * vec3(px, -uFocal));

    // ---- conserved specific angular momentum (about BH at origin) ----
    vec3 Lvec = cross(ro, rd);
    float h2 = dot(Lvec, Lvec);

    vec3 pos = ro;
    vec3 vel = rd;
    vec3 prevPos = pos;

    vec3  accumCol   = vec3(0.0);
    float transmittance = 1.0;
    bool  captured   = false;

    // adaptive step: smaller near the hole where curvature is strong
    const int   STEPS = 460;
    const float ESC_R = 60.0;

    for(int i=0; i<STEPS; i++){
        float r = length(pos);

        // event horizon
        if(r < uRs){ captured = true; break; }
        // escaped to infinity
        if(r > ESC_R && dot(vel, pos) > 0.0) break;

        // ---- accretion disk: equatorial plane crossing (y=0) ----
        if(uShowDisk && (prevPos.y * pos.y) < 0.0){
            float tc = prevPos.y / (prevPos.y - pos.y);
            vec3 hp = mix(prevPos, pos, tc);
            float hr = length(hp.xz);
            if(hr > uDiskInner && hr < uDiskOuter){
                // Keplerian orbital velocity (in units of c): v = sqrt(Rs/(2r))
                float beta = sqrt(uRs / (2.0*hr));
                // prograde tangential direction = (-z,x,0)/r
                vec3 tan = normalize(vec3(-hp.z, 0.0, hp.x));
                vec3 vsource = beta * tan;
                // photon direction at emission = vel (toward camera is -vel)
                vec3 nObs = -normalize(vel);
                float gamma = 1.0 / sqrt(1.0 - beta*beta);
                float doppler = 1.0 / (gamma * (1.0 - dot(vsource, nObs)));
                float gravRed = sqrt(1.0 - uRs / hr);
                float shift   = gravRed * doppler;          // total freq. shift g
                float boost   = pow(clamp(shift,0.0,4.0), 4.0);  // I ~ g^4

                vec3 base = diskColor(hr) * diskTurb(hp, hr);
                vec3 emit = base * boost;
                // local optical depth (thin-ish disk)
                float dens = clamp(length(base)*1.4, 0.0, 0.85);
                emit *= dens;
                accumCol += transmittance * emit;
                transmittance *= (1.0 - dens);
            }
        }

        // ---- lensed spacetime lattice (mode 1) ----
        if(uShowGrid){
            float stepLen = length(pos - prevPos);
            float g = latticeGlow(pos, 1.0);
            // tint by axis-ish hue using position; fade with distance
            float fade = 1.0 / (1.0 + 0.06*r);
            vec3 gcol = mix(vec3(0.1,0.5,1.0), vec3(0.2,0.9,0.7), 0.5+0.5*sin(r*0.5));
            accumCol += transmittance * gcol * g * stepLen * 1.2 * fade;
        }

        // ---- geodesic step ----
        prevPos = pos;
        // acceleration from the Binet-equation Cartesian form
        float r5 = r*r*r*r*r;
        vec3 acc = -1.5 * uRs * h2 / r5 * pos;
        // adaptive substep: finer near hole
        float dt = clamp(0.04 * (r / (r + uRs*2.0)) * 1.6 + 0.02, 0.012, 0.10);
        vel += acc * dt;
        vel = normalize(vel);        // light keeps |v| = c
        pos += vel * dt;
    }

    vec3 col;
    if(captured){
        col = accumCol;              // anything in front of the horizon remains
    } else {
        vec3 bg = starField(normalize(vel));
        col = accumCol + transmittance * bg;
    }

    // mild tonemap + vignette
    col = col / (1.0 + col);
    col = pow(col, vec3(1.0/2.2));
    float vig = smoothstep(1.5, 0.2, length(px));
    col *= 0.5 + 0.5*vig;

    FragColor = vec4(col, 1.0);
}
