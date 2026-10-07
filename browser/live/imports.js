/* Browser adaptations of the archived native submissions. Each model uses its own
   original ray tracer. Host lookup tables were exported by the native C++ builders. */
'use strict';
let bhNativeTablePromise;
function bhNativeTables(){
  return bhNativeTablePromise ||= fetch('/live/lookups/native-tables.json?v=2').then(r=>{if(!r.ok)throw Error('native lookup tables unavailable');return r.json();});
}
function bhNativeTexture(gl,data,channels=1){
  const tex=gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D,tex);
  gl.texImage2D(gl.TEXTURE_2D,0,channels===1?gl.R32F:gl.RGBA32F,data.length/channels,1,0,channels===1?gl.RED:gl.RGBA,gl.FLOAT,new Float32Array(data));
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);return tex;
}
const bhNativeTextureCache=new WeakMap();
function bhBindNative(gl,U,name,key,data,unit,channels=1){
 let textures=bhNativeTextureCache.get(gl);if(!textures){textures=new Map();bhNativeTextureCache.set(gl,textures);}
 let texture=textures.get(key);if(!texture){texture=bhNativeTexture(gl,data,channels);textures.set(key,texture);}
 gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,texture);gl.uniform1i(U(name),unit);
}
const bhNativeControls={
 quality:{key:'quality',label:'Resolution',type:'segments',default:1,autoHigh:false,options:[{value:0,label:'Fast'},{value:1,label:'Balanced'},{value:2,label:'High'}]},
 animate:{key:'animate',label:'Animate',type:'toggle',default:true},
 autoOrbit:{key:'autoOrbit',label:'Auto orbit',type:'toggle',default:false},
 disk:{key:'disk',label:'Disk',type:'toggle',default:true},
 halo:{key:'halo',label:'Halo',type:'toggle',default:true},
 stars:{key:'stars',label:'Stars',type:'toggle',default:true},
 grid:{key:'grid',label:'Sky grid',type:'toggle',default:true},
 exposure:{key:'exposure',label:'Exposure',type:'range',min:0.1,max:3,step:0.05,default:1},
 bloom:{key:'bloom',label:'Bloom',type:'toggle',default:true}
};
function bhImportedControls(keys,exposure=1){return keys.map(k=>({...bhNativeControls[k],...(k==='exposure'?{default:exposure}:{})}));}
function bhImportedConfig(id,init,extra){return {shader:`/live/shaders/${id}.frag?v=5`,init,distMin:5,distMax:180,pitchMin:-1.45,pitchMax:1.45,dragYaw:-0.005,dragPitch:0.005,maxRes:1200,dprCap:1.25,autospin:C=>C.autoOrbit?0.075:0,renderScale:C=>[0.24,0.4,0.65][C.quality??1],...extra};}
// Preserve the native tone curves. The browser bloom samples the native HDR image
// directly, avoiding changes to the ray-traced emission or a shared saturation boost.
function bhNativeComposite(style,threshold=1){
 const sonnet=style==='sonnet';
 const tone=sonnet?`mat3 mi=mat3(vec3(.59719,.076,.0284),vec3(.35458,.90834,.13383),vec3(.04823,.01566,.83777));mat3 mo=mat3(vec3(1.60475,-.10208,-.00327),vec3(-.53108,1.10813,-.07276),vec3(-.07367,-.00605,1.07602));vec3 v=mi*c;c=clamp(mo*((v*(v+.0245786)-.000090537)/(v*(.983729*v+.432951)+.238081)),0.0,1.0);float l=dot(c,vec3(.2126,.7152,.0722));c=mix(vec3(l),c,1.05);c*=1.0-.55*dot(vUV*2.0-1.0,vUV*2.0-1.0)*.25;c=mix(12.92*c,1.055*pow(max(c,0.0),vec3(1.0/2.4))-.055,step(vec3(.0031308),c));`:
 `c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.0,1.0);c=pow(c,vec3(1.0/2.2));${style==='deepseek'?'c*=1.0-.15*dot(vUV*2.0-1.0,vUV*2.0-1.0);':style==='qwen'?'c*=1.0-.30*clamp(length(vUV-.5)*1.45-.42,0.0,1.0);':''}`;
 const extract=style==='sol61'?'s*=smoothstep(.65,1.6,max(s.r,max(s.g,s.b)));':style==='grok'?'s*=uExposure;s*=smoothstep(.75,1.7,max(s.r,max(s.g,s.b)));':threshold===0?'':`s=max(s-${threshold.toFixed(1)},0.0);`;
 return `#version 300 es
 precision highp float;in vec2 vUV;out vec4 fragColor;uniform sampler2D uTex;uniform vec2 uTexel;uniform float uExposure,uBloomStrength;
 void main(){vec3 bloom=vec3(0);float total=0.0;
 if(uBloomStrength>0.0){for(int j=-4;j<=4;j++){for(int i=-4;i<=4;i++){
 vec3 s=texture(uTex,vUV+vec2(i,j)*uTexel*2.0).rgb;${extract}
 float w=exp(-float(i*i+j*j)/7.0);bloom+=s*w;total+=w;}}bloom/=total;}
 vec3 hdr=texture(uTex,vUV).rgb;
 vec3 c=${style==='grok'?'hdr*uExposure+bloom*uBloomStrength':'(hdr+bloom*uBloomStrength)*uExposure'};${tone}fragColor=vec4(c,1);}`;
}
Object.assign(BH_LIVE,{
 sol61:bhImportedConfig('sol61',{yaw:.25,pitch:.24,dist:52},{hdr:true,exposure:.75,bloom:.35,compositeFrag:bhNativeComposite('sol61'),controls:[...bhImportedControls(['quality','disk','animate','autoOrbit','bloom','exposure'],.75),{key:'lensing',label:'Lensing',type:'toggle',default:true},{key:'turbulence',label:'Turbulence',type:'toggle',default:true}],setUniforms(gl,U,S,_,C){
 const c=camSonnet(S.yaw,S.pitch,S.dist),f=(n,v)=>gl.uniform1f(U(n),v),i=(n,v)=>gl.uniform1i(U(n),v);gl.uniform2f(U('resolution'),S.w,S.h);
 for(const [n,k]of [['eye','pos'],['forward','fwd'],['right','right'],['up','up']])gl.uniform3fv(U(n),c[k]);
 f('tanHalfFov',.4*Math.max(1,1.5/(S.w/S.h)));f('simTime',C.animate?S.time*8:0);f('stepSize',[.035,.022,.01][C.quality??1]);i('diskEnabled',C.disk?1:0);i('lensingEnabled',C.lensing?1:0);i('turbulence',C.turbulence?1:0);i('colorMode',0);i('diagnostic',0);
 }}),
 grok47:bhImportedConfig('grok47',{yaw:0,pitch:Math.PI/2-1.15,dist:70},{hdr:true,exposure:1.05,bloom:.16,compositeFrag:bhNativeComposite('grok'),controls:[...bhImportedControls(['quality','disk','halo','grid','animate','autoOrbit','bloom','exposure'],1.05),{key:'flat',label:'Flat comparison',type:'toggle',default:false},{key:'colorMode',label:'Colour',type:'segments',default:0,options:[{value:0,label:'Blackbody'},{value:1,label:'Redshift'},{value:2,label:'Temperature'}]}],async prepare(){this.tables=await bhNativeTables();},setUniforms(gl,U,S,_,C){
 const c=camFable(S.yaw,S.pitch,S.dist),f=(n,v)=>gl.uniform1f(U(n),v),i=(n,v)=>gl.uniform1i(U(n),v);gl.uniform2f(U('uResolution'),S.w,S.h);f('uAspect',S.w/S.h);f('uTanHalf',Math.tan(23*Math.PI/180));
 for(const[n,k]of[['uCamPos','pos'],['uRight','right'],['uUp','up']])gl.uniform3fv(U(n),c[k]);gl.uniform3fv(U('uFwd'),c.fwd.map(x=>x*Math.sqrt(1-2/S.dist)));
 const t=this.tables.grok;bhBindNative(gl,U,'uFluxTex','grokFlux',t.flux,1);f('uFluxIn',t.rIn);f('uFluxOut',t.rOut);f('uFluxN',t.flux.length);
 for(const[n,v]of Object.entries({uDiskIn:6,uDiskOut:36,uTime:C.animate?S.time:0,uCoordStep:.072,uDiskGain:1.55,uHaloGain:.07,uTmax:8200}))f(n,v);
 for(const[n,v]of Object.entries({uStepLimit:170,uColorMode:C.colorMode??0,uFlat:C.flat?1:0,uShowDisk:C.disk?1:0,uShowHalo:C.halo?1:0,uShowGrid:C.grid?1:0}))i(n,v);
 gl.uniform3f(U('uSource0'),0,0,-1);gl.uniform3f(U('uSource1'),.35,.78,.52);
 }}),
 mistral4:bhImportedConfig('mistral4',{yaw:.6,pitch:.34,dist:15},{hdr:false,distMin:4.5,distMax:60,controls:[...bhImportedControls(['quality','halo','animate','autoOrbit']),{key:'debug',label:'Hit map',type:'toggle',default:false}],setUniforms(gl,U,S,_,C){
 const c=camSonnet(S.yaw,S.pitch,S.dist);gl.uniform2f(U('uRes'),S.w,S.h);for(const[n,k]of[['uCamPos','pos'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']])gl.uniform3fv(U(n),c[k]);
 for(const[n,v]of Object.entries({uTanHalfFov:Math.tan(25*Math.PI/180),uTime:C.animate?S.time:0,uRs:1,uDiskRin:3,uDiskRout:18,uDiskBrightness:1.2,uHaloStrength:C.halo?1:0}))gl.uniform1f(U(n),v);gl.uniform1i(U('uDebug'),C.debug?1:0);
 }}),
 mimo26flash:bhImportedConfig('mimo26flash',{yaw:.85,pitch:.45,dist:50},{hdr:true,exposure:.3,bloom:.25,compositeFrag:bhNativeComposite('mimo'),controls:[...bhImportedControls(['quality','disk','halo','stars','animate','autoOrbit','bloom','exposure'],.3),{key:'doppler',label:'Doppler',type:'toggle',default:true}],async prepare(){this.tables=await bhNativeTables();},setUniforms(gl,U,S,_,C){
 const c=camFable(S.yaw,S.pitch,S.dist),t=this.tables.mimo;gl.uniform2f(U('uRes'),S.w,S.h);for(const[n,k]of[['uCamPos','pos'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']])gl.uniform3fv(U(n),c[k]);
 for(const[n,v]of Object.entries({uTime:C.animate?S.time:0,uTanHalfFov:Math.tan(25*Math.PI/180),uStepMax:.2,uRFar:300,uDiskInner:6,uDiskOuter:24,uHaloOuter:22,uHaloH0:.04,uHaloFlare:.3,uHaloEmis:.09,uHaloOpacity:3,uF0:t.F0,uFPeak:t.FPeak,uTempScale:t.tempScale,uTurb:.26,uBgScale:3}))gl.uniform1f(U(n),v);
 for(const[n,v]of Object.entries({uMaxSteps:640,uShowDisk:C.disk?1:0,uShowHalo:C.halo?1:0,uShowStars:C.stars?1:0,uDoppler:C.doppler?1:0}))gl.uniform1i(U(n),v);bhBindNative(gl,U,'uBB','mimoBB',t.bb,0,4);
 }}),
 sol6:bhImportedConfig('sol6',{yaw:0,pitch:.34,dist:39},{hdr:false,controls:bhImportedControls(['quality','disk','halo','animate','autoOrbit']),setUniforms(gl,U,S,_,C){gl.uniform2f(U('uResolution'),S.w,S.h);for(const[n,v]of Object.entries({uTime:C.animate?S.time:0,uCameraRadius:S.dist,uAzimuth:S.yaw,uElevation:S.pitch}))gl.uniform1f(U(n),v);gl.uniform1i(U('uShowDisk'),C.disk?1:0);gl.uniform1i(U('uShowHalo'),C.halo?1:0);}}),
 qwen37flash:bhImportedConfig('qwen37flash',{yaw:-Math.PI/2,pitch:Math.PI/12,dist:18},{hdr:false,distMax:60,controls:bhImportedControls(['quality','grid','animate','autoOrbit','exposure'],1.5),setUniforms(gl,U,S,_,C){const c=camSonnet(S.yaw,S.pitch,S.dist);gl.uniform3fv(U('camPos'),c.pos);gl.uniform3f(U('camTarget'),0,0,0);gl.uniform1f(U('aspectRatio'),S.w/S.h);gl.uniform1f(U('time'),C.animate?S.time:0);gl.uniform1i(U('showGrid'),C.grid?1:0);gl.uniform1f(U('gridDensity'),1);gl.uniform1f(U('gridBend'),1);gl.uniform3f(U('bgColor'),.01,.01,.02);gl.uniform1f(U('exposure'),C.exposure??1.5);}}),
 deepseek4:bhImportedConfig('deepseek4',{yaw:Math.PI,pitch:.3,dist:25},{hdr:true,exposure:.85,bloom:1.1,compositeFrag:bhNativeComposite('deepseek'),controls:bhImportedControls(['quality','autoOrbit','bloom','exposure'],.85),setUniforms(gl,U,S){const c=camSonnet(S.yaw,S.pitch,S.dist),t=Math.tan(Math.PI/6);gl.uniform3fv(U('uCamPos'),c.pos);gl.uniform3fv(U('uFwd'),c.fwd);gl.uniform3fv(U('uRight'),c.right.map(v=>v*t*S.w/S.h));gl.uniform3fv(U('uUp'),c.up.map(v=>v*t));gl.uniform1i(U('uMaxIter'),640);gl.uniform1f(U('uStepBase'),.012);gl.uniform1f(U('uTpeak'),12000);}}),
 qwen3827b:bhImportedConfig('qwen3827b',{yaw:.4,pitch:.22,dist:42},{hdr:true,exposure:1,bloom:.5,compositeFrag:bhNativeComposite('qwen',0),controls:bhImportedControls(['quality','animate','autoOrbit','bloom','exposure']),setUniforms(gl,U,S,_,C){const c=camFable(S.yaw,S.pitch,S.dist);for(const [prefix,key]of [['cam','pos'],['fwd','fwd'],['rgt','right'],['up','up']])for(let j=0;j<3;j++)gl.uniform1f(U(`U.${prefix}_${'xyz'[j]}`),c[key][j]);gl.uniform1f(U('U.cam_r'),S.dist);gl.uniform1f(U('U.tan_fov'),Math.tan(.45));gl.uniform1f(U('U.aspect'),S.w/S.h);gl.uniform1f(U('U.time'),C.animate?S.time:0);}}),
 haiku55:bhImportedConfig('haiku55',{yaw:0,pitch:.12,dist:40},{hdr:false,renderScale:C=>[.2,.32,.5][C.quality??1],controls:[...bhImportedControls(['quality','disk','autoOrbit','exposure'],.85),{key:'background',label:'Sky',type:'segments',default:2,options:[{value:0,label:'Stars'},{value:1,label:'Grid'},{value:2,label:'Both'}]}],async prepare(){this.tables=await bhNativeTables();},setUniforms(gl,U,S,_,C){
 const c=camFable(S.yaw,S.pitch,S.dist),a=.9,rp=1+Math.sqrt(1-a*a),capture=rp+.5*(2*(1+Math.cos(2/3*Math.acos(-a)))-rp),t=this.tables.haiku;
 for(const[n,k]of[['camPos','pos'],['camRight','right'],['camUp','up'],['camFwd','fwd']])gl.uniform3fv(U(n),c[k]);gl.uniform2f(U('resolution'),S.w,S.h);
 for(const[n,v]of Object.entries({a,r_plus:rp,r_capture:capture,r_isco:t.isco,r_out:16,eta:.03,r_escape:Math.max(2*S.dist+40,100),cam_ut:1,disk_gain:1.4,T_peak:7500,exposure:C.exposure??.85}))gl.uniform1f(U(`U.${n}`),v);
 for(const[n,v]of Object.entries({lut_n:t.flux.length,max_steps:4000,bg_mode:C.background??2,disk_on:C.disk?1:0}))gl.uniform1i(U(`U.${n}`),v);bhBindNative(gl,U,'lut','haikuFlux',t.flux,0);
 }}),
 sonnet55:bhImportedConfig('sonnet55',{yaw:0,pitch:14*Math.PI/180,dist:64},{hdr:true,exposure:1,bloom:.1,compositeFrag:bhNativeComposite('sonnet',0),controls:[...bhImportedControls(['quality','disk','halo','stars','animate','autoOrbit','bloom','exposure']),{...bhNativeControls.grid,default:false},{key:'turbulence',label:'Turbulence',type:'toggle',default:true},{key:'debug',label:'View',type:'segments',default:0,options:[{value:0,label:'Normal'},{value:1,label:'Image order'},{value:2,label:'Redshift'},{value:3,label:'Ray cost'}]}],async prepare(){this.tables=await bhNativeTables();},setUniforms(gl,U,S,_,C){
 const a=.9,r=kerrRadii(a),t=this.tables.sonnet;gl.uniform4f(U('U.cam'),S.dist,Math.PI/2-S.pitch,S.yaw,Math.tan(16*Math.PI/180));gl.uniform4f(U('U.bh'),a,r.horizon,this.tables.haiku.isco,20);gl.uniform4f(U('U.view'),S.w,S.h,C.animate?S.time*3:0,0);gl.uniform4f(U('U.jit'),0,0,0,0);gl.uniform4f(U('U.disk'),8500,C.disk?1:0,C.halo?.45:0,C.stars?1:0);gl.uniform4f(U('U.opt'),C.grid?1:0,C.debug??0,.1,512);gl.uniform4f(U('U.sky'),1,.75*2*Math.tan(16*Math.PI/180)/S.h,1,Math.max(400,3*S.dist));gl.uniform4f(U('U.misc'),C.turbulence?1:0,t.gain,0,.02);
 bhBindNative(gl,U,'diskTab','sonnetDisk',t.disk,1);bhBindNative(gl,U,'bb','sonnetBB',t.bb,2,4);
 }})
});
