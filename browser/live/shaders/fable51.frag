#version 300 es
precision highp float;
precision highp int;

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uRes;
uniform vec2 uJitter;
uniform float uSpin;
uniform float uCamR;
uniform float uCamTh;
uniform float uCamPh;
uniform float uTanHalf;
uniform float uTimeCoord;
uniform float uDiskOut;
uniform float uGridOut;
uniform float uGridSpacing;
uniform float uEps;
uniform float uEpsAng;
uniform float uTurbulence;
uniform float uTempScale;
uniform float uSkyBrightness;
uniform int uDiskOn;
uniform int uGridOn;
uniform int uStarsOn;
uniform int uLimbOn;
uniform int uMaxSteps;

const float PI = 3.14159265358979323846;
const float TAU = 6.28318530717958647692;
const float TINY = 1.0e-7;

float gA;
float gE;
float gL;
float gRh;
float gISCO;

struct State {
  float r;
  float th;
  float ph;
  float t;
  float pr;
  float pth;
};

struct Deriv {
  float r;
  float th;
  float ph;
  float t;
  float pr;
  float pth;
};

float hash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

vec3 hash33(vec3 p3) {
  p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + 33.33);
  return fract((p3.xxy + p3.yxx) * p3.zyx);
}

float vnoise(vec3 x) {
  vec3 i = floor(x);
  vec3 f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash11(dot(i + vec3(0,0,0), vec3(1.0,57.0,113.0)));
  float n100 = hash11(dot(i + vec3(1,0,0), vec3(1.0,57.0,113.0)));
  float n010 = hash11(dot(i + vec3(0,1,0), vec3(1.0,57.0,113.0)));
  float n110 = hash11(dot(i + vec3(1,1,0), vec3(1.0,57.0,113.0)));
  float n001 = hash11(dot(i + vec3(0,0,1), vec3(1.0,57.0,113.0)));
  float n101 = hash11(dot(i + vec3(1,0,1), vec3(1.0,57.0,113.0)));
  float n011 = hash11(dot(i + vec3(0,1,1), vec3(1.0,57.0,113.0)));
  float n111 = hash11(dot(i + vec3(1,1,1), vec3(1.0,57.0,113.0)));
  return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
             mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
}

float fbm(vec3 p) {
  float s = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; ++i) {
    s += a * vnoise(p);
    p = p * 2.03 + vec3(17.1, 9.2, 13.7);
    a *= 0.5;
  }
  return s;
}

vec3 blackbody(float kelvin) {
  float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
  float r;
  float g;
  float b;
  if (t <= 66.0) {
    r = 1.0;
    g = clamp((99.4708025861 * log(t) - 161.1195681661) / 255.0, 0.0, 1.0);
    b = t <= 19.0 ? 0.0 : clamp((138.5177312231 * log(t - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
  } else {
    r = clamp((329.698727446 * pow(t - 60.0, -0.1332047592)) / 255.0, 0.0, 1.0);
    g = clamp((288.1221695283 * pow(t - 60.0, -0.0755148492)) / 255.0, 0.0, 1.0);
    b = 1.0;
  }
  return vec3(r, g, b);
}

float horizonRadius(float a) {
  return 1.0 + sqrt(max(0.0, 1.0 - a * a));
}

float photonOrbit(float a) {
  return 2.0 * (1.0 + cos((2.0 / 3.0) * acos(clamp(-abs(a), -1.0, 1.0))));
}

float iscoRadius(float a) {
  float z1 = 1.0 + pow(max(0.0, 1.0 - a*a), 1.0/3.0) *
    (pow(1.0 + a, 1.0/3.0) + pow(1.0 - a, 1.0/3.0));
  float z2 = sqrt(3.0*a*a + z1*z1);
  return 3.0 + z2 - sign(a + 1.0e-8) * sqrt(max(0.0, (3.0-z1)*(3.0+z1+2.0*z2)));
}

Deriv deriv(State y, out float H) {
  float r2 = y.r * y.r;
  float sth = sin(y.th);
  float cth = cos(y.th);
  float s2 = max(sth * sth, 1.0e-9);
  float sigma = r2 + gA*gA*cth*cth;
  float delta = r2 - 2.0*y.r + gA*gA;
  float P = (r2 + gA*gA)*gE - gA*gL;
  float Ls = gL - gA*gE*s2;
  float F = delta*y.pr*y.pr + y.pth*y.pth - P*P/delta + Ls*Ls/s2;
  H = F / (2.0 * sigma);

  float dDelta = 2.0*y.r - 2.0;
  float dP = 2.0*y.r*gE;
  float dFdr = dDelta*y.pr*y.pr - (2.0*P*dP*delta - P*P*dDelta)/(delta*delta);
  float ds2 = 2.0*sth*cth;
  float dLs = -gA*gE*ds2;
  float dWdth = (2.0*Ls*dLs*s2 - Ls*Ls*ds2)/(s2*s2);

  Deriv d;
  d.r = delta * y.pr;
  d.th = y.pth;
  d.ph = gA*P/delta + Ls/s2;
  d.t = (r2 + gA*gA)*P/delta + gA*Ls;
  d.pr = -0.5*dFdr + 2.0*y.r*H;
  d.pth = -0.5*dWdth - 2.0*gA*gA*cth*sth*H;
  return d;
}

State advance(State y, Deriv d, float h) {
  y.r += h*d.r;
  y.th += h*d.th;
  y.ph += h*d.ph;
  y.t += h*d.t;
  y.pr += h*d.pr;
  y.pth += h*d.pth;
  return y;
}

State rk4(State y, Deriv k1, float h) {
  float h2;
  Deriv k2 = deriv(advance(y, k1, 0.5*h), h2);
  Deriv k3 = deriv(advance(y, k2, 0.5*h), h2);
  Deriv k4 = deriv(advance(y, k3, h), h2);
  State z;
  z.r = y.r + h*(k1.r + 2.0*k2.r + 2.0*k3.r + k4.r)/6.0;
  z.th = y.th + h*(k1.th + 2.0*k2.th + 2.0*k3.th + k4.th)/6.0;
  z.ph = y.ph + h*(k1.ph + 2.0*k2.ph + 2.0*k3.ph + k4.ph)/6.0;
  z.t = y.t + h*(k1.t + 2.0*k2.t + 2.0*k3.t + k4.t)/6.0;
  z.pr = y.pr + h*(k1.pr + 2.0*k2.pr + 2.0*k3.pr + k4.pr)/6.0;
  z.pth = y.pth + h*(k1.pth + 2.0*k2.pth + 2.0*k3.pth + k4.pth)/6.0;
  return z;
}

float stepSize(State y, Deriv d) {
  float tiny = 1.0e-12;
  float escale = abs(gE) + 0.001;
  float hr = uEps * (max(y.r - gRh, 0.0) + 0.05) / (abs(d.r) + tiny);
  float hth = uEpsAng / (abs(d.th) + tiny);
  float hph = uEpsAng / (abs(d.ph) + tiny);
  float hpr = uEps * (abs(y.pr) + escale) / (abs(d.pr) + tiny);
  float hpt = uEps * (abs(y.pth) + escale*(1.0+y.r)) / (abs(d.pth) + tiny);
  float h = min(hr, min(hth, min(hph, min(hpr, hpt))));
  if (gL != 0.0) {
    float hpole = 0.5*abs(sin(y.th))/(abs(d.th)+tiny);
    h = min(h, hpole);
  }
  return h;
}

void zamo(float r, float th, out float alpha, out float omega) {
  float s = sin(th);
  float sigma = r*r + gA*gA*cos(th)*cos(th);
  float delta = r*r - 2.0*r + gA*gA;
  float A = (r*r + gA*gA)*(r*r + gA*gA) - gA*gA*delta*s*s;
  alpha = sqrt(max(delta*sigma/A, 0.0));
  omega = 2.0*gA*r/A;
}

float diskRedshift(float r) {
  float sr = sqrt(max(r, 1.0e-6));
  float r32 = r*sr;
  float omega = 1.0/(r32 + gA);
  float rad = max(r32 - 3.0*sr + 2.0*gA, 1.0e-8);
  float ut = (r32 + gA)/(pow(r, 0.75)*sqrt(rad));
  return 1.0/max(ut*(gE - omega*gL), 1.0e-6);
}

float ptTerm(float x, float x0, float xi, float xj, float xk, float a) {
  float ratio = max((x-xi)/(x0-xi), 1.0e-7);
  return 3.0*(xi-a)*(xi-a)/(xi*(xi-xj)*(xi-xk))*log(ratio);
}

float pageThorneFlux(float r, float a) {
  if (r <= gISCO * 1.00005) return 0.0;
  float x = sqrt(r);
  float x0 = sqrt(gISCO);
  if (abs(a) < 0.002) {
    return max(0.0, (1.0-sqrt(gISCO/r))/(r*r*r));
  }
  float q = acos(clamp(a, -1.0, 1.0))/3.0;
  float x1 = 2.0*cos(q-PI/3.0);
  float x2 = 2.0*cos(q+PI/3.0);
  float x3 = -2.0*cos(q);
  float bracket = x-x0-1.5*a*log(x/x0)
    - ptTerm(x,x0,x1,x2,x3,a)
    - ptTerm(x,x0,x2,x1,x3,a)
    - ptTerm(x,x0,x3,x1,x2,a);
  float den = pow(x,4.0) * max(x*x*x - 3.0*x + 2.0*a, 1.0e-8);
  return max(0.0, (3.0/(8.0*PI))*bracket/den);
}

vec3 shadeDisk(float r, float ph, float dt, float g, float mu) {
  float flux = pageThorneFlux(r, gA);
  float temp = uTempScale * pow(max((8.0*PI/3.0)*flux, 0.0), 0.25);
  float omega = 1.0/(r*sqrt(r)+gA);
  float psi = ph - omega*(uTimeCoord-dt);
  vec3 p = vec3(r*0.46*cos(psi), r*0.46*sin(psi), 0.12*uTimeCoord);
  float turb = mix(1.0, 0.52 + 1.02*fbm(p), uTurbulence);
  float rings = 0.88 + 0.12*sin(18.0*log(max(r/gISCO, 1.0)) + 2.0*fbm(p*1.7));
  float edgeIn = smoothstep(gISCO, gISCO*1.045, r);
  float edgeOut = 1.0-smoothstep(uDiskOut*0.91, uDiskOut, r);
  float tobs = max(900.0, temp*g*mix(0.78, 1.15, turb));
  float intensity = min(28.0, 0.62*pow(tobs/9000.0, 4.0));
  float limb = uLimbOn != 0 ? (0.5 + 0.75*clamp(mu,0.0,1.0)) : 1.0;
  return blackbody(tobs)*intensity*turb*rings*edgeIn*edgeOut*limb;
}

float gridLine(float q, float width) {
  float d = abs(fract(q+0.5)-0.5);
  return 1.0-smoothstep(width, width*2.2, d);
}

vec3 shadeGrid(float r, float ph, float g) {
  float spacing = max(uGridSpacing, 0.25);
  float radial = gridLine(r/spacing, 0.045);
  float arcLen = max(r, 1.0)*TAU/spacing;
  float angular = gridLine(ph*arcLen/TAU, 0.04);
  float line = max(radial, angular);
  float nearHorizon = exp(-3.0*max(r-gRh, 0.0));
  vec3 base = blackbody(6500.0*clamp(g,0.18,2.2));
  return line*base*(0.32 + 1.65*nearHorizon)*vec3(0.42,0.72,1.35);
}

vec3 skyColor(vec3 d) {
  d = normalize(d);
  vec3 col = vec3(0.0018,0.0025,0.0065);
  float band = exp(-pow(abs(dot(d, normalize(vec3(0.15,0.44,0.885))))/0.115, 1.35));
  float dust = fbm(d*7.0 + vec3(2.0,7.0,13.0));
  col += band*mix(vec3(0.016,0.024,0.055), vec3(0.09,0.052,0.025), dust)*uSkyBrightness;
  if (uStarsOn == 0) return col;

  vec3 ad = abs(d);
  vec2 uv;
  float face;
  if (ad.x >= ad.y && ad.x >= ad.z) {
    uv = d.yz/ad.x;
    face = d.x > 0.0 ? 0.0 : 1.0;
  } else if (ad.y >= ad.z) {
    uv = d.xz/ad.y;
    face = d.y > 0.0 ? 2.0 : 3.0;
  } else {
    uv = d.xy/ad.z;
    face = d.z > 0.0 ? 4.0 : 5.0;
  }
  float density = 155.0;
  vec2 cell = floor((uv*0.5+0.5)*density);
  vec2 local = fract((uv*0.5+0.5)*density);
  float pixelAngle = max(2.0*uTanHalf/uRes.y, 0.00025);
  for (int oy=-1; oy<=1; ++oy) {
    for (int ox=-1; ox<=1; ++ox) {
      vec2 off = vec2(float(ox),float(oy));
      vec3 h = hash33(vec3(cell+off, face*17.0));
      if (h.x < 0.60) continue;
      vec2 delta = off+h.yz-local;
      float ang = length(delta)*(2.0/density);
      float sigma = pixelAngle*mix(0.55,1.25,h.z);
      float star = exp(-0.5*ang*ang/(sigma*sigma));
      float bright = 0.6 + 6.5*pow(h.x,9.0);
      float temp = mix(2800.0,11000.0,hash11(h.x*913.0+face));
      col += blackbody(temp)*star*bright*uSkyBrightness;
    }
  }
  return col;
}

void main() {
  gA = clamp(uSpin, 0.0, 0.998);
  gRh = horizonRadius(gA);
  gISCO = iscoRadius(gA);

  vec2 p = ((gl_FragCoord.xy + uJitter)/uRes)*2.0-1.0;
  float aspect = uRes.x/uRes.y;
  vec3 n = normalize(vec3(-1.0, -p.y*uTanHalf, p.x*uTanHalf*aspect));
  float mr = -n.x;
  float mth = -n.y;
  float mph = -n.z;

  float r = uCamR;
  float th = clamp(uCamTh, 0.002, PI-0.002);
  float sth = sin(th);
  float cth = cos(th);
  float sigma = r*r + gA*gA*cth*cth;
  float delta = r*r - 2.0*r + gA*gA;
  float A = (r*r+gA*gA)*(r*r+gA*gA)-gA*gA*delta*sth*sth;
  float alpha;
  float omega;
  zamo(r, th, alpha, omega);
  gL = mph*sqrt(A/sigma)*sth;
  if (abs(gL) < 1.0e-4) gL = 0.0;
  gE = alpha + omega*gL;

  State y;
  y.r = r;
  y.th = th;
  y.ph = uCamPh;
  y.t = 0.0;
  y.pr = mr*sqrt(sigma/delta);
  y.pth = mth*sqrt(sigma);

  float rDoom = photonOrbit(gA)*0.999;
  float rEscape = max(120.0, 2.5*uCamR);
  vec3 color = vec3(0.0);
  bool done = false;
  for (int i=0; i<160; ++i) {
    if (i >= uMaxSteps || done) break;
    float H;
    Deriv D = deriv(y, H);
    float h = -stepSize(y, D);
    if (!(h < -1.0e-7) || isnan(h) || isinf(h)) {
      done = true;
      break;
    }
    State yn = rk4(y, D, h);
    if (any(isnan(vec4(yn.r,yn.th,yn.ph,yn.t))) || any(isinf(vec4(yn.r,yn.th,yn.ph,yn.t)))) {
      done = true;
      break;
    }
    if (yn.r <= gRh*1.005 || (yn.r < rDoom && yn.r < y.r)) {
      color = vec3(0.0);
      done = true;
      break;
    }

    float c0 = cos(y.th);
    float c1 = cos(yn.th);
    if (c0*c1 < 0.0 && abs(c0-c1) > 1.0e-8) {
      float f = clamp(c0/(c0-c1), 0.0, 1.0);
      State yh = rk4(y, D, h*f);
      float HH;
      Deriv Dh = deriv(yh, HH);
      float dcos = -sin(yh.th)*Dh.th;
      if (abs(dcos) > 1.0e-9) yh = rk4(yh, Dh, -cos(yh.th)/dcos);
      float hitR = yh.r;
      float hitPh = yh.ph + (sin(yh.th) > 0.0 ? 0.0 : PI);
      if (uDiskOn != 0 && hitR >= gISCO && hitR <= uDiskOut) {
        float gd = diskRedshift(hitR);
        float Om = 1.0/(hitR*sqrt(hitR)+gA);
        float sr = sqrt(hitR);
        float ut = (hitR*sr+gA)/(pow(hitR,0.75)*sqrt(max(hitR*sr-3.0*sr+2.0*gA,1.0e-8)));
        float mu = abs(yh.pth)/(max(gE-Om*gL,1.0e-6)*hitR*ut);
        color = shadeDisk(hitR, hitPh, -yh.t, clamp(gd,0.02,4.0), clamp(mu,0.0,1.0));
        done = true;
      } else if (uGridOn != 0 && hitR > gRh && hitR <= uGridOut) {
        float ag;
        float wg;
        zamo(hitR, 0.5*PI, ag, wg);
        float gg = ag/max(gE-wg*gL,1.0e-6);
        color = shadeGrid(hitR, hitPh, clamp(gg,0.05,3.0));
        done = true;
      }
    }
    if (done) break;

    if (yn.r > rEscape && yn.r > y.r) {
      float He;
      Deriv De = deriv(yn, He);
      float st = sin(yn.th);
      float ct = cos(yn.th);
      float cp = cos(yn.ph);
      float sp = sin(yn.ph);
      vec3 er = vec3(st*cp, st*sp, ct);
      vec3 eth = vec3(ct*cp, ct*sp, -st);
      vec3 eph = vec3(-sp, cp, 0.0);
      vec3 v = er*De.r + eth*(yn.r*De.th) + eph*(yn.r*st*De.ph);
      color = skyColor(-normalize(v)) / max(gE*gE*gE, 0.12);
      done = true;
      break;
    }
    y = yn;
  }

  fragColor = vec4(max(color,vec3(0.0)),1.0);
}
