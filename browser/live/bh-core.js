/* bh-core.js — run black-hole fragment shaders live on the user's GPU (WebGL2).
   ONE persistent BHView owns the canvas + GL context for the whole page; calling
   load(cfg) swaps in a different model's shader without ever recreating the
   context (recreating it was the "can't drag after switching back" bug). */
"use strict";

const BH_VERT = `#version 300 es
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main(){ vUV = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }`;

// HDR composite (Narkowicz ACES + golden-angle spiral bloom, ported from Opus's blit.frag)
const BH_COMPOSITE = `#version 300 es
precision highp float;
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uTex; uniform vec2 uTexel; uniform float uExposure; uniform float uBloomStrength;
uniform int uTonemap; uniform float uAsinh;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }
float asinh1(float x){ return log(x + sqrt(x*x + 1.0)); }
vec3 astronomical(vec3 c, float k){
  float l = dot(c, vec3(0.2126,0.7152,0.0722));
  if(l <= 1e-8) return vec3(0.0);
  float mapped = asinh1(l*k)/asinh1(k);
  return clamp(c*(mapped/l),0.0,1.0);
}
void main(){
  vec3 hdr = texture(uTex, vUV).rgb;
  vec3 bloom = vec3(0.0); float wsum = 0.0;
  for(int i=0;i<24;i++){
    float a = float(i)*2.3998277; float rad = 1.5 + float(i)*0.9;
    vec2 off = vec2(cos(a),sin(a))*rad*uTexel;
    vec3 s = texture(uTex, vUV+off).rgb;
    float w = 1.0/(1.0+float(i));
    bloom += max(s-1.0,0.0)*w; wsum += w;
  }
  bloom /= wsum;
  vec3 col = (hdr + bloom*uBloomStrength)*uExposure;
  col = uTonemap == 0 ? astronomical(col, uAsinh) : (uTonemap == 2 ? clamp(col,0.0,1.0) : aces(col));
  float luma = dot(col, vec3(0.2126,0.7152,0.0722));
  col = clamp(mix(vec3(luma), col, 1.4), 0.0, 1.0);
  col = pow(col, vec3(1.0/2.2));
  FragColor = vec4(col, 1.0);
}`;

const BH_ACCUMULATE = `#version 300 es
precision highp float;
in vec2 vUV; out vec4 fragColor;
uniform sampler2D uCurrent; uniform sampler2D uHistory; uniform float uWeight;
void main(){ fragColor=vec4(mix(texture(uHistory,vUV).rgb,texture(uCurrent,vUV).rgb,uWeight),1.0); }
`;

const BH_GRID_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform vec3 uOffset;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uNear;
uniform float uFar;
out vec3 vWorldPos;
out vec3 vNormal;
out float vRadius;
void main(){
  vec3 world = aPos + uOffset;
  vec3 rel = world - uCamPos;
  float vx = dot(rel, uCamRight);
  float vy = dot(rel, uCamUp);
  float vz = -dot(rel, uCamForward);
  float f = 1.0 / max(uTanHalfFov, 0.0001);
  float A = (uFar + uNear) / (uNear - uFar);
  float B = (2.0 * uFar * uNear) / (uNear - uFar);
  vWorldPos = world;
  vNormal = aNormal;
  vRadius = length(vec2(aPos.x, aPos.z));
  gl_Position = vec4(vx * f / uAspect, vy * f, A * vz + B, -vz);
}`;

const BH_GRID_FRAG = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in float vRadius;
out vec4 fragColor;
uniform vec3 uCamPos;
uniform float uRs;
uniform float uTime;
uniform int uMode;
void main(){
  float r = max(vRadius, uRs * 1.01);
  float throat = smoothstep(uRs * 6.0, uRs * 1.1, r);
  vec3 base;
  if (uMode == 0) {
    base = mix(vec3(0.15, 0.55, 0.75), vec3(0.35, 0.05, 0.55), throat);
    float fres = pow(1.0 - abs(dot(normalize(uCamPos - vWorldPos), normalize(vNormal))), 2.0);
    base += fres * vec3(0.4, 0.7, 1.0) * 0.35;
    float wave = 0.5 + 0.5 * sin(r * 1.2 - uTime * 1.5);
    base += wave * 0.05 * vec3(0.5, 0.8, 1.0);
    fragColor = vec4(base, mix(0.18, 0.55, throat));
  } else if (uMode == 1) {
    base = mix(vec3(0.3, 0.9, 1.0), vec3(1.0, 0.4, 0.9), throat);
    float pulse = 0.75 + 0.25 * sin(uTime * 2.0 + r);
    fragColor = vec4(base * pulse, mix(0.35, 0.95, throat));
  } else if (uMode == 2) {
    base = mix(vec3(0.2, 1.0, 0.55), vec3(1.0, 0.5, 0.1), throat);
    fragColor = vec4(base, mix(0.25, 0.85, throat));
  } else if (uMode == 3) {
    float curvature = exp(-length(vWorldPos) / (2.0 * uRs + 0.5)) * 3.0;
    float gridX = sin(vWorldPos.x * 3.0 + uTime * 0.1) * 0.5 + 0.5;
    float gridZ = sin(vWorldPos.z * 3.0 + uTime * 0.05) * 0.5 + 0.5;
    float grid = smoothstep(0.45, 0.55, max(gridX, gridZ));
    base = vec3(0.02, 0.01, 0.08) + vec3(0.3, 0.1, 0.6) * curvature * 0.6;
    fragColor = vec4(mix(base, vec3(0.95), grid), 0.66);
  } else if (uMode == 6) {
    float horizon = 1.0 - smoothstep(uRs * 1.03, uRs * 1.10, r);
    float photon = 1.0 - smoothstep(0.05, 0.13, abs(r - 1.5 * uRs));
    float isco = 1.0 - smoothstep(0.06, 0.15, abs(r - 3.0 * uRs));
    float outer = smoothstep(8.0 * uRs, 23.0 * uRs, r);
    base = mix(vec3(0.20, 0.78, 1.00), vec3(0.08, 0.22, 0.38), outer);
    base = mix(base, vec3(1.00, 0.18, 0.07), horizon);
    base = mix(base, vec3(1.00, 0.82, 0.22), photon);
    base = mix(base, vec3(0.35, 1.00, 0.55), isco);
    float fade = exp(-length(uCamPos - vWorldPos) * 0.018);
    float lip = smoothstep(uRs * 1.02, 5.0 * uRs, r);
    fragColor = vec4(base * (0.55 + 0.45 * lip), fade * (0.18 + 0.42 * lip));
  } else {
    float proximity = 1.0 - smoothstep(uRs, uRs + 8.0, r);
    base = mix(vec3(0.1, 0.4, 0.8), vec3(0.8, 0.2, 0.1), proximity);
    float pulse = 0.6 + 0.4 * sin(uTime * 1.5 - r * 0.5);
    float alpha = mix(0.15, 0.6, proximity) * smoothstep(20.0, 15.0, r);
    if (uMode == 4) {
      fragColor = vec4(base * pulse, alpha);
    } else {
      float depth = clamp(2.0 * sqrt(max(uRs * (r - uRs), 0.0)) / 16.0, 0.0, 1.0);
      float ramp = pow(depth, 1.3);
      float amp = 0.38 + 0.80 * depth;
      float radialT = pow(clamp((r - uRs) / 89.0, 0.0, 1.0), 1.0 / 2.6);
      base = mix(vec3(0.18, 0.55, 0.95), vec3(1.00, 0.34, 0.10), ramp) * amp;
      pulse = 0.72 + 0.28 * sin(uTime * 2.2 + radialT * 26.0);
      fragColor = vec4(base * pulse, 0.92);
    }
  }
}`;

function bhFlammZ(r, rs) {
  return r <= rs * 1.001 ? 0 : 2 * Math.sqrt(Math.max(rs * (r - rs), 0));
}

function bhCreateMesh(gl, verts, idx, lines) {
  const vao = gl.createVertexArray();
  const vbo = gl.createBuffer();
  const ebo = gl.createBuffer();
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx), gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
  gl.bindVertexArray(null);
  return { vao, vbo, ebo, count: idx.length, lines };
}

function bhCreateGridMeshes(gl, rs, side) {
  function surface(rMin, rMax, nRadial, nAzimuth) {
    rMin = Math.max(rMin, rs * 1.02);
    const verts = [], idx = [];
    for (let j = 0; j < nRadial; j++) {
      const r = rMin + (rMax - rMin) * (j / Math.max(nRadial - 1, 1));
      const y = bhFlammZ(r, rs) * (side < 0 ? -1 : 1);
      const dzdr = r > rs ? Math.sqrt(rs / (r - rs)) : 0;
      for (let i = 0; i < nAzimuth; i++) {
        const phi = 2 * Math.PI * (i / nAzimuth), c = Math.cos(phi), s = Math.sin(phi);
        let nx = -dzdr * c, ny = 1, nz = -dzdr * s;
        const inv = 1 / Math.sqrt(nx * nx + ny * ny + nz * nz + 1e-12);
        verts.push(r * c, y, r * s, nx * inv, ny * inv, nz * inv);
      }
    }
    for (let j = 0; j < nRadial - 1; j++) {
      for (let i = 0; i < nAzimuth; i++) {
        const i1 = (i + 1) % nAzimuth;
        const a = j * nAzimuth + i, b = j * nAzimuth + i1, c = (j + 1) * nAzimuth + i, d = (j + 1) * nAzimuth + i1;
        idx.push(a, c, b, b, c, d);
      }
    }
    if (!side) {
      const upperCount = verts.length / 6, upperIdx = idx.length;
      for (let k = 0; k < upperCount; k++) verts.push(verts[k * 6], -verts[k * 6 + 1], verts[k * 6 + 2], verts[k * 6 + 3], -verts[k * 6 + 4], verts[k * 6 + 5]);
      for (let k = 0; k < upperIdx; k += 3) idx.push(upperCount + idx[k], upperCount + idx[k + 2], upperCount + idx[k + 1]);
    }
    return bhCreateMesh(gl, verts, idx, false);
  }

  function flammLines(rMin, rMax, nRadial, nAzimuth) {
    rMin = Math.max(rMin, rs * 1.02);
    const verts = [], idx = [];
    const addV = (x, y, z) => { const id = verts.length / 6; verts.push(x, y, z, 0, 1, 0); return id; };
    for (let j = 0; j < nRadial; j++) {
      const r = rMin + (rMax - rMin) * (j / Math.max(nRadial - 1, 1));
      const y = bhFlammZ(r, rs), ringU = [], ringL = [];
      for (let i = 0; i < nAzimuth; i++) {
        const phi = 2 * Math.PI * i / nAzimuth, x = r * Math.cos(phi), z = r * Math.sin(phi);
        if (side >= 0) ringU.push(addV(x, y, z));
        if (side <= 0) ringL.push(addV(x, -y, z));
      }
      for (let i = 0; i < nAzimuth; i++) {
        const i1 = (i + 1) % nAzimuth;
        if (side >= 0) idx.push(ringU[i], ringU[i1]);
        if (side <= 0) idx.push(ringL[i], ringL[i1]);
      }
    }
    for (let s = 0; s < nAzimuth / 2; s++) {
      const phi = 2 * Math.PI * s / (nAzimuth / 2), c = Math.cos(phi), sn = Math.sin(phi);
      let prevU = 0, prevL = 0;
      for (let j = 0; j < nRadial; j++) {
        const r = rMin + (rMax - rMin) * (j / Math.max(nRadial - 1, 1));
        const y = bhFlammZ(r, rs);
        const u = side >= 0 ? addV(r * c, y, r * sn) : 0;
        const l = side <= 0 ? addV(r * c, -y, r * sn) : 0;
        if (j > 0) {
          if (side >= 0) idx.push(prevU, u);
          if (side <= 0) idx.push(prevL, l);
        }
        prevU = u; prevL = l;
      }
    }
    return bhCreateMesh(gl, verts, idx, true);
  }

  function cage(halfExtent, divisions) {
    const verts = [], idx = [];
    const addV = (x, y, z) => { const id = verts.length / 6; verts.push(x, y, z, 0, 1, 0); return id; };
    const nR = divisions, nA = divisions * 2, rMin = rs * 1.05;
    for (let j = 0; j <= nR; j++) {
      const r = rMin + (halfExtent - rMin) * (j / nR), y = bhFlammZ(r, rs) * 0.35, ring = [];
      for (let i = 0; i < nA; i++) {
        const phi = 2 * Math.PI * i / nA;
        ring.push(addV(r * Math.cos(phi), -y, r * Math.sin(phi)));
      }
      for (let i = 0; i < nA; i++) idx.push(ring[i], ring[(i + 1) % nA]);
    }
    for (let i = 0; i < nA; i += 2) {
      const phi = 2 * Math.PI * i / nA, c = Math.cos(phi), s = Math.sin(phi);
      let prev = 0;
      for (let j = 0; j <= nR; j++) {
        const r = rMin + (halfExtent - rMin) * (j / nR), y = bhFlammZ(r, rs) * 0.35;
        const id = addV(r * c, -y, r * s);
        if (j > 0) idx.push(prev, id);
        prev = id;
      }
    }
    return bhCreateMesh(gl, verts, idx, true);
  }

  return {
    surface: surface(rs * 1.05, 35, 64, 96),
    lines: flammLines(rs * 1.05, 35, 24, 64),
    cage: cage(30, 20),
  };
}

function bhCreateInklingGrid(gl, rs) {
  const size = 25.0, divisions = 50, half = size * 0.5;
  const verts = [], idx = [];
  for (let i = 0; i <= divisions; i++) {
    const x = -half + size * i / divisions;
    for (let j = 0; j <= divisions; j++) {
      const z = -half + size * j / divisions;
      const r = Math.hypot(x, z);
      const curvature = Math.exp(-r / (1.5 * rs + 0.3));
      let px = x, py = 0, pz = z;
      if (r < 4.0 * rs) {
        const depth = (1.0 - r / (4.0 * rs)) * 2.5;
        py -= depth * curvature;
        if (r > 1e-6) {
          px -= x / r * curvature * 0.8;
          pz -= z / r * curvature * 0.8;
        }
      }
      verts.push(px, py, pz, 0, 1, 0);
    }
  }
  for (let i = 0; i < divisions; i++) {
    for (let j = 0; j < divisions; j++) {
      const a = i * (divisions + 1) + j;
      const b = a + 1;
      const c = (i + 1) * (divisions + 1) + j;
      const d = c + 1;
      idx.push(a, b, c, b, d, c);
    }
  }
  return { surface: bhCreateMesh(gl, verts, idx, false) };
}

function bhCreateQwenGrid(gl, rs) {
  const rMin = rs + 0.01, rMax = 20.0, radialLines = 36, ringCount = 60;
  const verts = [], idx = [];
  const add = (x, y, z) => {
    const id = verts.length / 6;
    verts.push(x, y, z, 0, 1, 0);
    return id;
  };
  for (let i = 0; i < radialLines; i++) {
    const angle = 2 * Math.PI * i / radialLines;
    const ca = Math.cos(angle), sa = Math.sin(angle);
    for (let j = 0; j < ringCount; j++) {
      const r0 = rMin + (j / ringCount) * (rMax - rMin);
      const r1 = rMin + ((j + 1) / ringCount) * (rMax - rMin);
      const z0 = -2 * Math.sqrt(rs * (r0 - rs));
      const z1 = -2 * Math.sqrt(rs * (r1 - rs));
      idx.push(add(r0 * ca, z0, r0 * sa), add(r1 * ca, z1, r1 * sa));
    }
  }
  const segments = 128;
  for (let j = 0; j <= ringCount; j++) {
    const r = rMin + (j / ringCount) * (rMax - rMin);
    const y = -2 * Math.sqrt(rs * (r - rs));
    for (let i = 0; i < segments; i++) {
      const a0 = 2 * Math.PI * i / segments;
      const a1 = 2 * Math.PI * (i + 1) / segments;
      idx.push(add(r * Math.cos(a0), y, r * Math.sin(a0)), add(r * Math.cos(a1), y, r * Math.sin(a1)));
    }
  }
  return { lines: bhCreateMesh(gl, verts, idx, true) };
}

function bhCreateQwenMaxGrid(gl, rs) {
  const rMin = rs * 1.0015, rMax = 90.0, radialCount = 48, azimuthCount = 72;
  const radii = Array.from({ length: radialCount }, (_, i) => {
    const t = i / (radialCount - 1);
    return rMin + (rMax - rMin) * Math.pow(t, 2.6);
  });
  const verts = [], idx = [];
  const add = (r, phi) => {
    const id = verts.length / 6;
    const y = -2 * Math.sqrt(Math.max(rs * (r - rs), 0));
    verts.push(r * Math.cos(phi), y, r * Math.sin(phi), 0, 1, 0);
    return id;
  };
  for (const r of radii) {
    for (let j = 0; j < azimuthCount; j++) {
      const a0 = 2 * Math.PI * j / azimuthCount;
      const a1 = 2 * Math.PI * (j + 1) / azimuthCount;
      idx.push(add(r, a0), add(r, a1));
    }
  }
  for (let j = 0; j < azimuthCount; j++) {
    const phi = 2 * Math.PI * j / azimuthCount;
    for (let i = 0; i < radii.length - 1; i++) idx.push(add(radii[i], phi), add(radii[i + 1], phi));
  }
  const horizon = rs * 1.0002;
  for (let j = 0; j < azimuthCount; j++) {
    idx.push(add(horizon, 2 * Math.PI * j / azimuthCount), add(horizon, 2 * Math.PI * (j + 1) / azimuthCount));
  }
  return { lines: bhCreateMesh(gl, verts, idx, true) };
}

function bhCreateGrok46Grid(gl, rs) {
  const rMin = rs * 1.035, rMatch = 8.0 * rs, rFar = 23.0 * rs;
  const verts = [], idx = [];
  const yAt = (r) => r >= rMatch ? 0 : 2 * Math.sqrt(rs * (r - rs)) - 2 * Math.sqrt(rs * (rMatch - rs));
  const add = (r, phi, y = yAt(r)) => {
    const id = verts.length / 6;
    verts.push(r * Math.cos(phi), y, r * Math.sin(phi), 0, 1, 0);
    return id;
  };
  const line = (r0, p0, r1, p1, y0, y1) => idx.push(add(r0, p0, y0), add(r1, p1, y1));

  const radii = [];
  for (let i = 0; i <= 18; i++) {
    const t = i / 18;
    radii.push(rMin + (rMatch - rMin) * t * t);
  }
  for (let i = 1; i <= 10; i++) radii.push(rMatch + (rFar - rMatch) * i / 10);
  radii.push(rs * 1.04, 1.5 * rs, 3.0 * rs);
  for (const r of radii) {
    const segments = r > rMatch ? 128 : 160;
    for (let i = 0; i < segments; i++) {
      const p0 = 2 * Math.PI * i / segments;
      const p1 = 2 * Math.PI * (i + 1) / segments;
      line(r, p0, r, p1);
    }
  }

  for (let spoke = 0; spoke < 24; spoke++) {
    const phi = 2 * Math.PI * spoke / 24;
    for (let i = 0; i < 56; i++) {
      const t0 = i / 56, t1 = (i + 1) / 56;
      const u0 = t0 < 0.45 ? Math.pow(t0 / 0.45, 2) * 0.45 : t0;
      const u1 = t1 < 0.45 ? Math.pow(t1 / 0.45, 2) * 0.45 : t1;
      line(rMin + (rFar - rMin) * u0, phi, rMin + (rFar - rMin) * u1, phi);
    }
  }

  for (let spoke = 0; spoke < 12; spoke++) {
    const phi = 2 * Math.PI * spoke / 12;
    line(rMatch, phi, 4 * rs, phi, 0, yAt(4 * rs));
    line(4 * rs, phi, rMin + 0.075 * rs, phi, yAt(4 * rs), yAt(rMin + 0.075 * rs));
  }
  return { lines: bhCreateMesh(gl, verts, idx, true) };
}

// camera basis matching the native apps (target at origin):
//   pos = dist*(cp*sin(yaw), sin(pitch), -cp*cos(yaw)),  cp=cos(pitch)
//   f = normalize(-pos); right = normalize(cross((0,1,0),f)); up = cross(f,right)
//   rot columns = [right, up, -f]  (column-major, for uniformMatrix3fv)
function bhCamera(yaw, pitch, dist) {
  const cp = Math.cos(pitch);
  const pos = [dist * cp * Math.sin(yaw), dist * Math.sin(pitch), -dist * cp * Math.cos(yaw)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const f = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let r = [f[2], 0, -f[0]];
  const rl = Math.hypot(r[0], r[1], r[2]) || 1;
  r = [r[0] / rl, r[1] / rl, r[2] / rl];
  const u = [f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]];
  const rot = [r[0], r[1], r[2], u[0], u[1], u[2], -f[0], -f[1], -f[2]];
  return { pos, rot };
}

class BHView {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = canvas.getContext("webgl2", { antialias: false, alpha: false, powerPreference: "high-performance" });
    if (!this.gl) throw new Error("no-webgl2");
    // All HDR and history targets are RGBA16F. iOS can expose half-float
    // rendering without the 32-bit support required by EXT_color_buffer_float.
    this._floatExt = this.gl.getExtension("EXT_color_buffer_float") ||
      this.gl.getExtension("EXT_color_buffer_half_float");
    this._composite = null; this._culoc = new Map();
    this._accumProgram = null; this._accumLoc = new Map();
    this._historyFbo = [null, null]; this._historyTex = [null, null];
    this._historyW = 0; this._historyH = 0; this._historySrc = 0; this._accumCount = 0;
    this._gridProgram = null; this._gridLoc = new Map(); this._gridMeshes = null; this._gridRs = 0; this._gridFailed = false;
    this._fbo = null; this._fboTex = null; this._fbw = 0; this._fbh = 0; this._hdr = false;
    this.cfg = null;
    this.settings = {};
    this.S = { yaw: 0, pitch: 0.25, dist: 14, time: 0, w: 1, h: 1 };
    this.uloc = new Map();
    this._program = null;
    this.dragging = false; this.lastX = 0; this.lastY = 0;
    this._maxRes = 1400; this._dprCap = 2.0;
    this.visible = true; this._hidden = false; this.userTouchT = 0;
    this.raf = 0; this._active = false; this._last = 0;
    this._buildStatic();
    this._bindInput();
    this._observe();
  }

  _shader(type, src) {
    const gl = this.gl, s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(s); gl.deleteShader(s);
      throw new Error("compile: " + (log || "(no log)"));
    }
    return s;
  }
  _compile(frag, vert = BH_VERT) {
    const gl = this.gl, p = gl.createProgram();
    const vs = this._shader(gl.VERTEX_SHADER, vert);
    const fs = this._shader(gl.FRAGMENT_SHADER, frag);
    gl.attachShader(p, vs); gl.attachShader(p, fs);
    gl.bindAttribLocation(p, 0, "aPos");
    gl.linkProgram(p);
    gl.deleteShader(vs); gl.deleteShader(fs);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error("link: " + gl.getProgramInfoLog(p));
    return p;
  }
  _buildStatic() {
    const gl = this.gl;
    this._vao = gl.createVertexArray();
    gl.bindVertexArray(this._vao);
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
  }
  U(name) {
    if (this.uloc.has(name)) return this.uloc.get(name);
    const l = this.gl.getUniformLocation(this._program, name);
    this.uloc.set(name, l); return l;
  }

  _cu(name) {
    if (this._culoc.has(name)) return this._culoc.get(name);
    const l = this.gl.getUniformLocation(this._composite, name);
    this._culoc.set(name, l); return l;
  }
  _au(name) {
    if (this._accumLoc.has(name)) return this._accumLoc.get(name);
    const l = this.gl.getUniformLocation(this._accumProgram, name);
    this._accumLoc.set(name, l); return l;
  }
  _gu(name) {
    if (this._gridLoc.has(name)) return this._gridLoc.get(name);
    const l = this.gl.getUniformLocation(this._gridProgram, name);
    this._gridLoc.set(name, l); return l;
  }
  _ensureGrid(rs, side, kind) {
    if (!this._gridProgram) {
      this._gridProgram = this._compile(BH_GRID_FRAG, BH_GRID_VERT);
      this._gridLoc.clear();
    }
    if (!this._gridMeshes || Math.abs(this._gridRs - rs) > 1e-6 || this._gridSide !== side || this._gridKind !== kind) {
      this._gridMeshes = kind === "inkling"
        ? bhCreateInklingGrid(this.gl, rs)
        : kind === "grok46"
          ? bhCreateGrok46Grid(this.gl, rs)
        : kind === "qwenmax"
          ? bhCreateQwenMaxGrid(this.gl, rs)
        : kind === "qwen"
          ? bhCreateQwenGrid(this.gl, rs)
          : bhCreateGridMeshes(this.gl, rs, side);
      this._gridRs = rs;
      this._gridSide = side;
      this._gridKind = kind;
    }
  }
  _drawGridOverlay() {
    const overlay = this.cfg && this.cfg.gridOverlay;
    if (!overlay || this._gridFailed || (overlay.control && !this.settings[overlay.control])) return;
    try {
      const gl = this.gl, rs = overlay.rs || 2.0, kind = overlay.kind || "flamm";
      this._ensureGrid(rs, overlay.side || 0, kind);
      const camera = overlay.camera ? overlay.camera(this.S.yaw, this.S.pitch, this.S.dist) : null;
      if (!camera) return;
      const inset = overlay.inset;
      let viewW = this.S.w, viewH = this.S.h;
      if (inset) {
        const x = Math.round(this.S.w * inset.x), y = Math.round(this.S.h * inset.y);
        viewW = Math.max(1, Math.round(this.S.w * inset.w));
        viewH = Math.max(1, Math.round(this.S.h * inset.h));
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(x, y, viewW, viewH);
        gl.clearColor(...(inset.clear || [0.002, 0.006, 0.014, 1]));
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.viewport(x, y, viewW, viewH);
      }
      gl.enable(gl.DEPTH_TEST);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, overlay.additive ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(this._gridProgram);
      gl.uniform3f(this._gu("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniform3f(this._gu("uCamRight"), camera.right[0], camera.right[1], camera.right[2]);
      gl.uniform3f(this._gu("uCamUp"), camera.up[0], camera.up[1], camera.up[2]);
      gl.uniform3f(this._gu("uCamForward"), camera.fwd[0], camera.fwd[1], camera.fwd[2]);
      gl.uniform3f(this._gu("uOffset"), 0, overlay.yOffset ?? -0.5, 0);
      gl.uniform1f(this._gu("uTanHalfFov"), overlay.tanHalfFov || Math.tan(50 * Math.PI / 360));
      gl.uniform1f(this._gu("uAspect"), viewW / Math.max(1, viewH));
      gl.uniform1f(this._gu("uNear"), 0.05);
      gl.uniform1f(this._gu("uFar"), 500.0);
      gl.uniform1f(this._gu("uRs"), rs);
      gl.uniform1f(this._gu("uTime"), this.S.time);
      const draw = (mesh, mode) => {
        gl.uniform1i(this._gu("uMode"), mode);
        gl.bindVertexArray(mesh.vao);
        gl.drawElements(mesh.lines ? gl.LINES : gl.TRIANGLES, mesh.count, gl.UNSIGNED_SHORT, 0);
      };
      if (kind === "inkling") {
        draw(this._gridMeshes.surface, 3);
      } else if (kind === "qwen") {
        draw(this._gridMeshes.lines, 4);
      } else if (kind === "qwenmax") {
        draw(this._gridMeshes.lines, 5);
      } else if (kind === "grok46") {
        draw(this._gridMeshes.lines, 6);
      } else {
        if (overlay.surface !== false) draw(this._gridMeshes.surface, 0);
        draw(this._gridMeshes.lines, 1);
        draw(this._gridMeshes.cage, 2);
      }
      gl.bindVertexArray(null);
      gl.depthMask(true);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
      if (inset) {
        gl.disable(gl.SCISSOR_TEST);
        gl.viewport(0, 0, this.S.w, this.S.h);
      }
    } catch (e) {
      this._gridFailed = true;
      console.warn("grid overlay disabled", e);
    }
  }
  _ensureFBO(w, h) {
    const gl = this.gl;
    if (this._fbo && this._fbw === w && this._fbh === h) return;
    if (this._fboTex) gl.deleteTexture(this._fboTex);
    if (this._fbo) gl.deleteFramebuffer(this._fbo);
    this._fboTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this._fboTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w, h, 0, gl.RGBA, gl.HALF_FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this._fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this._fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this._fboTex, 0);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this._fbw = w; this._fbh = h;
  }

  _resetAccumulation() { this._accumCount = 0; }

  _ensureHistory(w, h) {
    const gl = this.gl;
    if (this._historyFbo[0] && this._historyW === w && this._historyH === h) return;
    for (let i = 0; i < 2; i++) {
      if (this._historyTex[i]) gl.deleteTexture(this._historyTex[i]);
      if (this._historyFbo[i]) gl.deleteFramebuffer(this._historyFbo[i]);
      const texture = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w, h, 0, gl.RGBA, gl.HALF_FLOAT, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const fbo = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
      this._historyTex[i] = texture; this._historyFbo[i] = fbo;
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this._historyW = w; this._historyH = h; this._historySrc = 0; this._resetAccumulation();
  }

  // swap in a new model (recompiles the fragment shader; keeps the context)
  load(cfg, settings) {
    const p = this._compile(cfg.frag);          // throws on bad shader -> caller falls back
    if (this._program) this.gl.deleteProgram(this._program);
    this._program = p;
    this.uloc.clear();
    this._hdr = !!cfg.hdr;
    if (this._hdr) {
      if (!this._floatExt) throw new Error("no-half-float-hdr");   // -> caller falls back to still
      const compositeSource = cfg.compositeFrag || BH_COMPOSITE;
      if (this._compositeSource !== compositeSource) {
        const composite = this._compile(compositeSource);
        if (this._composite) this.gl.deleteProgram(this._composite);
        this._composite = composite; this._compositeSource = compositeSource;
        this._culoc.clear();
      }
    }
    this.cfg = cfg;
    this.settings = Object.assign({}, settings || {});
    this._maxRes = cfg.maxRes || 1400;
    this._dprCap = cfg.dprCap || 2.0;
    this.S.yaw = cfg.init.yaw; this.S.pitch = cfg.init.pitch; this.S.dist = cfg.init.dist;
    this.S.time = 0;
    this.userTouchT = 0;
    this._gridFailed = false;
    this._resetAccumulation();
    if (cfg.applySettings) cfg.applySettings(this, this.settings, null, true);
  }

  setSettings(settings) {
    const previous = this.settings;
    this.settings = Object.assign({}, settings || {});
    if (this.cfg && this.cfg.applySettings) this.cfg.applySettings(this, this.settings, previous, false);
    this._resetAccumulation();
  }

  _resize() {
    const gl = this.gl, c = this.canvas;
    const rect = c.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, this._dprCap);
    let w = Math.max(1, Math.round(rect.width * dpr));
    let h = Math.max(1, Math.round(rect.height * dpr));
    const renderScale = this.cfg && this.cfg.renderScale ? this.cfg.renderScale(this.settings) : 1;
    w = Math.max(1, Math.round(w * renderScale));
    h = Math.max(1, Math.round(h * renderScale));
    const long = Math.max(w, h);
    if (long > this._maxRes) { const k = this._maxRes / long; w = Math.round(w * k); h = Math.round(h * k); }
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; this._resetAccumulation(); }
    this.S.w = c.width; this.S.h = c.height;
    gl.viewport(0, 0, c.width, c.height);
  }

  _bindInput() {
    const c = this.canvas;
    c.style.touchAction = "none";
    const down = (e) => {
      if (!this.cfg || !this._active) return;
      this.dragging = true;
      const p = e.touches ? e.touches[0] : e;
      this.lastX = p.clientX; this.lastY = p.clientY;
      c.classList.add("grabbing");
      if (e.pointerId != null && c.setPointerCapture) try { c.setPointerCapture(e.pointerId); } catch (x) {}
    };
    const move = (e) => {
      if (!this.dragging || !this.cfg) return;
      const p = e.touches ? e.touches[0] : e;
      const dx = p.clientX - this.lastX, dy = p.clientY - this.lastY;
      this.lastX = p.clientX; this.lastY = p.clientY;
      this.S.yaw += dx * (this.cfg.dragYaw ?? -0.005);
      this.S.pitch += dy * (this.cfg.dragPitch ?? 0.005);
      this.S.pitch = Math.max(this.cfg.pitchMin ?? -1.5, Math.min(this.cfg.pitchMax ?? 1.5, this.S.pitch));
      this.userTouchT = performance.now();
      this._resetAccumulation();
      e.preventDefault();
    };
    const up = () => { this.dragging = false; c.classList.remove("grabbing"); this.userTouchT = performance.now(); };
    c.addEventListener("pointerdown", down);
    window.addEventListener("pointermove", move, { passive: false });
    window.addEventListener("pointerup", up);
    c.addEventListener("wheel", (e) => {
      if (!this.cfg || !this._active) return;
      e.preventDefault();
      this.S.dist *= e.deltaY < 0 ? 0.92 : 1.08;
      this.S.dist = Math.max(this.cfg.distMin ?? 2.5, Math.min(this.cfg.distMax ?? 60, this.S.dist));
      this.userTouchT = performance.now();
      this._resetAccumulation();
    }, { passive: false });
  }
  _observe() {
    if ("IntersectionObserver" in window) {
      this._io = new IntersectionObserver((es) => { this.visible = es[0].isIntersecting; }, { threshold: 0.02 });
      this._io.observe(this.canvas);
    }
    document.addEventListener("visibilitychange", () => { this._hidden = document.hidden; });
  }

  start() { this._active = true; if (!this.raf) { this._last = performance.now(); this.raf = requestAnimationFrame((t) => this._tick(t)); } }
  stop() { this._active = false; if (this.raf) cancelAnimationFrame(this.raf); this.raf = 0; }

  _tick(t) {
    try { this._frame(t); }
    catch (error) { this.stop(); console.warn("Live renderer failed:", error); if (this.onError) this.onError(error); }
  }

  _frame(t) {
    this.raf = requestAnimationFrame((tt) => this._tick(tt));
    if (!this._active || this._hidden || !this.visible || !this._program || !this.cfg) { this._last = t; return; }
    const dt = Math.min(0.05, (t - this._last) / 1000); this._last = t;
    this.S.time += dt;
    const autospin = typeof this.cfg.autospin === "function" ? this.cfg.autospin(this.settings) : this.cfg.autospin;
    if (autospin && !this.dragging && (t - this.userTouchT) > 2200) this.S.yaw += autospin * dt;
    this._resize();
    const gl = this.gl;
    if (this.cfg.drawCustom && this.cfg.drawCustom(this, this.settings)) return;
    const cam = bhCamera(this.S.yaw, this.S.pitch, this.S.dist);
    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(true);
    gl.clear(gl.DEPTH_BUFFER_BIT);
    gl.bindVertexArray(this._vao);
    if (this._hdr) {
      this._ensureFBO(this.S.w, this.S.h);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this._fbo);
      gl.viewport(0, 0, this.S.w, this.S.h);
      gl.useProgram(this._program);
      this.S.sample = this._accumCount;
      this.cfg.setUniforms(gl, (n) => this.U(n), this.S, cam, this.settings);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      let compositeTex = this._fboTex;
      if (this.settings.accumulate) {
        this._ensureHistory(this.S.w, this.S.h);
        if (!this._accumProgram) this._accumProgram = this._compile(BH_ACCUMULATE);
        const dst = 1 - this._historySrc;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this._historyFbo[dst]);
        gl.viewport(0, 0, this.S.w, this.S.h);
        gl.useProgram(this._accumProgram);
        gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this._fboTex); gl.uniform1i(this._au("uCurrent"), 0);
        gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this._historyTex[this._historySrc]); gl.uniform1i(this._au("uHistory"), 1);
        const weight = this._accumCount === 0 ? 1 : (this.settings.animate === false ? 1 / (this._accumCount + 1) : Math.max(1 / (this._accumCount + 1), 0.22));
        gl.uniform1f(this._au("uWeight"), weight); gl.drawArrays(gl.TRIANGLES, 0, 3);
        this._historySrc = dst; this._accumCount += 1; compositeTex = this._historyTex[dst];
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, this.S.w, this.S.h);
      gl.useProgram(this._composite);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, compositeTex);
      gl.uniform1i(this._cu("uTex"), 0);
      gl.uniform2f(this._cu("uTexel"), 1 / this.S.w, 1 / this.S.h);
      gl.uniform1f(this._cu("uExposure"), this.settings.exposure ?? this.cfg.exposure ?? 1.0);
      gl.uniform1f(this._cu("uBloomStrength"), this.settings.bloom === false ? 0 : (this.settings.bloomStrength ?? this.cfg.bloom ?? 0.6));
      gl.uniform1i(this._cu("uTonemap"), this.settings.tonemap ?? 1);
      gl.uniform1f(this._cu("uAsinh"), this.settings.asinh ?? 26.0);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    } else {
      gl.useProgram(this._program);
      this.cfg.setUniforms(gl, (n) => this.U(n), this.S, cam, this.settings);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    this._drawGridOverlay();
  }
}
