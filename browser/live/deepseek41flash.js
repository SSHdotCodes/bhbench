/* DeepSeek V4.1 Flash: submitted RK4 shader, native Planck/CIE LUT,
   original four-level bloom and original ACES/sRGB display pipeline. */
'use strict';
(() => {
  const sources={},resources=new WeakMap();let table;
  const config=bhImportedConfig('deepseek41flash-raytrace',{yaw:.55,pitch:.20,dist:48},{
    shader:'/live/shaders/deepseek41flash-raytrace.frag',hdr:true,distMin:8,distMax:400,
    controls:[...bhImportedControls(['quality','animate','autoOrbit','bloom','exposure'],.45),
      {key:'grid',label:'Lensed coordinate grid',type:'toggle',default:false},
      {key:'texture',label:'Disk texture',type:'toggle',default:true},
      {key:'physical',label:'Physical radiance',type:'toggle',default:false}],
    async prepare(){
      await Promise.all(['bloom_down.frag','bloom_up.frag','composite.frag'].map(async f=>{
        const r=await fetch('/live/shaders/deepseek41flash-'+f);if(!r.ok)throw Error('DeepSeek shader '+f+' unavailable');sources[f]=await r.text();
      }));
      const r=await fetch('/live/lookups/deepseek41flash-bb.json');if(!r.ok)throw Error('DeepSeek native blackbody table unavailable');table=await r.json();
    },
    setUniforms(gl,U,S,_,C){
      const c=camFable(S.yaw,S.pitch,S.dist);
      gl.uniform2f(U('uRes'),S.w,S.h);gl.uniform2f(U('uViewportOffset'),0,0);gl.uniform2f(U('uJitter'),0,0);
      for(const[n,k]of [['uCamPos','pos'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']])gl.uniform3fv(U(n),c[k]);
      for(const[n,v]of Object.entries({uTanHalfFov:Math.tan(25*Math.PI/180),uTime:C.animate?S.time:0,uDiskInner:6,uDiskOuter:18,uDiskTempPeak:6000,uDiskBrightness:1,uCompress:.45,uBBMinT:700,uBBMaxT:250000,uStarBrightness:1,uGridOuter:42,uGridIntensity:1}))gl.uniform1f(U(n),v);
      for(const[n,v]of Object.entries({uPhysical:C.physical?1:0,uDiskTexture:C.texture?1:0,uShowStars:C.stars?1:0,uShowGrid:C.grid?1:0,uMaxSteps:420}))gl.uniform1i(U(n),v);
      const R=resources.get(gl);gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,R.bb);gl.uniform1i(U('uBB'),0);
    },
    drawCustom(view,C){
      const gl=view.gl,S=view.S;
      if(!view._floatExt)throw Error('DeepSeek needs half-float HDR');
      let R=resources.get(gl);
      if(!R){
        R={down:view._compile(sources['bloom_down.frag']),up:view._compile(sources['bloom_up.frag']),composite:view._compile(sources['composite.frag'])};
        R.bb=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,R.bb);const rgba=new Float32Array(4096);for(let i=0;i<1024;i++){rgba[i*4]=table[i*3];rgba[i*4+1]=table[i*3+1];rgba[i*4+2]=table[i*3+2];rgba[i*4+3]=1;}
        gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA16F,1024,1,0,gl.RGBA,gl.FLOAT,rgba);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);resources.set(gl,R);
      }
      if(R.w!==S.w||R.h!==S.h){for(const t of R.targets||[])dispose(gl,t);R.w=S.w;R.h=S.h;R.targets=[target(gl,S.w,S.h)];let w=S.w,h=S.h;for(let i=0;i<4;i++){w=Math.max(1,w>>1);h=Math.max(1,h>>1);R.targets.push(target(gl,w,h));}}
      const [scene,...levels]=R.targets;
      const pass=(p,t)=>{gl.bindFramebuffer(gl.FRAMEBUFFER,t?.fbo??null);gl.viewport(0,0,t?.w??S.w,t?.h??S.h);gl.useProgram(p);gl.bindVertexArray(view._vao);return n=>gl.getUniformLocation(p,n);};
      const bind=(tex,unit=0)=>{gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,tex);};
      gl.disable(gl.DEPTH_TEST);gl.disable(gl.SCISSOR_TEST);gl.disable(gl.BLEND);
      pass(view._program,scene);config.setUniforms(gl,n=>view.U(n),S,null,C);gl.drawArrays(gl.TRIANGLES,0,3);
      if(C.bloom){
        let src=scene;
        for(let i=0;i<4;i++){const U=pass(R.down,levels[i]);bind(src.tex);gl.uniform1i(U('uSrc'),0);gl.uniform2f(U('uTexel'),1/src.w,1/src.h);gl.uniform1i(U('uPrefilter'),i===0?1:0);gl.uniform1f(U('uThreshold'),.75);gl.uniform1f(U('uKnee'),.35);gl.drawArrays(gl.TRIANGLES,0,3);src=levels[i];}
        gl.enable(gl.BLEND);gl.blendFunc(gl.ONE,gl.ONE);
        for(let i=2;i>=0;i--){const U=pass(R.up,levels[i]);bind(levels[i+1].tex);gl.uniform1i(U('uSrc'),0);gl.uniform2f(U('uTexel'),1/levels[i+1].w,1/levels[i+1].h);gl.uniform1f(U('uRadius'),1);gl.drawArrays(gl.TRIANGLES,0,3);}
        gl.disable(gl.BLEND);
      }
      const U=pass(R.composite,null);bind(scene.tex);gl.uniform1i(U('uHDR'),0);bind(levels[0].tex,1);gl.uniform1i(U('uBloom'),1);gl.uniform1f(U('uBloomStrength'),C.bloom?.55:0);gl.uniform1f(U('uExposure'),C.exposure??.45);gl.uniform1f(U('uGamma'),1);gl.uniform1f(U('uVignette'),.32);gl.uniform1f(U('uTime'),C.animate?S.time:0);gl.uniform2f(U('uRes'),S.w,S.h);gl.drawArrays(gl.TRIANGLES,0,3);gl.activeTexture(gl.TEXTURE0);return true;
    }
  });
  function target(gl,w,h){const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA16F,w,h,0,gl.RGBA,gl.HALF_FLOAT,null);for(const n of [gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,n,gl.LINEAR);for(const n of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,n,gl.CLAMP_TO_EDGE);const fbo=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,tex,0);if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('DeepSeek incomplete framebuffer');return{tex,fbo,w,h};}
  function dispose(gl,t){gl.deleteTexture(t.tex);gl.deleteFramebuffer(t.fbo);}
  BH_LIVE.deepseek41flash=config;
})();
