import createRelativityCore from "./relativity-core.js?v=20260714a";

const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
const canvas = $("#viewport");
const app = $("#app");

const boot = (pct, text) => {
  $("#bootProgress").style.width = `${pct}%`;
  $("#bootStatus").textContent = text;
};

boot(18, "Loading C++ relativity core…");
const Module = await createRelativityCore({ locateFile: (path) => `${new URL(`./${path}`, import.meta.url).href}?v=20260714a` });
boot(44, "Binding geometric-unit dynamics…");

const core = {
  init: Module.cwrap("sim_init", null, ["number"]),
  setMass: Module.cwrap("sim_set_black_hole_mass", null, ["number"]),
  getMass: Module.cwrap("sim_get_black_hole_mass", "number", []),
  spawn: Module.cwrap("sim_spawn", "number", ["number","number","number","number","number","number","number","number"]),
  spawnAimed: Module.cwrap("sim_spawn_aimed", "number", ["number","number","number","number","number","number","number","number","number","number","number","number"]),
  step: Module.cwrap("sim_step", null, ["number"]),
  bodyCount: Module.cwrap("sim_body_count", "number", []),
  bodyActive: Module.cwrap("sim_body_active", "number", ["number"]),
  bodyValue: Module.cwrap("sim_body_value", "number", ["number","number"]),
  disk: Module.cwrap("sim_disk_density", "number", []),
  diskTemp: Module.cwrap("sim_disk_temperature", "number", []),
  diskMass: Module.cwrap("sim_disk_mass", "number", []),
  diskNormalX: Module.cwrap("sim_disk_normal_x", "number", []),
  diskNormalY: Module.cwrap("sim_disk_normal_y", "number", []),
  diskNormalZ: Module.cwrap("sim_disk_normal_z", "number", []),
  accreted: Module.cwrap("sim_accreted_mass", "number", []),
  mergerEnergy: Module.cwrap("sim_merger_energy", "number", []),
  event: Module.cwrap("sim_last_event", "number", []),
  rgKm: Module.cwrap("sim_rg_km", "number", []),
  tg: Module.cwrap("sim_tg_seconds", "number", []),
  horizon: Module.cwrap("sim_horizon_km", "number", []),
  circularV: Module.cwrap("sim_circular_velocity", "number", ["number"]),
  lapse: Module.cwrap("sim_lapse", "number", ["number"])
};
core.init(4.30e6);

const state = {
  mode: "observe", paused: false, timeRate: 1, simSeconds: 0, simGeom: 0,
  bhMass: 4.30e6, disk: 0, diskTemp: 0, diskMass: 0, diskNormal: [0,0,1], quality: 192, qualityIndex: 1,
  selectedType: "planet", phase: 0, renderer: null, bodies: [], aiming: false,
  observe: { radius: 26, yaw: -0.72, pitch: 0.17 },
  pilot: { pos: [18,0,2.8], vel: [0,0,0], yaw: Math.PI, pitch: -0.08, thrust: 0 },
  drag: false, lastPointer: [0,0], keys: new Set(), lastFrame: performance.now()
};

const presets = {
  planet: { id:0, name:"EARTH ANALOG", mass:1, unit:"M⊕", solar:3.0034896e-6, radius:6371, rmin:1000, rmax:80000, rstep:1,
    comps:[[0,"Silicate + iron"],[1,"Hydrogen / helium"],[2,"Water / ice"]] },
  star: { id:1, name:"SUN-LIKE STAR", mass:1, unit:"M☉", solar:1, radius:696340, rmin:70000, rmax:4000000, rstep:1000,
    comps:[[3,"Hydrogen burning"],[4,"Red giant envelope"],[5,"White dwarf matter"]] },
  neutron: { id:2, name:"NEUTRON STAR", mass:1.4, unit:"M☉", solar:1, radius:12, rmin:9, rmax:20, rstep:.1,
    comps:[[6,"Neutron superfluid"],[7,"Quark-rich core"],[8,"Magnetar crust"]] },
  blackhole: { id:3, name:"STELLAR BLACK HOLE", mass:12, unit:"M☉", solar:1, radius:35.44, rmin:8.86, rmax:30000000, rstep:1,
    comps:[[9,"Schwarzschild"],[10,"Low-spin Kerr analog"],[11,"Intermediate mass"]] }
};

const fmt = (n, digits=2) => Number(n).toLocaleString(undefined,{maximumFractionDigits:digits,minimumFractionDigits:digits});
const compact = (n, unit="") => {
  const a = Math.abs(n);
  if (a >= 1e9) return `${fmt(n/1e9,2)}B${unit}`;
  if (a >= 1e6) return `${fmt(n/1e6,2)}M${unit}`;
  if (a >= 1e3) return `${fmt(n/1e3,2)}K${unit}`;
  return `${fmt(n,2)}${unit}`;
};
const vecLen = (v) => Math.hypot(v[0],v[1],v[2]);
const norm = (v) => { const l=vecLen(v)||1; return [v[0]/l,v[1]/l,v[2]/l]; };
const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const dot = (a,b) => a[0]*b[0]+a[1]*b[1]+a[2]*b[2];

function cameraFrame() {
  if (state.mode === "observe") {
    const {radius:r,yaw:y,pitch:p} = state.observe;
    const pos = [r*Math.cos(p)*Math.cos(y), r*Math.cos(p)*Math.sin(y), r*Math.sin(p)];
    const fwd = norm(pos.map(x=>-x));
    const right = norm(cross(fwd,[0,0,1]));
    const up = norm(cross(right,fwd));
    return {pos,fwd,right,up};
  }
  const p = state.pilot;
  const fwd = norm([Math.cos(p.pitch)*Math.cos(p.yaw),Math.cos(p.pitch)*Math.sin(p.yaw),Math.sin(p.pitch)]);
  const right = norm(cross(fwd,[0,0,1]));
  const up = norm(cross(right,fwd));
  return {pos:p.pos,fwd,right,up};
}

const wgsl = /* wgsl */`
struct Uniforms {
  resolution: vec2f, time: f32, exposure: f32,
  camPos: vec3f, tanHalfFov: f32,
  camFwd: vec3f, diskDensity: f32,
  camRight: vec3f, diskTemp: f32,
  camUp: vec3f, bodyCount: f32,
  params: vec4f,
};
struct Body {
  pos_radius: vec4f,
  color_emit: vec4f,
  velocity_state: vec4f,
  debris: vec4f,
};
@group(0) @binding(0) var<uniform> U: Uniforms;
@group(0) @binding(1) var<storage, read> B: array<Body>;

struct VOut { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> VOut {
  var p = array<vec2f,3>(vec2f(-1.,-3.),vec2f(3.,1.),vec2f(-1.,1.));
  var o: VOut; o.position=vec4f(p[i],0.,1.); o.uv=p[i]*.5+vec2f(.5); return o;
}
fn hash11(n:f32)->f32 { return fract(sin(n)*43758.5453123); }
fn hash31(p:vec3f)->f32 { return fract(sin(dot(p,vec3f(127.1,311.7,74.7)))*43758.5453); }
fn noise(p:vec3f)->f32 {
  let i=floor(p); let f=fract(p); let u=f*f*(3.-2.*f);
  return mix(mix(mix(hash31(i),hash31(i+vec3f(1,0,0)),u.x),mix(hash31(i+vec3f(0,1,0)),hash31(i+vec3f(1,1,0)),u.x),u.y),mix(mix(hash31(i+vec3f(0,0,1)),hash31(i+vec3f(1,0,1)),u.x),mix(hash31(i+vec3f(0,1,1)),hash31(i+vec3f(1,1,1)),u.x),u.y),u.z);
}
fn fbm(q:vec3f)->f32 { var p=q; var s=0.; var a=.5; for(var i=0;i<4;i++){s+=a*noise(p);p=p*2.03+vec3f(3.1,7.7,1.9);a*=.52;} return s; }
fn blackbody(t0:f32)->vec3f {
  let t=clamp(t0,1000.,40000.)/100.;
  var c=vec3f(1.);
  if(t>66.) { c.r=clamp(1.292936*pow(t-60.,-.133204),0.,1.); }
  if(t<=66.) { c.g=clamp(.390082*log(t)-.631841,0.,1.); } else { c.g=clamp(1.129891*pow(t-60.,-.075515),0.,1.); }
  if(t<66.) { if(t<=19.){c.b=0.;}else{c.b=clamp(.543207*log(t-10.)-1.196254,0.,1.);} }
  return pow(c,vec3f(2.2));
}
fn stars(d:vec3f)->vec3f {
  var col=vec3f(.0015,.0025,.005);
  let pole=normalize(vec3f(.32,.16,.93)); let band=exp(-pow(dot(d,pole)*9.,2.));
  let neb=fbm(d*7.+vec3f(4,1,9)); col += band*(.012+.10*neb*neb)*mix(vec3f(.08,.15,.34),vec3f(.35,.14,.08),fbm(d*15.));
  let cell=floor(d*420.); let h=hash31(cell); let star=smoothstep(.9965,1.,h);
  col += blackbody(2600.+hash31(cell+vec3f(11))*11500.)*star*(1.+8.*pow(hash31(cell+vec3f(31)),5.));
  let cell2=floor(d*115.); let h2=hash31(cell2); col += blackbody(3800.+h2*8000.)*smoothstep(.985,1.,h2)*1.8;
  return col;
}
fn diskColor(r:f32,p:vec3f,nh:vec3f)->vec3f {
  let edge=smoothstep(5.9,6.5,r)*(1.-smoothstep(16.,20.,r));
  let tempRel=pow(max(1.-sqrt(6./r),0.)/(r*r*r),.25)*7.857;
  let diskN=normalize(U.params.yzw); let referenceAxis=select(vec3f(0,0,1),vec3f(0,1,0),abs(diskN.z)>.92);
  let diskX=normalize(cross(referenceAxis,diskN)); let diskY=cross(diskN,diskX);
  let phi=atan2(dot(p,diskY),dot(p,diskX)); let omega=pow(r,-1.5);
  let flow=fbm(vec3f(cos(phi-U.time*omega*2.5),sin(phi-U.time*omega*2.5),r*.7)*5.);
  let los=dot(normalize(cross(diskN,p)),normalize(U.camPos-p));
  let dop=clamp(1.+los/sqrt(max(r-2.,1.)),.35,2.2);
  let displayT=(4400.+min(U.diskTemp,2.e7)*.00012)*tempRel*dop;
  return blackbody(displayT)*edge*U.diskDensity*(.10+.72*flow*flow)*pow(dop,2.7)*.72;
}
fn aces(x:vec3f)->vec3f { return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),vec3f(0),vec3f(1)); }
fn unitOr(v:vec3f,fallback:vec3f)->vec3f {
  let l=length(v); return select(fallback,v/max(l,.00001),l>.00001);
}
fn raySphereT(ro:vec3f,rd:vec3f,c:vec3f,r:f32)->f32 {
  let oc=ro-c; let b=dot(oc,rd); let h=b*b-dot(oc,oc)+r*r;
  if(h<0.){return 1.e20;} let t=-b-sqrt(h); return select(1.e20,t,t>0.);
}
fn bodyAxis(body:Body)->vec3f {
  let radial=unitOr(body.pos_radius.xyz,vec3f(1,0,0));
  var velocity=unitOr(body.velocity_state.xyz,radial);
  if(dot(velocity,radial)<0.){velocity=-velocity;}
  return unitOr(mix(radial,velocity,clamp(body.debris.x*.72,0.,.72)),radial);
}
fn rayEllipsoidT(ro:vec3f,rd:vec3f,body:Body,axis:vec3f)->f32 {
  let d=body.debris.x; let fade=max(.12,body.debris.w);
  let coreRadius=body.pos_radius.w*mix(1.,.70,d)*fade;
  let stretch=1.+17.*d;
  let major=coreRadius*pow(stretch,.666667); let minor=coreRadius/pow(stretch,.333333);
  let oc=ro-body.pos_radius.xyz; let oa=dot(oc,axis); let da=dot(rd,axis);
  let op=oc-axis*oa; let dp=rd-axis*da;
  let a=dot(dp,dp)/(minor*minor)+da*da/(major*major);
  let b=dot(op,dp)/(minor*minor)+oa*da/(major*major);
  let c=dot(op,op)/(minor*minor)+oa*oa/(major*major)-1.;
  let h=b*b-a*c; if(h<0.){return 1.e20;}
  let t=(-b-sqrt(h))/a; return select(1.e20,t,t>0.);
}
fn ellipsoidNormal(hitP:vec3f,body:Body,axis:vec3f)->vec3f {
  let d=body.debris.x; let fade=max(.12,body.debris.w);
  let coreRadius=body.pos_radius.w*mix(1.,.70,d)*fade;
  let stretch=1.+17.*d;
  let major=coreRadius*pow(stretch,.666667); let minor=coreRadius/pow(stretch,.333333);
  let q=hitP-body.pos_radius.xyz; let qa=dot(q,axis);
  return normalize((q-axis*qa)/(minor*minor)+axis*qa/(major*major));
}
fn rayCapsuleT(ro:vec3f,rd:vec3f,pa:vec3f,pb:vec3f,ra:f32)->f32 {
  let ba=pb-pa; let oa=ro-pa; let baba=dot(ba,ba); let bard=dot(ba,rd); let baoa=dot(ba,oa);
  let rdoa=dot(rd,oa); let oaoa=dot(oa,oa); let a=baba-bard*bard;
  let b=baba*rdoa-baoa*bard; let c=baba*oaoa-baoa*baoa-ra*ra*baba;
  let h=b*b-a*c;
  if(h>=0. && abs(a)>.000001){let t=(-b-sqrt(h))/a;let y=baoa+t*bard;if(t>0.&&y>0.&&y<baba){return t;}}
  return min(raySphereT(ro,rd,pa,ra),raySphereT(ro,rd,pb,ra));
}
fn debrisPoint(body:Body,s:f32,arm:f32)->vec3f {
  let pos=body.pos_radius.xyz; let radial=unitOr(pos,vec3f(1,0,0));
  let velocity=unitOr(body.velocity_state.xyz,-radial);
  let referenceAxis=select(vec3f(0,0,1),vec3f(0,1,0),abs(radial.z)>.9);
  let normal=unitOr(cross(pos,velocity),cross(radial,referenceAxis));
  var tangent=unitOr(cross(normal,radial),velocity);if(dot(tangent,velocity)<0.){tangent=-tangent;}
  let disruption=body.debris.x; let phase=body.debris.y;
  let orbitality=smoothstep(.08,.90,length(cross(pos,velocity)));
  let tailLength=max(body.pos_radius.w*8.,body.debris.z*.36)*disruption+body.pos_radius.w*1.5;
  let arc=mix(.012*disruption,.20+min(5.2,disruption*2.7+phase*.24),orbitality);
  let bend=arm*arc*s*select(1.,.62,arm>0.);
  let drift=select(tailLength*.13*s,-tailLength*.055*s,arm>0.);
  let rr=max(2.015,length(pos)+drift);
  return rr*(cos(bend)*radial+sin(bend)*tangent)+normal*sin(s*6.2831853+U.time*.35)*body.pos_radius.w*.12*disruption*orbitality;
}
fn streamShade(body:Body,hitP:vec3f,s:f32)->vec3f {
  let kind=body.color_emit.w; let d=body.debris.x; let fade=body.debris.w;
  var hot=mix(body.color_emit.rgb,vec3f(1.15,.48,.12),.30+.40*d);
  if(kind>.5&&kind<1.5){
    let warm=mix(body.color_emit.rgb,vec3f(1.28,.34,.065),.48)*.88;
    let circularShock=smoothstep(3.2,5.2,body.debris.y);
    hot=mix(warm,blackbody(11500.)*1.08,circularShock);
  }
  if(kind>1.5){hot=mix(body.color_emit.rgb,vec3f(.75,1.35,1.9),.65*d);}
  let knots=.18+.58*fbm(hitP*(4.+7.*d)+vec3f(U.time*.07,-U.time*.04,s*9.));
  let shocks=1.+.85*pow(fbm(hitP*17.+vec3f(U.time*.13,4,9)),5.)*d;
  return hot*knots*shocks*fade*(.48+.52*(1.-s));
}
fn streamCoverage(body:Body,hitP:vec3f,s:f32)->f32 {
  let coarse=fbm(hitP*5.2+vec3f(s*13.,body.pos_radius.x*.31,body.pos_radius.y*.27));
  let fiber=.5+.5*sin(dot(hitP,unitOr(body.velocity_state.xyz,vec3f(1,0,0)))*19.+coarse*8.);
  return coarse*.72+fiber*.28;
}
fn bodyShade(body:Body,n:vec3f,hitP:vec3f,ro:vec3f)->vec3f {
  let viewDir=normalize(ro-hitP);
  let mu=clamp(dot(n,viewDir),0.,1.); let rim=pow(1.-mu,3.); let kind=body.color_emit.w;
  if(kind<-.5) { return vec3f(.04,.22,.24)*pow(1.-mu,9.)*1.8; }
  if(kind<.5) {
    let terrain=fbm(n*5.5+vec3f(3.1,7.2,1.4));
    let clouds=smoothstep(.56,.76,fbm(n*11.+vec3f(U.time*.018,0.,0.)));
    let bands=.82+.18*sin(n.z*26.+terrain*4.);
    let surface=body.color_emit.rgb*(.45+.68*terrain)*bands;
    return surface*(.14+.86*mu)+clouds*vec3f(.46,.55,.57)*mu*.55+rim*body.color_emit.rgb*.32;
  }
  if(kind<1.5) {
    let granules=fbm(n*13.+vec3f(U.time*.035,U.time*.018,0.));
    let limb=.32+.68*sqrt(mu);
    return body.color_emit.rgb*(1.15+1.4*granules)*limb+rim*vec3f(.75,.22,.04)*.8;
  }
  let pulse=.85+.15*sin(U.time*4.+n.z*24.);
  return body.color_emit.rgb*(2.2+.8*mu)*pulse+rim*vec3f(.4,1.2,1.8)*2.;
}

@fragment fn fs(i:VOut)->@location(0) vec4f {
  let aspect=U.resolution.x/U.resolution.y;
  let q=vec2f((i.uv.x*2.-1.)*aspect,(1.-i.uv.y)*2.-1.);
  var rd=normalize(U.camFwd+U.tanHalfFov*(q.x*U.camRight+q.y*U.camUp));
  let ro=U.camPos; let r0=length(ro); let er=ro/r0;

  // The intact object deforms into a volume-conserving ellipsoid. Once the
  // tidal limit is crossed, continuous leading/trailing capsules trace the
  // ballistic stream; the stream therefore exists before any disk can appear.
  var nearest=1.e20; var directCol=vec3f(0); var directHit=false;
  let centerB=dot(ro,rd); let horizonDisc=centerB*centerB-(dot(ro,ro)-4.);
  var horizonT=1.e20;
  if(horizonDisc>=0.) { let ht=-centerB-sqrt(horizonDisc); if(ht>0.){horizonT=ht;} }
  for(var directIndex=0;directIndex<16;directIndex++) {
    if(f32(directIndex)>=U.bodyCount){break;}
    let body=B[directIndex]; let axis=bodyAxis(body);
    let bodyT=rayEllipsoidT(ro,rd,body,axis);
    if(bodyT<nearest && bodyT<horizonT) {
      let hitP=ro+rd*bodyT;
      nearest=bodyT;directCol=bodyShade(body,ellipsoidNormal(hitP,body,axis),hitP,ro);directHit=true;
    }
    if(body.debris.x>.015 && body.color_emit.w>-.5) {
      for(var armIndex=0;armIndex<2;armIndex++) {
        let arm=select(-1.,1.,armIndex==1); var previous=debrisPoint(body,0.,arm);
        for(var segment=1;segment<=10;segment++) {
          let s=f32(segment)/10.; let current=debrisPoint(body,s,arm);
          let width=max(.012,body.pos_radius.w*mix(.38,.035,s)*mix(1.,.70,body.debris.x));
          let streamT=rayCapsuleT(ro,rd,previous,current,width);
          if(streamT<nearest && streamT<horizonT) {
            let streamP=ro+rd*streamT;
            if(streamCoverage(body,streamP,s)>.34+.20*s) {
              nearest=streamT;directCol=streamShade(body,streamP,s);directHit=true;
            }
          }
          previous=current;
        }
      }
    }
  }
  if(directHit) {
    var primary=aces(directCol*U.exposure);primary=pow(primary,vec3f(1./2.2));
    return vec4f(primary,1.);
  }

  var nv=cross(er,rd); var vt=max(length(nv),.0001); let nh=nv/vt; let e2=cross(nh,er);
  let sqrtF=sqrt(max(1.-2./r0,.001)); let vr=dot(rd,er);
  var u=1./r0; var w=-u*sqrtF*vr/vt; var phi=0.;
  var col=vec3f(0); var escaped=false; var captured=false; var hit=false;
  let diskN=normalize(U.params.yzw);
  var minR=999.; var prevP=ro; var prevPlane=dot(ro,diskN);
  for(var step=0;step<384;step++) {
    if(f32(step)>=U.params.x){break;}
    var h=select(.11,.065,u>.05); h=select(h,.033,u>.16);
    let u0=u; let w0=w;
    let k1u=w0; let k1w=3.*u0*u0-u0;
    let ua=u0+.5*h*k1u; let wa=w0+.5*h*k1w;
    let k2u=wa; let k2w=3.*ua*ua-ua;
    let ub=u0+.5*h*k2u; let wb=w0+.5*h*k2w;
    let k3u=wb; let k3w=3.*ub*ub-ub;
    let uc=u0+h*k3u; let wc=w0+h*k3w;
    let k4u=wc; let k4w=3.*uc*uc-uc;
    u=u0+h*(k1u+2.*k2u+2.*k3u+k4u)/6.;
    w=w0+h*(k1w+2.*k2w+2.*k3w+k4w)/6.; phi+=h;
    let r=1./max(u,.00001); minR=min(minR,r);
    let p=r*(cos(phi)*er+sin(phi)*e2);

    // The horizon is one-way. Never paint a spawned object over a photon
    // segment that has already crossed it.
    if(u>.5 || r<=2.001){captured=true;break;}

    let plane=dot(p,diskN);
    if(U.diskDensity>.003 && plane*prevPlane<=0.) {
      let crossT=abs(prevPlane)/max(abs(prevPlane)+abs(plane),.00001);
      let crossP=mix(prevP,p,crossT); let crossR=length(crossP);
      if(crossR>5.9 && crossR<20.) {
        let dc=diskColor(crossR,crossP,nh); col+=dc; if(length(dc)>.025){hit=true;break;}
      }
    }
    prevP=p; prevPlane=plane;
    if(u<.0065 && w<0.){escaped=true;break;}
    if(phi>22.){captured=true;break;}
  }
  if(escaped&&!hit) {
    let rhat=cos(phi)*er+sin(phi)*e2; let that=-sin(phi)*er+cos(phi)*e2;
    let escDir=normalize(-w*rhat+u*that); col+=stars(escDir);
  }
  let ring=exp(-pow((minR-3.)/.12,2.));
  col += vec3f(.55,.78,1.)*ring*.13;
  if(captured&&!hit){col*=.02;}
  let radial=length(q); col*=1.-.12*smoothstep(.65,1.5,radial);
  col=aces(col*U.exposure); col=pow(col,vec3f(1./2.2));
  let dither=(hash11(dot(i.uv,U.resolution)+U.time)-.5)/255.;
  return vec4f(col+dither,1.);
}`;

async function initWebGPU() {
  if (!navigator.gpu) throw new Error("WebGPU unavailable");
  const adapter = await navigator.gpu.requestAdapter({ powerPreference:"high-performance" });
  if (!adapter) throw new Error("No WebGPU adapter");
  const device = await adapter.requestDevice();
  device.lost.then(() => addEvent("GPU context lost; reload to resume", true));
  const context = canvas.getContext("webgpu");
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({device,format,alphaMode:"opaque"});
  const module = device.createShaderModule({code:wgsl});
  const info = await module.getCompilationInfo();
  const errors = info.messages.filter(m=>m.type==="error");
  if (errors.length) throw new Error(errors.map(e=>e.message).join("\n"));
  const pipeline = device.createRenderPipeline({
    layout:"auto", vertex:{module,entryPoint:"vs"}, fragment:{module,entryPoint:"fs",targets:[{format}]},
    primitive:{topology:"triangle-list"}
  });
  const uniformBuffer = device.createBuffer({size:128,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});
  const bodyBuffer = device.createBuffer({size:16*64,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST});
  const bindGroup = device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[
    {binding:0,resource:{buffer:uniformBuffer}},{binding:1,resource:{buffer:bodyBuffer}}
  ]});
  const uf = new Float32Array(32); const bf = new Float32Array(16*16);
  return {
    name:"WEBGPU", resize(){ resizeCanvas(); },
    render(frame,bodies) {
      uf.fill(0); bf.fill(0);
      uf.set([canvas.width,canvas.height,state.simGeom*.05,1.12],0);
      uf.set([...frame.pos,Math.tan(52*Math.PI/360)],4);
      uf.set([...frame.fwd,state.disk],8);
      uf.set([...frame.right,state.diskTemp],12);
      uf.set([...frame.up,Math.min(16,bodies.length)],16);
      uf.set([state.quality,...state.diskNormal],20);
      bodies.slice(0,16).forEach((b,j)=>{
        const colors = b.type===0 ? (b.comp===1?[.23,.48,.62]:b.comp===2?[.38,.64,.72]:[.48,.38,.28]) :
          b.type===1 ? [1.05,.46,.12] : b.type===2 ? [.35,.95,1.45] : [0,0,0];
        const kind = b.type===3 ? -1 : b.type;
        const fade=b.state===3?Math.max(.08,1-b.postCaptureAge/2.5):1;
        bf.set([b.x,b.y,b.z,b.radius,colors[0],colors[1],colors[2],kind,
          b.vx,b.vy,b.vz,b.state,b.disruption,b.streamPhase,b.tidalRadius,fade],j*16);
      });
      device.queue.writeBuffer(uniformBuffer,0,uf);
      device.queue.writeBuffer(bodyBuffer,0,bf);
      const encoder=device.createCommandEncoder();
      const pass=encoder.beginRenderPass({colorAttachments:[{view:context.getCurrentTexture().createView(),clearValue:{r:0,g:0,b:0,a:1},loadOp:"clear",storeOp:"store"}]});
      pass.setPipeline(pipeline);pass.setBindGroup(0,bindGroup);pass.draw(3);pass.end();device.queue.submit([encoder.finish()]);
    }
  };
}

function initWebGLFallback() {
  const gl=canvas.getContext("webgl2",{antialias:false,powerPreference:"high-performance"});
  if(!gl) throw new Error("No GPU canvas available");
  const vs=`#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}`;
  const fs=`#version 300 es
  precision highp float;in vec2 uv;out vec4 O;uniform vec2 res;uniform float time;uniform float radius;
  float h(vec3 p){return fract(sin(dot(floor(p),vec3(127.1,311.7,74.7)))*43758.5453);}
  vec3 stars(vec3 d){float a=h(d*360.);float b=h(d*120.);vec3 c=vec3(.002,.004,.009);c+=vec3(.7,.85,1.4)*smoothstep(.995,1.,a)*5.;c+=vec3(1.2,.65,.25)*smoothstep(.987,1.,b)*2.;float band=exp(-pow(dot(d,normalize(vec3(.3,.15,.94)))*8.,2.));return c+band*vec3(.035,.025,.06);}
  void main(){vec2 p=(uv*2.-1.)*vec2(res.x/res.y,1);float r=length(p);float bh=0.285*26./radius;float lens=bh*bh/max(r,.003);float ang=atan(p.y,p.x);vec2 q=vec2(cos(ang),sin(ang))*(r+lens);vec3 d=normalize(vec3(q,1));d.xy=mat2(cos(time*.003),-sin(time*.003),sin(time*.003),cos(time*.003))*d.xy;vec3 c=stars(d);float shadow=1.-smoothstep(bh*.93,bh,r);c*=1.-shadow;float ring=exp(-pow((r-bh*1.08)/(bh*.025),2.));c+=ring*vec3(.4,.75,1.2);c=pow(c/(1.+c),vec3(.4545));O=vec4(c,1);}`;
  const shader=(type,src)=>{const s=gl.createShader(type);gl.shaderSource(s,src);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(s));return s;};
  const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vs));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fs));gl.linkProgram(program);
  const vao=gl.createVertexArray();
  return {name:"WEBGL2 FALLBACK",resize(){resizeCanvas();gl.viewport(0,0,canvas.width,canvas.height);},render(frame){gl.useProgram(program);gl.bindVertexArray(vao);gl.uniform2f(gl.getUniformLocation(program,"res"),canvas.width,canvas.height);gl.uniform1f(gl.getUniformLocation(program,"time"),state.simGeom);gl.uniform1f(gl.getUniformLocation(program,"radius"),vecLen(frame.pos));gl.drawArrays(gl.TRIANGLES,0,3);}};
}

function resizeCanvas() {
  const dpr=Math.min(devicePixelRatio||1,state.quality>=300?1.65:state.quality>=180?1.35:1);
  const w=Math.max(1,Math.floor(canvas.clientWidth*dpr)), h=Math.max(1,Math.floor(canvas.clientHeight*dpr));
  if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
}

boot(66, "Compiling WebGPU geodesic pipeline…");
try { state.renderer=await initWebGPU(); }
catch(err) { console.warn(err); state.renderer=initWebGLFallback(); addEvent("WebGPU unavailable — high-quality WebGL fallback active",true); }
$("#engineName").textContent=state.renderer.name;
state.renderer.resize();
boot(88, "Calibrating observer frame…");

function setPreset(type) {
  state.selectedType=type; const p=presets[type];
  $$(".object-type").forEach(el=>{const yes=el.dataset.type===type;el.classList.toggle("active",yes);el.setAttribute("aria-checked",String(yes));});
  $("#presetLabel").textContent=p.name; $("#massUnit").textContent=p.unit;
  $("#objectMass").value=p.mass; $("#objectMass").step=type==="planet"?.1:.01;
  const r=$("#objectRadius"); r.min=p.rmin;r.max=p.rmax;r.step=p.rstep;r.value=p.radius;
  $("#composition").innerHTML=p.comps.map(([v,n])=>`<option value="${v}">${n}</option>`).join("");
  updateInjectionReadout();
}

function updateInjectionReadout() {
  const p=presets[state.selectedType]; const mass=Number($("#objectMass").value)||0;
  const rad=Number($("#objectRadius").value); const r=Number($("#spawnRadius").value);
  const orbit=Number($("#orbitFactor").value), inward=Number($("#inwardFactor").value);
  $("#objectMassOut").textContent=fmt(mass,mass<10?2:0); $("#objectRadiusOut").textContent=fmt(rad,rad<100?1:0);
  $("#spawnRadiusOut").textContent=fmt(r,1); $("#orbitFactorOut").textContent=`${Math.round(orbit*100)}%`; $("#inwardFactorOut").textContent=`${Math.round(inward*100)}%`;
  const v=core.circularV(r)*orbit; $("#injectionSpeed").textContent=`${fmt(v,0)} km/s`;
  const trajectory=Math.abs(inward)<.08?"CLICK TO AIM":inward>0?"PROGRADE CURVE":"RETROGRADE CURVE";
  $("#trajectoryType").textContent=trajectory;
}

function addEvent(message,warning=false) {
  const node=document.createElement("div");node.className=`event-toast${warning?" warning":""}`;node.innerHTML=message;
  $("#eventFeed").prepend(node);while($("#eventFeed").children.length>3)$("#eventFeed").lastElementChild.remove();setTimeout(()=>node.remove(),8000);
}

function cancelAim() {
  if(!state.aiming)return;
  state.aiming=false;app.classList.remove("aiming");$("#aimOverlay").setAttribute("aria-hidden","true");
  $("#launchButton span").textContent="INJECT INTO SPACETIME";$("#launchButton i").textContent="↗";
}

function beginAim() {
  if(state.aiming){cancelAim();return;}
  state.aiming=true;app.classList.add("aiming");$("#aimOverlay").setAttribute("aria-hidden","false");
  $("#launchButton span").textContent="CANCEL TARGETING";$("#launchButton i").textContent="×";
  $("#panel").classList.remove("open");
  addEvent("<b>TARGETING ACTIVE</b> — click the horizon for a direct plunge, or miss it deliberately");
}

function screenAim(clientX,clientY) {
  const rect=canvas.getBoundingClientRect(),u=(clientX-rect.left)/rect.width,v=(clientY-rect.top)/rect.height;
  const frame=cameraFrame(),aspect=rect.width/rect.height,tan=Math.tan(52*Math.PI/360);
  const qx=(u*2-1)*aspect,qy=(1-v)*2-1;
  const ray=norm(frame.fwd.map((x,i)=>x+tan*(qx*frame.right[i]+qy*frame.up[i])));
  const along=Math.max(0,-dot(frame.pos,ray));
  let target=frame.pos.map((x,i)=>x+ray[i]*along);let impact=vecLen(target);
  if(impact>18){target=target.map(x=>x*18/impact);impact=18;}
  return {frame,target,impact};
}

function updateAimPointer(e) {
  if(!state.aiming)return;
  const rect=$("#aimOverlay").getBoundingClientRect();
  app.style.setProperty("--aim-x",`${Math.max(0,Math.min(rect.width,e.clientX-rect.left))}px`);
  app.style.setProperty("--aim-y",`${Math.max(0,Math.min(rect.height,e.clientY-rect.top))}px`);
  const aim=screenAim(e.clientX,e.clientY);$("#aimReadout").innerHTML=`IMPACT PARAMETER ${fmt(aim.impact,2)} r<sub>g</sub>`;
}

function launchAt(e) {
  const p=presets[state.selectedType], displayMass=Number($("#objectMass").value);
  const solar=displayMass*p.solar, radius=Number($("#objectRadius").value), comp=Number($("#composition").value);
  const aim=screenAim(e.clientX,e.clientY),spawnR=Number($("#spawnRadius").value);
  const start=norm(aim.frame.pos).map(x=>x*spawnR);
  const slot=core.spawnAimed(p.id,solar,radius,comp,start[0],start[1],start[2],aim.target[0],aim.target[1],aim.target[2],Number($("#orbitFactor").value),Number($("#inwardFactor").value));
  if(slot<0){addEvent("<b>CAPACITY REACHED</b> — wait for an object to escape or be captured",true);return;}
  addEvent(`<b>OBJECT ${String(slot+1).padStart(2,"0")} INJECTED</b> — ${p.name} / aim b=${fmt(aim.impact,2)} r<sub>g</sub>`);
  if(p.id===3)addEvent(`<b>N-BODY GRAVITY ACTIVE</b> — companion mass ratio ${(solar/state.bhMass).toExponential(2)}`);
  $("#launchButton").animate([{filter:"brightness(1.8)"},{filter:"brightness(1)"}],{duration:380});
  cancelAim();
}

function setMode(mode) {
  cancelAim();
  state.mode=mode;app.classList.toggle("pilot",mode==="pilot");
  $$(".mode-switch button").forEach(b=>b.classList.toggle("active",b.dataset.mode===mode));
  $("#pilotHud").setAttribute("aria-hidden",String(mode!=="pilot"));
  if(mode==="pilot"){
    const r=vecLen(state.pilot.pos),rad=norm(state.pilot.pos),tangent=norm(cross([0,0,1],rad));
    const vc=core.circularV(r)/299792.458;state.pilot.vel=tangent.map(x=>x*vc);
    addEvent(`<b>PILOT FRAME ACTIVE</b> — circular insertion ${fmt(vc,4)} c`);
  }
}

function updatePilot(dt,dtGeom,frame) {
  if(state.mode!=="pilot")return;
  const p=state.pilot, keys=state.keys,lookRate=1.35*dt;
  if(keys.has("ArrowLeft"))p.yaw+=lookRate;if(keys.has("ArrowRight"))p.yaw-=lookRate;
  if(keys.has("ArrowUp"))p.pitch=Math.min(1.45,p.pitch+lookRate);if(keys.has("ArrowDown"))p.pitch=Math.max(-1.45,p.pitch-lookRate);
  const controlFrame=cameraFrame();let thrust=[0,0,0];
  if(keys.has("KeyW"))thrust=thrust.map((x,i)=>x+controlFrame.fwd[i]);
  if(keys.has("KeyS"))thrust=thrust.map((x,i)=>x-controlFrame.fwd[i]);
  if(keys.has("KeyD"))thrust=thrust.map((x,i)=>x+controlFrame.right[i]);
  if(keys.has("KeyA"))thrust=thrust.map((x,i)=>x-controlFrame.right[i]);
  if(keys.has("KeyE"))thrust=thrust.map((x,i)=>x+controlFrame.up[i]);
  if(keys.has("KeyQ"))thrust=thrust.map((x,i)=>x-controlFrame.up[i]);
  const firing=vecLen(thrust)>0; p.thrust+=(Number(firing)-p.thrust)*Math.min(1,dt*8);
  if(firing){thrust=norm(thrust);const accel=(keys.has("ShiftLeft")?0.085:0.028)*dt;p.vel=p.vel.map((x,i)=>x+thrust[i]*accel);}
  if(keys.has("KeyR")){
    const r=vecLen(p.pos),t=norm(cross([0,0,1],norm(p.pos))),v=core.circularV(r)/299792.458;p.vel=t.map(x=>x*v);
  }
  let r=vecLen(p.pos);const gravStep=Math.min(dtGeom,.055);
  if(r>2.02){const amag=1/Math.max(.08,(r-2)*(r-2));p.vel=p.vel.map((v,i)=>v-amag*(p.pos[i]/r)*gravStep);}
  let v=vecLen(p.vel);if(v>.985){p.vel=p.vel.map(x=>x*.985/v);v=.985;}
  p.pos=p.pos.map((x,i)=>x+p.vel[i]*gravStep);r=vecLen(p.pos);
  if(r<2.08){p.pos=norm(p.pos).map(x=>x*18);const t=norm(cross([0,0,1],norm(p.pos))),cv=core.circularV(18)/299792.458;p.vel=t.map(x=>x*cv);addEvent("<b>HORIZON INTERCEPT</b> — observer reset; no outward worldline exists",true);}
}

function collectBodies() {
  const list=[];for(let i=0;i<24;i++){if(!core.bodyActive(i))continue;list.push({slot:i,x:core.bodyValue(i,0),y:core.bodyValue(i,1),z:core.bodyValue(i,2),vx:core.bodyValue(i,3),vy:core.bodyValue(i,4),vz:core.bodyValue(i,5),mass:core.bodyValue(i,6),radius:core.bodyValue(i,8),temp:core.bodyValue(i,9),type:core.bodyValue(i,10),comp:core.bodyValue(i,11),state:core.bodyValue(i,12),age:core.bodyValue(i,13),disruption:core.bodyValue(i,14),streamPhase:core.bodyValue(i,15),tidalRadius:core.bodyValue(i,16),postCaptureAge:core.bodyValue(i,17),circularized:core.bodyValue(i,18)});}return list;
}

function updateEvent() {
  const e=core.event();if(!e)return;
  if(e===2)addEvent("<b>TIDAL LIMIT CROSSED</b> — the near side accelerates away from the far side; leading and trailing debris arms are forming",true);
  if(e===3)addEvent("<b>STREAM SELF-INTERSECTION</b> — returning debris is shocking, losing orbital energy, and beginning to circularize",true);
  if(e===4)addEvent(`<b>HORIZON CAPTURE</b> — central mass increased to ${compact(core.getMass()," M☉")}`,true);
  if(e===5)addEvent(`<b>BLACK-HOLE MERGER</b> — ${core.mergerEnergy().toExponential(2)} J radiated in ringdown`,true);
  if(e===6)addEvent("<b>OBJECT ESCAPED</b> — positive-energy trajectory left the local domain");
  if(e===7)addEvent("<b>COMPANION CAPTURE</b> — injected black hole absorbed nearby matter",true);
  if(e===8)addEvent("<b>DEBRIS CROSSING HORIZON</b> — the core is gone, but the exterior tidal stream remains visible",true);
}

function updateUI(frame) {
  const mass=core.getMass(),rg=core.rgKm(),h=core.horizon(),r=vecLen(frame.pos),lapse=core.lapse(r);
  $("#topMass").textContent=mass>=1e6?`${fmt(mass/1e6,2)}M M☉`:`${compact(mass," M☉")}`;
  $("#topHorizon").textContent=h>=1e6?`${fmt(h/1e6,2)}M km`:`${compact(h," km")}`;
  $("#observerRadius").innerHTML=`${fmt(r,2)} r<sub>g</sub>`;$("#observerKm").textContent=`${compact(r*rg," km")}`;
  $("#timeDilation").textContent=fmt(lapse,5);$("#photonKm").textContent=`${compact(3*rg," km")}`;
  const elapsed=state.simSeconds;const d=Math.floor(elapsed/86400),hh=Math.floor(elapsed/3600)%24,mm=Math.floor(elapsed/60)%60,ss=Math.floor(elapsed)%60;
  $("#simClock").textContent=`T + ${d?`${d}D `:""}${String(hh).padStart(2,"0")}:${String(mm).padStart(2,"0")}:${String(ss).padStart(2,"0")}`;
  if(state.mode==="pilot"){
    const v=vecLen(state.pilot.vel),proper=lapse*Math.sqrt(Math.max(0,1-v*v));
    $("#pilotHud").dataset.yaw=String(state.pilot.yaw);$("#pilotHud").dataset.pitch=String(state.pilot.pitch);
    $("#hudVelocity").textContent=`${fmt(v,4)} c`;$("#hudVelocityKm").textContent=`${fmt(v*299792.458,0)} km/s`;
    $("#hudRadius").innerHTML=`${fmt(r,2)} r<sub>g</sub>`;$("#hudProper").textContent=`τ/t∞ ${fmt(proper,4)}`;
    $("#thrustMeter").style.width=`${state.pilot.thrust*100}%`;
    const hazard=r<3?"NO STABLE WORLDLINE":r<6?"INSIDE ISCO":r<10?"EXTREME TIDAL FIELD":"STABLE ORBIT";
    $("#hazardLabel").textContent=hazard;$("#hazardLabel").style.color=r<6?"var(--danger)":"var(--cyan)";
  }
}

function loop(now) {
  const dt=Math.min(.05,(now-state.lastFrame)/1000||.016);state.lastFrame=now;
  const tg=Math.max(core.tg(),1e-8),simReal=state.paused?0:dt*state.timeRate,dtGeom=Math.min(2500,simReal/tg);
  if(!state.paused){core.step(dtGeom);state.simSeconds+=simReal;state.simGeom+=dtGeom;}
  let frame=cameraFrame();updatePilot(dt,dtGeom,frame);frame=cameraFrame();
  state.bodies=collectBodies();state.disk=core.disk();state.diskTemp=core.diskTemp();state.diskMass=core.diskMass();
  state.diskNormal=[core.diskNormalX(),core.diskNormalY(),core.diskNormalZ()];
  state.renderer.render(frame,state.bodies);updateEvent();updateUI(frame);requestAnimationFrame(loop);
}

$$('[data-mode]').forEach(b=>b.addEventListener("click",()=>setMode(b.dataset.mode)));
$$('.object-type').forEach(b=>b.addEventListener("click",()=>setPreset(b.dataset.type)));
[$("#objectMass"),$("#objectRadius"),$("#spawnRadius"),$("#orbitFactor"),$("#inwardFactor")].forEach(el=>el.addEventListener("input",updateInjectionReadout));
$("#launchButton").addEventListener("click",beginAim);
$("#playPause").addEventListener("click",()=>{state.paused=!state.paused;$("#playPause").textContent=state.paused?"▶":"Ⅱ";});
$("#timeRate").addEventListener("input",e=>{state.timeRate=10**Number(e.target.value);const exp=Number(e.target.value);$("#timeRateLabel").innerHTML=`${exp<1?fmt(state.timeRate,1):`10<sup>${fmt(exp,1)}</sup>`}× <small>${exp===0?"REAL TIME":"VISUALIZATION"}</small>`;});
$("#qualityButton").addEventListener("click",()=>{const q=[[112,"PERFORMANCE / 112"],[192,"HIGH / 192"],[320,"CINEMATIC / 320"]];state.qualityIndex=(state.qualityIndex+1)%q.length;[state.quality,$("#qualityLabel").textContent]=q[state.qualityIndex];state.renderer.resize();});
const massToSlider=m=>(Math.log10(m)-Math.log10(3))/(11-Math.log10(3));
const sliderToMass=v=>10**(Math.log10(3)+Number(v)*(11-Math.log10(3)));
$("#bhMassSlider").value=massToSlider(state.bhMass);
$("#bhMassSlider").addEventListener("input",e=>{$("#bhMass").value=Math.round(sliderToMass(e.target.value));$("#bhMass").dispatchEvent(new Event("change"));});
$("#bhMass").addEventListener("change",e=>{state.bhMass=Math.max(3,Math.min(1e11,Number(e.target.value)||4.3e6));core.setMass(state.bhMass);e.target.value=Math.round(state.bhMass);$("#bhMassSlider").value=massToSlider(state.bhMass);addEvent(`<b>CENTRAL MASS UPDATED</b> — ${compact(state.bhMass," M☉")}`);});
$("#panelToggle").addEventListener("click",()=>$("#panel").classList.add("open"));$("#panelClose").addEventListener("click",()=>$("#panel").classList.remove("open"));
$("#scienceInfo").addEventListener("click",()=>$("#scienceDialog").showModal());$(".dialog-close").addEventListener("click",()=>$("#scienceDialog").close());
window.addEventListener("resize",()=>state.renderer.resize());
canvas.addEventListener("pointerdown",e=>{if(state.aiming){e.preventDefault();launchAt(e);return;}state.drag=true;state.lastPointer=[e.clientX,e.clientY];canvas.setPointerCapture(e.pointerId);});
canvas.addEventListener("pointerup",()=>state.drag=false);
canvas.addEventListener("pointermove",e=>{updateAimPointer(e);if(!state.drag||state.aiming)return;const dx=e.clientX-state.lastPointer[0],dy=e.clientY-state.lastPointer[1];state.lastPointer=[e.clientX,e.clientY];const c=state.mode==="pilot"?state.pilot:state.observe;c.yaw-=dx*.004;c.pitch=Math.max(-1.45,Math.min(1.45,c.pitch-dy*.004));});
canvas.addEventListener("wheel",e=>{if(state.mode==="observe"){e.preventDefault();state.observe.radius=Math.max(6.3,Math.min(90,state.observe.radius*Math.exp(e.deltaY*.0008)));}},{passive:false});
window.addEventListener("keydown",e=>{if(e.code==="Escape"&&state.aiming){cancelAim();e.preventDefault();return;}if(["KeyW","KeyA","KeyS","KeyD","KeyQ","KeyE","KeyR","ShiftLeft","ArrowLeft","ArrowRight","ArrowUp","ArrowDown"].includes(e.code)){state.keys.add(e.code);if(state.mode==="pilot"){if(!e.repeat){if(e.code==="ArrowLeft")state.pilot.yaw+=.035;if(e.code==="ArrowRight")state.pilot.yaw-=.035;if(e.code==="ArrowUp")state.pilot.pitch=Math.min(1.45,state.pilot.pitch+.035);if(e.code==="ArrowDown")state.pilot.pitch=Math.max(-1.45,state.pilot.pitch-.035);}e.preventDefault();}}if(e.code==="Space"&&!/INPUT|SELECT/.test(document.activeElement.tagName)){$("#playPause").click();e.preventDefault();}});
window.addEventListener("keyup",e=>state.keys.delete(e.code));

setPreset("planet");
setTimeout(()=>{boot(100,"Spacetime solution stable");app.classList.remove("loading");app.classList.add("ready");addEvent("<b>LABORATORY ONLINE</b> — clean Schwarzschild scene / no disk");requestAnimationFrame(loop);},350);
