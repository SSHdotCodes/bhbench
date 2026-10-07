from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'submissions'; DST=ROOT/'browser/live/shaders'
HEADER='#version 300 es\nprecision highp float;\nprecision highp int;\n'
def glsl(s):
    s=re.sub(r'#version[^\n]*',HEADER.rstrip(),s)
    s=re.sub(r'\b(?:uv|vUv)\b','vUV',s)
    return s.lstrip()

def raw(folder,path,name):
    s=(SRC/folder/path).read_text()
    return re.search(r'\b'+name+r'\s*=\s*R"GLSL\((.*?)\)GLSL"',s,re.S).group(1)
for mid,folder,path in [('sol61','gpt-6.1-sol-xhigh','shaders/raytrace.frag'),('grok47','grok-4.7','shaders/trace.frag'),('mimo26flash','mimo-v2.6-flash','shaders/raytrace.frag'),('sol6','gpt-6-sol-xhigh','shaders/blackhole.frag'),('qwen37flash','qwen-3.7-flash','shaders/blackhole.frag')]:
    (DST/(mid+'.frag')).write_text(glsl((SRC/folder/path).read_text()))
(DST/'mistral4.frag').write_text(glsl(raw('mistral-large-4','src/shaders.h','kRtFs')))
s=glsl(raw('deepseek-v4-flash','src/shader_sources.hpp','LENSING_FRAG'))
s=s.replace('vUV.x * uRight + vUV.y * uUp','(vUV.x * 2.0 - 1.0) * uRight + (vUV.y * 2.0 - 1.0) * uUp')
(DST/'deepseek4.frag').write_text(s)

def cpp(s):
    s=re.sub(r'template\s*<[^>]+>','',s)
    s=re.sub(r'<(?:T|float)>','',s)
    s=re.sub(r'\bT\b','float',s)
    s=re.sub(r'\b(?:BH_INLINE|inline|static)\s+','',s)
    s=re.sub(r'\bconstant\s+','const ',s)
    s=re.sub(r'\b(?:thread|BH_REF)\s+','',s)
    s=re.sub(r'\b(?:float|uint|int)([234])\b',lambda m:{'float':'vec','int':'ivec','uint':'uvec'}[m.group(0)[:-1]]+m[1],s)
    s=s.replace('float3x3','mat3').replace('float4x4','mat4')
    s=re.sub(r'\b(\d*\.?\d+(?:[eE][-+]?\d+)?)[fh]\b',r'\1',s)
    s=s.replace('bh::','').replace('fabs(','abs(').replace('fmax(','max(').replace('fmin(','min(').replace('atan2(','atan(')
    s=re.sub(r'\(void\)\w+;','',s)
    # Read-only references become values; writable references become inout.
    s=re.sub(r'const\s+(\w+)\s*&',r'\1 ',s)
    s=re.sub(r'\b(\w+)\s*&\s*([A-Za-z_]\w*)',r'inout \1 \2',s)
    s=re.sub(r'\bconst\s+','',s)
    s=re.sub(r'\bout\b','result',s)
    return s
# Qwen's own MSL ray tracer, with pixel coordinates changed from Metal's top-left to GL's bottom-left.
s=(SRC/'qwen-3.8-27b/src/shaders.hpp').read_text().split('R"metal(')[1].split(')metal"')[0]
s=s[s.index('struct URT'):]
s=s[:s.index('kernel void raytrace')]+s[s.index('kernel void raytrace'):s.index('// ----------------------------------------------------------------- post') if '// ----------------------------------------------------------------- post' in s else s.index('kernel void blurH')]
prefix=s[:s.index('kernel void raytrace')]
body=s[s.index('kernel void raytrace'):]
startbrace=body.index('{'); depth=1; endbrace=startbrace+1
while depth:
    depth += (body[endbrace]=='{')-(body[endbrace]=='}'); endbrace+=1
body=body[startbrace+1:endbrace-1]
body=re.sub(r'const uint W.*?if \(gid.x.*?return;', 'vec2 px = vUV;\n',body,flags=re.S) if False else body
# Remove the original dispatch bounds and px setup, retaining all geodesic work.
start=body.index('float2 nd')
body=body[start:]
body=re.sub(r'float2 nd\s*=.*?;', 'vec2 nd = vUV * 2.0 - 1.0;',body,count=1)
body=body.replace('nd.y = -nd.y;', '')
body=re.sub(r'dst.write\(half4\((.*?)\), gid\);',r'fragColor = vec4(\1);',body,flags=re.S)
# Some Qwen source versions use a float4 output.
body=re.sub(r'outTex.write\(float4\((.*?)\), gid\);',r'fragColor = vec4(\1);',body,flags=re.S)
body=body.replace('half4','vec4').replace('half3','vec3').replace('half(','float(')
s=cpp(re.sub(r'\bT\b','tempK',prefix))+'\nuniform URT U;\nin vec2 vUV; out vec4 fragColor;\nfloat saturate(float x){return clamp(x,0.0,1.0);}\nvoid main(){\n'+cpp(body)+'\n}\n'
s=s.replace('float saturate(float x){return clamp(x,0.0,1.0);}','')
(DST/'qwen3827b.frag').write_text(HEADER+'#define saturate(x) clamp(x,0.0,1.0)\n'+s)
# Sonnet's shared Kerr integrator is translated directly, retaining its projection, wedge intersections and corona.
base=SRC/'claude-sonnet-5.5-xhigh/src'
s=(base/'kerr_shared.h').read_text(); s=s[s.index('template <class T>'):s.rindex('}  // namespace bh')]
s=cpp(s)
common=cpp(re.sub(r'\bT\b','tempK',(base/'shaders/common.metal').read_text()))
common=re.sub(r'device (?:const )?(?:float|vec4)\* (\w+)',r'sampler2D \1',common)
common=common.replace('lut[i + 1]','texelFetch(lut, ivec2(i + 1,0),0)').replace('lut[i]','texelFetch(lut, ivec2(i,0),0)')
common=common.replace('tab[i + 1]','texelFetch(tab, ivec2(i + 1,0),0).r').replace('tab[i]','texelFetch(tab, ivec2(i,0),0).r')
rt=(base/'shaders/raytrace.metal').read_text(); prefix=rt[:rt.index('kernel void raytrace')]
body=rt[rt.index('kernel void raytrace'):rt.index('// Validation probe')]; body=body[body.index('{')+1:body.rindex('}')]
body=re.sub(r'const uint W.*?if \(gid.x.*?return;','float W = U.view.x, H = U.view.y;',body,flags=re.S)
body=re.sub(r'float2 ndc = .*?;', 'vec2 ndc = vUV * 2.0 - 1.0;',body,count=1)
body=body.replace('ndc.y = -ndc.y;','')
body=body.replace('outTex.write(float4(col, moving), gid);','fragColor = vec4(col, moving);')
rt=cpp(prefix+ '\nvoid main(){'+body+'}\n')
rt=re.sub(r'device (?:const )?(?:float|vec4)\* (\w+)',r'sampler2D \1',rt)
rt=rt.replace('vec3 pal[4] = {','vec3 pal[4] = vec3[4](').replace('vec3(0.4, 1.0, 0.4)};','vec3(0.4, 1.0, 0.4));')
types='struct RTUniforms { vec4 cam; vec4 bh; vec4 view; vec4 jit; vec4 disk; vec4 opt; vec4 sky; vec4 misc; };\nuniform RTUniforms U;\nuniform sampler2D diskTab, bb;\nin vec2 vUV; out vec4 fragColor;\n'
(DST/'sonnet55.frag').write_text(HEADER+types+s+common+rt)
# Haiku: use native vector types for V3 and explicit Phase sums for its C++ operator overloads.
base=SRC/'claude-haiku-5.5-xhigh/src'; s=(base/'kerr_core.h').read_text(); s=s[s.index('// Phase-space point'):s.rindex('#endif')]
s=re.sub(r'template <typename T> inline Phase<T> operator\+.*?\n}', '',s,flags=re.S)
s=re.sub(r'template <typename T> inline Phase<T> operator\*.*?\n}', '',s,flags=re.S)
s=cpp(s).replace('V3','vec3')
s=s.replace('float l[3];','vec3 l;').replace('float dr[3];','vec3 dr;').replace('float dH[3];','vec3 dH;').replace('float dl[3][3];','mat3 dl;')
a=s.index('Phase rk4_step_k1'); b=s.index('Phase rk4_step(',a)
s=s[:a]+'''Phase phase_add(Phase a, Phase b){return Phase(a.x+b.x,a.p+b.p);}
Phase phase_scale(float h, Phase a){return Phase(h*a.x,h*a.p);}
Phase rk4_step_k1(float a, Phase s, Phase k1, float h){
 Phase k2=ray_rhs(a,phase_add(s,phase_scale(0.5*h,k1)));
 Phase k3=ray_rhs(a,phase_add(s,phase_scale(0.5*h,k2)));
 Phase k4=ray_rhs(a,phase_add(s,phase_scale(h,k3)));
 return phase_add(s,phase_scale(h/6.0,phase_add(phase_add(k1,phase_scale(2.0,k2)),phase_add(phase_scale(2.0,k3),k4))));
}
'''+s[b:]
# Replace the observer's four-vectors/array output with the equivalent GLSL vec4 frame.
a=s.index('void lower4');b=s.index('// Prograde equatorial circular photon orbit',a)
s=s[:a]+'''vec4 lower4(KSData k, float c0, vec3 cs){
 float lam=c0+dot(k.l,cs); return vec4(-c0+k.H*lam,cs+k.H*k.l*lam);
}
float g_dot(KSData k, vec4 a, vec4 b){return dot(a,lower4(k,b.x,b.yzw));}
struct Observer {vec3 pos; vec4 u; vec4 e[3]; float ut;};
Observer make_observer(float a,vec3 pos,vec3 right,vec3 up,vec3 fwd){
 Observer o; KSData k=ks_eval(a,pos); float ut=1.0/sqrt(1.0-k.H);
 o.pos=pos;o.ut=ut;o.u=lower4(k,ut,vec3(0));
 vec3 basis[3]=vec3[3](right,up,fwd);vec4 ec[3];
 for(int j=0;j<3;j++){
  vec4 c=vec4(dot(o.u.yzw,basis[j])*ut,basis[j]);
  for(int m=0;m<j;m++) c-=g_dot(k,c,ec[m])*ec[m];
  ec[j]=c/sqrt(g_dot(k,c,c));
 }
 for(int j=0;j<3;j++) o.e[j]=lower4(k,ec[j].x,ec[j].yzw);
 return o;
}
Phase pixel_ray(Observer o,float sx,float sy){
 float inv=1.0/sqrt(sx*sx+sy*sy+1.0);
 vec4 p=-o.u+sx*inv*o.e[0]+sy*inv*o.e[1]+inv*o.e[2];
 return Phase(o.pos,p.yzw/p.x);
}
'''+s[b:]
a=s.index('float disk_flux_norm');b=s.index('// Traces one photon',a)
s=s[:a]+'''float disk_flux_norm(sampler2D lut,int n,float r,float r0,float r1){
 float u=clamp((r-r0)/(r1-r0)*float(n-1),0.0,float(n-1));int i=int(u);float f=u-float(i);
 return mix(texelFetch(lut,ivec2(i,0),0).r,texelFetch(lut,ivec2(min(i+1,n-1),0),0).r,f);
}
'''+s[b:]
s=s.replace('inout TraceParams tp','TraceParams tp')
sh=(base/'shading.h').read_text();sh=sh[sh.index('inline float clampf'):sh.rindex('#endif')]
sh=cpp(sh).replace('V3','vec3').replace('P lut','sampler2D lut')
types='''struct TraceUniforms {float a,r_plus,r_capture,r_isco,r_out,eta,r_escape,cam_ut,disk_gain,T_peak,exposure;int lut_n,max_steps,bg_mode,disk_on;};
uniform TraceUniforms U;
uniform sampler2D lut; uniform vec3 camPos,camRight,camUp,camFwd; uniform vec2 resolution;
in vec2 vUV; out vec4 fragColor;
vec3 v3(float x,float y,float z){return vec3(x,y,z);}
float dot3(vec3 a,vec3 b){return dot(a,b);}float len3(vec3 a){return length(a);}vec3 norm3(vec3 a){return normalize(a);}
'''
main='''void main(){
 Observer obs=make_observer(U.a,camPos,camRight,camUp,camFwd);
 vec2 nd=vUV*2.0-1.0;
 Phase ray=pixel_ray(obs,nd.x*resolution.x/resolution.y*0.466307658,nd.y*0.466307658);
 TraceParams tp=TraceParams(U.a,U.r_plus,U.r_capture,U.r_isco,U.r_out,U.eta,U.r_escape,obs.ut,U.max_steps);
 Hit hit=trace_photon(tp,ray);vec3 c=shade_hit(U,lut,hit);
 c=1.0-exp(-U.exposure*c);fragColor=vec4(pow(clamp(c,0.0,1.0),vec3(1.0/2.2)),1.0);
}
'''
(DST/'haiku55.frag').write_text(HEADER+'#define OUT_ESCAPED 0\n#define OUT_CAPTURED 1\n#define OUT_DISK 2\n#define OUT_UNRESOLVED 3\n'+types+s+sh+main)
print('Generated ten browser ray tracers')

# Reproduce native bilinear lookup sampling without a float-linear extension.
for mid,sampler in [('grok47','uFluxTex'),('mimo26flash','uBB')]:
    p=DST/(mid+'.frag');s=p.read_text().replace('texture('+sampler+',','nativeLookup('+sampler+',')
    helper="""vec4 nativeLookup(sampler2D table,vec2 uv){
 int n=textureSize(table,0).x;float x=uv.x*float(n)-.5;int i=int(floor(x));
 return mix(texelFetch(table,ivec2(clamp(i,0,n-1),0),0),texelFetch(table,ivec2(clamp(i+1,0,n-1),0),0),fract(x));
}
"""
    k=s.index('\n',s.index('precision highp int;'))+1;p.write_text(s[:k]+helper+s[k:])
