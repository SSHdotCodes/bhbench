/* StepFun 5 Preview: the submitted Schwarzschild shader and native camera.
   The single-pass bloom adapts the native bright-pass/blur chain; the ray
   integrator, disk, sky, grid and Flamm scene are preserved in step5.frag. */
'use strict';
BH_LIVE.step5 = {
  shader: '/live/shaders/step5.frag?v=2',
  init: { yaw: 0.9, pitch: 0.55, dist: 20 },
  distMin: 2.2, distMax: 80, pitchMin: -1.55, pitchMax: 1.55,
  dragYaw: -0.0055, dragPitch: 0.0055, maxRes: 1200, dprCap: 1.25,
  renderScale: C => [0.24, 0.4, 0.65][C.quality ?? 1],
  autospin: C => C.autoOrbit ? 0.075 : 0,
  hdr: true, exposure: 1, bloom: 0.85,
  compositeFrag: `#version 300 es
precision highp float;
in vec2 vUV; out vec4 outColor;
uniform sampler2D uTex; uniform vec2 uTexel;
uniform float uExposure, uBloomStrength;
void main(){
  vec3 bloom=vec3(0); float total=0.0;
  if(uBloomStrength>0.0){
    for(int j=-4;j<=4;j++){for(int i=-4;i<=4;i++){
      vec3 s=texture(uTex,vUV+vec2(i,j)*uTexel*3.0).rgb;
      s*=smoothstep(1.15,1.75,max(s.r,max(s.g,s.b)));
      float w=exp(-float(i*i+j*j)/7.0); bloom+=s*w; total+=w;
    }} bloom/=total;
  }
  vec3 c=(texture(uTex,vUV).rgb+bloom*uBloomStrength)*uExposure;
  c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.0,1.0);
  c*=smoothstep(.95,.30,length(vUV-.5));
  float d=fract(sin(dot(vUV*1024.0,vec2(12.9898,78.233)))*43758.5453);
  c+=(d-.5)/255.0;
  outColor=vec4(pow(max(c,vec3(0)),vec3(1.0/2.2)),1);
}`,
  controls: [
    {key:'viewMode', label:'Scene', type:'segments', default:0, options:[
      {value:0,label:'Full'}, {value:1,label:'Grid'},
      {value:2,label:'Lensing'}, {value:3,label:'Funnel'}
    ]},
    ...bhImportedControls(['quality','disk','stars','grid','animate','autoOrbit','bloom','exposure']),
    {key:'single',label:'Raw samples',type:'toggle',default:false},
    {key:'gridSpacing',label:'Grid spacing',type:'range',min:1,max:10,step:0.5,default:5},
    {key:'timeScale',label:'Disk speed',type:'range',min:0,max:4,step:0.1,default:1}
  ],
  setUniforms(gl,U,S,_,C){
    const cp=Math.cos(S.pitch);
    const pos=[S.dist*cp*Math.cos(S.yaw),S.dist*Math.sin(S.pitch),S.dist*cp*Math.sin(S.yaw)];
    const fwd=pos.map(v=>-v/S.dist);
    // Retain the submitted basis, including its native orientation.
    const len=Math.hypot(fwd[0],fwd[2]) || 1;
    const right=[fwd[2]/len,0,-fwd[0]/len];
    const up=[right[1]*fwd[2]-right[2]*fwd[1],right[2]*fwd[0]-right[0]*fwd[2],right[0]*fwd[1]-right[1]*fwd[0]];
    gl.uniform2f(U('uResolution'),S.w,S.h);
    for(const[n,v]of Object.entries({uCamPos:pos,uCamFwd:fwd,uCamRight:right,uCamUp:up}))gl.uniform3fv(U(n),v);
    const scene=C.viewMode ?? 0;
    const layers=scene===1?(C.grid?4:0):scene===2?(C.stars?1:0):((C.stars?1:0)|(C.disk?2:0)|(C.grid?4:0));
    gl.uniform2f(U('uJitter'),0,0);
    for(const[n,v]of Object.entries({uTanHalfFov:Math.tan(52*Math.PI/360),uNativeGain:C.single?1:.05,uTime:(C.animate?S.time:0)*(C.timeScale??1),uStepScale:1,uGridSpacing:C.gridSpacing??5,uGridGain:scene===1?3.2:1,uGridFade:scene===1?.016:.05,uTimeScale:C.timeScale??1}))gl.uniform1f(U(n),v);
    for(const[n,v]of Object.entries({uMaxSteps:320,uLayers:layers,uScene:scene}))gl.uniform1i(U(n),v);
  }
};
