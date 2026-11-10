/* Inkling 2 Small: submitted OpenGL 4.1 ray tracer, Flamm grid, horizon occluder
   and marker rings. GLSL 410 sources are the model's, retargeted to WebGL2.
   The native HUD is not recreated. */
'use strict';

function inkling2LookAt(eye, fwd, right, up) {
  const m = new Float32Array(16);
  m[0] = right[0]; m[1] = up[0]; m[2] = -fwd[0];
  m[4] = right[1]; m[5] = up[1]; m[6] = -fwd[1];
  m[8] = right[2]; m[9] = up[2]; m[10] = -fwd[2];
  m[12] = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
  m[13] = -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]);
  m[14] = fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2];
  m[15] = 1;
  return m;
}
function inkling2Perspective(fovY, aspect) {
  const f = 1 / Math.tan(fovY * 0.5), near = 0.5, far = 500;
  const m = new Float32Array(16);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) / (near - far);
  m[11] = -1;
  m[14] = (2 * far * near) / (near - far);
  return m;
}
function inkling2Mul(a, b) {
  const r = new Float32Array(16);
  for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 4; row++) {
      let sum = 0;
      for (let k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
      r[col * 4 + row] = sum;
    }
  }
  return r;
}
function inkling2Mesh(gl, data, attribs, indices) {
  const vao = gl.createVertexArray(), vbo = gl.createBuffer();
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  let offset = 0;
  const stride = attribs.reduce((n, a) => n + a, 0) * 4;
  attribs.forEach((count, loc) => {
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, count, gl.FLOAT, false, stride, offset);
    offset += count * 4;
  });
  let count = data.length / attribs.reduce((n, a) => n + a, 0);
  if (indices) {
    const ebo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
    count = indices.length;
  }
  gl.bindVertexArray(null);
  return { vao, count };
}
function inkling2Geometry() {
  const rOuter = 50, yScale = 1.35, rIn = 2.0015, nRings = 56, ringSegs = 220, k = 2.35;
  const nSpokes = 56, spokeSamples = 190;
  const zRim = Math.sqrt(8 * (rOuter - 2)) * yScale;
  const radiusOf = (t) => 2 + (Math.exp(t * k) - 1) / (Math.exp(k) - 1) * (rOuter - 2);
  const flamm = (r) => Math.sqrt(Math.max(0, 8 * (r - 2))) * yScale;
  const n = (nRings + 1) * ringSegs * 2 + nSpokes * spokeSamples * 2;
  const lines = new Float32Array(n * 4);
  let o = 0;
  const put = (r, az) => {
    const depth = Math.min(1, Math.max(0, 1 - flamm(r) / zRim));
    lines[o++] = Math.cos(az) * r;
    lines[o++] = flamm(r) - zRim;
    lines[o++] = Math.sin(az) * r;
    lines[o++] = depth;
  };
  for (let i = 0; i <= nRings; i++) {
    const r = Math.max(rIn, radiusOf(i / nRings));
    for (let j = 0; j < ringSegs; j++) {
      put(r, 2 * Math.PI * j / ringSegs);
      put(r, 2 * Math.PI * (j + 1) / ringSegs);
    }
  }
  for (let s = 0; s < nSpokes; s++) {
    const az = 2 * Math.PI * s / nSpokes;
    for (let j = 0; j < spokeSamples; j++) {
      put(Math.max(rIn, radiusOf(j / spokeSamples)), az);
      put(Math.max(rIn, radiusOf((j + 1) / spokeSamples)), az);
    }
  }
  const stacks = 40, slices = 64, radius = 2;
  const sphere = new Float32Array((stacks + 1) * (slices + 1) * 6);
  const sphereIdx = new Uint16Array(stacks * slices * 6);
  let sv = 0;
  for (let i = 0; i <= stacks; i++) {
    const phi = Math.PI * i / stacks, y = Math.cos(phi) * radius, rr = Math.sin(phi) * radius;
    for (let j = 0; j <= slices; j++) {
      const th = 2 * Math.PI * j / slices, x = rr * Math.cos(th), z = rr * Math.sin(th);
      sphere[sv++] = x; sphere[sv++] = y; sphere[sv++] = z;
      sphere[sv++] = x / radius; sphere[sv++] = y / radius; sphere[sv++] = z / radius;
    }
  }
  let si = 0;
  for (let i = 0; i < stacks; i++) {
    for (let j = 0; j < slices; j++) {
      const a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;
      sphereIdx[si++] = a; sphereIdx[si++] = b; sphereIdx[si++] = c;
      sphereIdx[si++] = b; sphereIdx[si++] = d; sphereIdx[si++] = c;
    }
  }
  const ring = (radius) => {
    const data = new Float32Array((257) * 3);
    for (let i = 0; i <= 256; i++) {
      const a = 2 * Math.PI * i / 256;
      data[i * 3] = Math.cos(a) * radius;
      data[i * 3 + 2] = Math.sin(a) * radius;
    }
    return data;
  };
  return { lines, sphere, sphereIdx, photon: ring(3), isco: ring(6) };
}

const inkling2Contexts = new WeakMap();
function inkling2State(view) {
  const gl = view.gl;
  if (inkling2Contexts.has(gl)) return inkling2Contexts.get(gl);
  const src = BH_LIVE.inkling2small.sources;
  const compile = (frag, vert) => view._compile(frag, vert);
  const geo = inkling2Geometry();
  const state = {
    grid: compile(src.gridFrag, src.gridVert),
    horizon: compile(src.horizonFrag, src.horizonVert),
    ring: compile(src.ringFrag, src.ringVert),
    lines: inkling2Mesh(gl, geo.lines, [3, 1]),
    sphere: inkling2Mesh(gl, geo.sphere, [3, 3], geo.sphereIdx),
    photon: inkling2Mesh(gl, geo.photon, [3]),
    isco: inkling2Mesh(gl, geo.isco, [3]),
  };
  const loc = (p, n) => gl.getUniformLocation(p, n);
  state.gVP = loc(state.grid, 'uViewProj');
  state.gTime = loc(state.grid, 'uTime');
  state.gAlpha = loc(state.grid, 'uGridAlpha');
  state.hVP = loc(state.horizon, 'uViewProj');
  state.hCam = loc(state.horizon, 'uCamPos');
  state.rVP = loc(state.ring, 'uViewProj');
  state.rColor = loc(state.ring, 'uRingColor');
  state.rAlpha = loc(state.ring, 'uRingAlpha');
  inkling2Contexts.set(gl, state);
  return state;
}
function inkling2Draw(view, C) {
  const gl = view.gl;
  const S = view.S;
  gl.disable(gl.DEPTH_TEST);
  gl.depthMask(false);
  gl.disable(gl.BLEND);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.bindVertexArray(view._vao);
  gl.useProgram(view._program);
  view.cfg.setUniforms(gl, (n) => view.U(n), S, null, C);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  const st = inkling2State(view);
  const cam = camKimi(S.yaw, S.pitch, S.dist);
  const aspect = S.w / Math.max(1, S.h);
  const vp = inkling2Mul(inkling2Perspective(55 * Math.PI / 180, aspect), inkling2LookAt(cam.pos, cam.fwd, cam.right, cam.up));
  gl.enable(gl.DEPTH_TEST);
  gl.depthMask(true);
  gl.depthFunc(gl.LESS);
  gl.useProgram(st.horizon);
  gl.uniformMatrix4fv(st.hVP, false, vp);
  gl.uniform3fv(st.hCam, cam.pos);
  gl.bindVertexArray(st.sphere.vao);
  gl.drawElements(gl.TRIANGLES, st.sphere.count, gl.UNSIGNED_SHORT, 0);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.ONE, gl.ONE);
  gl.depthMask(false);
  if (C.grid) {
    gl.useProgram(st.grid);
    gl.uniformMatrix4fv(st.gVP, false, vp);
    gl.uniform1f(st.gTime, 4.2 + (C.animate ? S.time : 0));
    gl.uniform1f(st.gAlpha, 0.9);
    gl.bindVertexArray(st.lines.vao);
    gl.drawArrays(gl.LINES, 0, st.lines.count);
  }
  gl.useProgram(st.ring);
  gl.uniformMatrix4fv(st.rVP, false, vp);
  const ring = (mesh, color, alpha) => {
    gl.uniform3fv(st.rColor, color);
    gl.uniform1f(st.rAlpha, alpha);
    gl.bindVertexArray(mesh.vao);
    gl.drawArrays(gl.LINE_STRIP, 0, mesh.count);
  };
  if (C.photonRing) ring(st.photon, [1, 0.58, 0.22], 0.55);
  if (C.iscoRing) ring(st.isco, [0.38, 0.78, 1], 0.5);
  gl.bindVertexArray(null);
  gl.depthMask(true);
  gl.disable(gl.DEPTH_TEST);
  gl.disable(gl.BLEND);
  return true;
}

BH_LIVE.inkling2small = {
  shader: '/live/shaders/inkling2small.frag?v=1',
  init: { yaw: -0.62, pitch: 0.30, dist: 38 },
  distMin: 4.6, distMax: 120, pitchMin: -1.52, pitchMax: 1.52,
  dragYaw: -0.0052, dragPitch: 0.0052,
  maxRes: 1280, dprCap: 1.25,
  autospin: (C) => C.autoOrbit ? 0.085 : 0,
  renderScale: (C) => [0.42, 0.68, 1][C.quality ?? 1],
  controls: [
    { key: 'quality', label: 'Resolution', type: 'segments', default: 1, options: QUALITY_3 },
    { key: 'disk', label: 'Accretion disk', type: 'toggle', default: true },
    { key: 'lensing', label: 'Lensing', type: 'toggle', default: true },
    { key: 'stars', label: 'Star field', type: 'toggle', default: true },
    { key: 'grid', label: 'Spacetime grid', type: 'toggle', default: true },
    { key: 'halo', label: 'Photon halo', type: 'segments', default: 0.9, options: [
      { value: 0, label: 'Off' }, { value: 0.9, label: '0.9' }, { value: 1.7, label: '1.7' },
    ] },
    { key: 'photonRing', label: 'Photon sphere', type: 'toggle', default: true },
    { key: 'iscoRing', label: 'ISCO ring', type: 'toggle', default: true },
    { key: 'animate', label: 'Disk motion', type: 'toggle', default: true },
    { key: 'autoOrbit', label: 'Auto orbit', type: 'toggle', default: false },
    { key: 'exposure', label: 'Exposure', type: 'range', default: 1, min: 0.35, max: 2.6, step: 0.05, suffix: 'x' },
  ],
  async prepare() {
    const load = async (file) => {
      const r = await fetch(`/live/shaders/${file}?v=1`);
      if (!r.ok) throw new Error(file);
      return r.text();
    };
    this.sources = {
      gridVert: await load('inkling2small-grid.vert'),
      gridFrag: await load('inkling2small-grid.frag'),
      horizonVert: await load('inkling2small-horizon.vert'),
      horizonFrag: await load('inkling2small-horizon.frag'),
      ringVert: await load('inkling2small-ring.vert'),
      ringFrag: await load('inkling2small-ring.frag'),
    };
  },
  drawCustom: inkling2Draw,
  setUniforms(gl, U, S, _cam, C) {
    const c = camKimi(S.yaw, S.pitch, S.dist);
    gl.uniform2f(U('uResolution'), S.w, S.h);
    for (const [n, k] of [['uCamPos', 'pos'], ['uCamFwd', 'fwd'], ['uCamRight', 'right'], ['uCamUp', 'up']]) gl.uniform3fv(U(n), c[k]);
    gl.uniform1f(U('uTanHalfFov'), Math.tan(55 * Math.PI / 360));
    gl.uniform1f(U('uAspect'), S.w / Math.max(1, S.h));
    gl.uniform1f(U('uTime'), 4.2 + (C.animate ? S.time : 0));
    gl.uniform1f(U('uExposure'), C.exposure ?? 1);
    gl.uniform1i(U('uLensing'), C.lensing ? 1 : 0);
    gl.uniform1i(U('uDiskOn'), C.disk ? 1 : 0);
    gl.uniform1i(U('uStarsOn'), C.stars ? 1 : 0);
    gl.uniform1f(U('uHaloStrength'), C.halo ?? 0.9);
    gl.uniform1f(U('uDiskInner'), 6);
    gl.uniform1f(U('uDiskOuter'), 30);
    gl.uniform1f(U('uDiskPeakTemp'), 6200);
    gl.uniform1f(U('uDiskIntensity'), 0.85);
    gl.uniform1f(U('uDiskSwirl'), 0.85);
    gl.uniform1i(U('uDebug'), 0);
  },
};
