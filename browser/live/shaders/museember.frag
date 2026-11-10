#version 300 es
precision highp float;
precision highp int;
// ==========================================================================
// Schwarzschild null-geodesic ray tracer -- Cartesian Hamiltonian form
// --------------------------------------------------------------------------
// Schwarzschild in Cartesian coordinates (G=c=M=1, rs=2M):
//   ds^2 = -f dt^2 + (delta_ij + (rs/r)/f n_i n_j) dx^i dx^j,
//   f = 1-rs/r, n = x/r.  Contravariant spatial metric:
//   g^{ij} = delta^{ij} - (rs/r) n^i n^j.
// Hamilton's equations for photon covariant momentum p_i (p_t = -E const):
//   dx^i/dl = g^{ij} p_j = p_i - (rs/r) n^i (n.p)
//   dp_i/dl = (rs/r^3) [ p_i (x.p) - 3/2 x_i (x.p)^2 / r^2 ]
// (from dp_i/dl = -1/2 (dg^{jk}/dx^i) p_j p_k with
//  g^{jk} = delta^{jk} - rs x^j x^k / r^3).
// This chart is regular everywhere except the horizon itself: there is no
// polar-axis singularity, so rays passing near the axis are integrated as
// accurately as any others (the spherical-coordinate version of these
// equations has an integrable 1/sin^2(theta) phi-spin singularity at the
// poles which corrupts exactly those rays).  E is an exact constant of
// the motion; the null constraint H=0 holds by construction of the
// initial data.  Integration: RK4 with step size shrinking towards the
// horizon, h ~ rs * clamp(0.03*(r/rs-0.7), 0.02, 0.6).
// Initial data: at the camera, choose E so a static observer measures
// photon energy 1 (E = sqrt(f_cam)); the contravariant spatial momentum
// k^i is built so its components in the static observer's orthonormal
// frame (radial component scaled by 1/sqrt(f)) equal the pixel ray
// direction D exactly.  p_i = g_ij k^j then follows.
// ==========================================================================
in vec2 vUV;
out vec4 FragColor;

uniform float uTime;
uniform vec2  uResolution;
uniform float uAspect;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanFov;
uniform float uRs;          // 2.0 (M=1)
uniform float uEscapeR;     // > camera distance: ray considered escaped
uniform float uDiskBrightness;
uniform int   uInnerGlow;
uniform int   uSteps;       // RK4 steps per pixel
uniform float uTimeScale;   // visualisation time dilution (see README)
uniform int   uDebugMode;   // 0 normal, 1 background-only, 2 encode final direction

const float PI = 3.141592653589793;

// ---- hashing / noise -------------------------------------------------------
float hash11(float p){ p = fract(p*0.1031); p *= p+33.33; p *= p+p; return fract(p); }
vec3 hash33(vec3 p){
  p = fract(p*vec3(0.1031,0.1030,0.0973));
  p += dot(p, p.yxz+33.33);
  return fract((p.xxy+p.yxx)*p.zyx);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  vec2 u = f*f*(3.0-2.0*f);
  float a = hash11(dot(i, vec2(127.1,311.7)));
  float b = hash11(dot(i+vec2(1.,0.), vec2(127.1,311.7)));
  float c = hash11(dot(i+vec2(0.,1.), vec2(127.1,311.7)));
  float d = hash11(dot(i+vec2(1.,1.), vec2(127.1,311.7)));
  return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
}
float fbm(vec2 p){
  float v = 0.0, a = 0.5;
  for(int i=0;i<3;i++){ v += a*vnoise(p); p = p*2.03 + vec2(19.7,7.3); a *= 0.5; }
  return v;
}
// Approximate blackbody chromaticity (Tanner Helland fit), T in kelvin.
vec3 blackbody(float T){
  T = clamp(T, 800.0, 12000.0);
  float t = T/100.0;
  float r = t <= 66.0 ? 255.0 : 329.698727446 * pow(t-60.0, -0.1332047592);
  float g = t <= 66.0 ? 99.4708025861*log(max(t,1.0)) - 161.1195681661 : 288.1221695283*pow(t-60.0,-0.0755148492);
  float b = t >= 66.0 ? 255.0 : (t <= 19.0 ? 0.0 : 138.5177312231*log(max(t-10.0,1.0)) - 305.0447927307);
  vec3 c = clamp(vec3(r,g,b)/255.0, 0.0, 1.0);
  c = pow(c, vec3(1.4));
  return c;
}
vec3 starLayer(vec3 dir, float scale, float thresh, float bright){
  vec3 q = dir*scale;
  vec3 id = floor(q);
  vec3 f = fract(q)-0.5;
  vec3 rnd = hash33(id);
  vec3 centre = (rnd-0.5)*0.7;
  float d = length(f-centre);
  float star = smoothstep(0.16, 0.0, d) * step(thresh, rnd.x);
  vec3 tint = mix(vec3(0.75,0.85,1.0), vec3(1.0,0.85,0.65), rnd.y);
  return tint * star * bright * (0.35+0.65*rnd.z);
}
vec3 background(vec3 dir){
  vec3 n = normalize(vec3(0.28, 0.92, 0.27));
  float band = exp(-pow(dot(dir,n)*2.4, 2.0));
  vec3 col = vec3(0.003, 0.004, 0.009);
  col += vec3(0.03,0.06,0.11) * band * fbm(dir.xz*2.5 + dir.yy*1.7);
  col += vec3(0.06,0.03,0.07) * band * fbm(dir.xy*3.0+7.0);
  col += starLayer(dir,  38.0, 0.985, 0.55);
  col += starLayer(dir,  90.0, 0.993, 0.90);
  col *= 1.0 + 1.6*band;
  return col;
}

// ---- Cartesian geodesic state ----------------------------------------------
struct State { vec3 x; vec3 p; };
struct Deriv { vec3 dx; vec3 dp; };
Deriv deriv(State s, float rs){
  float r = max(length(s.x), rs*0.51);
  vec3 n = s.x / r;
  float xp = dot(s.x, s.p);
  Deriv d;
  d.dx = s.p - (rs/r) * n * dot(n, s.p);
  d.dp = (rs/(r*r*r)) * (s.p*xp - 1.5 * s.x * xp*xp/(r*r));
  return d;
}
State addScaled(State s, Deriv d, float h){
  return State(s.x + d.dx*h, s.p + d.dp*h);
}
State rk4(State s, float h, float rs){
  Deriv k1 = deriv(s, rs);
  Deriv k2 = deriv(addScaled(s,k1,h*0.5), rs);
  Deriv k3 = deriv(addScaled(s,k2,h*0.5), rs);
  Deriv k4 = deriv(addScaled(s,k3,h),     rs);
  Deriv k;
  k.dx = (k1.dx + 2.0*k2.dx + 2.0*k3.dx + k4.dx)/6.0;
  k.dp = (k1.dp + 2.0*k2.dp + 2.0*k3.dp + k4.dp)/6.0;
  return addScaled(s, k, h);
}

// ---- accretion disk shading -------------------------------------------------
vec4 diskShading(float r, float ph, vec3 pCross, float E, float rs, float rIn, float rOut, float fCam){
  float M = rs*0.5;
  float omega = sqrt(M / pow(r, 3.0));
  float phRot = ph - omega * uTime * uTimeScale;
  vec2 diskXY = vec2(cos(phRot), sin(phRot)) * r;
  float turb = fbm(diskXY*0.55 + vec2(0.0, uTime*0.015));
  float turb2 = fbm(diskXY*1.7 - vec2(uTime*0.02, 0.0));
  float rings = 0.85 + 0.15*sin(r*3.1 + turb2*4.0);
  float T_emit = 10500.0 * pow(r/rIn, -0.75) * (0.82 + 0.36*turb);
  // g = (k.u_obs)/(k.u_emit) with u_emit = ut*(1, r*omega*e_phi) at equator:
  // k.u_emit = ut*(-E + r*omega*(p.e_phi)); Lz = r*(p.e_phi) there.
  float ut = 1.0 / sqrt(max(1.0 - 3.0*M/r, 0.02));
  vec3 ephi = vec3(-sin(ph), 0.0, cos(ph));
  float pephi = dot(pCross, ephi);
  float denom = ut * (1.0 - r*omega*pephi/max(E,1e-6));
  denom = clamp(denom, 0.25, 4.0);
  float g = (1.0 / sqrt(max(fCam, 0.05))) / denom;
  g = clamp(g, 0.25, 3.5);
  float T_obs = T_emit * g;
  float flux = pow(rIn/r, 3.0) * (1.0 - sqrt(rIn/r));
  float intensity = flux * 42.0 * pow(g, 3.0) * (0.55 + 0.85*turb) * rings;
  vec3 col = blackbody(T_obs) * intensity * uDiskBrightness;
  col += blackbody(12000.0) * exp(-(r-rIn)/(0.12*rs)) * 2.2 * pow(g,3.0) * uDiskBrightness;
  float alpha = 1.0 - smoothstep(rOut*0.70, rOut, r);
  return vec4(col, alpha);
}

vec3 ace(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }

void main(){
  vec2 ndc = vUV*2.0 - 1.0;
  ndc.x *= uAspect;
  vec3 D = normalize(uCamFwd + uTanFov*(uCamRight*ndc.x + uCamUp*ndc.y));

  float rs = uRs;
  float rIn = 3.0*rs;
  float rOut = 9.5*rs;

  vec3 x0 = uCamPos;
  float r0 = length(x0);
  vec3 n0 = x0 / r0;
  float f0 = 1.0 - rs/r0;
  float fCam = f0;
  float Dr = dot(D, n0);
  vec3 Dt = D - Dr*n0;
  vec3 k0 = Dt + sqrt(max(f0,1e-4))*Dr*n0;
  float a0 = (rs/r0)/max(f0,1e-4);
  State s;
  s.x = x0;
  s.p = k0 + a0*n0*dot(n0,k0);
  float E = sqrt(max(f0,1e-4));

  vec3 glow = vec3(0.0);
  bool captured = false, escaped = false, hitDisk = false;
  vec4 diskCol = vec4(0.0);
  State prev = s;

  // WebGL2 requires a constant loop bound. 280 is the submitted maximum; uSteps still stops the ray.
  for(int i=0; i<280; i++){
    if(i >= uSteps) break;
    prev = s;
    float rNow = length(s.x);
    float h = rs * clamp(0.040 * (rNow/rs - 0.7), 0.020, 0.85);
    s = rk4(s, h, rs);

    // Equatorial-plane (y=0) crossing?
    if(prev.x.y * s.x.y < 0.0){
      float a = prev.x.y/(prev.x.y - s.x.y);
      vec3 xCross = mix(prev.x, s.x, a);
      float rCross = length(xCross);
      if(rCross > rIn && rCross < rOut && uDebugMode == 0){
        vec3 pCross = mix(prev.p, s.p, a);
        float phCross = atan(xCross.z, xCross.x);
        diskCol = diskShading(rCross, phCross, pCross, E, rs, rIn, rOut, fCam);
        hitDisk = true;
        break;
      }
    }

    float rCur = length(s.x);
    if(uInnerGlow == 1 && uDebugMode == 0 && rCur < rIn && rCur > rs*1.05){
      float eq = exp(-pow((s.x.y/max(rCur,1e-4))/0.22, 2.0));
      float dens = eq * (1.0 - rs/rCur) * pow(rIn/rCur, 1.5);
      glow += vec3(1.0, 0.78, 0.5) * dens * h * 0.035;
    }

    if(rCur < rs*1.02){ captured = true; break; }
    if(rCur > uEscapeR){ escaped = true; break; }
  }

  float rf = max(length(s.x), rs*1.01);
  vec3 nf = s.x / rf;
  float ff = 1.0 - rs/rf;
  vec3 kf = s.p - (rs/rf) * nf * dot(nf, s.p);
  float nk = dot(nf, kf);
  vec3 Dfinal = normalize( (kf - nk*nf) + (nk/sqrt(max(ff,1e-4)))*nf );

  vec3 col;
  if(uDebugMode == 2){
    col = Dfinal*0.5 + 0.5;
  } else if(hitDisk){
    vec3 bg = (diskCol.a < 0.999) ? background(Dfinal) : vec3(0.0);
    col = mix(bg, diskCol.rgb, diskCol.a) + glow*0.35;
  } else if(captured){
    col = glow;
  } else {
    col = background(Dfinal) * sqrt(max(fCam, 0.05)) + glow;
    if(!escaped) col += vec3(0.9, 0.8, 0.6) * 0.04;
  }
  col = min(col, vec3(4.0));

  col = ace(col * 1.15);
  col = pow(col, vec3(1.0/2.2));
  vec2 q = vUV*2.0-1.0;
  col *= 1.0 - 0.22*dot(q,q);
  FragColor = vec4(col, 1.0);
}
