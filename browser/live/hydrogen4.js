/* Gemini 4 Hydrogen: the submitted Metal tracer, Kerr embedding mesh, depth
   occlusion and bloom/composite passes adapted to WebGL2. See NOTES.md. */
'use strict';
(() => {
  const contexts = new WeakMap();
  const norm = v => { const n=Math.hypot(...v); return v.map(x=>x/n); };
  const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const dot = (a,b) => a.reduce((s,x,i)=>s+x*b[i],0);
  function physics(a) {
    a=Math.max(0,Math.min(.998,a));
    const rp=1+Math.sqrt(Math.max(0,1-a*a)),rm=1-Math.sqrt(Math.max(0,1-a*a));
    const z1=1+Math.cbrt(Math.max(0,1-a*a))*(Math.cbrt(1+a)+Math.cbrt(1-a));
    const z2=Math.sqrt(3*a*a+z1*z1);
    return {a,rp,rm,isco:3+z2-Math.sqrt(Math.max(0,(3-z1)*(3+z1+2*z2)))};
  }
  // Direct translation of BlackHolePhysics.hpp, including native 64-step Simpson integration.
  function embedding(r,p,rOuter) {
    r=Math.max(r,p.rp);if(rOuter<=p.rp)return 0;
    const integrand=s=>{const rr=p.rp+s*s;return 2*Math.sqrt(Math.max(0,2*rr-p.a*p.a)/Math.max(1e-12,p.rp-p.rm+s*s));};
    const sMax=Math.sqrt(rOuter-p.rp),sVal=Math.sqrt(r-p.rp),h=(sMax-sVal)/64;
    if(h<=0)return 0;
    let sum=integrand(sVal)+integrand(sMax);
    for(let i=1;i<64;i++)sum+=(i%2===0?2:4)*integrand(sVal+i*h);
    return -(h/3)*sum;
  }
  // MetalApp.mm buildSpacetimeGridMesh: same 128+12 rings, 160 segments,
  // quadratic radii, 0.52 embedding scale, 2.8-M throat and 2.4*a twist.
  // The default spin uses the verbatim C++ export. Other spins regenerate this mesh.
  function meshData(a) {
    const p=physics(a),nr=128,ns=160,nt=12,total=nr+nt,rr=[],zz=[];
    for(let i=0;i<nr;i++){const t=i/(nr-1),r=p.rp+(28-p.rp)*t*t;rr[nt+i]=r;zz[nt+i]=.52*embedding(r,p,28);}
    for(let i=0;i<nt;i++){const frac=i/nt;rr[i]=p.rp*(.72+.28*frac);zz[i]=zz[nt]-2.8*Math.pow(1-frac,1.35);}
    const vertices=[];
    for(let i=0;i<total;i++){
      const r=rr[i],z=zz[i],prev=Math.max(0,i-1),next=Math.min(total-1,i+1);
      const slope=(zz[next]-zz[prev])/Math.max(1e-4,rr[next]-rr[prev]);
      const twist=2.4*a*Math.pow(p.rp/Math.max(r,p.rp*.72),2.2);
      for(let j=0;j<=ns;j++){
        // Native kPi, cos/sin, vector components are floats at these points.
        const phi=2*Math.fround(Math.PI)*j/ns,co=Math.fround(Math.cos(phi)),si=Math.fround(Math.sin(phi));
        const n=norm([Math.fround(-slope*co),Math.fround(-slope*si),1]);
        vertices.push(Math.fround(r)*co,Math.fround(r)*si,z,r,...n,twist);
      }
    }
    const indices=[];
    for(let i=0;i<total-1;i++)for(let j=0;j<ns;j++){const a=i*(ns+1)+j,b=a+1,c=(i+1)*(ns+1)+j,d=c+1;indices.push(a,c,b,b,c,d);}
    return {vertices:new Float32Array(vertices),indices:new Uint32Array(indices)};
  }
  function camera(S,fov) {
    const theta=Math.PI/2-S.pitch,phi=S.yaw;
    const pos=[S.dist*Math.sin(theta)*Math.cos(phi),S.dist*Math.sin(theta)*Math.sin(phi),S.dist*Math.cos(theta)];
    const fwd=norm(pos.map(x=>-x)),right=norm(cross(fwd,[0,0,1])),up=norm(cross(right,fwd));
    const v=[right[0],up[0],-fwd[0],0,right[1],up[1],-fwd[1],0,right[2],up[2],-fwd[2],0,-dot(right,pos),-dot(up,pos),dot(fwd,pos),1];
    const f=1/Math.tan(fov*Math.PI/360),n=.25,z=250;
    const p=[f/(S.w/S.h),0,0,0,0,f,0,0,0,0,z/(n-z),-1,0,0,n*z/(n-z),0];
    const vp=new Float32Array(Array.from({length:16},(_,i)=>{const c=i>>2,r=i%4;return [0,1,2,3].reduce((s,k)=>s+p[k*4+r]*v[c*4+k],0);}));
    return {pos,fwd,right,up,theta,phi,vp};
  }
  function state(view) {
    const gl=view.gl;if(contexts.has(gl))return contexts.get(gl);
    if(!gl.getExtension('EXT_color_buffer_float'))throw Error('Hydrogen requires float color and hit-distance targets');
    const cfg=BH_LIVE.hydrogen4;
    const s={grid:view._compile(cfg.gridFrag,cfg.gridVert),extract:view._compile(cfg.extractFrag),blur:view._compile(cfg.blurFrag),targets:[],time:0,last:view.S.time};
    s.vao=gl.createVertexArray();s.vbo=gl.createBuffer();s.ebo=gl.createBuffer();
    gl.bindVertexArray(s.vao);gl.bindBuffer(gl.ARRAY_BUFFER,s.vbo);
    for(const [loc,off]of [[0,0],[1,16]]){gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,4,gl.FLOAT,false,32,off);}
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,s.ebo);
    contexts.set(gl,s);return s;
  }
  function targets(gl,s,w,h) {
    if(s.w===w&&s.h===h)return;
    for(const t of s.targets){gl.deleteTexture(t.tex);gl.deleteFramebuffer(t.fbo);}
    if(s.rayFbo)gl.deleteFramebuffer(s.rayFbo);
    s.targets=[];s.w=w;s.h=h;s.bw=Math.max(64,Math.floor(w/2));s.bh=Math.max(64,Math.floor(h/2));
    const target=(tw,th,depth=false)=>{
      const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);
      gl.texImage2D(gl.TEXTURE_2D,0,depth?gl.R32F:gl.RGBA16F,tw,th,0,depth?gl.RED:gl.RGBA,depth?gl.FLOAT:gl.HALF_FLOAT,null);
      for(const k of [gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,k,depth?gl.NEAREST:gl.LINEAR);
      for(const k of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,k,gl.CLAMP_TO_EDGE);
      const fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);
      if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Hydrogen framebuffer incomplete');
      const t={tex,fbo};s.targets.push(t);return t;
    };
    s.scene=target(w,h);s.depth=target(w,h,true);
    s.bright=target(s.bw,s.bh);s.temp=target(s.bw,s.bh);s.tight=target(s.bw,s.bh);s.wide=target(s.bw,s.bh);
    s.rayFbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,s.rayFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,s.scene.tex,0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT1,gl.TEXTURE_2D,s.depth.tex,0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0,gl.COLOR_ATTACHMENT1]);
    if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Hydrogen ray framebuffer incomplete');
  }
  function uniforms(gl,U,S,C,cam,time) {
    const fov=BH_LIVE.hydrogen4.presets[C.preset??0].fov,p=physics(C.spin??.9);
    for(const [n,v]of Object.entries({
      camPosTime:[...cam.pos,time],camForwardFov:[...cam.fwd,Math.tan(fov*Math.PI/360)],
      camRightAspect:[...cam.right,S.w/S.h],camUpDummy:[...cam.up,0],camBL:[S.dist,cam.theta,cam.phi,Math.cos(cam.theta)],
      bhParams:[p.a,p.rp,p.isco,13.5],diskParams:[C.disk?1:0,C.doppler?1:0,C.brightness??.95,C.halo??1],
      gridParams:[C.grid?1:0,C.gridOffset??-2.4,28,C.gridOpacity??.9],
      visualParams:[C.background??0,C.bloom===false?0:(C.bloomAmount??.42),C.infall?1:0,C.exposure??1.05],
      screenParams:[S.w,S.h,1/S.w,1/S.h]
    }))gl.uniform4fv(U(n),v);
    gl.uniformMatrix4fv(U('viewProj'),false,cam.vp);
  }
  BH_LIVE.hydrogen4={
    shader:'/live/shaders/hydrogen4.frag?v=1',hdr:true,
    init:{yaw:25*Math.PI/180,pitch:25*Math.PI/180,dist:25},
    distMin:3.4,distMax:65,pitchMin:-Math.PI/2+.04,pitchMax:Math.PI/2-.04,
    dragYaw:-.005,dragPitch:.005,maxRes:1600,dprCap:2,
    renderScale:C=>[.5,.75,1][C.quality??1],autospin:C=>C.autoOrbit?.10:0,
    presets:[{r:25,theta:65,phi:25,fov:52},{r:20,theta:88.5,phi:0,fov:46},{r:26,theta:14,phi:45,fov:50},{r:9.5,theta:79,phi:10,fov:56}],
    controls:[
      {key:'preset',label:'Camera preset',type:'segments',wide:true,default:0,options:[{value:0,label:'Inclined'},{value:1,label:'Edge-on'},{value:2,label:'Polar'},{value:3,label:'Photon skim'}]},
      ...bhImportedControls(['quality','disk','animate','autoOrbit','bloom','exposure'],1.05),
      {key:'spin',label:'Kerr spin a/M',type:'range',min:0,max:.998,step:.002,precision:3,default:.9},
      {key:'doppler',label:'Doppler and redshift',type:'toggle',default:true},
      {key:'grid',label:'Spacetime trapdoor grid',type:'toggle',default:true},
      {key:'infall',label:'Infalling waves',type:'toggle',default:true},
      {key:'background',label:'Lensed background',type:'segments',default:0,options:[{value:0,label:'Deep space'},{value:1,label:'Einstein grid'}]},
      {key:'brightness',label:'Disk luminosity',type:'range',min:.2,max:3,step:.05,default:.95},
      {key:'halo',label:'Photon ring and corona',type:'range',min:0,max:2.5,step:.05,default:1},
      {key:'gridOffset',label:'Grid vertical offset',type:'range',min:-7,max:.5,step:.1,default:-2.4},
      {key:'gridOpacity',label:'Grid opacity',type:'range',min:.1,max:1,step:.05,default:.9}
    ],
    async prepare(){
      const get=async n=>{const r=await fetch('/live/shaders/hydrogen4-'+n+'?v=1');if(!r.ok)throw Error('Hydrogen shader unavailable');return r.text();};
      [this.gridVert,this.gridFrag,this.extractFrag,this.blurFrag,this.compositeFrag]=await Promise.all(['grid.vert','grid.frag','extract.frag','blur.frag','composite.frag'].map(get));
      const binary=async n=>{const r=await fetch('/live/hydrogen4-mesh/grid.'+n+'?v=1');if(!r.ok)throw Error('Hydrogen native mesh unavailable');return r.arrayBuffer();};
      const [v,i]=await Promise.all(['f32','u32'].map(binary));
      this.nativeMesh={vertices:new Float32Array(v),indices:new Uint32Array(i)};
      if(!this.resetBound){
        // The site's reset button resets settings. Also restore the native camera/time.
        document.getElementById('renderControlsReset')?.addEventListener('click',()=>{
          if(typeof bhEngine==='undefined'||!bhEngine?.cfg?.shader.includes('hydrogen4'))return;
          this.applySettings(bhEngine,bhEngine.settings,null,true);
        });
        this.resetBound=true;
      }
    },
    applySettings(view,C,previous,initial){
      if(initial||C.preset!==previous.preset){const p=this.presets[C.preset??0];view.S.yaw=p.phi*Math.PI/180;view.S.pitch=(90-p.theta)*Math.PI/180;view.S.dist=p.r;}
      if(initial){
        view._hydrogenBloom=.42;
        const s=contexts.get(view.gl);if(s){s.time=0;s.last=view.S.time;}
      }else if(['spin','disk','doppler','grid','infall','brightness','halo','gridOffset','gridOpacity','background'].some(k=>C[k]!==previous[k])){
        // Reproduce native onControlChanged, including its corona/bloom coupling.
        view._hydrogenBloom=.48*((C.halo??1)/1.15);
      }
    },
    // Exposed for deterministic validation and checking translated mesh against native export.
    meshData,
    drawCustom(view,C){
      const gl=view.gl,S=view.S,s=state(view),a=C.spin??.9;
      if(C.animate!==false)s.time+=Math.max(0,S.time-s.last);s.last=S.time;
      const cam=camera(S,this.presets[C.preset??0].fov);
      C={...C,bloomAmount:view._hydrogenBloom??.42};
      gl.disable(gl.DEPTH_TEST);gl.disable(gl.CULL_FACE);gl.disable(gl.BLEND);
      targets(gl,s,S.w,S.h);
      if(s.spin!==a){
        const m=a===.9?this.nativeMesh:meshData(a);s.spin=a;s.count=m.indices.length;
        gl.bindVertexArray(s.vao);gl.bindBuffer(gl.ARRAY_BUFFER,s.vbo);gl.bufferData(gl.ARRAY_BUFFER,m.vertices,gl.STATIC_DRAW);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,s.ebo);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,m.indices,gl.STATIC_DRAW);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER,s.rayFbo);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._program);gl.bindVertexArray(view._vao);
      uniforms(gl,n=>view.U(n),S,C,cam,s.time);gl.drawArrays(gl.TRIANGLES,0,3);
      if(C.grid){
        gl.bindFramebuffer(gl.FRAMEBUFFER,s.scene.fbo);gl.useProgram(s.grid);gl.bindVertexArray(s.vao);
        const U=n=>gl.getUniformLocation(s.grid,n);uniforms(gl,U,S,C,cam,s.time);
        gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,s.depth.tex);gl.uniform1i(U('depthTex'),0);
        gl.enable(gl.BLEND);gl.blendFunc(gl.ONE,gl.ONE_MINUS_SRC_ALPHA);
        gl.drawElements(gl.TRIANGLES,s.count,gl.UNSIGNED_INT,0);gl.disable(gl.BLEND);
      }
      gl.bindVertexArray(view._vao);
      if(C.bloom!==false){
        gl.bindFramebuffer(gl.FRAMEBUFFER,s.bright.fbo);gl.viewport(0,0,s.bw,s.bh);gl.useProgram(s.extract);
        gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,s.scene.tex);gl.uniform1i(gl.getUniformLocation(s.extract,'inHDR'),0);gl.uniform2f(gl.getUniformLocation(s.extract,'outputSize'),s.bw,s.bh);gl.drawArrays(gl.TRIANGLES,0,3);
        gl.useProgram(s.blur);const U=n=>gl.getUniformLocation(s.blur,n);gl.uniform1i(U('sourceTex'),0);
        const blur=(dst,src,sigma,x,y)=>{gl.bindFramebuffer(gl.FRAMEBUFFER,dst.fbo);gl.bindTexture(gl.TEXTURE_2D,src.tex);gl.uniform1f(U('sigma'),sigma);gl.uniform2f(U('direction'),x/s.bw,y/s.bh);gl.drawArrays(gl.TRIANGLES,0,3);};
        blur(s.temp,s.bright,5,1,0);blur(s.tight,s.temp,5,0,1);
        blur(s.temp,s.bright,20,1,0);blur(s.wide,s.temp,20,0,1);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._composite);
      const U=n=>gl.getUniformLocation(view._composite,n);uniforms(gl,U,S,C,cam,s.time);
      for(const [unit,n,t]of [[0,'sceneHDR',s.scene],[1,'bloomTight',s.tight],[2,'bloomWide',s.wide]]){gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,t.tex);gl.uniform1i(U(n),unit);}
      gl.drawArrays(gl.TRIANGLES,0,3);gl.activeTexture(gl.TEXTURE0);return true;
    },
    setTime(view,time){const s=state(view);s.time=time;s.last=view.S.time;}
  };
})();
