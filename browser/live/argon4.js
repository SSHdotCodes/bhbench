/* Gemini 4 Argon: native ray/grid shaders, exported native mesh geometry,
   bright-pass and two separable blur iterations, then native ACES composite. */
'use strict';
const argon4Contexts = new WeakMap();
function argon4State(view) {
  const gl=view.gl;
  if(argon4Contexts.has(gl))return argon4Contexts.get(gl);
  const cfg=BH_LIVE.argon4,grid=view._compile(cfg.gridFrag,cfg.gridVert),bloom=view._compile(cfg.bloomFrag);
  const state={grid,bloom,GU:n=>gl.getUniformLocation(grid,n),BU:n=>gl.getUniformLocation(bloom,n),meshes:new Map(),targets:[]};
  for(const p of cfg.presets){
    const vao=gl.createVertexArray(),vbo=gl.createBuffer();gl.bindVertexArray(vao);gl.bindBuffer(gl.ARRAY_BUFFER,vbo);
    gl.bufferData(gl.ARRAY_BUFFER,p.vertices,gl.STATIC_DRAW);
    for(const[loc,count,offset]of[[0,3,0],[1,4,12],[2,2,28]]){
      gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,count,gl.FLOAT,false,36,offset);
    }
    state.meshes.set(p.id,{vao,vbo});
  }
  argon4Contexts.set(gl,state);return state;
}
function argon4Targets(gl,state,w,h){
  w=Math.max(1,Math.floor(w/2));h=Math.max(1,Math.floor(h/2));
  if(state.width===w&&state.height===h)return;
  for(const t of state.targets){gl.deleteFramebuffer(t.fbo);gl.deleteTexture(t.tex);}
  state.targets=[];state.width=w;state.height=h;
  for(let i=0;i<2;i++){
    const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA16F,w,h,0,gl.RGBA,gl.HALF_FLOAT,null);
    for(const n of[gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,n,gl.LINEAR);
    for(const n of[gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,n,gl.CLAMP_TO_EDGE);
    const fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);
    if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw new Error('Argon bloom framebuffer incomplete');
    state.targets.push({tex,fbo});
  }
}
function argon4Camera(S,p){
  const cp=Math.cos(S.pitch),pos=[S.dist*cp*Math.sin(S.yaw),S.dist*Math.sin(S.pitch),S.dist*cp*Math.cos(S.yaw)];
  const v=[-pos[0],p.targetY-pos[1],-pos[2]],len=Math.hypot(...v),fwd=v.map(x=>x/len);
  const rl=Math.hypot(fwd[0],fwd[2]),right=[-fwd[2]/rl,0,fwd[0]/rl];
  const up=[-right[2]*fwd[1],right[2]*fwd[0]-right[0]*fwd[2],right[0]*fwd[1]];
  const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
  const view=[right[0],up[0],-fwd[0],0,right[1],up[1],-fwd[1],0,right[2],up[2],-fwd[2],0,-dot(right,pos),-dot(up,pos),dot(fwd,pos),1];
  const t=1/Math.tan(p.fov*Math.PI/360),proj=[t/(S.w/S.h),0,0,0,0,t,0,0,0,0,180.1/(-179.9),-1,0,0,36/(-179.9),0];
  const vp=new Float32Array(Array.from({length:16},(_,n)=>{const c=Math.floor(n/4),i=n%4;return[0,1,2,3].reduce((s,k)=>s+proj[k*4+i]*view[c*4+k],0);}));
  return{pos,right,up,fwd,vp};
}
BH_LIVE.argon4={
  shader:'/live/shaders/argon4.frag?v=1',
  init:{yaw:.35,pitch:.085,dist:24.5},distMin:4.2,distMax:55,pitchMin:-1.48,pitchMax:1.48,
  dragYaw:-.0055,dragPitch:.0055,maxRes:1200,dprCap:1.25,hdr:true,
  renderScale:C=>[.55,.75,1][C.quality??1],autospin:C=>C.autoOrbit?.22:0,
  controls:[
    {key:'preset',label:'Preset',type:'segments',wide:true,default:-1,options:[{value:-1,label:'Hybrid'},{value:6,label:'Gargantua'},{value:7,label:'Funnel'},{value:8,label:'M87*'},{value:9,label:'Schwarzschild'},{value:0,label:'Extreme Kerr'}]},
    ...bhImportedControls(['quality','disk','animate','autoOrbit','bloom','exposure'],1.05),
    {key:'grid',label:'Spacetime funnel',type:'toggle',default:true},
    {key:'beams',label:'Photon beams',type:'segments',default:-1,options:[{value:-1,label:'Preset'},{value:1,label:'On'},{value:0,label:'Off'}]},
    {key:'jets',label:'Polar jets',type:'segments',default:-1,options:[{value:-1,label:'Preset'},{value:1,label:'On'},{value:0,label:'Off'}]},
    {key:'lensedGrid',label:'Lensed grid',type:'toggle',default:false},
    {key:'temperatureOffset',label:'Temperature offset (K)',type:'range',min:-2000,max:12000,step:400,default:0},
    {key:'doppler',label:'Doppler',type:'segments',default:1,options:[{value:1,label:'100%'},{value:1.35,label:'135%'},{value:0,label:'Off'}]}
  ],
  async prepare(){
    const shader=async name=>{const r=await fetch(`/live/shaders/argon4-${name}?v=1`);if(!r.ok)throw new Error('Argon shader unavailable');return r.text();};
    [this.gridVert,this.gridFrag,this.bloomFrag,this.compositeFrag]=await Promise.all(['grid.vert','grid.frag','bloom.frag','composite.frag'].map(shader));
    const r=await fetch('/live/argon4-meshes/presets.json?v=1');if(!r.ok)throw new Error('Argon presets unavailable');this.presets=await r.json();
    const initial=this.presets[0];this.init={yaw:initial.yaw,pitch:initial.pitch,dist:initial.distance};
    await Promise.all(this.presets.map(async p=>{const r=await fetch(`/live/argon4-meshes/${p.file}?v=1`);if(!r.ok)throw new Error('Argon native mesh unavailable');p.vertices=new Float32Array(await r.arrayBuffer());}));
  },
  applySettings(view,C,previous,initial){
    if(initial){view._argonTime=0;view._argonLast=0;}
    else if(previous.animate)view._argonTime+=view.S.time-view._argonLast;
    view._argonLast=view.S.time;
    if(initial||C.preset!==previous.preset){
      const p=this.presets.find(p=>p.id===(C.preset??-1));
      view.S.yaw=p.yaw;view.S.pitch=p.pitch;view.S.dist=p.distance;
    }
  },
  setUniforms(gl,U,S,cam,C,p){
    for(const[n,k]of[['uCamPos','pos'],['uCamRight','right'],['uCamUp','up'],['uCamFwd','fwd']])gl.uniform3fv(U(n),cam[k]);
    const fs={uTanHalfFov:Math.tan(p.fov*Math.PI/360),uAspect:S.w/S.h,uTime:S.argonTime,uMass:1,uSpin:p.spin,uHorizonR:p.horizon,uISCO:p.isco,uPhotonRPro:p.photon,uDiskOuter:17.5,uDiskTemp:p.temperature+(C.temperatureOffset??0),uDiskIntensity:p.intensity,uDiskThickness:.1,uDopplerStrength:C.doppler??1,uHaloStrength:p.halo,uTrapdoorDepth:p.depth,uGridExtent:28};
    for(const[n,v]of Object.entries(fs))gl.uniform1f(U(n),v);
    gl.uniform1i(U('uMaxSteps'),210);gl.uniform1i(U('uShowDisk'),C.disk?1:0);
    gl.uniform1i(U('uShowJets'),(C.jets===-1?p.jets:C.jets===1)?1:0);gl.uniform1i(U('uShowLensedGrid'),C.lensedGrid?1:0);
  },
  drawCustom(view,C){
    const gl=view.gl,S=view.S,state=argon4State(view),p=this.presets.find(p=>p.id===(C.preset??-1));
    if(C.animate)view._argonTime+=S.time-view._argonLast;view._argonLast=S.time;S.argonTime=view._argonTime;
    const cam=argon4Camera(S,p);view._ensureFBO(S.w,S.h);argon4Targets(gl,state,S.w,S.h);
    gl.bindFramebuffer(gl.FRAMEBUFFER,view._fbo);gl.viewport(0,0,S.w,S.h);gl.disable(gl.DEPTH_TEST);gl.disable(gl.BLEND);
    gl.useProgram(view._program);gl.bindVertexArray(view._vao);this.setUniforms(gl,n=>view.U(n),S,cam,C,p);gl.drawArrays(gl.TRIANGLES,0,3);
    if(C.grid){
      gl.useProgram(state.grid);gl.bindVertexArray(state.meshes.get(p.id).vao);
      gl.uniformMatrix4fv(state.GU('uViewProj'),false,cam.vp);gl.uniform1f(state.GU('uTime'),S.argonTime);
      gl.uniform1f(state.GU('uHorizonR'),p.horizon);gl.uniform1f(state.GU('uMass'),1);gl.uniform3fv(state.GU('uCamPos'),cam.pos);
      gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE);gl.lineWidth(1);
      gl.drawArrays(gl.TRIANGLES,0,p.surfaceCount);gl.drawArrays(gl.LINES,p.surfaceCount,p.gridCount);
      if(C.beams===-1?p.beams:C.beams===1)gl.drawArrays(gl.LINES,p.surfaceCount+p.gridCount,p.beamCount);
      gl.disable(gl.BLEND);
    }
    gl.bindVertexArray(view._vao);gl.useProgram(state.bloom);gl.activeTexture(gl.TEXTURE0);
    gl.uniform1i(state.BU('uSourceTex'),0);gl.uniform2f(state.BU('uTexelSize'),1/state.width,1/state.height);
    const blur=(target,source,x,y)=>{gl.bindFramebuffer(gl.FRAMEBUFFER,target.fbo);gl.viewport(0,0,state.width,state.height);gl.bindTexture(gl.TEXTURE_2D,source);gl.uniform2f(state.BU('uDirection'),x,y);gl.drawArrays(gl.TRIANGLES,0,3);};
    blur(state.targets[0],view._fboTex,0,0);
    for(let i=0;i<2;i++){blur(state.targets[1],state.targets[0].tex,1,0);blur(state.targets[0],state.targets[1].tex,0,1);}
    gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._composite);
    const U=n=>gl.getUniformLocation(view._composite,n);
    gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,view._fboTex);gl.uniform1i(U('uSceneTex'),0);
    gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,state.targets[0].tex);gl.uniform1i(U('uBloomTex'),1);
    gl.uniform1f(U('uBloomStrength'),C.bloom?.36:0);gl.uniform1f(U('uExposure'),C.exposure??1.05);gl.drawArrays(gl.TRIANGLES,0,3);
    gl.activeTexture(gl.TEXTURE0);return true;
  }
};
