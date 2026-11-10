/* GPT 6.1 Terra: unchanged submitted scene shader with WebGL2 syntax. */
'use strict';
BH_LIVE.terra61={
  shader:'/live/shaders/terra61.frag?v=1',hdr:false,
  init:{yaw:0,pitch:.28,dist:1},distMin:.35,distMax:1.8,pitchMin:-1.35,pitchMax:1.35,
  dragYaw:.006,dragPitch:.004,maxRes:1400,dprCap:1.5,
  renderScale:C=>[.35,.5,1][C.quality??1],autospin:0,
  controls:[
    {key:'scene',label:'View',type:'segments',default:0,options:[{value:0,label:'Lensing'},{value:1,label:'Spatial curvature'}]},
    ...bhImportedControls(['quality','disk','animate'])
  ],
  applySettings(view,C,previous,initial){
    if(initial){view.S.terraTime=0;view.S.terraLast=0;}
    else if(previous.animate)view.S.terraTime+=view.S.time-view.S.terraLast;
    view.S.terraLast=view.S.time;
  },
  setUniforms(gl,U,S,_,C){
    // The native zoom control changes field of view, not observer radius.
    gl.uniform2f(U('resolution'),S.w,S.h);gl.uniform1f(U('yaw'),S.yaw);
    gl.uniform1f(U('inclination'),S.pitch);gl.uniform1f(U('zoom'),S.dist);
    if(C.animate)S.terraTime+=S.time-S.terraLast;S.terraLast=S.time;
    gl.uniform1f(U('time'),S.terraTime);
    gl.uniform1i(U('mode'),C.scene??0);gl.uniform1i(U('diskEnabled'),C.disk?1:0);
  }
};
