#version 300 es
precision highp float;
precision highp int;
out vec4 FragColor;
in vec2 vUV;
uniform vec2  uRes;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform int   uSteps;      // geodesic integration steps
uniform float uShowDisk;   // 0/1
uniform float uLensing;    // 0/1 (0 = straight rays, for comparison)
uniform float uExposure;

// ---- Schwarzschild geometry, units: Rs = 1 (G=c=1, M=1/2) ----
// Event horizon r=1, photon sphere r=1.5, ISCO r=3, disk [3, 9]
const float RS = 1.0;
const float DISK_IN = 3.0;
const float DISK_OUT = 9.0;

float hash13(vec3 p){ p=fract(p*0.1031); p+=dot(p,p.zyx+31.32); return fract((p.x+p.y)*p.z); }
vec3 hash33(vec3 p){ p=fract(p*vec3(0.1031,0.1030,0.0973)); p+=dot(p,p.yxz+33.33); return fract((p.xxy+p.yxx)*p.zyx); }
float vnoise(vec3 p){
  vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  float n000=hash13(i), n100=hash13(i+vec3(1,0,0)), n010=hash13(i+vec3(0,1,0)), n110=hash13(i+vec3(1,1,0));
  float n001=hash13(i+vec3(0,0,1)), n101=hash13(i+vec3(1,0,1)), n011=hash13(i+vec3(0,1,1)), n111=hash13(i+vec3(1,1,1));
  return mix(mix(mix(n000,n100,f.x),mix(n010,n110,f.x),f.y),mix(mix(n001,n101,f.x),mix(n011,n111,f.x),f.y),f.z);
}
float fbm(vec3 p){ float v=0.0,a=0.5; for(int i=0;i<5;i++){ v+=a*vnoise(p); p*=2.03; a*=0.55; } return v; }

// Blackbody-ish colour from temperature in arbitrary units (0..1.6)
vec3 blackbody(float t){
  t = clamp(t, 0.0, 1.6);
  vec3 c1=vec3(0.08,0.01,0.0);   // deep red
  vec3 c2=vec3(0.85,0.18,0.02);  // orange-red
  vec3 c3=vec3(1.00,0.55,0.12);  // orange
  vec3 c4=vec3(1.00,0.88,0.62);  // warm white
  vec3 c5=vec3(0.75,0.86,1.00);  // blue-white (hottest)
  vec3 col = mix(c1,c2,smoothstep(0.0,0.35,t));
  col = mix(col,c3,smoothstep(0.35,0.70,t));
  col = mix(col,c4,smoothstep(0.70,1.05,t));
  col = mix(col,c5,smoothstep(1.05,1.60,t));
  return col;
}

vec3 background(vec3 d){
  // Distant starfield: sharp points from 3D cell hashing
  vec3 col = vec3(0.002,0.003,0.008);
  // Milky-way band (tilted plane through direction)
  float band = exp(-pow(abs(dot(d, normalize(vec3(0.3,1.0,0.2)))),2.0)*6.0);
  col += vec3(0.04,0.05,0.08)*band*(0.5+0.5*fbm(d*6.0));
  for(int layer=0; layer<2; layer++){
    float scale = layer==0 ? 28.0 : 55.0;
    vec3 cell = floor(d*scale);
    vec3 rnd = hash33(cell);
    vec3 starPos = (rnd-0.5)*0.75;
    vec3 local = fract(d*scale)-0.5;
    float dist = length(local-starPos);
    float bright = smoothstep(0.13,0.0,dist);
    float isStar = step(0.94, hash13(cell+float(layer)*17.0));
    vec3 tint = mix(vec3(0.7,0.8,1.0), vec3(1.0,0.85,0.65), rnd.x);
    col += tint * bright * isStar * (1.2+rnd.y*2.0) * (layer==0?1.0:0.55);
  }
  // Faint nebula
  col += vec3(0.05,0.02,0.09)*pow(fbm(d*3.0+7.0),2.0)*0.45;
  col += vec3(0.00,0.05,0.07)*pow(fbm(d*2.0-3.0),2.0)*0.3;
  return col;
}

// Disk shading at intersection point p, photon dir v (travelling direction)
vec4 shadeDisk(vec3 p, vec3 v, float r){
  float phi = atan(p.z, p.x);
  // Keplerian angular velocity for Schwarzschild (M=1/2): Omega = sqrt(M/r^3)
  float omega = sqrt(0.5 / (r*r*r));
  // Differential rotation: wind the pattern backwards by Omega*t
  float rot = phi + uTime * omega * 18.0; // scaled for visibility
  // Turbulence in co-rotating frame: radius + wound angle
  float wound = rot * 3.0 - r*1.8; // spiral winding
  vec3 np = vec3(cos(wound), sin(wound), r*0.55) * 1.4;
  np += vec3(r*0.9, rot*1.2, uTime*0.03);
  float turb = fbm(np);
  float turb2 = fbm(np*2.7 + 4.7);
  float density = smoothstep(DISK_IN, DISK_IN+0.55, r) * smoothstep(DISK_OUT, DISK_OUT-2.2, r);
  density *= 0.55 + 0.75*turb + 0.35*turb2;
  // Novikov-Thorne: T^4 ~ (1/r^3)(1 - sqrt(rin/r)), normalised
  float tempProfile = (pow(DISK_IN/r, 0.75)) * pow(max(1.0 - sqrt(DISK_IN/r), 0.0), 0.25) * 2.1;
  // --- Relativistic beaming + redshift ---
  // Orbital velocity of emitter (circular geodesic, static observer): beta = 1/sqrt(2(r-1))
  float beta = clamp(1.0 / sqrt(2.0*(r-1.0)), 0.0, 0.68);
  vec3 phiHat = normalize(vec3(-p.z, 0.0, p.x));
  vec3 betaVec = phiHat * beta;
  float gamma = 1.0 / sqrt(1.0-beta*beta);
  // Photon direction as seen: use -v as direction toward observer approx via emission angle
  float dop = gamma * (1.0 + dot(betaVec, normalize(v))); // v points away from camera along ray
  // More standard: approaching side has photon emitted opposite motion -> beamed brighter.
  // dot(betaVec, v): on approaching side v has component along beta -> use 1/(gamma*(1-...)) form:
  float gDop = 1.0 / max(gamma * (1.0 - dot(betaVec, -v)), 0.05);
  // Gravitational redshift: g_grav = sqrt(1 - Rs/r)
  float gGrav = sqrt(max(1.0 - RS/r, 0.0));
  float g = clamp(gGrav * gDop, 0.25, 3.2);
  float temp = tempProfile * g;         // observed temperature shifted by g
  float intensity = density * pow(g, 3.0) * 1.25; // I_obs = g^3 I_emit (Liouville)
  vec3 col = blackbody(temp*0.95) * intensity * (1.05 + tempProfile*0.85);
  // Thin bright inner rim near ISCO
  col += vec3(1.0,0.75,0.45) * exp(-abs(r-DISK_IN-0.12)*7.0) * 1.2 * pow(g,3.0);
  float alpha = clamp(density*1.15, 0.0, 1.0);
  return vec4(col, alpha);
}

vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }

void main(){
  vec2 uv = (gl_FragCoord.xy*2.0 - uRes) / uRes.y;
  vec3 ro = uCamPos;
  vec3 rd = normalize(uCamFwd + uv.x*uCamRight + uv.y*uCamUp);

  vec3 p = ro;
  vec3 v = rd;
  // Specific angular momentum squared h^2 = |p x v|^2 (conserved along geodesic)
  vec3 hVec = cross(p, v);
  float h2 = dot(hVec, hVec);

  vec3 col = vec3(0.0);
  float transmittance = 1.0;
  bool captured = false;
  float minR = 1e5;
  float prevY = p.y;
  vec3 prevP = p;

  // Geodesic integration: d^2x/dlambda^2 = -3/2 * h^2 * x / |x|^5  (Schwarzschild null, Rs=1)
  // Leapfrog (kick-drift-kick) — symplectic, stable for photon orbits.
  for(int i=0; i<400; i++){
    if(i>=uSteps) break;
    float r2 = dot(p,p);
    float r = sqrt(r2);
    minR = min(minR, r);
    if(r < RS){ captured = true; break; }
    if(r > 55.0) break;
    // Adaptive step: small near hole / photon sphere, large far away
    float dt = clamp(0.10 * (r-0.85), 0.015, 0.55);
    if(uLensing < 0.5) dt = 0.35;
    vec3 acc = vec3(0.0);
    if(uLensing > 0.5){
      acc = -1.5 * h2 * p / pow(r2, 2.5);
    }
    // Leapfrog
    v += acc * (dt*0.5);
    prevP = p; prevY = p.y;
    p += v * dt;
    float r2b = dot(p,p);
    vec3 acc2 = vec3(0.0);
    if(uLensing > 0.5) acc2 = -1.5 * h2 * p / pow(r2b, 2.5);
    v += acc2 * (dt*0.5);
    v = normalize(v); // keep null (numerical drift guard); direction only matters

    // Disk crossing: sign change of y between prevP and p
    if(uShowDisk > 0.5 && prevY * p.y < 0.0){
      float f = prevY / (prevY - p.y);
      vec3 hit = mix(prevP, p, f);
      float hr = length(hit.xz);
      // Also require |hit| in 3D close to plane radius (hit.y ~ 0 by construction)
      float hr3 = length(hit);
      if(hr > DISK_IN && hr < DISK_OUT && transmittance > 0.02){
        vec4 disk = shadeDisk(hit, v, hr);
        // Higher-order (lensed) images are dimmer — natural via transmittance falloff
        col += transmittance * disk.rgb;
        transmittance *= (1.0 - disk.a * 0.92);
        // Nudge past plane to avoid immediate re-hit
        p += v * dt * 0.6;
      }
    }
  }

  // Rays still trapped near the photon sphere when the step budget ends are captured
  if(!captured && length(p) < 2.6) captured = true;
  if(!captured){
    col += transmittance * background(normalize(v));
    // Photon ring: only rays that ESCAPE after skimming r~1.5 form the thin ring.
    // Captured rays stay exactly black (true shadow).
    float ring = exp(-pow((minR-1.52)*5.5, 2.0));
    col += vec3(1.0,0.62,0.28) * ring * 0.55 * transmittance;
    float halo = exp(-max(minR-1.0,0.0)*0.55) * 0.07;
    col += vec3(1.0,0.55,0.22) * halo;
  }
  // Event-horizon shadow is pure black where captured (col stays 0 for those rays
  // except disk contributions in front of the hole, already accumulated).

  col *= uExposure;
  col = aces(col);
  col = pow(col, vec3(1.0/2.2));
  FragColor = vec4(col, 1.0);
}
