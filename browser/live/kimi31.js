/* Kimi K3.1 Pro: submitted Schwarzschild tracer, native ACES and Flamm mesh. */
'use strict';
(() => {
  const states=new WeakMap();
  const norm=a=>{const n=Math.hypot(...a);return a.map(x=>x/n);};
  const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
  function matrix(yaw,pitch,dist,aspect){
    const target=[0,-5,0],eye=[dist*Math.cos(pitch)*Math.sin(yaw),dist*Math.sin(pitch)-5,dist*Math.cos(pitch)*Math.cos(yaw)];
    const f=norm(target.map((x,i)=>x-eye[i])),r=norm(cross(f,[0,1,0])),u=cross(r,f);
    const v=[r[0],u[0],-f[0],0,r[1],u[1],-f[1],0,r[2],u[2],-f[2],0,-dot(r,eye),-dot(u,eye),dot(f,eye),1];
    const t=1/Math.tan(Math.PI/8),p=[t/aspect,0,0,0,0,t,0,0,0,0,(260+.1)/(.1-260),-1,0,0,2*260*.1/(.1-260),0];
    return new Float32Array(Array.from({length:16},(_,n)=>{const c=n>>2,i=n%4;return [0,1,2,3].reduce((s,k)=>s+p[k*4+i]*v[c*4+k],0);}));
  }
  function mesh(gl,vertices,indices){
    const vao=gl.createVertexArray();gl.bindVertexArray(vao);gl.bindBuffer(gl.ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.STATIC_DRAW);
    for(const [a,o]of[[0,0],[1,12]]){gl.enableVertexAttribArray(a);gl.vertexAttribPointer(a,3,gl.FLOAT,false,24,o);}
    if(indices){gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint32Array(indices),gl.STATIC_DRAW);}
    return {vao,count:indices?indices.length:vertices.length/6,indexed:!!indices};
  }
  function resources(view){
    const gl=view.gl;if(states.has(gl))return states.get(gl);
    const cfg=BH_LIVE.kimi31,p=view._compile(cfg.gridFrag,cfg.gridVert),verts=[],indices=[],lines=[];
    // Direct translation of embedding.cpp: all segment counts, radii and colours retained.
    const vertex=(out,r,a,c,lift=0)=>out.push(r*Math.cos(a),2*Math.sqrt(2*(r-2))-2*Math.sqrt(56)+lift,r*Math.sin(a),...c);
    const ring=(r,c,segments=256)=>{for(let i=0;i<segments;i++){vertex(lines,r,2*Math.PI*i/segments,c,.025);vertex(lines,r,2*Math.PI*(i+1)/segments,c,.025);}};
    for(let j=0;j<=112;j++){const t=j/112,r=2+28*Math.pow(t,1.45),shade=.25+.75*t,c=[.010+.012*shade,.028+.050*shade,.045+.075*shade];for(let i=0;i<=192;i++)vertex(verts,r,2*Math.PI*i/192,c);}
    for(let j=0;j<112;j++)for(let i=0;i<192;i++){const a=j*193+i,b=a+193;indices.push(a,b,a+1,b,b+1,a+1);}
    const c=[.1,.34,.42];
    for(let i=0;i<48;i++)for(let j=0;j<112;j++){vertex(lines,2+28*Math.pow(j/112,1.45),2*Math.PI*i/48,c,.025);vertex(lines,2+28*Math.pow((j+1)/112,1.45),2*Math.PI*i/48,c,.025);}
    for(let j=1;j<=42;j++)ring(2+28*Math.pow(j/42,1.45),c,160);
    ring(2,[.95,.16,.07]);ring(3,[1,.55,.08]);ring(6,[.95,.88,.35]);
    const state={program:p,U:n=>gl.getUniformLocation(p,n),surface:mesh(gl,verts,indices),lines:mesh(gl,lines)};
    states.set(gl,state);return state;
  }
  BH_LIVE.kimi31={
    shader:'/live/shaders/kimi31.frag?v=1',hdr:true,exposure:1.15,bloom:0,
    init:{yaw:0,pitch:.32,dist:34},distMin:4,distMax:120,pitchMin:-1.45,pitchMax:1.45,
    dragYaw:-.005,dragPitch:.005,maxRes:1400,dprCap:1.5,
    renderScale:C=>[.35,.68,1][C.quality??1],autospin:C=>C.autoOrbit?.1:0,
    controls:[
      {key:'scene',label:'View',type:'segments',default:0,options:[{value:0,label:'Lensing + grid'},{value:1,label:'Lensing'},{value:2,label:'Spatial grid'}]},
      ...bhImportedControls(['quality','disk','stars','autoOrbit','exposure'],1.15)
    ],
    async prepare(){
      const get=async n=>{const r=await fetch('/live/shaders/kimi31-'+n+'?v=1');if(!r.ok)throw Error('Kimi shader unavailable');return r.text();};
      [this.compositeFrag,this.gridVert,this.gridFrag]=await Promise.all(['present.frag','embedding.vert','embedding.frag'].map(get));
    },
    setUniforms(gl,U,S,_,C){
      const c=camSonnet(S.yaw,S.pitch,S.dist);gl.uniform2f(U('uResolution'),S.w,S.h);
      for(const [n,k]of[['uCameraPos','pos'],['uCameraRight','right'],['uCameraUp','up'],['uCameraForward','fwd']])gl.uniform3fv(U(n),c[k]);
      gl.uniform1f(U('uTanHalfFov'),Math.tan(50*Math.PI/360));gl.uniform1f(U('uObserverLapse'),1-2/S.dist);gl.uniform1f(U('uDiskOpacity'),.88);
      gl.uniform1i(U('uShowDisk'),C.disk?1:0);gl.uniform1i(U('uShowStars'),C.stars?1:0);gl.uniform1i(U('uMaxSteps'),480);
    },
    drawCustom(view,C){
      const gl=view.gl,S=view.S,state=resources(view),scene=C.scene??0;
      gl.disable(gl.DEPTH_TEST);gl.disable(gl.BLEND);gl.bindVertexArray(view._vao);
      if(scene!==2){
        view._ensureFBO(S.w,S.h);gl.bindFramebuffer(gl.FRAMEBUFFER,view._fbo);gl.viewport(0,0,S.w,S.h);gl.useProgram(view._program);
        this.setUniforms(gl,n=>view.U(n),S,null,C);gl.drawArrays(gl.TRIANGLES,0,3);
        gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.useProgram(view._composite);gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,view._fboTex);gl.uniform1i(view._cu('uTex'),0);gl.uniform1f(view._cu('uExposure'),C.exposure??1.15);gl.drawArrays(gl.TRIANGLES,0,3);
      }
      if(scene!==1){
        gl.bindFramebuffer(gl.FRAMEBUFFER,null);
        // Native inset proportions, followed by its original depth-tested mesh pass.
        const w=scene===2?S.w:Math.round(S.w*.28),h=scene===2?S.h:Math.round(S.h*.30),x=scene===2?0:S.w-w-Math.round(S.w*.018),y=scene===2?0:Math.round(S.h*.034);
        gl.viewport(x,y,w,h);gl.enable(gl.SCISSOR_TEST);gl.scissor(x,y,w,h);gl.clearColor(.003,.007,.014,1);gl.depthMask(true);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.disable(gl.SCISSOR_TEST);
        gl.useProgram(state.program);gl.uniformMatrix4fv(state.U('uMVP'),false,matrix(scene===2?S.yaw+.65:.65,scene===2?Math.max(.05,S.pitch+.26):.58,scene===2?S.dist*66/34:66,w/h));gl.uniform1f(state.U('uAlpha'),1);
        gl.enable(gl.DEPTH_TEST);gl.enable(gl.POLYGON_OFFSET_FILL);gl.polygonOffset(1,1);gl.bindVertexArray(state.surface.vao);gl.drawElements(gl.TRIANGLES,state.surface.count,gl.UNSIGNED_INT,0);gl.disable(gl.POLYGON_OFFSET_FILL);
        gl.bindVertexArray(state.lines.vao);gl.drawArrays(gl.LINES,0,state.lines.count);gl.disable(gl.DEPTH_TEST);gl.bindVertexArray(null);gl.viewport(0,0,S.w,S.h);
      }
      return true;
    }
  };
})();
