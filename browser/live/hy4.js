/* Tencent HY4: unchanged submitted photon integrator and display curve.
   Only the desktop GLSL header and the camera/uniform host are adapted. */
'use strict';
BH_LIVE.hy4 = bhImportedConfig('hy4', {yaw:.30,pitch:.22,dist:30}, {
  hdr:false, distMin:5, distMax:100,
  controls:[...bhImportedControls(['quality','disk','halo','stars','animate','autoOrbit','exposure'],.78),
    {key:'lensing',label:'Lensing',type:'toggle',default:true},
    {key:'relativity',label:'Relativistic shifts',type:'toggle',default:true}],
  setUniforms(gl,U,S,_,C){
    const native=camClaude(S.yaw,S.pitch,S.dist), c={...native,pos:native.eye};
    gl.uniform2f(U('uRes'),S.w,S.h);
    for(const[n,k]of [['uCamPos','pos'],['uCamFwd','fwd'],['uCamRight','right'],['uCamUp','up']]) gl.uniform3fv(U(n),c[k]);
    for(const[n,v]of Object.entries({uTime:C.animate?S.time:0,uTanHalfFov:Math.tan(23*Math.PI/180),uRs:1,uEscapeR:Math.max(60,2*S.dist),uDiskInner:3,uDiskOuter:9,uDiskBright:1,uExposure:C.exposure??.78,uTpeak:5400,uCorona:.09,uCoronaThick:.9,uSpinSign:1})) gl.uniform1f(U(n),v);
    for(const[n,v]of Object.entries({uSteps:340,uDiskOn:C.disk?1:0,uLensingOn:C.lensing?1:0,uRelOn:C.relativity?1:0,uStarsOn:C.stars?1:0,uHaloOn:C.halo?1:0})) gl.uniform1i(U(n),v);
    gl.uniform3f(U('uAxis'),0,1,0);
  }
});
