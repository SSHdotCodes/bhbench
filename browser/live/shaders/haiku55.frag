#version 300 es
precision highp float;
precision highp int;
#define OUT_ESCAPED 0
#define OUT_CAPTURED 1
#define OUT_DISK 2
#define OUT_UNRESOLVED 3
struct TraceUniforms {float a,r_plus,r_capture,r_isco,r_out,eta,r_escape,cam_ut,disk_gain,T_peak,exposure;int lut_n,max_steps,bg_mode,disk_on;};
uniform TraceUniforms U;
uniform sampler2D lut; uniform vec3 camPos,camRight,camUp,camFwd; uniform vec2 resolution;
in vec2 vUV; out vec4 fragColor;
vec3 v3(float x,float y,float z){return vec3(x,y,z);}
float dot3(vec3 a,vec3 b){return dot(a,b);}float len3(vec3 a){return length(a);}vec3 norm3(vec3 a){return normalize(a);}
// Phase-space point of a photon: position x and covariant spatial momentum p_i (p_t = +1 implicitly).
 struct Phase {
    vec3 x;
    vec3 p;
};



// Metric pieces and their first derivatives at one Cartesian point.
 struct KSData {
    float r;         // Kerr-Schild radius
    float H;         // scalar function H
    vec3 l;      // covariant spatial part of l_mu (l_t = 1)
    vec3 dr;     // d r / d x^i
    vec3 dH;     // d H / d x^i (total derivative, includes the r dependence)
    mat3 dl;  // dl[i][j] = d l_j / d x^i
};

// Evaluates the Kerr-Schild radius, H, l_mu and all first derivatives analytically.
// Divisions are shared through four reciprocals (r, D, Q, S) because this runs four times per RK4 step.
 KSData ks_eval(float a, vec3 x) {
    KSData k;
    float a2 = a * a;
    float z2 = x.z * x.z;
    float rho2 = x.x * x.x + x.y * x.y;
    float A = rho2 + z2 - a2;                                  // R^2 - a^2
    float r2 = float(0.5) * (A + sqrt(A * A + float(4) * a2 * z2));
    float r = sqrt(r2);
    float ir = float(1) / r;
    float ir2 = ir * ir;
    float r4 = r2 * r2;
    float D = r2 + a2;                                         // r^2 + a^2
    float iD = float(1) / D;
    float Q = r4 + a2 * z2;                                    // r^4 + a^2 z^2
    float iQ = float(1) / Q;
    float S = r * rho2 * iD * iD + z2 * ir * ir2;              // -(1/2) dF/dr for F = rho^2/D + z^2/r^2 - 1
    float iS = float(1) / S;

    k.r = r;
    k.H = float(2) * r * r2 * iQ;
    k.dr[0] = x.x * iD * iS;
    k.dr[1] = x.y * iD * iS;
    k.dr[2] = x.z * ir2 * iS;

    float iQ2 = iQ * iQ;
    float dHdr = float(2) * r2 * (float(3) * a2 * z2 - r4) * iQ2;
    float dHdz = -float(4) * a2 * x.z * r * r2 * iQ2;
    for (int i = 0; i < 3; ++i) {
        k.dH[i] = dHdr * k.dr[i] + (i == 2 ? dHdz : float(0));
    }

    float nx = r * x.x + a * x.y;                              // D * l_x
    float ny = r * x.y - a * x.x;                              // D * l_y
    k.l[0] = nx * iD;
    k.l[1] = ny * iD;
    k.l[2] = x.z * ir;

    for (int i = 0; i < 3; ++i) {
        float dD = float(2) * r * k.dr[i];                         // d D / d x^i
        float dnx = x.x * k.dr[i] + (i == 0 ? r : float(0)) + (i == 1 ? a : float(0));
        float dny = x.y * k.dr[i] + (i == 1 ? r : float(0)) - (i == 0 ? a : float(0));
        // d(num/D)/dx = (d num - num dD / D) / D
        k.dl[i][0] = (dnx - nx * dD * iD) * iD;
        k.dl[i][1] = (dny - ny * dD * iD) * iD;
        k.dl[i][2] = (i == 2 ? ir : float(0)) - x.z * k.dr[i] * ir2;
    }
    return k;
}

// Kerr-Schild radius only (cheap; used for horizon and disk tests).
 float ks_radius(float a, vec3 x) {
    float a2 = a * a;
    float A = x.x * x.x + x.y * x.y + x.z * x.z - a2;
    float r2 = float(0.5) * (A + sqrt(A * A + float(4) * a2 * x.z * x.z));
    return sqrt(r2);
}

// Hamiltonian flow of h = (1/2) g^{mu nu} p_mu p_nu with p_t = 1:
//   dx^i/dlambda = p_i - H l_i q,          q = l^mu p_mu = -1 + l_j p_j
//   dp_i/dlambda = (1/2) [ dH/dx^i q^2 + 2 H q p_j dl_j/dx^i ]
 Phase ray_rhs(float a, Phase s) {
    KSData k = ks_eval(a, s.x);
    float q = float(-1) + k.l[0] * s.p.x + k.l[1] * s.p.y + k.l[2] * s.p.z;
    Phase d;
    d.x = v3(s.p.x - k.H * k.l[0] * q, s.p.y - k.H * k.l[1] * q, s.p.z - k.H * k.l[2] * q);
    float pdl[3];
    for (int i = 0; i < 3; ++i) {
        pdl[i] = s.p.x * k.dl[i][0] + s.p.y * k.dl[i][1] + s.p.z * k.dl[i][2];
    }
    d.p = v3(float(0.5) * (k.dH[0] * q * q + float(2) * k.H * q * pdl[0]),
             float(0.5) * (k.dH[1] * q * q + float(2) * k.H * q * pdl[1]),
             float(0.5) * (k.dH[2] * q * q + float(2) * k.H * q * pdl[2]));
    return d;
}

// Classical fourth-order Runge-Kutta step of length h in the affine parameter, given k1 = ray_rhs(s).
 Phase phase_add(Phase a, Phase b){return Phase(a.x+b.x,a.p+b.p);}
Phase phase_scale(float h, Phase a){return Phase(h*a.x,h*a.p);}
Phase rk4_step_k1(float a, Phase s, Phase k1, float h){
 Phase k2=ray_rhs(a,phase_add(s,phase_scale(0.5*h,k1)));
 Phase k3=ray_rhs(a,phase_add(s,phase_scale(0.5*h,k2)));
 Phase k4=ray_rhs(a,phase_add(s,phase_scale(h,k3)));
 return phase_add(s,phase_scale(h/6.0,phase_add(phase_add(k1,phase_scale(2.0,k2)),phase_add(phase_scale(2.0,k3),k4))));
}
Phase rk4_step(float a, Phase s, float h) {
    return rk4_step_k1(a, s, ray_rhs(a, s), h);
}

// Adaptive affine step. Two limits, both scaled by eta:
//   position: the step moves the photon about eta * |x| in space (coordinate speed |dx/dlambda| grows near the hole);
//   momentum: the step changes the covariant momentum by about eta * |p| (dp/dlambda grows like |p|^2 H
//             for infalling photons, which is where an unscaled step loses the null condition).
 float step_length(float eta, float R, Phase s, Phase k1) {
    float speed = len3(k1.x);
    float hx = R / (speed > float(0.1) ? speed : float(0.1));
    float pmag = len3(s.p);
    float dpmag = len3(k1.p);
    float hp = (pmag > float(0.1) ? pmag : float(0.1)) / (dpmag > float(1e-9) ? dpmag : float(1e-9));
    float h = hx < hp ? hx : hp;
    return eta * h;
}

// Null residual g^{mu nu} p_mu p_nu. Must stay at zero along an exact photon path.
 float null_residual(float a, Phase s) {
    KSData k = ks_eval(a, s.x);
    float q = float(-1) + k.l[0] * s.p.x + k.l[1] * s.p.y + k.l[2] * s.p.z;
    return float(-1) + dot3(s.p, s.p) - k.H * q * q;
}

// Conserved z-angular momentum per unit energy of the photon (E = 1), physical sign.
 float photon_L(Phase s) { return s.x.y * s.p.x - s.x.x * s.p.y; }

// Covector lowering: c_mu = g_{mu nu} c^nu, given the contravariant 4-vector (c0, c_spatial).
 vec4 lower4(KSData k, float c0, vec3 cs){
 float lam=c0+dot(k.l,cs); return vec4(-c0+k.H*lam,cs+k.H*k.l*lam);
}
float g_dot(KSData k, vec4 a, vec4 b){return dot(a,lower4(k,b.x,b.yzw));}
struct Observer {vec3 pos; vec4 u; vec4 e[3]; float ut;};
Observer make_observer(float a,vec3 pos,vec3 right,vec3 up,vec3 fwd){
 Observer o; KSData k=ks_eval(a,pos); float ut=1.0/sqrt(1.0-k.H);
 o.pos=pos;o.ut=ut;o.u=lower4(k,ut,vec3(0));
 vec3 basis[3]=vec3[3](right,up,fwd);vec4 ec[3];
 for(int j=0;j<3;j++){
  vec4 c=vec4(dot(o.u.yzw,basis[j])*ut,basis[j]);
  for(int m=0;m<j;m++) c-=g_dot(k,c,ec[m])*ec[m];
  ec[j]=c/sqrt(g_dot(k,c,c));
 }
 for(int j=0;j<3;j++) o.e[j]=lower4(k,ec[j].x,ec[j].yzw);
 return o;
}
Phase pixel_ray(Observer o,float sx,float sy){
 float inv=1.0/sqrt(sx*sx+sy*sy+1.0);
 vec4 p=-o.u+sx*inv*o.e[0]+sy*inv*o.e[1]+inv*o.e[2];
 return Phase(o.pos,p.yzw/p.x);
}
// Prograde equatorial circular photon orbit (Bardeen 1972): r = 2 [1 + cos((2/3) arccos(-a))].
 float photon_orbit_prograde_radius(float a) {
    return float(2) * (float(1) + cos(float(2.0 / 3.0) * acos(-a)));
}

// Radius inside which a photon is declared captured. Escaping photons cannot turn around inside the
// smallest unstable circular photon orbit, so r < r_capture implies capture. Stopping here avoids the
// momentum growth near the horizon, which is physical in these coordinates but costly to resolve.
 float capture_radius(float a) {
    float rp = float(1) + sqrt(float(1) - a * a);
    return rp + float(0.5) * (photon_orbit_prograde_radius(a) - rp);
}

 struct TraceParams {
    float a;
    float r_plus;
    float r_capture;
    float r_isco;
    float r_out;
    float eta;
    float r_escape;
    float cam_ut;
    int max_steps;
};

 struct Hit {
    int outcome;
    vec3 dir;   // escaped: asymptotic direction the traced photon travels in (the sky direction)
    float r;         // disk: emission radius
    float g;         // disk: redshift factor E_obs / E_emit
    float L;         // disk: photon L/E at emission
    int steps;
};

// Interpolated normalised flux F(r)/F_max from a table on the uniform grid [r0, r1].
 float disk_flux_norm(sampler2D lut,int n,float r,float r0,float r1){
 float u=clamp((r-r0)/(r1-r0)*float(n-1),0.0,float(n-1));int i=int(u);float f=u-float(i);
 return mix(texelFetch(lut,ivec2(i,0),0).r,texelFetch(lut,ivec2(min(i+1,n-1),0),0).r,f);
}
// Traces one photon back from the observer. Returns where it ends: escaped to infinity, captured by
// the horizon, or its first crossing of the equatorial disk at r in [r_isco, r_out].
 Hit trace_photon(TraceParams  tp, Phase s) {
    Hit result;
    result.outcome = OUT_UNRESOLVED;
    result.dir = v3(float(0), float(0), float(0));
    result.r = float(0);
    result.g = float(0);
    result.L = float(0);
    result.steps = 0;
    float a2 = tp.a * tp.a;
    for (int n = 0; n < tp.max_steps; ++n) {
        result.steps = n;
        float R = len3(s.x);
        if (ks_radius(tp.a, s.x) < tp.r_capture) {
            result.outcome = OUT_CAPTURED;
            return result;
        }
        if (R > tp.r_escape && dot3(s.x, s.p) > float(0)) {
            result.outcome = OUT_ESCAPED;
            result.dir = norm3(s.p);
            return result;
        }
        Phase k1 = ray_rhs(tp.a, s);
        float h = step_length(tp.eta, R, s, k1);
        Phase next = rk4_step_k1(tp.a, s, k1, h);
        bool up = s.x.z > float(0);
        if (up != (next.x.z > float(0))) {
            // The photon crossed the equatorial plane inside this step: bisect on the sub-step length.
            float lo = float(0);
            float hi = float(1);
            for (int it = 0; it < 16; ++it) {
                float mid = float(0.5) * (lo + hi);
                Phase m = rk4_step(tp.a, s, mid * h);
                if ((m.x.z > float(0)) == up) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            Phase hitp = rk4_step(tp.a, s, hi * h);
            float rho2 = hitp.x.x * hitp.x.x + hitp.x.y * hitp.x.y;
            float r2h = rho2 - a2;
            if (r2h > float(0)) {
                float rh = sqrt(r2h);
                if (rh >= tp.r_isco && rh <= tp.r_out) {
                    // Circular prograde equatorial orbit at rh: Omega, u^t from Bardeen-Press-Teukolsky.
                    float r32 = rh * sqrt(rh);
                    float Om = float(1) / (r32 + tp.a);
                    float ut = (r32 + tp.a) / (sqrt(rh) * sqrt(sqrt(rh)) * sqrt(r32 - float(3) * sqrt(rh) + float(2) * tp.a));
                    float L = photon_L(hitp);
                    result.outcome = OUT_DISK;
                    result.r = rh;
                    result.L = L;
                    result.g = tp.cam_ut / (ut * (float(1) - Om * L));
                    result.steps = n;
                    return result;
                }
            }
        }
        s = next;
    }
    return result;
}

float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float hash_unit(uint h) { return float(h & 0xFFFFFFu) * (1.0 / 16777216.0); }

// Linear-sRGB colour of a blackbody at temperature Tk, normalised to unit luminance.
vec3 blackbody_rgb(float Tk) {
    float t = clampf(Tk, 1000.0, 40000.0) / 100.0;
    float r = 255.0;
    float g;
    float b;
    if (t > 66.0) {
        r = 329.698727446 * pow(t - 60.0, -0.1332047592);
        g = 288.1221695283 * pow(t - 60.0, -0.0755148492);
        b = 255.0;
    } else {
        g = 99.4708025861 * log(t) - 161.1195681661;
        b = t <= 19.0 ? 0.0 : 138.5177312231 * log(t - 10.0) - 305.0447927307;
    }
    r = clampf(r, 0.0, 255.0) / 255.0;
    g = clampf(g, 0.0, 255.0) / 255.0;
    b = clampf(b, 0.0, 255.0) / 255.0;
    // sRGB -> linear (gamma 2.2 approximation), then unit luminance.
    r = pow(r, 2.2);
    g = pow(g, 2.2);
    b = pow(b, 2.2);
    float lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    vec3 c = v3(r / lum, g / lum, b / lum);
    return c;
}

// Background sky for an escaped photon whose asymptotic direction is d (unit vector).
// Stars are placed on a cube-map grid of cells; the Milky Way is a broad band around a galactic plane.
// The optional grid is a latitude/longitude grid on the sky sphere, which the lensing visibly bends.
vec3 background(vec3 d, int mode) {
    vec3 col = v3(0.0, 0.0, 0.0);

    if (mode == 0 || mode == 2) {
        // Stars on latitude/longitude bins of 0.6 degrees. A bin holds a star with probability
        // proportional to sin(theta), so the density is uniform on the sky (no pole crowding).
        float step = 0.0104719755;   // 0.6 degrees
        float theta = acos(clampf(d.z, -1.0, 1.0));
        float phi = atan(d.y, d.x);
        int bt = int(floor(theta / step));
        int bp = int(floor(phi / step));
        for (int dt = -1; dt <= 1; ++dt) {
            for (int dp = -1; dp <= 1; ++dp) {
                int it = bt + dt;
                int ip = bp + dp;
                uint seed = hash_u32(uint(it) * 0x9E3779B1u ^ hash_u32(uint(ip) * 0x85EBCA77u + 0x632BE5ABu));
                uint h1 = hash_u32(seed + 1u);
                uint h2 = hash_u32(seed + 2u);
                uint h3 = hash_u32(seed + 3u);
                uint h4 = hash_u32(seed + 4u);
                uint h5 = hash_u32(seed + 5u);
                float thc = (float(it) + 0.5) * step;
                if (hash_unit(h1) > 0.035 * sin(clampf(thc, 0.0, 3.14159))) {
                    continue;
                }
                // Star position inside its bin.
                float ts = (float(it) + hash_unit(h2)) * step;
                float ps = (float(ip) + hash_unit(h3)) * step;
                vec3 s = v3(sin(ts) * cos(ps), sin(ts) * sin(ps), cos(ts));
                float omc = 1.0 - (s.x * d.x + s.y * d.y + s.z * d.z);   // about theta^2/2
                float sigma2 = 0.0000045;                          // (0.12 deg)^2
                float mag = 0.12 + 2.6 * pow(hash_unit(h4), 10.0);
                float tStar = 3300.0 + 9000.0 * hash_unit(h5);
                vec3 c = blackbody_rgb(tStar);
                float w = exp(-omc / sigma2);
                col = col + (mag * w) * c;
            }
        }
        // Milky Way band: broad Gaussian in the angle to a galactic plane.
        vec3 ng = norm3(v3(-0.42, 0.31, 0.85));
        float sinb = d.x * ng.x + d.y * ng.y + d.z * ng.z;
        float band = exp(-(sinb * sinb) / 0.012);
        // Smooth mottling along the band (no cell structure).
        float mottle = 0.65 + 0.35 * sin(5.0 * d.x + 1.7) * cos(4.0 * d.y - 0.6) * sin(3.5 * d.z + 2.2);
        col = col + (0.045 * band * mottle) * v3(0.85, 0.9, 1.0);
    }

    if (mode == 1 || mode == 2) {
        // Latitude/longitude grid every 15 degrees on the celestial sphere.
        float theta = acos(clampf(d.z, -1.0, 1.0));
        float phi = atan(d.y, d.x);
        float step = 0.2617993878;   // 15 degrees
        float ft = theta / step;
        float dt = abs(ft - floor(ft + 0.5)) * step;
        float fp = phi / step;
        float dp = abs(fp - floor(fp + 0.5)) * step * clampf(sin(theta), 0.05, 1.0);
        float lineT = exp(-(dt * dt) / (0.0045 * 0.0045));
        float lineP = exp(-(dp * dp) / (0.0045 * 0.0045));
        float lines = clampf(lineT + lineP, 0.0, 1.0);
        col = col + (0.12 * lines) * v3(0.35, 0.8, 1.0);
    }
    return col;
}

// Radiance of the disk at emission radius r with redshift g.
 vec3 disk_radiance(TraceUniforms  U, sampler2D lut, float r, float g) {
    float F = disk_flux_norm(lut, U.lut_n, r, U.r_isco, U.r_out);   // F / F_max
    float Tem = U.T_peak * pow(F, 0.25);                            // emitted temperature
    float Tobs = g * Tem;                                            // observed temperature
    vec3 col = blackbody_rgb(Tobs);
    float g2 = g * g;
    float I = U.disk_gain * g2 * g2 * F;                             // I_obs = g^4 I_emit
    return I * col;
}

// Final colour of a traced pixel before tone mapping.
 vec3 shade_hit(TraceUniforms  U, sampler2D lut, Hit  h) {
    vec3 c = v3(0.0, 0.0, 0.0);
    if (h.outcome == OUT_ESCAPED) {
        vec3 d = v3(float(h.dir.x), float(h.dir.y), float(h.dir.z));
        c = background(d, U.bg_mode);
    } else if (h.outcome == OUT_DISK && U.disk_on != 0) {
        c = disk_radiance(U, lut, float(h.r), float(h.g));
    }
    return c;
}

void main(){
 Observer obs=make_observer(U.a,camPos,camRight,camUp,camFwd);
 vec2 nd=vUV*2.0-1.0;
 Phase ray=pixel_ray(obs,nd.x*resolution.x/resolution.y*0.466307658,nd.y*0.466307658);
 TraceParams tp=TraceParams(U.a,U.r_plus,U.r_capture,U.r_isco,U.r_out,U.eta,U.r_escape,obs.ut,U.max_steps);
 Hit hit=trace_photon(tp,ray);vec3 c=shade_hit(U,lut,hit);
 c=1.0-exp(-U.exposure*c);fragColor=vec4(pow(clamp(c,0.0,1.0),vec3(1.0/2.2)),1.0);
}
