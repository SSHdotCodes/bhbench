/* live.js — per-model live-shader configs + mount/unmount on the stage canvas.
   Each config ports one native renderer's fragment shader (GLSL-ES) and
   replicates its camera + uniform setup so it runs live on the user's GPU. */
"use strict";

// sonnet's camera convention: pos z uses +cos, right = normalize(cross(fwd, up))
function camSonnet(yaw, pitch, dist) {
  const cp = Math.cos(pitch);
  const pos = [dist * cp * Math.sin(yaw), dist * Math.sin(pitch), dist * cp * Math.cos(yaw)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [-fwd[2], 0, fwd[0]];                 // normalize(cross(fwd,(0,1,0)))
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

// HY3 is y-up: pos=dist*(cos pitch*sin yaw, sin pitch, cos pitch*cos yaw)
function camHy3(yaw, pitch, dist) {
  const cp = Math.cos(pitch);
  const pos = [dist * cp * Math.sin(yaw), dist * Math.sin(pitch), dist * cp * Math.cos(yaw)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [-fwd[2], 0, fwd[0]];                 // cross(fwd,(0,1,0))
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

// Grok Build is z-up with a polar orbit: pos=dist*(sin polar*cos az, sin polar*sin az, cos polar)
function camGrokBuild(az, polar, dist) {
  const sp = Math.sin(polar);
  const pos = [dist * sp * Math.cos(az), dist * sp * Math.sin(az), dist * Math.cos(polar)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [fwd[1], -fwd[0], 0];                 // cross(fwd,(0,0,1))
  let rl = Math.hypot(right[0], right[1], right[2]);
  if (rl < 0.001) { right = [1, 0, 0]; rl = 1; }
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

// Fable is z-up: cam=dist*(cb*ca, cb*sa, sb); right = normalize(cross(fwd,(0,0,1)))
function camFable(alpha, beta, dist) {
  const cb = Math.cos(beta), sb = Math.sin(beta), ca = Math.cos(alpha), sa = Math.sin(alpha);
  const pos = [dist * cb * ca, dist * cb * sa, dist * sb];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [fwd[1], -fwd[0], 0];                 // cross(fwd,(0,0,1))
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}
// Opus/claude is y-up: dir=(ce*cos az, sin el, ce*sin az); eye=dir*dist; fwd=-dir
function camClaude(az, el, dist) {
  const ce = Math.cos(el);
  const dir = [ce * Math.cos(az), Math.sin(el), ce * Math.sin(az)];
  const eye = [dir[0] * dist, dir[1] * dist, dir[2] * dist];
  const fwd = [-dir[0], -dir[1], -dir[2]];
  let right = [-fwd[2], 0, fwd[0]];                 // cross(fwd,(0,1,0))
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { eye, fwd, right, up };
}

// Composer is y-up with a polar camera: pos=dist*(sin theta*cos phi, cos theta, sin theta*sin phi)
function camComposer(phi, theta, dist) {
  const st = Math.sin(theta);
  const pos = [dist * st * Math.cos(phi), dist * Math.cos(theta), dist * st * Math.sin(phi)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [-fwd[2], 0, fwd[0]];                // cross(fwd,(0,1,0))
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

// Kimi K3 matches its native Metal camera: x=cos(pitch)*cos(yaw), z=cos(pitch)*sin(yaw).
function camKimi(yaw, pitch, dist) {
  const cp = Math.cos(pitch);
  const pos = [dist * cp * Math.cos(yaw), dist * Math.sin(pitch), dist * cp * Math.sin(yaw)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [-fwd[2], 0, fwd[0]];
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

// Qwen 3.8 Max is z-up: pos=dist*(cos pitch*cos yaw, cos pitch*sin yaw, sin pitch).
function camQwenMax(yaw, pitch, dist) {
  const cp = Math.cos(pitch);
  const pos = [dist * cp * Math.cos(yaw), dist * cp * Math.sin(yaw), dist * Math.sin(pitch)];
  const fl = Math.hypot(pos[0], pos[1], pos[2]) || 1;
  const fwd = [-pos[0] / fl, -pos[1] / fl, -pos[2] / fl];
  let right = [fwd[1], -fwd[0], 0];
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up, basis: [...right, ...up, ...fwd] };
}

function camQwenMaxGrid(yaw, pitch, dist) {
  const camera = camQwenMax(yaw, pitch, dist);
  const map = (v) => [v[0], v[2], v[1]];
  return { pos: map(camera.pos), fwd: map(camera.fwd), right: map(camera.right), up: map(camera.up) };
}

function camTerraInset(time) {
  const angle = 0.72 + 0.18 * Math.sin(time * 0.18);
  const pos = [31 * Math.sin(angle), 21, 31 * Math.cos(angle)];
  const target = [0, -4.8, 0];
  let fwd = [target[0] - pos[0], target[1] - pos[1], target[2] - pos[2]];
  const fl = Math.hypot(fwd[0], fwd[1], fwd[2]) || 1;
  fwd = [fwd[0] / fl, fwd[1] / fl, fwd[2] / fl];
  let right = [-fwd[2], 0, fwd[0]];
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
  return { pos, fwd, right, up };
}

const QUALITY_3 = [
  { label: "Fast", value: 0 },
  { label: "Balanced", value: 1 },
  { label: "High", value: 2 },
];

function kerrRadii(spin) {
  const a = Math.max(0, Math.min(0.9999, spin));
  const z1 = 1 + Math.cbrt(1 - a * a) * (Math.cbrt(1 + a) + Math.cbrt(1 - a));
  const z2 = Math.sqrt(3 * a * a + z1 * z1);
  return {
    horizon: 1 + Math.sqrt(1 - a * a),
    isco: 3 + z2 - Math.sqrt((3 - z1) * (3 + z1 + 2 * z2)),
  };
}

const OPUS5_MESH_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
uniform vec3 uEye, uRight, uUp, uForward, uCenter;
uniform float uTanHalfFov, uAspect, uNear, uFar;
out vec3 vNormal; out vec3 vWorld; out vec4 vColor;
void main(){
  vec3 rel = aPos-uEye;
  float vx=dot(rel,uRight), vy=dot(rel,uUp), vz=dot(rel,uForward);
  float f=1.0/max(uTanHalfFov,0.0001);
  float z=((uFar+uNear)/(uFar-uNear))*vz-(2.0*uFar*uNear)/(uFar-uNear);
  gl_Position=vec4(vx*f/uAspect,vy*f,z,vz);
  gl_PointSize=7.0;
  vNormal=aNormal; vWorld=aPos; vColor=aColor;
}`;

const OPUS5_MESH_FRAG = `#version 300 es
precision highp float;
in vec3 vNormal; in vec3 vWorld; in vec4 vColor; out vec4 fragColor;
uniform vec3 uEye; uniform int uLit; uniform float uAlpha;
void main(){
  if(uLit==0){ fragColor=vec4(vColor.rgb,vColor.a*uAlpha); return; }
  vec3 n=normalize(vNormal), view=normalize(uEye-vWorld);
  if(dot(n,view)<0.0) n=-n;
  vec3 l1=normalize(vec3(0.45,0.30,0.85));
  vec3 l2=normalize(vec3(-0.60,-0.50,0.25));
  float diffuse=0.42*max(dot(n,l1),0.0)+0.16*max(dot(n,l2),0.0);
  float rim=pow(1.0-max(dot(n,view),0.0),4.0);
  float spec=pow(max(dot(n,normalize(l1+view)),0.0),64.0)*0.10;
  vec3 color=vColor.rgb*(0.16+diffuse)+vec3(0.18,0.34,0.68)*rim*0.22+vec3(spec);
  fragColor=vec4(color,vColor.a*uAlpha);
}`;

const opus5MeshStates = new WeakMap();

function opus5Vertex(out, p, n, c, alpha = 1) {
  out.push(p[0], p[1], p[2], n[0], n[1], n[2], c[0], c[1], c[2], alpha);
}

function opus5Upload(gl, vertices, mode) {
  const vao = gl.createVertexArray(), buffer = gl.createBuffer();
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
  const stride = 40;
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, stride, 0);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, stride, 12);
  gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.FLOAT, false, stride, 24);
  gl.bindVertexArray(null);
  return { vao, buffer, count: vertices.length / 10, mode };
}

function opus5DisposeMeshes(gl, group) {
  if (!group) return;
  Object.values(group).forEach((mesh) => {
    if (!mesh || !mesh.vao) return;
    gl.deleteVertexArray(mesh.vao); gl.deleteBuffer(mesh.buffer);
  });
}

function opus5ColorRamp(logK) {
  const t = Math.max(0, Math.min(1, (logK + 7.6) / 8.6));
  const stops = [[0.02, 0.05, 0.16], [0.07, 0.26, 0.62], [0.52, 0.20, 0.55], [0.92, 0.42, 0.20], [1.0, 0.74, 0.30]];
  const edges = [0, 0.25, 0.55, 0.80, 1];
  let i = 0; while (i < 3 && t > edges[i + 1]) i++;
  const f = (t - edges[i]) / (edges[i + 1] - edges[i]);
  return stops[i].map((value, axis) => value + (stops[i + 1][axis] - value) * f);
}

function opus5BuildFunnel(gl, spin) {
  const radii = kerrRadii(spin), rMin = radii.horizon * 1.0001, rMax = 10, profileN = 420;
  const rs = [], radiiEmbed = [], zs = new Array(profileN).fill(0);
  const embedR = (r) => Math.sqrt(r * r + spin * spin + 2 * spin * spin / Math.max(r, 1e-6));
  const dEmbedR = (r) => (r - spin * spin / (r * r)) / Math.max(embedR(r), 1e-9);
  const slope = (r) => {
    const delta = r * r - 2 * r + spin * spin;
    if (delta <= 1e-9) return 0;
    return Math.sqrt(Math.max(r * r / delta - dEmbedR(r) ** 2, 0));
  };
  for (let i = 0; i < profileN; i++) {
    const f = i / (profileN - 1), r = rMin + (rMax - rMin) * f * f;
    rs.push(r); radiiEmbed.push(embedR(r));
  }
  for (let i = profileN - 2; i >= 0; i--) {
    const r0 = rs[i + 1], r1 = rs[i], mid = 0.5 * (r0 + r1);
    zs[i] = zs[i + 1] + (r1 - r0) * (slope(r0) + 4 * slope(mid) + slope(r1)) / 6;
  }
  const zAt = (r) => {
    const f = Math.sqrt(Math.max(0, Math.min(1, (r - rMin) / (rMax - rMin)))) * (profileN - 1);
    const i = Math.min(profileN - 2, Math.floor(f)), q = f - i;
    return zs[i] + (zs[i + 1] - zs[i]) * q;
  };
  const point = (r, phi) => {
    const R = embedR(r), cp = Math.cos(phi), sp = Math.sin(phi), dR = dEmbedR(r);
    const delta = r * r - 2 * r + spin * spin;
    const dz = Math.sqrt(Math.max(delta > 1e-9 ? r * r / delta - dR * dR : 0, 0));
    let n = [-dz * R * cp, -dz * R * sp, dR * R];
    const nl = Math.hypot(...n) || 1; n = n.map((v) => v / nl);
    return { p: [R * cp, R * sp, zAt(r)], n };
  };
  const surface = [], lines = [], markers = [], nr = 96, nphi = 96;
  const radiusAt = (i) => rMin + (rMax - rMin) * (i / nr) ** 2;
  for (let i = 0; i < nr; i++) {
    const r0 = radiusAt(i), r1 = radiusAt(i + 1);
    const c0 = opus5ColorRamp(Math.log10(48 / Math.max(r0 ** 6, 1e-12)));
    const c1 = opus5ColorRamp(Math.log10(48 / Math.max(r1 ** 6, 1e-12)));
    for (let j = 0; j < nphi; j++) {
      const a0 = 2 * Math.PI * j / nphi, a1 = 2 * Math.PI * (j + 1) / nphi;
      const p0 = point(r0, a0), p1 = point(r1, a0), p2 = point(r1, a1), p3 = point(r0, a1);
      [[p0, c0], [p1, c1], [p2, c1], [p0, c0], [p2, c1], [p3, c0]].forEach(([v, c]) => opus5Vertex(surface, v.p, v.n, c));
    }
  }
  const lineColor = [0.50, 0.88, 1.0], up = [0, 0, 1], seg = 128;
  for (let k = 0; k <= 17; k++) {
    const f = k / 17, r = rMin + (rMax - rMin) * f * f, alpha = 0.35 + 0.5 * (1 - f);
    for (let j = 0; j < seg; j++) {
      opus5Vertex(lines, point(r, 2 * Math.PI * j / seg).p, up, lineColor, alpha);
      opus5Vertex(lines, point(r, 2 * Math.PI * (j + 1) / seg).p, up, lineColor, alpha);
    }
  }
  for (let spoke = 0; spoke < 24; spoke++) {
    const phi = 2 * Math.PI * spoke / 24;
    for (let j = 0; j < 100; j++) {
      const f0 = j / 100, f1 = (j + 1) / 100;
      opus5Vertex(lines, point(rMin + (rMax - rMin) * f0 * f0, phi).p, up, lineColor, 0.35 + 0.5 * (1 - f0));
      opus5Vertex(lines, point(rMin + (rMax - rMin) * f1 * f1, phi).p, up, lineColor, 0.35 + 0.5 * (1 - f1));
    }
  }
  const ring = (r, color) => {
    if (r < rMin || r > rMax) return;
    for (let j = 0; j < 180; j++) {
      opus5Vertex(markers, point(r, 2 * Math.PI * j / 180).p, up, color);
      opus5Vertex(markers, point(r, 2 * Math.PI * (j + 1) / 180).p, up, color);
    }
  };
  const photon = 2 * (1 + Math.cos((2 / 3) * Math.acos(-Math.max(0, Math.min(0.9999, spin)))));
  ring(radii.horizon * 1.0002, [1, 0.15, 0.15]); ring(2, [1, 0.55, 0.10]);
  ring(photon, [1, 0.95, 0.35]); ring(radii.isco, [0.35, 1, 0.55]);
  return {
    surface: opus5Upload(gl, surface, gl.TRIANGLES), lines: opus5Upload(gl, lines, gl.LINES),
    markers: opus5Upload(gl, markers, gl.LINES), depth: Math.abs(zs[0]), spin,
  };
}

function opus5BuildCones(gl, spin) {
  const cones = [], floor = [], markers = [], up = [0, 0, 1], radii = kerrRadii(spin);
  const rhoH = Math.hypot(radii.horizon, spin), rhoE = Math.hypot(2, spin), rMax = 7;
  const metric = (x, y) => {
    const rho2 = x * x + y * y, r = Math.sqrt(Math.max(rho2 - spin * spin, 1e-8)), f = 2 / r;
    const lx = (r * x + spin * y) / Math.max(rho2, 1e-8), ly = (r * y - spin * x) / Math.max(rho2, 1e-8);
    return { tt: -1 + f, tx: f * lx, ty: f * ly, xx: 1 + f * lx * lx, xy: f * lx * ly, yy: 1 + f * ly * ly };
  };
  const spacing = (rMax - 0.5 * rhoH) / 8, dt = 0.55 * spacing;
  for (let ir = 0; ir < 9; ir++) for (let spoke = 0; spoke < 4; spoke++) {
    const rho = 0.5 * rhoH + spacing * ir, phi = 2 * Math.PI * spoke / 4;
    const x = rho * Math.cos(phi), y = rho * Math.sin(phi), g = metric(x, y);
    const color = rho < rhoH ? [1, 0.22, 0.24] : (g.tt > 0 ? [1, 0.62, 0.14] : [0.38, 0.82, 1]);
    const apex = [x, y, 0]; let previous = null;
    for (let k = 0; k <= 64; k++) {
      const psi = 2 * Math.PI * k / 64, nx = Math.cos(psi), ny = Math.sin(psi);
      const B = g.tx * nx + g.ty * ny, C = g.xx * nx * nx + 2 * g.xy * nx * ny + g.yy * ny * ny;
      const disc = B * B - C * g.tt, step = disc >= 0 && C > 1e-9 ? dt * (-B + Math.sqrt(disc)) / C : -1;
      if (step <= 0) { previous = null; continue; }
      const rim = [x + step * nx, y + step * ny, dt];
      if (previous) { opus5Vertex(cones, previous, up, color, 0.9); opus5Vertex(cones, rim, up, color, 0.9); }
      if (k % 4 === 0) { opus5Vertex(cones, apex, up, color, 0.3); opus5Vertex(cones, rim, up, color, 0.75); }
      previous = rim;
    }
    opus5Vertex(cones, apex, up, [0.85, 0.85, 0.9], 0.55);
    opus5Vertex(cones, [x, y, dt], up, [0.85, 0.85, 0.9], 0.55);
  }
  for (let k = 1; k <= 14; k++) for (let j = 0; j < 128; j++) {
    const r = rMax * k / 14, p0 = 2 * Math.PI * j / 128, p1 = 2 * Math.PI * (j + 1) / 128;
    opus5Vertex(floor, [r * Math.cos(p0), r * Math.sin(p0), 0], up, [0.17, 0.22, 0.34], 0.6);
    opus5Vertex(floor, [r * Math.cos(p1), r * Math.sin(p1), 0], up, [0.17, 0.22, 0.34], 0.6);
  }
  for (let spoke = 0; spoke < 24; spoke++) {
    const p = 2 * Math.PI * spoke / 24;
    opus5Vertex(floor, [rhoH * Math.cos(p), rhoH * Math.sin(p), 0], up, [0.17, 0.22, 0.34], 0.6);
    opus5Vertex(floor, [rMax * Math.cos(p), rMax * Math.sin(p), 0], up, [0.17, 0.22, 0.34], 0.6);
  }
  const ring = (r, color) => { for (let j = 0; j < 180; j++) {
    const p0 = 2 * Math.PI * j / 180, p1 = 2 * Math.PI * (j + 1) / 180;
    opus5Vertex(markers, [r * Math.cos(p0), r * Math.sin(p0), 0.01], up, color);
    opus5Vertex(markers, [r * Math.cos(p1), r * Math.sin(p1), 0.01], up, color);
  } };
  ring(rhoH, [1, 0.15, 0.15]); ring(rhoE, [1, 0.55, 0.10]); ring(Math.hypot(radii.isco, spin), [0.35, 1, 0.55]);
  return { cones: opus5Upload(gl, cones, gl.LINES), floor: opus5Upload(gl, floor, gl.LINES), markers: opus5Upload(gl, markers, gl.LINES), spin };
}

function opus5DrawMeshView(view, controls) {
  if (!controls.view) return false;
  const gl = view.gl; let state = opus5MeshStates.get(gl);
  if (!state) {
    state = { program: view._compile(OPUS5_MESH_FRAG, OPUS5_MESH_VERT), locations: new Map(), funnel: null, cones: null };
    opus5MeshStates.set(gl, state);
  }
  if (controls.view === 1 && (!state.funnel || Math.abs(state.funnel.spin - controls.spin) > 0.0005)) {
    opus5DisposeMeshes(gl, state.funnel); state.funnel = opus5BuildFunnel(gl, controls.spin);
  }
  if (controls.view === 2 && (!state.cones || Math.abs(state.cones.spin - controls.spin) > 0.0005)) {
    opus5DisposeMeshes(gl, state.cones); state.cones = opus5BuildCones(gl, controls.spin);
  }
  const centerZ = controls.view === 1 ? -0.40 * state.funnel.depth : 0.7;
  const elevation = Math.max(-1.35, Math.min(1.35, view.S.pitch * (controls.view === 1 ? 0.86 : 0.72) + (controls.view === 1 ? 0.22 : 0.44)));
  const dist = view.S.dist * (controls.view === 1 ? 0.50 : 0.30), ce = Math.cos(elevation), se = Math.sin(elevation);
  const eye = [dist * ce * Math.cos(view.S.yaw), dist * ce * Math.sin(view.S.yaw), centerZ + dist * se];
  let forward = [-eye[0], -eye[1], centerZ - eye[2]], fl = Math.hypot(...forward) || 1;
  forward = forward.map((v) => v / fl);
  let right = [forward[1], -forward[0], 0], rl = Math.hypot(...right) || 1; right = right.map((v) => v / rl);
  const up = [right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0]];
  const U = (name) => { if (!state.locations.has(name)) state.locations.set(name, gl.getUniformLocation(state.program, name)); return state.locations.get(name); };
  gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, view.S.w, view.S.h);
  gl.clearColor(0.004, 0.006, 0.013, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.useProgram(state.program);
  gl.uniform3f(U("uEye"), ...eye); gl.uniform3f(U("uRight"), ...right); gl.uniform3f(U("uUp"), ...up); gl.uniform3f(U("uForward"), ...forward);
  gl.uniform3f(U("uCenter"), 0, 0, centerZ); gl.uniform1f(U("uTanHalfFov"), Math.tan(45 * Math.PI / 360));
  gl.uniform1f(U("uAspect"), view.S.w / Math.max(1, view.S.h)); gl.uniform1f(U("uNear"), 0.05); gl.uniform1f(U("uFar"), 4000);
  gl.enable(gl.DEPTH_TEST); gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  const draw = (mesh, lit, alpha) => {
    gl.uniform1i(U("uLit"), lit); gl.uniform1f(U("uAlpha"), alpha); gl.bindVertexArray(mesh.vao); gl.drawArrays(mesh.mode, 0, mesh.count);
  };
  if (controls.view === 1) {
    gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1.5, 2); draw(state.funnel.surface, 1, 1); gl.disable(gl.POLYGON_OFFSET_FILL);
    gl.depthMask(false); draw(state.funnel.lines, 0, 0.9); draw(state.funnel.markers, 0, 1.3); gl.depthMask(true);
  } else {
    draw(state.cones.floor, 0, 1); draw(state.cones.markers, 0, 1.3);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE); draw(state.cones.cones, 0, 1.1); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  }
  gl.bindVertexArray(null); gl.disable(gl.BLEND); gl.disable(gl.DEPTH_TEST);
  return true;
}

const muse13MeshStates = new WeakMap();

function muse13BuildFunnel(gl) {
  const vertices = [], up = [0, 0, 1], rMax = 10, ringCount = 30, segmentCount = 128;
  const radii = [];
  const flammW = (r) => 2 * Math.sqrt(Math.max(0, r - 1));
  const point = (r, angle) => [r * Math.cos(angle), r * Math.sin(angle), -flammW(r)];
  const curvatureColor = (r) => {
    const k = 12 / Math.max(r ** 6, 1e-12);
    const t = Math.max(0, Math.min(1, Math.log10(1 + k) / Math.log10(13)));
    return [0.10 + 0.90 * t * t, 0.18 + 0.62 * t, 0.45 + 0.55 * (1 - t) * 0.4 + 0.45 * t];
  };
  for (let i = 0; i < ringCount; i++) {
    const t = i / (ringCount - 1);
    radii.push(1.02 + (rMax - 1.02) * t ** 1.6);
  }
  for (const r of radii) {
    let color = curvatureColor(r);
    if (Math.abs(r - 3) < 0.09) color = [1, 0.55, 0.15];
    if (Math.abs(r - 1.5) < 0.05) color = [1, 0.9, 0.2];
    for (let i = 0; i < segmentCount; i++) {
      opus5Vertex(vertices, point(r, 2 * Math.PI * i / segmentCount), up, color);
      opus5Vertex(vertices, point(r, 2 * Math.PI * (i + 1) / segmentCount), up, color);
    }
  }
  for (let spoke = 0; spoke < 48; spoke++) {
    const angle = 2 * Math.PI * spoke / 48;
    for (let i = 0; i + 1 < radii.length; i++) {
      opus5Vertex(vertices, point(radii[i], angle), up, curvatureColor(radii[i]).map((v) => v * 0.55));
      opus5Vertex(vertices, point(radii[i + 1], angle), up, curvatureColor(radii[i + 1]).map((v) => v * 0.55));
    }
  }
  const markerRing = (r, color) => {
    for (let i = 0; i < segmentCount; i++) {
      const lift = (p) => [p[0], p[1], p[2] + 0.01];
      opus5Vertex(vertices, lift(point(r, 2 * Math.PI * i / segmentCount)), up, color);
      opus5Vertex(vertices, lift(point(r, 2 * Math.PI * (i + 1) / segmentCount)), up, color);
    }
  };
  markerRing(1, [1, 0.15, 0.15]);
  markerRing(1.5, [1, 0.9, 0.2]);
  markerRing(3, [1, 0.5, 0.1]);
  const probe = opus5Upload(gl, [0, 0, 0, 0, 0, 1, 1, 0.75, 0.2, 1], gl.POINTS);
  return { lines: opus5Upload(gl, vertices, gl.LINES), probe };
}

// Direct WebGL rendition of Muse 1.3's submitted line-only Flamm mesh,
// including its curvature colors, critical-radius rings and infalling probe.
function muse13DrawMeshView(view, controls) {
  if (controls.view !== 1) return false;
  const gl = view.gl;
  let state = muse13MeshStates.get(gl);
  if (!state) {
    state = { program: view._compile(OPUS5_MESH_FRAG, OPUS5_MESH_VERT), locations: new Map(), mesh: muse13BuildFunnel(gl) };
    muse13MeshStates.set(gl, state);
  }
  const targetZ = -2.4, cp = Math.cos(view.S.pitch), sp = Math.sin(view.S.pitch), dist = view.S.dist;
  const eye = [dist * cp * Math.sin(view.S.yaw), dist * cp * Math.cos(view.S.yaw), targetZ + dist * sp];
  let forward = [-eye[0], -eye[1], targetZ - eye[2]], fl = Math.hypot(...forward) || 1;
  forward = forward.map((value) => value / fl);
  let right = [forward[1], -forward[0], 0], rl = Math.hypot(...right) || 1;
  right = right.map((value) => value / rl);
  const up = [right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0]];
  const U = (name) => { if (!state.locations.has(name)) state.locations.set(name, gl.getUniformLocation(state.program, name)); return state.locations.get(name); };
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(0, 0, view.S.w, view.S.h);
  gl.clearColor(0.004, 0.005, 0.012, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.useProgram(state.program);
  gl.uniform3f(U("uEye"), ...eye); gl.uniform3f(U("uRight"), ...right); gl.uniform3f(U("uUp"), ...up); gl.uniform3f(U("uForward"), ...forward);
  gl.uniform3f(U("uCenter"), 0, 0, targetZ); gl.uniform1f(U("uTanHalfFov"), Math.tan(55 * Math.PI / 360));
  gl.uniform1f(U("uAspect"), view.S.w / Math.max(1, view.S.h)); gl.uniform1f(U("uNear"), 0.1); gl.uniform1f(U("uFar"), 200);
  gl.uniform1i(U("uLit"), 0); gl.uniform1f(U("uAlpha"), 1);
  gl.enable(gl.DEPTH_TEST);
  gl.bindVertexArray(state.mesh.lines.vao);
  gl.drawArrays(state.mesh.lines.mode, 0, state.mesh.lines.count);
  const cycle = (view.S.time * 0.25) % 1, radius = 10 - 8.9 * cycle * cycle, angle = 1.2 + view.S.time * 0.35;
  const probe = [radius * Math.cos(angle), radius * Math.sin(angle), -2 * Math.sqrt(Math.max(0, radius - 1)) + 0.05];
  gl.bindBuffer(gl.ARRAY_BUFFER, state.mesh.probe.buffer);
  gl.bufferSubData(gl.ARRAY_BUFFER, 0, new Float32Array([...probe, 0, 0, 1, 1, 0.75, 0.2, 1]));
  gl.bindVertexArray(state.mesh.probe.vao);
  gl.drawArrays(gl.POINTS, 0, 1);
  gl.bindVertexArray(null);
  gl.disable(gl.DEPTH_TEST);
  return true;
}

const BH_LIVE = {
  // ---- GLM 5.3 : exact Schwarzschild Binet tracer + ray-traced Flamm grid ----
  glm53: {
    shader: "/live/shaders/glm53.frag",
    init: { yaw: 2.35, pitch: 0.14, dist: 11.5 },
    distMin: 2.2, distMax: 60, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: 0.005, dragPitch: 0.005, autospin: 0,
    maxRes: 1280, dprCap: 1.5,
    controls: [
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "viewMode", label: "View mode", type: "segments", default: 0, options: [
        { label: "Realistic", value: 0 }, { label: "Grid", value: 1 }, { label: "Combined", value: 2 },
      ] },
      { key: "resolution", label: "Resolution", type: "segments", default: 1, options: [
        { label: "50%", value: 0 }, { label: "66%", value: 1 }, { label: "80%", value: 2 }, { label: "100%", value: 3 },
      ] },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "stars", label: "Background stars", type: "toggle", default: true },
      { key: "animate", label: "Disk animation", type: "toggle", default: true },
      { key: "animSpeed", label: "Animation speed", type: "range", default: 1, min: 0, max: 8, step: 0.05, precision: 2, suffix: "x" },
      { key: "exposure", label: "Exposure", type: "range", default: 1.1, min: 0.1, max: 6, step: 0.05, precision: 2, suffix: "x" },
      { key: "diskTemp", label: "Disk temperature", type: "range", default: 6400, min: 2500, max: 20000, step: 100, suffix: " K" },
    ],
    renderScale: (C) => [0.5, 0.66, 0.8, 1][C.resolution] || 0.66,
    applySettings(_view, C, previous, initial) {
      C._anim = initial ? 0 : (previous?._anim || 0);
      C._lastClock = initial ? 0 : (previous?._lastClock || 0);
    },
    setUniforms(gl, U, S, _cam, C) {
      const camera = camHy3(S.yaw, S.pitch, S.dist);
      const clockDelta = Math.max(0, S.time - (C._lastClock || 0));
      if (C.animate) C._anim += clockDelta * C.animSpeed;
      C._lastClock = S.time;
      const basis = new Float32Array([
        camera.right[0], camera.right[1], camera.right[2],
        camera.up[0], camera.up[1], camera.up[2],
        camera.fwd[0], camera.fwd[1], camera.fwd[2],
      ]);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uAnim"), C._anim);
      gl.uniform3f(U("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniformMatrix3fv(U("uCamBasis"), false, basis);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(27.5 * Math.PI / 180));
      gl.uniform1i(U("uMode"), C.viewMode);
      gl.uniform1i(U("uDiskOn"), C.disk ? 1 : 0);
      gl.uniform1i(U("uStarsOn"), C.stars ? 1 : 0);
      gl.uniform1i(U("uSteps"), [320, 560, 900][C.quality] || 560);
      gl.uniform1f(U("uDtScale"), [1, 0.8, 0.6][C.quality] || 0.8);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uDiskTemp"), C.diskTemp);
    },
  },

  // ---- GLM 5.2 : compact single-pass geodesic tracer, tonemap baked in ----
  glm: {
    shader: "/live/shaders/glm.frag",
    init: { yaw: 0.6, pitch: 0.32, dist: 14 },
    distMin: 2.5, distMax: 55, pitchMin: -1.4, pitchMax: 1.4,
    dragYaw: -0.005, dragPitch: 0.005, autospin: 0.055,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Lensed grid", type: "toggle", default: false },
    ],
    setUniforms(gl, U, S, cam, C) {
      gl.uniform3f(U("uCamPos"), cam.pos[0], cam.pos[1], cam.pos[2]);
      gl.uniformMatrix3fv(U("uCamRot"), false, cam.rot);
      gl.uniform1f(U("uFocal"), 1.4);
      gl.uniform1f(U("uRs"), 1.0);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1i(U("uMode"), 0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowGrid"), C.grid ? 1 : 0);
      gl.uniform1f(U("uDiskInner"), 3.0);
      gl.uniform1f(U("uDiskOuter"), 12.0);
    },
  },

  // ---- Qwen 3.8 Max 0902: adaptive-RK4 Schwarzschild tracer + thermal disk ----
  qwen38max0902: {
    shader: "/live/shaders/qwen38max0902.frag?v=1",
    init: { yaw: 0.55, pitch: 0.16, dist: 20 },
    distMin: 3, distMax: 150, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? 0.035 : 0,
    maxRes: 1100, dprCap: 1.35,
    gridOverlay: {
      kind: "qwenmax", control: "grid", camera: camKimi, rs: 1.0,
      yOffset: 0, tanHalfFov: Math.tan(42 * Math.PI / 360), surface: false, additive: true,
    },
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Flamm grid overlay", type: "toggle", default: false },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: false },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "exposure", label: "Exposure", type: "range", default: 1.0, min: 0.2, max: 4, step: 0.05, precision: 2 },
      { key: "diskBrightness", label: "Disk brightness", type: "range", default: 2.0, min: 0.4, max: 4, step: 0.1, precision: 1, suffix: "x" },
    ],
    renderScale: (C) => [0.5, 0.7, 0.85][C.quality] || 0.7,
    setUniforms(gl, U, S, _cam, C) {
      const camera = camKimi(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uTanHalf"), Math.tan(42 * Math.PI / 360));
      gl.uniform1f(U("uRs"), 1.0);
      gl.uniform1f(U("uEscR"), Math.max(45, S.dist * 1.8));
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uDiskBright"), C.diskBrightness);
      gl.uniform1i(U("uMaxSteps"), [220, 380, 900][C.quality] || 380);
      gl.uniform1i(U("uDiskOn"), C.disk ? 1 : 0);
      gl.uniform3f(U("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniform3f(U("uFwd"), camera.fwd[0], camera.fwd[1], camera.fwd[2]);
      gl.uniform3f(U("uRight"), camera.right[0], camera.right[1], camera.right[2]);
      gl.uniform3f(U("uUp"), camera.up[0], camera.up[1], camera.up[2]);
    },
  },

  // ---- Qwen 3.8 Max : exact Schwarzschild geodesics + Novikov-Thorne disk ----
  qwen38max: {
    shader: "/live/shaders/qwen38max.frag",
    init: { yaw: 0.6, pitch: 0.3, dist: 34 },
    distMin: 4, distMax: 150, pitchMin: -1.52, pitchMax: 1.52,
    dragYaw: -0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? 0.05 : 0,
    maxRes: 1280, dprCap: 1.5,
    gridOverlay: {
      kind: "qwenmax", control: "grid", camera: camQwenMaxGrid, rs: 1.0,
      yOffset: 0, tanHalfFov: Math.tan(50 * Math.PI / 360), surface: false, additive: true,
    },
    controls: [
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: true },
      { key: "flatBackground", label: "Flat background", type: "toggle", default: false },
      { key: "view", label: "View", type: "segments", default: 1, options: [
        { label: "Default", value: 1 }, { label: "High", value: 2 }, { label: "Edge-on", value: 3 },
      ] },
      { key: "quality", label: "Quality", type: "segments", default: 2, options: QUALITY_3 },
      { key: "exposure", label: "Exposure", type: "range", default: 1.3, min: 0.15, max: 8, step: 0.05, precision: 2 },
    ],
    renderScale: (C) => [0.55, 0.75, 1.0][C.quality] || 1,
    applySettings(view, C, previous, initial) {
      if (!initial && previous && C.view === previous.view) return;
      const preset = C.view === 2 ? { pitch: 1.05, dist: 44 } : (C.view === 3 ? { pitch: 0.05, dist: 30 } : { pitch: 0.3, dist: 34 });
      view.S.pitch = preset.pitch;
      view.S.dist = preset.dist;
      view.userTouchT = performance.now();
    },
    setUniforms(gl, U, S, _cam, C) {
      const camera = camQwenMax(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniformMatrix3fv(U("uCamBasis"), false, camera.basis);
      gl.uniform1f(U("uFocal"), 1 / Math.tan(0.4363));
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform3f(U("uDiskN"), 0, 0, 1);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uDiskIn"), 3.0);
      gl.uniform1f(U("uDiskOut"), 15.0);
      gl.uniform1i(U("uFlatBG"), C.flatBackground ? 1 : 0);
      gl.uniform1i(U("uMaxSteps"), [800, 1600, 2500][C.quality] || 2500);
    },
  },

  // ---- Qwen 3.8 Flash Next (NVFP4): Cartesian Schwarzschild tracer + lensed Flamm grid ----
  qwen38flashnext: {
    shader: "/live/shaders/qwen38-flash-next.frag?v=2",
    init: { yaw: 0.45, pitch: 0.16, dist: 23 },
    distMin: 1.95, distMax: 60, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.0042, dragPitch: 0.0042,
    autospin: (C) => C.autoOrbit ? 0.045 : 0,
    maxRes: 1280, dprCap: 1.5,
    controls: [
      { key: "lensing", label: "Gravitational lensing", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "stars", label: "Star field", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: true },
    ],
    setUniforms(gl, U, S, _cam, C) {
      const camera = camSonnet(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform3f(U("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniform3f(U("uCamRight"), camera.right[0], camera.right[1], camera.right[2]);
      gl.uniform3f(U("uCamUp"), camera.up[0], camera.up[1], camera.up[2]);
      gl.uniform3f(U("uCamFwd"), camera.fwd[0], camera.fwd[1], camera.fwd[2]);
      gl.uniform1f(U("uFovScale"), Math.tan(27 * Math.PI / 180));
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1i(U("uLens"), C.lensing ? 1 : 0);
      gl.uniform1i(U("uDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("uStars"), C.stars ? 1 : 0);
    },
  },

  // ---- Qwen 3.8 Preview : Cartesian RK4 Schwarzschild tracer + Flamm grid ----
  qwen38: {
    shader: "/live/shaders/qwen38.frag",
    init: { yaw: 0.4, pitch: 1.2, dist: 18 },
    distMin: 4, distMax: 60, pitchMin: 0.1, pitchMax: Math.PI - 0.1,
    dragYaw: -0.005, dragPitch: -0.005, autospin: 0,
    maxRes: 1000, dprCap: 1.25,
    gridOverlay: {
      kind: "qwen", control: "grid", camera: camComposer, rs: 2.0,
      yOffset: 0, tanHalfFov: Math.tan(60 * Math.PI / 360), surface: false,
    },
    controls: [
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "stars", label: "Star field", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "diskInner", label: "Disk inner radius", type: "range", default: 3, min: 2.1, max: 6, step: 0.1, precision: 1 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 12, min: 7, max: 20, step: 0.5, precision: 1 },
    ],
    renderScale: (C) => [0.68, 0.88, 1.08][C.quality] || 0.88,
    setUniforms(gl, U, S, cam, C) {
      const c = camComposer(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(60 * Math.PI / 360));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uRs"), 2.0);
      gl.uniform1f(U("uDiskInner"), C.diskInner);
      gl.uniform1f(U("uDiskOuter"), C.diskOuter);
      gl.uniform1i(U("uMaxSteps"), [140, 220, 300][C.quality] || 220);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowStars"), C.stars ? 1 : 0);
    },
  },

  // ---- Gemini 3.8 Flash : submitted Metal RK4 tracer + Kerr disk and Flamm trapdoor ----
  gemini38: {
    shader: "/live/shaders/gemini38.frag?v=1",
    init: { yaw: 0.52, pitch: 0.22, dist: 28 },
    distMin: 3, distMax: 80, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? C.orbitSpeed : 0,
    maxRes: 1180, dprCap: 1.35,
    controls: [
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "colorMode", label: "Disk spectrum", type: "segments", default: 0, options: [
        { label: "Blackbody", value: 0 }, { label: "Temperature", value: 1 }, { label: "g-factor", value: 2 },
      ] },
      { key: "lensing", label: "Gravitational lensing", type: "toggle", default: true },
      { key: "grid", label: "Flamm trapdoor grid", type: "toggle", default: true },
      { key: "disk", label: "Relativistic disk", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: true },
      { key: "spin", label: "Kerr spin a/M", type: "range", default: 0.9, min: 0, max: 0.998, step: 0.002, precision: 3 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 14, min: 6, max: 25, step: 0.5, precision: 1, suffix: " M" },
      { key: "diskAlpha", label: "Disk optical density", type: "range", default: 0.85, min: 0.1, max: 1.5, step: 0.01, precision: 2 },
      { key: "diskTemp", label: "Temperature scale", type: "range", default: 1, min: 0.4, max: 2.5, step: 0.05, precision: 2, suffix: "x" },
      { key: "gridOffset", label: "Trapdoor height", type: "range", default: -1.2, min: -6, max: 3, step: 0.1, precision: 1, suffix: " M" },
      { key: "stepScale", label: "Integration step scale", type: "range", default: 1, min: 0.4, max: 1.8, step: 0.05, precision: 2 },
      { key: "orbitSpeed", label: "Auto-orbit speed", type: "range", default: 0.15, min: -0.5, max: 0.5, step: 0.01, precision: 2 },
    ],
    renderScale: (C) => [0.5, 0.7, 0.9][C.quality] || 0.7,
    setUniforms(gl, U, S, _cam, C) {
      const camera = camHy3(S.yaw, S.pitch, S.dist);
      const a = Math.max(0, Math.min(0.998, C.spin));
      const horizon = 1 + Math.sqrt(Math.max(0, 1 - a * a));
      const z1 = 1 + Math.cbrt(1 - a * a) * (Math.cbrt(1 + a) + Math.cbrt(1 - a));
      const z2 = Math.sqrt(3 * a * a + z1 * z1);
      const isco = 3 + z2 - Math.sqrt(Math.max(0, (3 - z1) * (3 + z1 + 2 * z2)));
      gl.uniform2f(U("screenResolution"), S.w, S.h);
      gl.uniform3f(U("camPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniform3f(U("camForward"), camera.fwd[0], camera.fwd[1], camera.fwd[2]);
      gl.uniform3f(U("camUp"), camera.up[0], camera.up[1], camera.up[2]);
      gl.uniform3f(U("camRight"), camera.right[0], camera.right[1], camera.right[2]);
      gl.uniform1f(U("fov"), 55 * Math.PI / 180);
      gl.uniform1f(U("time"), S.time);
      gl.uniform1f(U("mass"), 1);
      gl.uniform1f(U("spin"), a);
      gl.uniform1f(U("horizonRadius"), horizon);
      gl.uniform1f(U("diskInnerRadius"), isco);
      gl.uniform1f(U("diskOuterRadius"), Math.max(C.diskOuter, isco + 1));
      gl.uniform1f(U("diskAlpha"), C.diskAlpha);
      gl.uniform1f(U("diskTempScale"), C.diskTemp);
      gl.uniform1i(U("enableGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("enableDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("enableLensing"), C.lensing ? 1 : 0);
      gl.uniform1f(U("gridHeightOffset"), C.gridOffset);
      gl.uniform1f(U("stepSizeScale"), C.stepScale);
      gl.uniform1i(U("maxSteps"), [180, 280, 350][C.quality] || 280);
      gl.uniform1i(U("colorMode"), C.colorMode);
    },
  },

  // ---- Gemini 3.7 Flash : Kerr RK4 tracer + Novikov-Thorne disk and dual spacetime grid ----
  gemini37: {
    shader: "/live/shaders/gemini37.frag",
    init: { yaw: 0.35, pitch: 0.22, dist: 18 },
    distMin: 3, distMax: 80, pitchMin: -1.55, pitchMax: 1.55,
    dragYaw: 0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? C.orbitSpeed : 0,
    maxRes: 1280, dprCap: 1.5,
    controls: [
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "viewMode", label: "View mode", type: "segments", default: 0, options: [
        { label: "Beauty", value: 0 }, { label: "Deflection", value: 1 }, { label: "Doppler", value: 2 },
      ] },
      { key: "skyMode", label: "Celestial background", type: "segments", default: 0, options: [
        { label: "Milky Way", value: 0 }, { label: "Lens grid", value: 1 },
      ] },
      { key: "corona", label: "Corona", type: "toggle", default: true },
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "photonRing", label: "Photon-ring emphasis", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: true },
      { key: "spin", label: "Kerr spin a/M", type: "range", default: 0.88, min: -0.998, max: 0.998, step: 0.001, precision: 3 },
      { key: "diskInner", label: "Disk inner radius", type: "range", default: 2.402, min: 1, max: 10, step: 0.01, precision: 2, suffix: " M" },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 14.5, min: 5, max: 30, step: 0.1, precision: 1, suffix: " M" },
      { key: "diskHeight", label: "Disk scale height", type: "range", default: 0.045, min: 0.01, max: 0.2, step: 0.001, precision: 3 },
      { key: "diskDensity", label: "Plasma density", type: "range", default: 0.85, min: 0.1, max: 3, step: 0.01, precision: 2 },
      { key: "doppler", label: "Doppler beaming", type: "range", default: 3.5, min: 0, max: 5, step: 0.05, precision: 2 },
      { key: "diskSpeed", label: "Disk orbit speed", type: "range", default: 1, min: 0, max: 3, step: 0.05, precision: 2, suffix: "x" },
      { key: "haloIntensity", label: "Halo intensity", type: "range", default: 0.7, min: 0, max: 2.5, step: 0.01, precision: 2 },
      { key: "haloRadius", label: "Halo radius", type: "range", default: 4.5, min: 2, max: 10, step: 0.1, precision: 1, suffix: " M" },
      { key: "haloFalloff", label: "Halo falloff", type: "range", default: 2.4, min: 1, max: 5, step: 0.05, precision: 2 },
      { key: "gridSpacing", label: "Grid spacing", type: "range", default: 1, min: 0.2, max: 3, step: 0.05, precision: 2, suffix: " M" },
      { key: "gridDepth", label: "Trapdoor depth", type: "range", default: 0.85, min: 0.1, max: 2, step: 0.01, precision: 2 },
      { key: "gridThickness", label: "Grid thickness", type: "range", default: 0.08, min: 0.01, max: 0.25, step: 0.005, precision: 3 },
      { key: "gridAlpha", label: "Grid opacity", type: "range", default: 0.65, min: 0.1, max: 1, step: 0.01, precision: 2 },
      { key: "exposure", label: "HDR exposure", type: "range", default: 1.35, min: 0.2, max: 3.5, step: 0.01, precision: 2, suffix: "x" },
      { key: "maxSteps", label: "RK4 steps", type: "range", default: 320, min: 50, max: 600, step: 10 },
      { key: "stepSize", label: "Integration step", type: "range", default: 0.08, min: 0.02, max: 0.2, step: 0.005, precision: 3 },
      { key: "orbitSpeed", label: "Auto-orbit speed", type: "range", default: 0.08, min: -0.5, max: 0.5, step: 0.01, precision: 2 },
      { key: "cameraDistance", label: "Camera distance", type: "range", default: 18, min: 4, max: 60, step: 0.1, precision: 1, suffix: " M" },
      { key: "cameraAzimuth", label: "Camera azimuth", type: "range", default: 0.35, min: 0, max: 6.28, step: 0.01, precision: 2 },
      { key: "cameraElevation", label: "Camera elevation", type: "range", default: 0.22, min: -1.4, max: 1.4, step: 0.01, precision: 2 },
    ],
    renderScale: (C) => [0.58, 0.78, 1][C.quality] || 0.78,
    applySettings(view, C, previous, initial) {
      if (initial || !previous || C.cameraDistance !== previous.cameraDistance) view.S.dist = C.cameraDistance;
      if (initial || !previous || C.cameraAzimuth !== previous.cameraAzimuth) view.S.yaw = C.cameraAzimuth;
      if (initial || !previous || C.cameraElevation !== previous.cameraElevation) view.S.pitch = C.cameraElevation;
      view.userTouchT = performance.now();
    },
    setUniforms(gl, U, S, _cam, C) {
      const camera = camHy3(S.yaw, S.pitch, S.dist);
      const a = Math.max(-0.998, Math.min(0.998, C.spin));
      const horizon = 1 + Math.sqrt(Math.max(0.001, 1 - a * a));
      gl.uniform2f(U("screenResolution"), S.w, S.h);
      gl.uniform3f(U("camPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniform3f(U("camTarget"), 0, 0, 0);
      gl.uniform3f(U("camUp"), 0, 1, 0);
      gl.uniform1f(U("fovY"), 55 * Math.PI / 180);
      gl.uniform1f(U("aspectRatio"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("time"), S.time);
      gl.uniform1f(U("mass"), 1);
      gl.uniform1f(U("spin"), a);
      gl.uniform1f(U("horizonRadius"), horizon);
      gl.uniform1f(U("diskInnerRadius"), Math.max(C.diskInner, horizon * 1.001));
      gl.uniform1f(U("diskOuterRadius"), C.diskOuter);
      gl.uniform1f(U("diskScaleHeight"), C.diskHeight);
      gl.uniform1f(U("diskTemperatureBase"), 1);
      gl.uniform1f(U("diskDensity"), C.diskDensity);
      gl.uniform1f(U("diskSpeedMultiplier"), C.diskSpeed);
      gl.uniform1f(U("dopplerStrength"), C.doppler);
      gl.uniform1f(U("haloIntensity"), C.haloIntensity);
      gl.uniform1f(U("haloRadius"), C.haloRadius);
      gl.uniform1f(U("haloFalloff"), C.haloFalloff);
      gl.uniform1i(U("enableCorona"), C.corona ? 1 : 0);
      gl.uniform1i(U("enableSpacetimeGrid"), C.grid ? 1 : 0);
      gl.uniform1f(U("gridSpacing"), C.gridSpacing);
      gl.uniform1f(U("gridThickness"), C.gridThickness);
      gl.uniform1f(U("gridDepthScale"), C.gridDepth);
      gl.uniform1f(U("gridAlpha"), C.gridAlpha);
      gl.uniform1i(U("maxSteps"), C.maxSteps);
      gl.uniform1f(U("stepSize"), C.stepSize);
      gl.uniform1f(U("escapeRadius"), 50);
      gl.uniform1i(U("renderMode"), C.viewMode);
      gl.uniform1f(U("exposure"), C.exposure);
      gl.uniform1i(U("enableSkybox"), 1);
      gl.uniform1i(U("celestialLensingMode"), C.skyMode);
      gl.uniform1i(U("showPhotonRingGlow"), C.photonRing ? 1 : 0);
    },
  },

  // ---- Sonnet 5 : OpenGL 4.1 geodesic tracer, luminance-Reinhard tonemap ----
  sonnet: {
    shader: "/live/shaders/sonnet.frag",
    init: { yaw: 0.6, pitch: 0.34, dist: 22 },
    distMin: 3, distMax: 200, pitchMin: -1.55, pitchMax: 1.55,
    dragYaw: -0.005, dragPitch: -0.005, autospin: 0.05,
    controls: [
      { key: "lensing", label: "Gravitational lensing", type: "toggle", default: true },
      { key: "maxSteps", label: "Ray steps", type: "range", default: 520, min: 60, max: 900, step: 20 },
      { key: "diskBrightness", label: "Disk brightness", type: "range", default: 1.1, min: 0.2, max: 2.5, step: 0.05, suffix: "x" },
    ],
    setUniforms(gl, U, S, cam, C) {
      const c = camSonnet(S.yaw, S.pitch, S.dist);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(50 * Math.PI / 180 * 0.5));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uRs"), 1.0);
      gl.uniform1f(U("uDiskInner"), 3.0);
      gl.uniform1f(U("uDiskOuter"), 12.0);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uDiskBrightness"), C.diskBrightness);
      gl.uniform1i(U("uMaxSteps"), C.maxSteps);
      gl.uniform1f(U("uStepScale"), 0.045);
      gl.uniform1i(U("uShowLensing"), C.lensing ? 1 : 0);
    },
  },

  // ---- GLM 5.3 Flash: submitted OpenGL 4.1 Schwarzschild tracer + lensed grid ----
  oxalpha: {
    shader: "/live/shaders/oxalpha.frag",
    hdr: true, exposure: 1.0, bloom: 0.35,
    init: { yaw: 0.55, pitch: 0.16, dist: 27 },
    distMin: 3, distMax: 60, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.005, dragPitch: 0.005, autospin: (C) => C.animate ? 0.05 : 0,
    maxRes: 1100, dprCap: 1.35,
    controls: [
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "viewMode", label: "View", type: "segments", default: 0, options: [
        { label: "Accretion disk", value: 0 }, { label: "Spacetime grid", value: 1 },
      ] },
      { key: "animate", label: "Disk animation", type: "toggle", default: true },
      { key: "diskTemp", label: "Disk temperature", type: "range", default: 6400, min: 2500, max: 20000, step: 100, suffix: " K" },
      { key: "exposure", label: "Exposure", type: "range", default: 1.0, min: 0.3, max: 2.5, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.62, 0.82, 1.0][C.quality] || 0.82,
    setUniforms(gl, U, S, _cam, C) {
      const camera = camKimi(S.yaw, S.pitch, S.dist);
      const basis = new Float32Array([
        camera.right[0], camera.right[1], camera.right[2],
        camera.up[0], camera.up[1], camera.up[2],
        camera.fwd[0], camera.fwd[1], camera.fwd[2],
      ]);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uTime"), C.animate ? S.time : 0);
      gl.uniform3f(U("uCamPos"), camera.pos[0], camera.pos[1], camera.pos[2]);
      gl.uniformMatrix3fv(U("uCamBasis"), false, basis);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(55 * Math.PI / 360));
      gl.uniform1i(U("uMaxSteps"), [260, 460, 700][C.quality] || 460);
      gl.uniform1f(U("uStepBase"), [0.20, 0.16, 0.12][C.quality] || 0.16);
      gl.uniform1f(U("uDiskInner"), 3.0);
      gl.uniform1f(U("uDiskOuter"), 14.0);
      gl.uniform1f(U("uTpeak"), C.diskTemp);
      gl.uniform1f(U("uDiskSpeed"), 1.0);
      gl.uniform1i(U("uGridMode"), C.viewMode);
      gl.uniform1f(U("uExposure"), 1.0);
    },
  },

  // ---- GPT-5.5 (codex) : isotropic optical-metric tracer, exposure tonemap ----
  codex: {
    shader: "/live/shaders/codex.frag?v=2",
    init: { yaw: 0.5, pitch: 0.32, dist: 18.5 },
    distMin: 5, distMax: 38, pitchMin: -1.28, pitchMax: 1.28,
    dragYaw: 0.0065, dragPitch: 0.0065, autospin: 0.05,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Spacetime grid", type: "toggle", default: false },
      { key: "halo", label: "Photon halo", type: "toggle", default: true },
      { key: "exposure", label: "Exposure", type: "range", default: 1.18, min: 0.4, max: 2.5, step: 0.02, suffix: "x" },
      { key: "raySteps", label: "Ray steps", type: "range", default: 420, min: 120, max: 620, step: 20 },
    ],
    setUniforms(gl, U, S, cam, C) {
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uYaw"), S.yaw);
      gl.uniform1f(U("uPitch"), S.pitch);
      gl.uniform1f(U("uDistance"), S.dist);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1i(U("uRaySteps"), C.raySteps);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("uShowHalo"), C.halo ? 1 : 0);
    },
  },

  // ---- GPT-5.6 Sol (xhigh) : Schwarzschild null-orbit tracer + Flamm inset ----
  solxhigh: {
    shader: "/live/shaders/solxhigh.frag",
    init: { yaw: 0.35, pitch: 24 * Math.PI / 180, dist: 25 },
    distMin: 10, distMax: 45, pitchMin: -0.08, pitchMax: 1.25,
    dragYaw: -0.0045, dragPitch: 0.0035, autospin: 0.035,
    maxRes: 720, dprCap: 1.0,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "halo", label: "Photon halo", type: "toggle", default: true },
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
    ],
    renderScale: (C) => [0.72, 1, 1.2][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      gl.uniform2f(U("u_resolution"), S.w, S.h);
      gl.uniform1f(U("u_time"), S.time);
      gl.uniform1f(U("u_yaw"), S.yaw);
      gl.uniform1f(U("u_pitch"), S.pitch);
      gl.uniform1f(U("u_cameraDistance"), S.dist);
      gl.uniform1i(U("u_showDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("u_showHalo"), C.halo ? 1 : 0);
      gl.uniform1i(U("u_showGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("u_quality"), C.quality);
    },
  },

  // ---- Gargantua / zoomx64 : near-extremal Kerr separated-geodesic tracer ----
  gargantua: {
    shader: "/live/shaders/gargantua.frag",
    init: { yaw: 0, pitch: 1.42, dist: 24 },
    distMin: 12, distMax: 60, pitchMin: 0.12, pitchMax: Math.PI - 0.12,
    dragYaw: 0.006, dragPitch: 0.003, autospin: 0.04,
    maxRes: 820, dprCap: 1.0,
    controls: [
      { key: "spin", label: "Kerr spin", type: "range", default: 0.998, min: 0, max: 0.999, step: 0.001, precision: 3 },
      { key: "maxSteps", label: "Ray steps", type: "range", default: 192, min: 64, max: 320, step: 16 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.4, max: 2.5, step: 0.05, suffix: "x" },
    ],
    setUniforms(gl, U, S, cam, C) {
      const radii = kerrRadii(C.spin);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uAzimuth"), S.yaw);
      gl.uniform1f(U("uInclination"), S.pitch);
      gl.uniform1f(U("uObserverRadius"), S.dist);
      gl.uniform1f(U("uFov"), 46 * Math.PI / 180);
      gl.uniform1f(U("uSpin"), C.spin);
      gl.uniform1f(U("uHorizon"), radii.horizon);
      gl.uniform1f(U("uISCO"), radii.isco);
      gl.uniform1i(U("uMaxSteps"), C.maxSteps);
      gl.uniform1f(U("uExposure"), C.exposure);
    },
  },

  // ---- GPT-5.6 Luna (xhigh) : isotropic optical-metric tracer + warped grid ----
  lunaxhigh: {
    shader: "/live/shaders/lunaxhigh.frag",
    init: { yaw: 0, pitch: 0.72, dist: 26 },
    distMin: 10, distMax: 55, pitchMin: 0.05, pitchMax: 1.52,
    dragYaw: 0.0048, dragPitch: 0.0042, autospin: 0.035,
    maxRes: 720, dprCap: 1.0,
    controls: [
      { key: "grid", label: "Warped grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "halo", label: "Photon halo", type: "toggle", default: true },
      { key: "exposure", label: "Exposure", type: "range", default: 1.35, min: 0.4, max: 3, step: 0.05, suffix: "x" },
    ],
    setUniforms(gl, U, S, cam, C) {
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uCameraDistance"), S.dist);
      gl.uniform1f(U("uInclination"), S.pitch);
      gl.uniform1f(U("uYaw"), S.yaw);
      gl.uniform1f(U("uMass"), 1.0);
      gl.uniform1i(U("uShowGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowHalo"), C.halo ? 1 : 0);
      gl.uniform1f(U("uExposure"), C.exposure);
    },
  },

  // ---- GPT-5.6 Terra (xhigh) : planar Schwarzschild RK4 + Flamm inset ----
  terraxhigh: {
    shader: "/live/shaders/terraxhigh.frag",
    init: { yaw: 0, pitch: 0.24, dist: 15 },
    distMin: 6, distMax: 70, pitchMin: -1.1, pitchMax: 1.1,
    dragYaw: 0.005, dragPitch: 0.005, autospin: 0.065,
    maxRes: 720, dprCap: 1.0,
    gridOverlay: { control: "grid", camera: (_, __, ___) => camTerraInset(performance.now() / 1000), rs: 2.0, yOffset: 0, tanHalfFov: Math.tan(42 * Math.PI / 360), surface: false, inset: { x: 0.016, y: 0.045, w: 0.32, h: 0.32 } },
    controls: [
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "fov", label: "Field of view", type: "range", default: 48, min: 20, max: 85, step: 1, suffix: "°" },
    ],
    setUniforms(gl, U, S, cam, C) {
      const c = camHy3(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamera"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("uRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform1f(U("uFov"), C.fov * Math.PI / 180);
    },
  },

  // ---- GPT-5.6 Sol Ultra : observer / curvature Schwarzschild lab ----
  solultra: {
    shader: "/live/shaders/solultra.frag", hdr: true, exposure: 1.22, bloom: 0.28, viewMode: 0,
    init: { yaw: 0.18, pitch: 0.32, dist: 32 },
    distMin: 12, distMax: 60, pitchMin: 0.055, pitchMax: 1.02,
    dragYaw: -0.0045, dragPitch: -0.0038, autospin: 0.045,
    maxRes: 820, dprCap: 1.0,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "halo", label: "Photon halo", type: "toggle", default: true },
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "lensing", label: "Gravitational lensing", type: "toggle", default: true },
      { key: "bloom", label: "Bloom", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: [
        { label: "Performance", value: 0 }, { label: "Balanced", value: 1 },
        { label: "High", value: 2 }, { label: "Reference", value: 3 },
      ] },
      { key: "exposure", label: "Exposure", type: "range", default: 1.22, min: 0.3, max: 3, step: 0.02, suffix: "x" },
    ],
    renderScale: (C) => [0.72, 1, 1.18, 1.35][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const preset = [
        { step: 0.050, rays: 300, embedding: 180 },
        { step: 0.034, rays: 520, embedding: 240 },
        { step: 0.023, rays: 820, embedding: 290 },
        { step: 0.015, rays: 1400, embedding: 320 },
      ][C.quality] || { step: 0.034, rays: 520, embedding: 240 };
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uYaw"), S.yaw);
      gl.uniform1f(U("uPitch"), S.pitch);
      gl.uniform1f(U("uDistance"), S.dist);
      gl.uniform1f(U("uStepScale"), preset.step);
      gl.uniform1i(U("uMaxSteps"), preset.rays);
      gl.uniform1i(U("uEmbeddingSteps"), preset.embedding);
      gl.uniform1i(U("uViewMode"), this.viewMode ?? 0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowHalo"), C.halo ? 1 : 0);
      gl.uniform1i(U("uShowGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("uLensing"), C.lensing ? 1 : 0);
    },
  },

  // ---- Composer 2.5 : full Christoffel RK4 Schwarzschild tracer ----
  composer: {
    shader: "/live/shaders/composer.frag",
    init: { yaw: 0.12, pitch: 0.85, dist: 30 },
    distMin: 8, distMax: 120, pitchMin: 0.08, pitchMax: 3.05,
    dragYaw: -0.005, dragPitch: 0.005, autospin: 0.045,
    maxRes: 1150, dprCap: 1.6,
    gridOverlay: { control: "grid", camera: camComposer, rs: 2.0, yOffset: 0, tanHalfFov: Math.tan(55 * Math.PI / 360), surface: true },
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
    ],
    setUniforms(gl, U, S, cam, C) {
      const c = camComposer(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1f(U("uRs"), 2.0);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uFovTan"), Math.tan(55 * Math.PI / 360));
      gl.uniform1i(U("uSteps"), 96);
      gl.uniform1f(U("uStepSize"), 0.15);
      gl.uniform1i(U("uShowGridOverlay"), 0);
    },
  },

  // ---- Tencent HY3 : CPU Cartesian Schwarzschild tracer ported to WebGL2 ----
  hy3: {
    shader: "/live/shaders/hy3.frag",
    init: { yaw: 0.0, pitch: 0.25, dist: 22.8 },
    distMin: 8, distMax: 90, pitchMin: -1.4, pitchMax: 1.4,
    dragYaw: -0.005, dragPitch: 0.005, autospin: 0.04,
    maxRes: 950, dprCap: 1.4,
    gridOverlay: { control: "grid", camera: camHy3, rs: 2.0, yOffset: 0, tanHalfFov: Math.tan(55 * Math.PI / 360), surface: true },
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
    ],
    renderScale: (C) => [0.62, 1, 1.28][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camHy3(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform1f(U("uFovTan"), Math.tan(55 * Math.PI / 360));
      gl.uniform1i(U("uSteps"), [240, 360, 540][C.quality] || 360);
      gl.uniform1f(U("uStepScale"), 1.0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
    },
  },

  // ---- Muse Spark 1.3 : adaptive Cartesian Schwarzschild tracer + Flamm embedding ----
  muse13: {
    shader: "/live/shaders/muse13.frag?v=1",
    init: { yaw: 0, pitch: 0.28, dist: 11 },
    distMin: 4.5, distMax: 40, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? 0.05 : 0,
    maxRes: 1100, dprCap: 1.35,
    controls: [
      { key: "view", label: "View", type: "segments", default: 0, options: [
        { label: "Ray trace", value: 0 }, { label: "Flamm embedding", value: 1 },
      ] },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "bending", label: "Gravitational bending", type: "toggle", default: true },
      { key: "doppler", label: "Relativistic transfer", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "animate", label: "Disk animation", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto orbit", type: "toggle", default: true },
      { key: "stepScale", label: "Step scale", type: "range", default: 1, min: 0.4, max: 2.5, step: 0.05, suffix: "x" },
      { key: "exposure", label: "Exposure", type: "range", default: 1.15, min: 0.3, max: 3, step: 0.05, suffix: "x" },
      { key: "bgGain", label: "Sky brightness", type: "range", default: 1, min: 0.2, max: 2, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.58, 0.8, 1][C.quality] || 0.8,
    applySettings(view, C, previous, initial) {
      if (initial || !previous || C.view !== previous.view) view.S.dist = 11;
    },
    drawCustom: muse13DrawMeshView,
    setUniforms(gl, U, S, _cam, C) {
      const c = camHy3(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), C.animate ? S.time : 0);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamFwd"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform1f(U("uFovDeg"), 55);
      gl.uniform1f(U("uRs"), 1);
      gl.uniform1i(U("uSteps"), [140, 220, 320][C.quality] || 220);
      gl.uniform1f(U("uStepScale"), C.stepScale);
      gl.uniform1i(U("uBending"), C.bending ? 1 : 0);
      gl.uniform1i(U("uDoppler"), C.doppler ? 1 : 0);
      gl.uniform1i(U("uDisk"), C.disk ? 1 : 0);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uBgGain"), C.bgGain);
    },
  },

  // ---- Muse Spark 1.2 : Schwarzschild Verlet tracer + ray-traced Flamm grid ----
  muse12: {
    shader: "/live/shaders/muse12.frag",
    init: { yaw: 0.55, pitch: 0.38, dist: 22.4 },
    distMin: 7.2, distMax: 60.8, pitchMin: -1.48, pitchMax: 1.48,
    dragYaw: -0.005, dragPitch: 0.005,
    autospin: (C) => C.autoOrbit ? 0.13 : 0,
    maxRes: 1050, dprCap: 1.4,
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "halo", label: "Photon halo", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto-orbit", type: "toggle", default: true },
      { key: "radius", label: "Black hole mass", type: "range", default: 1.6, min: 0.6, max: 3.5, step: 0.1, precision: 1, suffix: " Rs" },
    ],
    setUniforms(gl, U, S, _cam, C) {
      const c = camHy3(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("iResolution"), S.w, S.h);
      gl.uniform1f(U("iTime"), S.time);
      gl.uniform3f(U("camPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("camForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("camRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("camUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("bhPos"), 0, 0, 0);
      gl.uniform1f(U("Rs"), C.radius);
      gl.uniform1f(U("tanHalfFov"), Math.tan(30 * Math.PI / 180));
      gl.uniform1i(U("showGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("showDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("haloBoost"), C.halo ? 1 : 0);
    },
  },

  // ---- Kimi K3 : Metal Schwarzschild Binet-equation tracer + Flamm grid ----
  kimi: {
    shader: "/live/shaders/kimi.frag",
    init: { yaw: 0.0, pitch: 0.30, dist: 18 },
    distMin: 4, distMax: 60, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: 0.005, dragPitch: -0.005, autospin: 0.05,
    maxRes: 760, dprCap: 1.0,
    gridOverlay: { control: "grid", camera: camKimi, rs: 1.0, yOffset: 0, tanHalfFov: Math.tan(38 * Math.PI / 360), surface: true, side: -1 },
    controls: [
      { key: "grid", label: "Flamm grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "sky", label: "Sky", type: "toggle", default: true },
      { key: "beaming", label: "Relativistic beaming", type: "toggle", default: true },
      { key: "redshift", label: "Gravitational redshift", type: "toggle", default: true },
      { key: "raytrace", label: "Ray-traced background", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.3, max: 3, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.72, 1, 1.18][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camKimi(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(38 * Math.PI / 360));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1i(U("uMaxSteps"), [360, 620, 700][C.quality] || 620);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowSky"), C.sky ? 1 : 0);
      gl.uniform1i(U("uBeaming"), C.beaming ? 1 : 0);
      gl.uniform1i(U("uRedshift"), C.redshift ? 1 : 0);
      gl.uniform1i(U("uRaytrace"), C.raytrace ? 1 : 0);
    },
  },

  // ---- Inkling : OpenGL Einstein-deflection pass + thermal disk + trapdoor grid ----
  inkling: {
    shader: "/live/shaders/inkling.frag",
    init: { yaw: 0.42, pitch: 0.32, dist: 21 },
    distMin: 8, distMax: 44, pitchMin: -0.15, pitchMax: 1.25,
    dragYaw: 0.005, dragPitch: 0.004, autospin: 0.12,
    maxRes: 1050, dprCap: 1.5,
    gridOverlay: {
      kind: "inkling", control: "grid", camera: camKimi, rs: 2.0,
      yOffset: 0, tanHalfFov: Math.tan(70 * Math.PI / 360),
    },
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "lensing", label: "Gravitational lensing", type: "toggle", default: true },
      { key: "stars", label: "Star field", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.35, max: 2.5, step: 0.05, suffix: "x" },
      { key: "diskBrightness", label: "Disk brightness", type: "range", default: 1, min: 0.2, max: 2.5, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.68, 1, 1.22][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camKimi(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(70 * Math.PI / 360));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uRs"), 2.0);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uDiskBrightness"), C.diskBrightness);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uShowLensing"), C.lensing ? 1 : 0);
      gl.uniform1i(U("uShowStars"), C.stars ? 1 : 0);
    },
  },

  // ---- Grok 4.6 : Schwarzschild Hamiltonian tracer + Flamm fabric ----
  grok46: {
    shader: "/live/shaders/grok46.frag?v=1", hdr: true, exposure: 0.95, bloom: 0.48,
    init: { yaw: 1.22, pitch: 0.36, dist: 44 },
    distMin: 6.5, distMax: 120, pitchMin: -1.15, pitchMax: 1.25,
    dragYaw: -0.0055, dragPitch: 0.0055, autospin: 0,
    maxRes: 1280, dprCap: 1.5,
    gridOverlay: {
      kind: "grok46", control: "grid", camera: camKimi, rs: 2.0,
      yOffset: 0, tanHalfFov: Math.tan(48 * Math.PI / 360), surface: false,
    },
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "halo", label: "Photon-sphere halo", type: "toggle", default: true },
      { key: "stars", label: "Star field", type: "toggle", default: true },
      { key: "bloom", label: "Bloom", type: "toggle", default: true },
      { key: "animate", label: "Disk motion", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: [
        { label: "Fast", value: 0 }, { label: "Default", value: 1 },
        { label: "Sharp", value: 2 }, { label: "High", value: 3 },
      ] },
      { key: "exposure", label: "Exposure", type: "range", default: 0.95, min: 0.15, max: 4, step: 0.05, suffix: "x" },
      { key: "bloomStrength", label: "Bloom strength", type: "range", default: 0.48, min: 0, max: 1.5, step: 0.02, precision: 2 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 12.5, min: 8, max: 24, step: 0.5, suffix: " M" },
    ],
    renderScale: (C) => [0.45, 0.62, 0.85, 1][C.quality] || 0.62,
    setUniforms(gl, U, S, cam, C) {
      const c = camKimi(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(48 * Math.PI / 360));
      gl.uniform1f(U("uTime"), C.animate ? S.time : 0);
      gl.uniform1f(U("uMass"), 1.0);
      gl.uniform1i(U("uEnableDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uEnableHalo"), C.halo ? 1 : 0);
      gl.uniform1i(U("uEnableStars"), C.stars ? 1 : 0);
      gl.uniform1i(U("uMaxSteps"), [110, 140, 210, 240][C.quality] || 140);
      gl.uniform1f(U("uDiskOuter"), C.diskOuter);
    },
  },

  // ---- Grok 4.5 : planar Schwarzschild null-geodesic tracer ----
  grok45: {
    shader: "/live/shaders/grok45.frag",
    init: { yaw: 0.35, pitch: 0.45, dist: 28 },
    distMin: 8, distMax: 120, pitchMin: -1.55, pitchMax: 1.55,
    dragYaw: 0.005, dragPitch: -0.005, autospin: 0.04,
    maxRes: 720, dprCap: 1.0,
    gridOverlay: { control: "grid", camera: camHy3, rs: 2.0, yOffset: -0.5, tanHalfFov: Math.tan(50 * Math.PI / 360), surface: true },
    controls: [
      { key: "grid", label: "Spacetime grid", type: "toggle", default: true },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: [
        { label: "Fast", value: 0 }, { label: "Normal", value: 1 }, { label: "High", value: 2 },
      ] },
    ],
    renderScale: (C) => [0.72, 1, 1.18][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camHy3(S.yaw, S.pitch, S.dist);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(50 * Math.PI / 360));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uM"), 1.0);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1i(U("uMaxSteps"), [180, 320, 500][C.quality] || 320);
      gl.uniform1f(U("uDiskInner"), 6.0);
      gl.uniform1f(U("uDiskOuter"), 18.0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uQuality"), C.quality);
    },
  },

  // ---- Grok Build 0.1 : OpenGL Schwarzschild geodesic shader ----
  grokbuild: {
    shader: "/live/shaders/grokbuild.frag",
    init: { yaw: 0.8, pitch: 1.35, dist: 22 },
    distMin: 3, distMax: 140, pitchMin: 0.05, pitchMax: 3.13,
    dragYaw: 0.0072, dragPitch: 0.0054, autospin: 0.035,
    maxRes: 460, dprCap: 1.0,
    controls: [
      { key: "diskBrightness", label: "Disk brightness", type: "range", default: 1.8, min: 0.2, max: 5, step: 0.1, suffix: "x" },
      { key: "stepSize", label: "Step size", type: "range", default: 0.085, min: 0.01, max: 0.4, step: 0.005, precision: 3 },
      { key: "maxSteps", label: "Ray steps", type: "range", default: 92, min: 20, max: 240, step: 4 },
      { key: "exposure", label: "Exposure", type: "range", default: 1.35, min: 0.4, max: 4, step: 0.05, suffix: "x" },
      { key: "diskInner", label: "Disk inner radius", type: "range", default: 6, min: 3.5, max: 12, step: 0.5 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 22, min: 13, max: 36, step: 1 },
    ],
    setUniforms(gl, U, S, cam, C) {
      const c = camGrokBuild(S.yaw, S.pitch, S.dist);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamForward"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uFov"), 52 * Math.PI / 180);
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      gl.uniform1f(U("uRs"), 2.0);
      gl.uniform1f(U("uDiskInner"), C.diskInner);
      gl.uniform1f(U("uDiskOuter"), C.diskOuter);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform1i(U("uMaxSteps"), C.maxSteps);
      gl.uniform1f(U("uStepSize"), C.stepSize);
      gl.uniform1f(U("uExposure"), C.exposure);
      gl.uniform1f(U("uDiskBrightness"), C.diskBrightness);
      gl.uniform1f(U("uEpsilon"), 0.0008);
    },
  },

  // ---- Fable 5.1 : submitted Kerr Mino-time tracer + native embedding view ----
  fable51: {
    shader: "/live/shaders/fable51.frag?v=5", hdr: true, exposure: 1.0, bloom: 0.34,
    init: { yaw: 0, pitch: 14 * Math.PI / 180, dist: 42 },
    distMin: 4, distMax: 200, pitchMin: -1.53, pitchMax: 1.53,
    dragYaw: -0.005, dragPitch: 0.005, autospin: (C) => C.autoOrbit ? 0.1 : 0,
    maxRes: 1120, dprCap: 1.35,
    controls: [
      { key: "view", label: "View", type: "segments", default: 0, options: [
        { label: "Ray trace", value: 0 }, { label: "Kerr embedding", value: 1 },
      ] },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Lensed coordinate grid", type: "toggle", default: true },
      { key: "stars", label: "Procedural stars", type: "toggle", default: true },
      { key: "limb", label: "Limb darkening", type: "toggle", default: true },
      { key: "animate", label: "Disk rotation", type: "toggle", default: true },
      { key: "autoOrbit", label: "Auto orbit", type: "toggle", default: false },
      { key: "accumulate", label: "Progressive anti-aliasing", type: "toggle", default: true },
      { key: "spin", label: "Kerr spin", type: "range", default: 0.9, min: 0, max: 0.998, step: 0.002, precision: 3 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 24, min: 8, max: 60, step: 1, suffix: " M" },
      { key: "temperature", label: "Temperature scale", type: "range", default: 40000, min: 12000, max: 90000, step: 1000, suffix: " K" },
      { key: "turbulence", label: "Disk turbulence", type: "range", default: 0.35, min: 0, max: 1, step: 0.05, precision: 2 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.15, max: 3, step: 0.05, suffix: "x" },
      { key: "bloomStrength", label: "Bloom", type: "range", default: 0.34, min: 0, max: 1.2, step: 0.02, suffix: "x" },
    ],
    renderScale: (C) => [0.26, 0.37, 0.48][C.quality] || 0.37,
    drawCustom: opus5DrawMeshView,
    setUniforms(gl, U, S, _cam, C) {
      const halton = (index, base) => {
        let f = 1, value = 0;
        for (let i = index; i > 0; i = Math.floor(i / base)) { f /= base; value += f * (i % base); }
        return value;
      };
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform2f(U("uJitter"), S.sample ? halton(S.sample, 2) - 0.5 : 0, S.sample ? halton(S.sample, 3) - 0.5 : 0);
      gl.uniform1f(U("uSpin"), C.spin);
      gl.uniform1f(U("uCamR"), S.dist);
      gl.uniform1f(U("uCamTh"), Math.PI / 2 - S.pitch);
      gl.uniform1f(U("uCamPh"), S.yaw);
      gl.uniform1f(U("uTanHalf"), Math.tan(48 * Math.PI / 360));
      gl.uniform1f(U("uTimeCoord"), C.animate ? S.time * 12 : 0);
      gl.uniform1f(U("uDiskOut"), C.diskOuter);
      gl.uniform1f(U("uGridOut"), 55);
      gl.uniform1f(U("uGridSpacing"), 2);
      gl.uniform1f(U("uEps"), [0.075, 0.05, 0.035][C.quality] || 0.05);
      gl.uniform1f(U("uEpsAng"), [0.075, 0.05, 0.035][C.quality] || 0.05);
      gl.uniform1f(U("uTurbulence"), C.turbulence);
      gl.uniform1f(U("uTempScale"), C.temperature);
      gl.uniform1f(U("uSkyBrightness"), 1);
      gl.uniform1i(U("uDiskOn"), C.disk ? 1 : 0);
      gl.uniform1i(U("uGridOn"), C.grid ? 1 : 0);
      gl.uniform1i(U("uStarsOn"), C.stars ? 1 : 0);
      gl.uniform1i(U("uLimbOn"), C.limb ? 1 : 0);
      gl.uniform1i(U("uMaxSteps"), [55, 95, 160][C.quality] || 95);
    },
  },

  // ---- Opus 5 : adaptive Kerr Cash-Karp tracer + native curvature views ----
  opus5: {
    shader: "/live/shaders/opus5.frag", hdr: true, exposure: 0.55, bloom: 0.22,
    init: { yaw: 0, pitch: 10 * Math.PI / 180, dist: 48 },
    distMin: 4, distMax: 180, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.005, dragPitch: 0.005, autospin: 0,
    maxRes: 1280, dprCap: 1.5,
    controls: [
      { key: "view", label: "View", type: "segments", default: 0, options: [
        { label: "Ray trace", value: 0 }, { label: "Embedding", value: 1 }, { label: "Light cones", value: 2 },
      ] },
      { key: "quality", label: "Quality", type: "segments", default: 2, options: QUALITY_3 },
      { key: "diskMode", label: "Accretion flow", type: "segments", default: 2, options: [
        { label: "Off", value: 0 }, { label: "Disk", value: 1 }, { label: "Disk + halo", value: 2 }, { label: "Halo", value: 3 },
      ] },
      { key: "colorMode", label: "Planck color", type: "segments", default: 0, options: [
        { label: "Visible", value: 0 }, { label: "Physical", value: 1 },
      ] },
      { key: "tonemap", label: "Tone curve", type: "segments", default: 0, options: [
        { label: "Asinh", value: 0 }, { label: "ACES", value: 1 }, { label: "Linear", value: 2 },
      ] },
      { key: "debugView", label: "Diagnostic", type: "segments", default: 0, options: [
        { label: "Beauty", value: 0 }, { label: "Steps", value: 1 }, { label: "Constraint", value: 2 }, { label: "Redshift", value: 3 },
      ] },
      { key: "skyGrid", label: "Lensed sky grid", type: "toggle", default: false },
      { key: "animate", label: "Disk rotation", type: "toggle", default: true },
      { key: "accumulate", label: "Progressive accumulation", type: "toggle", default: true },
      { key: "spin", label: "Kerr spin", type: "range", default: 0.9, min: 0, max: 0.998, step: 0.002, precision: 3 },
      { key: "diskOuter", label: "Disk outer radius", type: "range", default: 24, min: 8, max: 60, step: 1, suffix: " M" },
      { key: "massPower", label: "Black hole mass", type: "range", default: 7, min: 1, max: 10, step: 0.25, precision: 2, format: (value) => `10^${Number(value).toFixed(2)} solar masses` },
      { key: "eddRatio", label: "Eddington ratio", type: "range", default: 0.3, min: 0.01, max: 2, step: 0.01, precision: 2 },
      { key: "turbulence", label: "Disk turbulence", type: "range", default: 0.55, min: 0, max: 1, step: 0.05, precision: 2 },
      { key: "nebula", label: "Nebula", type: "range", default: 0.35, min: 0, max: 1, step: 0.05, precision: 2 },
      { key: "stars", label: "Star brightness", type: "range", default: 1, min: 0, max: 2, step: 0.05, suffix: "x" },
      { key: "corona", label: "Halo brightness", type: "range", default: 0.3, min: 0, max: 1.5, step: 0.05, suffix: "x" },
      { key: "raySteps", label: "Ray steps", type: "range", default: 900, min: 240, max: 1400, step: 20 },
      { key: "exposure", label: "Exposure", type: "range", default: 0.55, min: 0.1, max: 2, step: 0.05, suffix: "x" },
      { key: "bloomStrength", label: "Bloom", type: "range", default: 0.22, min: 0, max: 1.2, step: 0.02, suffix: "x" },
    ],
    renderScale: (C) => [0.52, 0.74, 1.0][C.quality] || 1.0,
    drawCustom: opus5DrawMeshView,
    setUniforms(gl, U, S, cam, C) {
      const c = camFable(S.yaw, S.pitch, S.dist), radii = kerrRadii(C.spin);
      const tempRef = 285000 * Math.pow(C.eddRatio / 0.3, 0.25) * Math.pow(1e7 / Math.pow(10, C.massPower), 0.25);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uSpin"), C.spin);
      gl.uniform3f(U("uCamP"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamF"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform3f(U("uCamR"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamU"), c.up[0], c.up[1], c.up[2]);
      gl.uniform1f(U("uTanHalf"), Math.tan(45 * Math.PI / 360));
      gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
      const halton = (index, base) => {
        let f = 1, value = 0;
        for (let i = index; i > 0; i = Math.floor(i / base)) { f /= base; value += f * (i % base); }
        return value;
      };
      gl.uniform2f(U("uJitter"), S.sample ? halton(S.sample, 2) - 0.5 : 0, S.sample ? halton(S.sample, 3) - 0.5 : 0);
      gl.uniform1f(U("uTimeCoord"), C.animate ? S.time * 12 : 0);
      gl.uniform1i(U("uDiskMode"), C.diskMode);
      gl.uniform1f(U("uDiskIn"), radii.isco);
      gl.uniform1f(U("uDiskOut"), C.diskOuter);
      gl.uniform1f(U("uDiskBright"), 1);
      gl.uniform1f(U("uTurb"), C.turbulence);
      gl.uniform1f(U("uTempRef"), tempRef);
      gl.uniform1f(U("uTempA"), C.colorMode ? 1 : 11000 / Math.max(tempRef, 1));
      gl.uniform1f(U("uTempB"), 1);
      gl.uniform1f(U("uCoronaH"), 0.20);
      gl.uniform1f(U("uCoronaOut"), Math.min(C.diskOuter * 1.6, 70));
      gl.uniform1f(U("uCoronaBright"), C.corona);
      gl.uniform1f(U("uCoronaTemp"), tempRef * 0.85);
      gl.uniform1f(U("uStarBright"), C.stars);
      gl.uniform1f(U("uSkyGrid"), C.skyGrid ? 0.9 : 0);
      gl.uniform1f(U("uNebula"), C.nebula);
      gl.uniform1f(U("uTol"), [8e-6, 4e-6, 2e-6][C.quality] || 2e-6);
      gl.uniform1i(U("uMaxSteps"), C.raySteps);
      gl.uniform1f(U("uRFar"), [500, 850, 1200][C.quality] || 1200);
      gl.uniform1i(U("uDebugView"), C.debugView);
      gl.uniform1f(U("uExposure"), 1);
    },
  },

  // ---- Fable 5 : HDR geodesic tracer + bloom/ACES (z-up world) ----
  fable: {
    shader: "/live/shaders/fable.frag", hdr: true, exposure: 1.0, bloom: 0.5,
    init: { yaw: -2.30, pitch: 0.30, dist: 26 },
    distMin: 6.5, distMax: 90, pitchMin: -1.5, pitchMax: 1.5,
    dragYaw: -0.005, dragPitch: 0.005, autospin: 0.05,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "grid", label: "Spacetime grid", type: "toggle", default: false },
      { key: "stars", label: "Stars", type: "toggle", default: true },
      { key: "quality", label: "Quality", type: "segments", default: 1, options: QUALITY_3 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.1, max: 4, step: 0.05, suffix: "x" },
      { key: "bloomStrength", label: "Bloom", type: "range", default: 0.5, min: 0, max: 1.5, step: 0.05, suffix: "x" },
      { key: "diskGain", label: "Disk gain", type: "range", default: 1, min: 0.2, max: 3, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.65, 1, 1.3][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camFable(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uRes"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), c.pos[0], c.pos[1], c.pos[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamFwd"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(55 * Math.PI / 360));
      gl.uniform1i(U("uDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uGrid"), C.grid ? 1 : 0);
      gl.uniform1i(U("uStars"), C.stars ? 1 : 0);
      gl.uniform1f(U("uDiskGain"), C.diskGain);
    },
  },

  // ---- Opus 4.8 : HDR geodesic tracer + bloom/ACES (y-up world) ----
  opus: {
    shader: "/live/shaders/opus.frag", hdr: true, exposure: 1.0, bloom: 0.6,
    init: { yaw: 0.62, pitch: 0.34, dist: 22 },
    distMin: 3.5, distMax: 130, pitchMin: -1.45, pitchMax: 1.45,
    dragYaw: -0.006, dragPitch: 0.006, autospin: 0.05,
    controls: [
      { key: "disk", label: "Accretion disk", type: "toggle", default: true },
      { key: "background", label: "Background", type: "segments", default: 0, options: [
        { label: "Stars", value: 0 }, { label: "Lensing grid", value: 1 },
      ] },
      { key: "quality", label: "Resolution", type: "segments", default: 1, options: QUALITY_3 },
      { key: "steps", label: "Ray steps", type: "range", default: 320, min: 60, max: 640, step: 20 },
      { key: "exposure", label: "Exposure", type: "range", default: 1, min: 0.2, max: 4, step: 0.05, suffix: "x" },
      { key: "bloomStrength", label: "Bloom", type: "range", default: 0.6, min: 0, max: 1.5, step: 0.05, suffix: "x" },
      { key: "diskBrightness", label: "Disk brightness", type: "range", default: 0.7, min: 0.2, max: 2.5, step: 0.05, suffix: "x" },
    ],
    renderScale: (C) => [0.65, 1, 1.3][C.quality] || 1,
    setUniforms(gl, U, S, cam, C) {
      const c = camClaude(S.yaw, S.pitch, S.dist);
      gl.uniform2f(U("uResolution"), S.w, S.h);
      gl.uniform1f(U("uTime"), S.time);
      gl.uniform3f(U("uCamPos"), c.eye[0], c.eye[1], c.eye[2]);
      gl.uniform3f(U("uCamRight"), c.right[0], c.right[1], c.right[2]);
      gl.uniform3f(U("uCamUp"), c.up[0], c.up[1], c.up[2]);
      gl.uniform3f(U("uCamFwd"), c.fwd[0], c.fwd[1], c.fwd[2]);
      gl.uniform1f(U("uTanHalfFov"), Math.tan(50 * Math.PI / 360));
      gl.uniform1i(U("uSteps"), C.steps);
      gl.uniform1f(U("uDiskInner"), 3.0);
      gl.uniform1f(U("uDiskOuter"), 10.0);
      gl.uniform1i(U("uShowDisk"), C.disk ? 1 : 0);
      gl.uniform1i(U("uBgMode"), C.background);
      gl.uniform1f(U("uDiskBrightness"), C.diskBrightness);
    },
  },
};

const _bhShaderCache = {};
async function bhFetchShader(id) {
  const cfg = BH_LIVE[id];
  if (!cfg) throw new Error("no-live-config");
  if (cfg.prepare) await cfg.prepare();
  if (!_bhShaderCache[id]) {
    const response = await fetch(cfg.shader);
    if (!response.ok) throw new Error(`shader-http-${response.status}`);
    _bhShaderCache[id] = await response.text();
  }
  return _bhShaderCache[id];
}
