/* Muse Ember: submitted Cartesian RK4 tracer and Flamm trapdoor grid.
   WebGL2 adaptations: GLSL 300 es, vUV to match the site vertex, and a
   constant 280-step loop (the submitted maximum) with the uSteps break kept.
   The native HUD is not recreated. Ray and grid stay separate views. */
"use strict";

const museEmberGpu = new WeakMap();

function museEmberMVP(eye, aspect) {
  const fovY = 52 * Math.PI / 180, near = 0.1, far = 500;
  const f = 1 / Math.tan(fovY * 0.5);
  const proj = [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, (2 * far * near) / (near - far), 0];
  const center = [0, 0, -1.2], up = [0, 1, 0];
  let fx = center[0] - eye[0], fy = center[1] - eye[1], fz = center[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl; fy /= fl; fz /= fl;
  let sx = fy * up[2] - fz * up[1], sy = fz * up[0] - fx * up[2], sz = fx * up[1] - fy * up[0];
  const sl = Math.hypot(sx, sy, sz) || 1;
  sx /= sl; sy /= sl; sz /= sl;
  const ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
  const view = [
    sx, ux, -fx, 0,
    sy, uy, -fy, 0,
    sz, uz, -fz, 0,
    -(sx * eye[0] + sy * eye[1] + sz * eye[2]),
    -(ux * eye[0] + uy * eye[1] + uz * eye[2]),
    fx * eye[0] + fy * eye[1] + fz * eye[2],
    1,
  ];
  const m = new Float32Array(16);
  for (let c = 0; c < 4; c++) {
    for (let row = 0; row < 4; row++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += proj[k * 4 + row] * view[c * 4 + k];
      m[c * 4 + row] = s;
    }
  }
  return m;
}

function museEmberState(view) {
  const gl = view.gl;
  if (museEmberGpu.has(gl)) return museEmberGpu.get(gl);
  const cfg = BH_LIVE.museember;
  const program = view._compile(cfg.gridFrag, cfg.gridVert);
  const N = 220, half = 30;
  const pos = new Float32Array((N + 1) * (N + 1) * 3);
  const idx = new Uint32Array(N * N * 6);
  let p = 0;
  for (let j = 0; j <= N; j++) {
    for (let i = 0; i <= N; i++) {
      pos[p++] = -half + (2 * half) * i / N;
      pos[p++] = 0;
      pos[p++] = -half + (2 * half) * j / N;
    }
  }
  let e = 0;
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
      idx[e++] = a; idx[e++] = c; idx[e++] = b;
      idx[e++] = b; idx[e++] = c; idx[e++] = d;
    }
  }
  const gridVAO = gl.createVertexArray();
  const gridVBO = gl.createBuffer();
  const gridIBO = gl.createBuffer();
  gl.bindVertexArray(gridVAO);
  gl.bindBuffer(gl.ARRAY_BUFFER, gridVBO);
  gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gridIBO);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);

  const rings = [
    { r: 2, color: [1, 0.2, 0.05] },
    { r: 3, color: [1, 0.8, 0.25] },
    { r: 6, color: [0.3, 0.85, 1] },
    { r: 19, color: [0.5, 0.5, 0.6] },
  ];
  const S = 200;
  for (const ring of rings) {
    const pts = new Float32Array((S + 1) * 3);
    for (let i = 0; i <= S; i++) {
      const a = i / S * Math.PI * 2;
      pts[i * 3] = Math.cos(a) * ring.r;
      pts[i * 3 + 2] = Math.sin(a) * ring.r;
    }
    ring.vao = gl.createVertexArray();
    const vbo = gl.createBuffer();
    gl.bindVertexArray(ring.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, pts, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);
  }

  const parts = new Float32Array(700 * 3);
  let seed = 12345 >>> 0;
  const rnd = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return (seed >>> 8) / 16777216;
  };
  for (let i = 0; i < 700; i++) {
    parts[i * 3] = rnd();
    parts[i * 3 + 1] = rnd() * Math.PI * 2;
  }
  const partVAO = gl.createVertexArray();
  const partVBO = gl.createBuffer();
  gl.bindVertexArray(partVAO);
  gl.bindBuffer(gl.ARRAY_BUFFER, partVBO);
  gl.bufferData(gl.ARRAY_BUFFER, parts, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);
  gl.bindVertexArray(null);

  const locs = {};
  const state = {
    program, gridVAO, indexCount: idx.length, rings, partVAO,
    loc(name) {
      if (!Object.prototype.hasOwnProperty.call(locs, name)) locs[name] = gl.getUniformLocation(program, name);
      return locs[name];
    },
  };
  museEmberGpu.set(gl, state);
  return state;
}

function museEmberRayUniforms(gl, U, S, cam, C) {
  gl.uniform1f(U("uTime"), S.emberTime || 0);
  gl.uniform2f(U("uResolution"), S.w, S.h);
  gl.uniform1f(U("uAspect"), S.w / Math.max(1, S.h));
  gl.uniform3fv(U("uCamPos"), cam.pos);
  gl.uniform3fv(U("uCamRight"), cam.right);
  gl.uniform3fv(U("uCamUp"), cam.up);
  gl.uniform3fv(U("uCamFwd"), cam.fwd);
  gl.uniform1f(U("uTanFov"), Math.tan((52 * Math.PI / 180) * 0.5));
  gl.uniform1f(U("uRs"), 2);
  gl.uniform1f(U("uEscapeR"), Math.max(80, S.dist * 2));
  gl.uniform1f(U("uDiskBrightness"), C.diskBright ?? 1);
  gl.uniform1i(U("uInnerGlow"), C.glow === false ? 0 : 1);
  gl.uniform1i(U("uSteps"), [120, 180, 280][C.quality ?? 1]);
  gl.uniform1f(U("uTimeScale"), 5);
  gl.uniform1i(U("uDebugMode"), 0);
}

BH_LIVE.museember = {
  shader: "/live/shaders/museember.frag?v=1",
  init: { yaw: 0.6, pitch: 0.45, dist: 42 },
  distMin: 10, distMax: 90, pitchMin: -1.45, pitchMax: 1.45,
  dragYaw: 0.005, dragPitch: 0.005, maxRes: 1280, dprCap: 1.25, hdr: false,
  renderScale: (C) => [0.55, 0.72, 0.9][C.quality ?? 1],
  autospin: (C) => (C.autoOrbit ? 0.06 : 0),
  controls: [
    { key: "viewMode", label: "View", type: "segments", default: 0, options: [{ value: 0, label: "Ray trace" }, { value: 1, label: "Grid" }] },
    { key: "quality", label: "Quality", type: "segments", default: 1, autoHigh: false, options: [{ value: 0, label: "120 steps" }, { value: 1, label: "180 steps" }, { value: 2, label: "280 steps" }] },
    ...bhImportedControls(["animate"]),
    { key: "autoOrbit", label: "Auto orbit", type: "toggle", default: true },
    { key: "glow", label: "Inner glow", type: "toggle", default: true },
    { key: "diskBright", label: "Disk", type: "range", min: 0.2, max: 2.5, step: 0.05, default: 1 },
  ],
  async prepare() {
    const load = async (file) => {
      const res = await fetch(file);
      if (!res.ok) throw new Error("Muse Ember shader unavailable: " + file);
      return res.text();
    };
    [this.gridVert, this.gridFrag] = await Promise.all([
      load("/live/shaders/museember-grid.vert?v=1"),
      load("/live/shaders/museember-grid.frag?v=1"),
    ]);
  },
  applySettings(view, C, previous, initial) {
    if (initial) { view._emberTime = 0; view._emberLast = 0; }
    else if (previous && previous.animate !== false) view._emberTime += view.S.time - view._emberLast;
    view._emberLast = view.S.time;
  },
  setUniforms(gl, U, S, cam, C) { museEmberRayUniforms(gl, U, S, cam, C); },
  drawCustom(view, C) {
    const gl = view.gl, S = view.S;
    if (C.animate !== false) view._emberTime += S.time - view._emberLast;
    view._emberLast = S.time;
    S.emberTime = view._emberTime;
    const cam = camKimi(S.yaw, S.pitch, S.dist);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, S.w, S.h);
    gl.disable(gl.BLEND);
    gl.depthMask(true);
    if ((C.viewMode ?? 0) === 0) {
      gl.disable(gl.DEPTH_TEST);
      gl.useProgram(view._program);
      gl.bindVertexArray(view._vao);
      museEmberRayUniforms(gl, (n) => view.U(n), S, cam, C);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.bindVertexArray(null);
      return true;
    }
    const grid = museEmberState(view);
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.clearColor(0.005, 0.006, 0.012, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(grid.program);
    gl.uniformMatrix4fv(grid.loc("uMVP"), false, museEmberMVP(cam.pos, S.w / Math.max(1, S.h)));
    gl.uniform1f(grid.loc("uRs"), 2);
    gl.uniform1f(grid.loc("uTime"), S.emberTime);
    gl.uniform1i(grid.loc("uMode"), 0);
    gl.bindVertexArray(grid.gridVAO);
    gl.drawElements(gl.TRIANGLES, grid.indexCount, gl.UNSIGNED_INT, 0);
    gl.uniform1i(grid.loc("uMode"), 1);
    for (const ring of grid.rings) {
      gl.uniform3fv(grid.loc("uColor"), ring.color);
      gl.bindVertexArray(ring.vao);
      gl.drawArrays(gl.LINE_STRIP, 0, 201);
    }
    gl.uniform1i(grid.loc("uMode"), 2);
    gl.bindVertexArray(grid.partVAO);
    gl.drawArrays(gl.POINTS, 0, 700);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.bindVertexArray(null);
    return true;
  },
};
