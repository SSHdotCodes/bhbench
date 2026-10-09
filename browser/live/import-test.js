
const defaultIds=['step5','sol61','grok47','mistral4','mimo26flash','sol6','qwen37flash','deepseek4','qwen3827b','haiku55','sonnet55','fable51','sonnet'];
const ids=(new URLSearchParams(location.search).get('models')||defaultIds.join(',')).split(',').filter(id=>BH_LIVE[id]);
document.querySelector('#run').onclick=async()=>{
 const canvas=document.querySelector('canvas'),out=document.querySelector('#result');out.textContent='starting';let view=new BHView(canvas);view.visible=true;
 const results=[];const paint=()=>{view._active=true;view._hidden=false;view.visible=true;view._frame(performance.now());view.stop();view.gl.finish();const data=new Uint8Array(view.S.w*view.S.h*4);view.gl.readPixels(0,0,view.S.w,view.S.h,view.gl.RGBA,view.gl.UNSIGNED_BYTE,data);return data;};
 for(const id of ids){
 try{const cfg=BH_LIVE[id],settings=Object.fromEntries((cfg.controls||[]).map(c=>[c.key,c.default]));settings.animate=false;settings.autoOrbit=false;settings.quality=0;
 const frag=await bhFetchShader(id);view.load({...cfg,frag,maxRes:240,dprCap:1,renderScale:()=>1},settings);let start=performance.now();let a=paint();const ms=performance.now()-start;const img=document.createElement('img');const caption=document.createElement('p');caption.textContent=id+' live';img.src=canvas.toDataURL();img.style.width='240px';caption.append(img);document.querySelector('#gallery').append(caption);const lit=a.reduce((n,v,i)=>n+(i%4!==3&&v>5),0);view.S.yaw+=.55;view.S.pitch+=.2;let b=paint();let changed=0;for(let i=0;i<a.length;i++)if(i%4!==3&&Math.abs(a[i]-b[i])>2)changed++;
 view.S.dist*=.8;let z=paint(),zoomChanged=0;for(let i=0;i<b.length;i++)if(i%4!==3&&Math.abs(b[i]-z[i])>2)zoomChanged++;
 const orbit=view.S.yaw,pitch=view.S.pitch,dist=view.S.dist;view.load({...cfg,frag,maxRes:240,dprCap:1,renderScale:()=>1},settings);const resetOk=view.S.yaw===cfg.init.yaw&&view.S.pitch===cfg.init.pitch&&view.S.dist===cfg.init.dist;
 let controlChanged=null;if(settings.disk!==undefined){view.settings.disk=false;let d=paint();let original=paint();controlChanged=0;view.settings.disk=true;let disk=paint();for(let i=0;i<d.length;i++)if(i%4!==3&&Math.abs(d[i]-disk[i])>2)controlChanged++;}
 let rawChanged=null;const scenes=[];if(id==='step5'){view.settings.disk=true;view.settings.stars=true;view.settings.grid=true;view.settings.viewMode=0;const full=paint();view.settings.single=true;const raw=paint();rawChanged=0;for(let i=0;i<raw.length;i++)if(i%4!==3&&Math.abs(raw[i]-full[i])>2)rawChanged++;view.settings.single=false;for(let scene=1;scene<=3;scene++){view.settings.viewMode=scene;const pixels=paint();let diff=0,visible=0;for(let i=0;i<pixels.length;i++){if(i%4!==3&&pixels[i]>5)visible++;if(i%4!==3&&Math.abs(pixels[i]-full[i])>2)diff++;}scenes.push({scene,visible,changed:diff,ok:visible>100&&diff>100});}}
 const error=view.gl.getError();results.push({id,scenes,rawChanged,ok:lit>100&&changed>100&&zoomChanged>100&&resetOk&&(controlChanged===null||controlChanged>100)&&(rawChanged===null||rawChanged>100)&&scenes.every(scene=>scene.ok)&&error===0,lit,changed,zoomChanged,resetOk,controlChanged,ms:Math.round(ms),error});
 }catch(error){results.push({id,ok:false,error:String(error)});}out.textContent=JSON.stringify(results,null,2);await new Promise(r=>setTimeout(r,50));
 }out.dataset.complete='true';
};
