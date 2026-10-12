/* Muse Wonder 1.0: submitted ray shader and native polar line mesh.
   Only the host API and GLSL dialect are adapted for WebGL2. */
'use strict';
const museWonderMeshes = new WeakMap();
function museWonderMesh(view) {
  const gl = view.gl;
  if (museWonderMeshes.has(gl)) return museWonderMeshes.get(gl);
  const cfg = BH_LIVE.musewonder, program = view._compile(cfg.gridFrag, cfg.gridVert);
  const points = [], seg = (x,z,X,Z) => points.push(x,0,z,X,0,Z);
  for (let i=0;i<42;i++) {
    const f=i/41, r=Math.min(14,1.02+12.98*f*f*.35+f*2);
    for(let j=0;j<128;j++) {
      const a=j/128*6.2831853,b=(j+1)/128*6.2831853;
      seg(Math.cos(a)*r,Math.sin(a)*r,Math.cos(b)*r,Math.sin(b)*r);
    }
  }
  for(let s=0;s<48;s++) {
    const a=s/48*6.2831853; let prev=1.02;
    for(let j=1;j<=64;j++) {
      const r=1.02+12.98*j/64;
      seg(Math.cos(a)*prev,Math.sin(a)*prev,Math.cos(a)*r,Math.sin(a)*r);prev=r;
    }
  }
  const vao=gl.createVertexArray(),vbo=gl.createBuffer();
  gl.bindVertexArray(vao);gl.bindBuffer(gl.ARRAY_BUFFER,vbo);
  gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(points),gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);gl.vertexAttribPointer(0,3,gl.FLOAT,false,12,0);
  const state={program,vao,vbo,count:points.length/3,U:n=>gl.getUniformLocation(program,n)};
  museWonderMeshes.set(gl,state);return state;
}
function museWonderMVP(pos,aspect) {
  const dot=(a,b)=>a.reduce((v,x,i)=>v+x*b[i],0);
  const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const norm=a=>a.map(x=>x/Math.hypot(...a));
  const f=norm([-pos[0],-.4-pos[1],-pos[2]]),r=norm(cross(f,[0,1,0])),u=cross(r,f);
  const v=[r[0],u[0],-f[0],0,r[1],u[1],-f[1],0,r[2],u[2],-f[2],0,-dot(r,pos),-dot(u,pos),dot(f,pos),1];
  const t=1/Math.tan(.95/2),p=[t/aspect,0,0,0,0,t,0,0,0,0,200.1/(-199.9),-1,0,0,40/(-199.9),0];
  const m=[1,0,0,0,0,1,0,0,0,0,1,0,0,-1.55,0,1];
  const mul=(a,b)=>Array.from({length:16},(_,n)=>{const c=Math.floor(n/4),i=n%4;return [0,1,2,3].reduce((s,k)=>s+a[k*4+i]*b[c*4+k],0);});
  return new Float32Array(mul(p,mul(v,m)));
}
BH_LIVE.musewonder = {
  shader:'/live/shaders/musewonder.frag?v=1',
  init:{yaw:.7,pitch:Math.PI/2-1.22,dist:11.5},
  distMin:4,distMax:35,pitchMin:Math.PI/2-3.06,pitchMax:Math.PI/2-.08,
  dragYaw:.005,dragPitch:.005,maxRes:1200,dprCap:1.25,hdr:false,
  renderScale:C=>[.55,.8,1][C.quality??1],
  autospin:C=>C.autoOrbit&&C.animate?.045:0,
  controls:[
    {key:'viewMode',label:'View',type:'segments',default:3,options:[{value:1,label:'Black hole'},{value:2,label:'Grid'},{value:3,label:'Combined'}]},
    {key:'quality',label:'Quality',type:'segments',default:1,autoHigh:false,options:[{value:0,label:'160 steps'},{value:1,label:'280 steps'},{value:2,label:'420 steps'}]},
    ...bhImportedControls(['disk','animate']),
    {key:'grid',label:'Spacetime grid',type:'toggle',default:true},
    {key:'autoOrbit',label:'Auto orbit',type:'toggle',default:true},
    {key:'lensing',label:'Lensing',type:'toggle',default:true},
    {key:'exposure',label:'Exposure',type:'range',min:.2,max:3,step:.05,default:1.15}
  ],
  async prepare(){
    [this.gridVert,this.gridFrag]=await Promise.all(['vert','frag'].map(async ext=>{
      const res=await fetch(`/live/shaders/musewonder-grid.${ext}?v=1`);
      if(!res.ok)throw new Error('Muse Wonder grid shader unavailable');return res.text();
    }));
  },
  applySettings(view,C,previous,initial){
    if(initial){view._wonderTime=0;view._wonderLast=0;}
    // Account for elapsed time under the previous animation state, then freeze.
    else if(previous.animate)view._wonderTime+=view.S.time-view._wonderLast;
    view._wonderLast=view.S.time;
  },
  setUniforms(gl,U,S,cam,C){
    gl.uniform2f(U('uRes'),S.w,S.h);
    for(const[n,k]of[['uCamPos','pos'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']])gl.uniform3fv(U(n),cam[k]);
    gl.uniform1f(U('uTime'),S.wonderTime);
    gl.uniform1i(U('uSteps'),[160,280,420][C.quality??1]);
    gl.uniform1f(U('uShowDisk'),C.disk?1:0);gl.uniform1f(U('uLensing'),C.lensing?1:0);
    gl.uniform1f(U('uExposure'),C.exposure??1.15);
  },
  drawCustom(view,C){
    const gl=view.gl,S=view.S,cfg=this;
    if(C.animate)view._wonderTime+=S.time-view._wonderLast;
    view._wonderLast=S.time;S.wonderTime=view._wonderTime;
    const cam=camSonnet(S.yaw,S.pitch,S.dist),mode=C.viewMode??3;
    gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,S.w,S.h);
    gl.disable(gl.DEPTH_TEST);gl.disable(gl.BLEND);gl.depthMask(true);
    gl.clearColor(...(mode===2?[.004,.005,.012,1]:[0,0,0,1]));gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    if(mode===1||mode===3){
      gl.useProgram(view._program);gl.bindVertexArray(view._vao);
      cfg.setUniforms(gl,n=>view.U(n),S,cam,C);gl.drawArrays(gl.TRIANGLES,0,3);
    }
    if((mode===2||mode===3)&&C.grid){
      const grid=museWonderMesh(view);gl.useProgram(grid.program);gl.bindVertexArray(grid.vao);
      gl.uniformMatrix4fv(grid.U('uMVP'),false,museWonderMVP(cam.pos,S.w/S.h));
      gl.uniform1f(grid.U('uTime'),S.wonderTime);gl.uniform3f(grid.U('uColor'),.25,.55,.95);
      gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.lineWidth(1);
      gl.drawArrays(gl.LINES,0,grid.count);gl.disable(gl.BLEND);
    }
    gl.bindVertexArray(view._vao);return true;
  }
};
