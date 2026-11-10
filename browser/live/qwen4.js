/* Qwen4: submitted RK4 ray shader, original Flamm grid and original bloom passes.
   The continuation only added app packaging; renderer source is from run 1. */
'use strict';
(() => {
  const sources={}, resources=new WeakMap();
  const config=bhImportedConfig('qwen4-raytrace',{yaw:35*Math.PI/180,pitch:10*Math.PI/180,dist:26},{
    hdr:true,shader:'/live/shaders/qwen4-raytrace.frag',distMin:5,distMax:80,
    controls:[...bhImportedControls(['quality','disk','stars','animate','autoOrbit','bloom','exposure']),
      {key:'grid',label:'Spacetime grid',type:'toggle',default:true}],
    async prepare(){
      await Promise.all(['grid.vert','grid.frag','bright.frag','blur.frag','composite.frag'].map(async f=>{
        const r=await fetch('/live/shaders/qwen4-'+f);if(!r.ok)throw Error('Qwen4 shader '+f+' unavailable');sources[f]=await r.text();
      }));
    },
    setUniforms(gl,U,S,_,C){
      const c=camClaude(S.yaw,S.pitch,S.dist), matrix=pv(c,S.w/S.h);
      gl.uniform2f(U('uResolution'),S.w,S.h);
      for(const[n,k]of [['uCamPos','eye'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']])gl.uniform3fv(U(n),c[k]);
      gl.uniformMatrix4fv(U('uPV'),false,matrix);
      for(const[n,v]of Object.entries({uTanHalfFov:Math.tan(Math.PI/8),uAspect:S.w/S.h,uTime:C.animate?S.time:0,uStepScale:.8,uDiskInner:3,uDiskOuter:12,uDiskTemp:5500,uShowDisk:C.disk?1:0,uShowStars:C.stars?1:0}))gl.uniform1f(U(n),v);
      gl.uniform1i(U('uMaxSteps'),600);
    },
    drawCustom(view,C){
      const gl=view.gl,S=view.S;
      if(!view._floatExt)throw Error('Qwen4 needs half-float HDR');
      let R=resources.get(gl);
      if(!R){
        R={grid:view._compile(sources['grid.frag'],sources['grid.vert']),bright:view._compile(sources['bright.frag']),blur:view._compile(sources['blur.frag']),composite:view._compile(sources['composite.frag'])};
        const verts=[];
        const point=(r,a)=>[r*Math.cos(a),-2*Math.sqrt(r-1),r*Math.sin(a)];
        const add=(r,a)=>{const b=.35+.65*Math.max(0,1-(r-1)/15),alpha=.55*Math.pow(1-Math.min(1,(r-1)/15),1.4)+.05;verts.push(...point(r,a),.30*b,.78*b,b,alpha);};
        for(let i=0;i<26;i++){const r=1.02+15*Math.pow(i/25,1.8);for(let j=0;j<96;j++){add(r,2*Math.PI*j/96);add(r,2*Math.PI*(j+1)/96);}}
        for(let k=0;k<36;k++)for(let j=0;j<80;j++){add(1.02+15*j/80,2*Math.PI*k/36);add(1.02+15*(j+1)/80,2*Math.PI*k/36);}
        R.gridCount=verts.length/7;R.vao=gl.createVertexArray();R.vbo=gl.createBuffer();gl.bindVertexArray(R.vao);gl.bindBuffer(gl.ARRAY_BUFFER,R.vbo);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(verts),gl.STATIC_DRAW);
        gl.enableVertexAttribArray(0);gl.vertexAttribPointer(0,3,gl.FLOAT,false,28,0);gl.enableVertexAttribArray(1);gl.vertexAttribPointer(1,4,gl.FLOAT,false,28,12);resources.set(gl,R);
      }
      if(R.w!==S.w||R.h!==S.h){
        for(const t of R.targets||[])dispose(gl,t);
        R.w=S.w;R.h=S.h;
        R.targets=[target(gl,S.w,S.h,true),target(gl,Math.max(1,S.w>>2),Math.max(1,S.h>>2)),target(gl,Math.max(1,S.w>>2),Math.max(1,S.h>>2))];
      }
      const [scene,a,b]=R.targets;
      const pass=(p,t)=>{gl.bindFramebuffer(gl.FRAMEBUFFER,t?.fbo??null);gl.viewport(0,0,t?.w??S.w,t?.h??S.h);gl.useProgram(p);gl.bindVertexArray(view._vao);return n=>gl.getUniformLocation(p,n);};
      gl.disable(gl.BLEND);gl.disable(gl.SCISSOR_TEST);gl.disable(gl.DEPTH_TEST);gl.depthMask(true);
      pass(view._program,scene);gl.clearColor(0,0,0,1);gl.clearDepth(1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
      config.setUniforms(gl,n=>view.U(n),S,null,C);gl.drawArrays(gl.TRIANGLES,0,3);
      if(C.grid){
        gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LESS);gl.depthMask(false);gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);
        gl.useProgram(R.grid);gl.uniformMatrix4fv(gl.getUniformLocation(R.grid,'uPV'),false,pv(camClaude(S.yaw,S.pitch,S.dist),S.w/S.h));gl.uniform3fv(gl.getUniformLocation(R.grid,'uCamPos'),camClaude(S.yaw,S.pitch,S.dist).eye);gl.bindVertexArray(R.vao);gl.drawArrays(gl.LINES,0,R.gridCount);
        gl.depthMask(true);gl.disable(gl.BLEND);gl.disable(gl.DEPTH_TEST);
      }
      const bind=(tex,unit=0)=>{gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,tex);};
      if(C.bloom){
        let U=pass(R.bright,a);bind(scene.tex);gl.uniform1i(U('uTex'),0);gl.uniform2f(U('uTexel'),1/S.w,1/S.h);gl.drawArrays(gl.TRIANGLES,0,3);
        for(let i=0;i<2;i++){
          U=pass(R.blur,b);bind(a.tex);gl.uniform1i(U('uTex'),0);gl.uniform2f(U('uDir'),1/a.w,0);gl.drawArrays(gl.TRIANGLES,0,3);
          U=pass(R.blur,a);bind(b.tex);gl.uniform1i(U('uTex'),0);gl.uniform2f(U('uDir'),0,1/a.h);gl.drawArrays(gl.TRIANGLES,0,3);
        }
      }
      const U=pass(R.composite,null);bind(scene.tex);gl.uniform1i(U('uScene'),0);bind(a.tex,1);gl.uniform1i(U('uBloom'),1);gl.uniform1f(U('uExposure'),C.exposure??1);gl.uniform1f(U('uBloomStrength'),C.bloom?.40:0);gl.drawArrays(gl.TRIANGLES,0,3);gl.activeTexture(gl.TEXTURE0);return true;
    }
  });
  function target(gl,w,h,depth=false){const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA16F,w,h,0,gl.RGBA,gl.HALF_FLOAT,null);for(const n of [gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,n,gl.LINEAR);for(const n of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,n,gl.CLAMP_TO_EDGE);const fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);let rb=null;if(depth){rb=gl.createRenderbuffer();gl.bindRenderbuffer(gl.RENDERBUFFER,rb);gl.renderbufferStorage(gl.RENDERBUFFER,gl.DEPTH_COMPONENT24,w,h);gl.framebufferRenderbuffer(gl.FRAMEBUFFER,gl.DEPTH_ATTACHMENT,gl.RENDERBUFFER,rb);}if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Qwen4 incomplete framebuffer');return{tex,fbo,rb,w,h};}
  function dispose(gl,t){gl.deleteTexture(t.tex);gl.deleteFramebuffer(t.fbo);if(t.rb)gl.deleteRenderbuffer(t.rb);}
  function pv(c,aspect){const r=c.right,u=c.up,f=c.fwd,p=c.eye,k=1/Math.tan(Math.PI/8),A=(500+.1)/(.1-500),B=2*500*.1/(.1-500),dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);return new Float32Array([k/aspect*r[0],k*u[0],-A*f[0],f[0],k/aspect*r[1],k*u[1],-A*f[1],f[1],k/aspect*r[2],k*u[2],-A*f[2],f[2],-k/aspect*dot(r,p),-k*dot(u,p),A*dot(f,p)+B,-dot(f,p)]);}
  BH_LIVE.qwen4=config;
})();
