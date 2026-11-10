/* Claude Fable 5.2: native Kerr shader, C++ tables and embedding, native bloom pyramid.
   Browser controls orbit the original static-observer tetrad. Spin remains native 0.9. */
'use strict';
(() => {
  const contexts=new WeakMap();
  const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
  const norm=a=>{const n=Math.hypot(...a);return a.map(x=>x/n);};
  const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  function camera(S,grid){
    const cp=Math.cos(S.pitch),pos=[S.dist*cp*Math.cos(S.yaw),S.dist*cp*Math.sin(S.yaw),S.dist*Math.sin(S.pitch)];
    const fwd=norm([-pos[0],-pos[1],(grid?-5:0)-pos[2]]),right=norm([fwd[1],-fwd[0],0]),up=cross(right,fwd);
    // physics.hpp ksMetric + staticTetrad, time-reversed chart spin a=-0.9.
    const a=-.9,w=dot(pos,pos)-a*a,r2=.5*(w+Math.sqrt(w*w+4*a*a*pos[2]*pos[2])),r=Math.sqrt(r2),f=2*r2*r/(r2*r2+a*a*pos[2]*pos[2]);
    const l=[1,(r*pos[0]+a*pos[1])/(r2+a*a),(r*pos[1]-a*pos[0])/(r2+a*a),pos[2]/r];
    const g=Array.from({length:4},(_,i)=>Array.from({length:4},(_,j)=>(i===j?(i===0?-1:1):0)+f*l[i]*l[j]));
    const metric=(u,v)=>u.reduce((s,x,i)=>s+v.reduce((t,y,j)=>t+g[i][j]*x*y,0),0),T=[[1/Math.sqrt(-g[0][0]),0,0,0]],legs=[];
    for(const seed of[fwd,right,up]){
      let v=[0,...seed],c=metric(v,T[0]);v=v.map((x,i)=>x+c*T[0][i]);
      for(const e of legs){const c=metric(v,e);v=v.map((x,i)=>x-c*e[i]);}
      const n=Math.sqrt(metric(v,v));legs.push(v.map(x=>x/n));
    }
    const reorder=e=>[e[1],e[2],e[3],e[0]],tet=[reorder(T[0]),reorder(legs[1]),reorder(legs[2]),reorder(legs[0])];
    const v=[right[0],up[0],-fwd[0],0,right[1],up[1],-fwd[1],0,right[2],up[2],-fwd[2],0,-dot(right,pos),-dot(up,pos),dot(fwd,pos),1];
    const t=1/Math.tan(20*Math.PI/180),near=.05,far=4000,p=[t/(S.w/S.h),0,0,0,0,t,0,0,0,0,(far+near)/(near-far),-1,0,0,2*far*near/(near-far),0];
    const vp=new Float32Array(Array.from({length:16},(_,n)=>{const c=n>>2,i=n%4;return [0,1,2,3].reduce((s,k)=>s+p[k*4+i]*v[c*4+k],0);}));
    return {pos,tet,vp};
  }
  function texture(gl,w,h,internal,format,type,data,filter=gl.LINEAR){
    const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);gl.texImage2D(gl.TEXTURE_2D,0,internal,w,h,0,format,type,data);
    for(const n of[gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,n,filter);
    for(const n of[gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,n,gl.CLAMP_TO_EDGE);return tex;
  }
  function target(gl,w,h){
    const tex=texture(gl,w,h,gl.RGBA16F,gl.RGBA,gl.HALF_FLOAT,null),fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);
    if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Fable HDR target incomplete');return {tex,fbo,w,h};
  }
  function bind(gl,unit,tex,U,name,cube=false){gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(cube?gl.TEXTURE_CUBE_MAP:gl.TEXTURE_2D,tex);gl.uniform1i(U(name),unit);}
  function state(view){
    const gl=view.gl;if(contexts.has(gl))return contexts.get(gl);
    const cfg=BH_LIVE.fable52,t=cfg.tables;
    const program=(frag,vert)=>{const p=view._compile(frag,vert);return {p,U:n=>gl.getUniformLocation(p,n)};};
    const s={grid:program(cfg.gridFrag,cfg.gridVert),down:program(cfg.downFrag),up:program(cfg.upFrag),skyProgram:program(cfg.skyFrag),targets:[]};
    s.bb=texture(gl,1024,1,gl.RGBA32F,gl.RGBA,gl.FLOAT,new Float32Array(t.bb),gl.NEAREST);
    s.disk=texture(gl,1024,1,gl.R32F,gl.RED,gl.FLOAT,new Float32Array(t.disk),gl.NEAREST);
    // Bake the native procedural cubemap, including native mip filtering.
    const size=1024; // browser memory cap; native screenshot keeps its 2048 sky.
    s.sky=gl.createTexture();gl.bindTexture(gl.TEXTURE_CUBE_MAP,s.sky);
    for(let i=0;i<6;i++)gl.texImage2D(gl.TEXTURE_CUBE_MAP_POSITIVE_X+i,0,gl.RGBA16F,size,size,0,gl.RGBA,gl.HALF_FLOAT,null);
    gl.texParameteri(gl.TEXTURE_CUBE_MAP,gl.TEXTURE_MIN_FILTER,gl.LINEAR_MIPMAP_LINEAR);gl.texParameteri(gl.TEXTURE_CUBE_MAP,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
    for(const n of[gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T,gl.TEXTURE_WRAP_R])gl.texParameteri(gl.TEXTURE_CUBE_MAP,n,gl.CLAMP_TO_EDGE);
    const fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.viewport(0,0,size,size);gl.disable(gl.BLEND);gl.disable(gl.DEPTH_TEST);gl.useProgram(s.skyProgram.p);gl.bindVertexArray(view._vao);
    const U=s.skyProgram.U;bind(gl,0,s.bb,U,'uBlackbody');gl.uniform2f(U('uLogT'),t.logTmin,t.logTmax);gl.uniform1f(U('uLutN'),1024);gl.uniform1f(U('uTexel'),2/size);gl.uniform1i(U('uFirstLayer'),2);
    for(let i=0;i<6;i++){gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_CUBE_MAP_POSITIVE_X+i,s.sky,0);if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Fable sky target incomplete');gl.uniform1i(U('uFace'),i);gl.drawArrays(gl.TRIANGLES,0,3);}
    gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.deleteFramebuffer(fbo);gl.bindTexture(gl.TEXTURE_CUBE_MAP,s.sky);gl.generateMipmap(gl.TEXTURE_CUBE_MAP);
    // Original buildFunnelMesh uses these 201 C++ integration samples, 192 azimuths.
    const vertices=[],indices=[];
    for(const e of t.embedding)for(let j=0;j<=192;j++){const phi=2*Math.PI*j/192;vertices.push(e[1]*Math.cos(phi),e[1]*Math.sin(phi),e[2],e[0],phi,e[3],e[4],e[5]);}
    for(let i=0;i<200;i++)for(let j=0;j<192;j++){const a=i*193+j,b=a+1,c=a+193,d=c+1;indices.push(a,c,b,b,c,d);}
    s.vao=gl.createVertexArray();gl.bindVertexArray(s.vao);gl.bindBuffer(gl.ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.STATIC_DRAW);
    for(const[loc,n,offset]of[[0,3,0],[1,4,12],[2,1,28]]){gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,n,gl.FLOAT,false,32,offset);}
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint32Array(indices),gl.STATIC_DRAW);s.count=indices.length;
    contexts.set(gl,s);return s;
  }
  function targets(gl,s,w,h){
    if(s.w===w&&s.h===h)return;
    for(const t of s.targets){gl.deleteFramebuffer(t.fbo);gl.deleteTexture(t.tex);}s.targets=[];
    if(s.depth)gl.deleteTexture(s.depth);if(s.colour)gl.deleteFramebuffer(s.colour);
    s.scene=target(gl,w,h);s.targets.push(s.scene);s.depth=texture(gl,w,h,gl.R32F,gl.RED,gl.FLOAT,null,gl.NEAREST);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT1,gl.TEXTURE_2D,s.depth,0);gl.drawBuffers([gl.COLOR_ATTACHMENT0,gl.COLOR_ATTACHMENT1]);
    if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Fable ray/depth target incomplete');
    s.colour=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,s.colour);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,s.scene.tex,0);
    for(let i=0,bw=w>>1,bh=h>>1;i<7&&bw>=8&&bh>=8;i++,bw>>=1,bh>>=1)s.targets.push(target(gl,bw,bh));
    s.w=w;s.h=h;
  }
  BH_LIVE.fable52={
    shader:'/live/shaders/fable52.frag?v=1',hdr:true,
    init:{yaw:.35,pitch:12*Math.PI/180,dist:62},distMin:5,distMax:600,pitchMin:-1.55,pitchMax:1.55,
    dragYaw:-.0055,dragPitch:.28*Math.PI/180,maxRes:1400,dprCap:1.25,
    renderScale:C=>[.35,.65,1][C.quality??1],autospin:C=>C.autoOrbit?.05:0,
    controls:[...bhImportedControls(['quality','disk','animate','autoOrbit']),
      {key:'grid',label:'Spacetime funnel',type:'toggle',default:true},
      {key:'lensedGrid',label:'Lensed equatorial grid',type:'toggle',default:false},
      {key:'doppler',label:'Doppler + redshift',type:'toggle',default:true},
      {key:'turbulence',label:'Turbulence',type:'toggle',default:true},
      {key:'redshiftMap',label:'Redshift map',type:'toggle',default:false},
      {key:'bloom',label:'Bloom',type:'toggle',default:true},
      {key:'exposure',label:'Exposure',type:'range',min:.2,max:12,step:.1,default:5}
    ],
    async prepare(){
      if(this.tables)return;
      const shader=async n=>{const r=await fetch('/live/shaders/fable52-'+n+'?v=1');if(!r.ok)throw Error('Fable shader unavailable');return r.text();};
      [this.skyFrag,this.gridVert,this.gridFrag,this.downFrag,this.upFrag,this.compositeFrag]=await Promise.all(['sky_gen.frag','grid.vert','grid.frag','bloom_down.frag','bloom_up.frag','composite.frag'].map(shader));
      const r=await fetch('/live/lookups/fable52.json?v=1');if(!r.ok)throw Error('Fable native tables unavailable');this.tables=await r.json();
    },
    applySettings(view,C,previous,initial){
      if(initial){view._fableTime=0;view._fableLast=0;}
      else if(previous.animate)view._fableTime+=(view.S.time-view._fableLast)*6;
      view._fableLast=view.S.time;
    },
    setUniforms(gl,U,S,c,C,s){
      const t=this.tables;gl.uniform2f(U('uResolution'),S.w,S.h);gl.uniform2f(U('uJitter'),0,0);gl.uniform3fv(U('uCamPos'),c.pos);
      for(let i=0;i<4;i++)gl.uniform4fv(U(['uTetT','uTetR','uTetU','uTetF'][i]),c.tet[i]);
      const fs={uTanHalfFov:Math.tan(Math.PI/9),uAspect:S.w/S.h,uPixelAngle:2*Math.tan(Math.PI/9)/S.h,uSpin:.9,uRh:t.horizon,uRfar:60,uEps:.2,uDiskIn:t.isco,uDiskOut:22,uLutRmax:60,uTpeak:5000,uLutN:1024,uLogLumRef:t.logLumRef,uDiskContrast:2.2,uTime:S.fableTime,uSkyGain:.22*5/(C.exposure??5)};
      for(const[n,v]of Object.entries(fs))gl.uniform1f(U(n),v);gl.uniform2f(U('uLogT'),t.logTmin,t.logTmax);gl.uniform1i(U('uMaxSteps'),260);
      const flags=(C.doppler?1:0)|(C.turbulence?2:0)|4|(C.disk?8:0)|16|(C.redshiftMap?32:0)|(C.lensedGrid?64:0);
      gl.uniform1i(U('uFlags'),flags);bind(gl,0,s.sky,U,'uSky',true);bind(gl,1,s.bb,U,'uBlackbody');bind(gl,2,s.disk,U,'uDiskLUT');
    },
    drawCustom(view,C){
      const gl=view.gl,S=view.S,s=state(view),t=this.tables;
      if(C.animate)view._fableTime+=(S.time-view._fableLast)*6;view._fableLast=S.time;S.fableTime=view._fableTime;
      targets(gl,s,S.w,S.h);const c=camera(S,C.grid);
      gl.disable(gl.DEPTH_TEST);gl.disable(gl.BLEND);gl.bindFramebuffer(gl.FRAMEBUFFER,s.scene.fbo);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._program);gl.bindVertexArray(view._vao);
      this.setUniforms(gl,n=>view.U(n),S,c,C,s);gl.drawArrays(gl.TRIANGLES,0,3);
      if(C.grid){
        gl.bindFramebuffer(gl.FRAMEBUFFER,s.colour);gl.enable(gl.BLEND);gl.blendFunc(gl.ONE,gl.ONE);gl.useProgram(s.grid.p);const U=s.grid.U;
        gl.uniformMatrix4fv(U('uViewProj'),false,c.vp);gl.uniform3fv(U('uCamPos'),c.pos);gl.uniform2f(U('uResolution'),S.w,S.h);gl.uniform2f(U('uRph'),t.photonCo,t.photonCounter);
        for(const[n,v]of Object.entries({uTime:S.fableTime%24,uRimR:46,uRh:t.horizon,uRisco:t.isco,uRergo:2,uRingSpacing:2,uIntensity:.9/(C.exposure??5)}))gl.uniform1f(U(n),v);
        gl.uniform1i(U('uStyle'),0);bind(gl,0,s.depth,U,'uSceneDepth');gl.bindVertexArray(s.vao);gl.drawElements(gl.TRIANGLES,s.count,gl.UNSIGNED_INT,0);gl.disable(gl.BLEND);
      }
      gl.bindVertexArray(view._vao);
      if(C.bloom){
        gl.useProgram(s.down.p);
        for(let i=1;i<s.targets.length;i++){const d=s.targets[i],src=s.targets[i-1];gl.bindFramebuffer(gl.FRAMEBUFFER,d.fbo);gl.viewport(0,0,d.w,d.h);bind(gl,0,src.tex,s.down.U,'uSrc');gl.uniform2f(s.down.U('uTexel'),1/src.w,1/src.h);gl.uniform1i(s.down.U('uFirst'),i===1?1:0);gl.drawArrays(gl.TRIANGLES,0,3);}
        gl.enable(gl.BLEND);gl.blendFunc(gl.ONE,gl.ONE);gl.useProgram(s.up.p);gl.uniform1f(s.up.U('uRadius'),1);
        for(let i=s.targets.length-2;i>=1;i--){const d=s.targets[i],src=s.targets[i+1];gl.bindFramebuffer(gl.FRAMEBUFFER,d.fbo);gl.viewport(0,0,d.w,d.h);bind(gl,0,src.tex,s.up.U,'uSrc');gl.uniform2f(s.up.U('uTexel'),1/src.w,1/src.h);gl.drawArrays(gl.TRIANGLES,0,3);}gl.disable(gl.BLEND);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._composite);const U=n=>view._cu(n);
      bind(gl,0,s.scene.tex,U,'uScene');bind(gl,1,s.targets[1].tex,U,'uBloom');gl.uniform2f(U('uSceneSize'),S.w,S.h);gl.uniform1f(U('uExposure'),C.exposure??5);gl.uniform1f(U('uBloomStrength'),C.bloom?.08:0);gl.uniform1f(U('uBloomNorm'),1/(s.targets.length-1));gl.uniform1i(U('uTonemap'),2);gl.uniform1f(U('uContrast'),1.3);gl.drawArrays(gl.TRIANGLES,0,3);
      return true;
    }
  };
})();
