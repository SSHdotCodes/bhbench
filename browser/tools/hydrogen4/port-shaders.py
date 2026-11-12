"""Mechanical MSL -> GLSL ES 3.0 translation of the preserved submission.
Shader bodies remain the submission's. Compute texture writes become MRT outputs;
Metal's top-left coordinates become OpenGL bottom-left storage coordinates.
"""
from pathlib import Path
import re
repo=Path(__file__).resolve().parents[3]
root=repo/'submissions/gemini-4-hydrogen'
s=(root/'src/shaders/BlackHole.metal').read_text()
out=repo/'browser/live/shaders';out.mkdir(parents=True,exist_ok=True)
header='#version 300 es\nprecision highp float;\nprecision highp int;\n'
uniforms='''uniform vec4 camPosTime, camForwardFov, camRightAspect, camUpDummy, camBL;
uniform vec4 bhParams, diskParams, gridParams, visualParams, screenParams;
uniform mat4 viewProj;
'''
def glsl(t):
 t=re.sub(r'\b(?:inline|constexpr|constant)\s+',lambda m:'const ' if m[0].strip()!='inline' else '',t)
 for a,b in [('float4x4','mat4'),('float2x2','mat2'),('float2','vec2'),('float3','vec3'),('float4','vec4'),('atan2','atan'),('out','result')]:t=re.sub(r'\b'+a+r'\b',b,t)
 t=re.sub(r'(\d(?:[\d.]*)(?:e[+-]?\d+)?)f\b',r'\1',t)
 return t.replace('ubo.','')
helpers=s[s.index('constant float PI'):s.index('kernel void raytraceBlackHole')]
ray=s[s.index('    float2 uv =',s.index('kernel void raytraceBlackHole')):s.index('// ============================================================================\n// Pass 2')]
ray=ray[:ray.rindex('}')]
ray=ray.replace('(float2(gid) + 0.5f)','vec2(gl_FragCoord.x, screenParams.y-gl_FragCoord.y)')
ray=ray.replace('outColor.write(float4(accumColor, 1.0f), gid);','outColor = vec4(accumColor, 1.0);')
ray=ray.replace('outDepth.write(float4(hitDist, 0.0f, 0.0f, 0.0f), gid);','outDepth = vec4(hitDist, 0.0, 0.0, 0.0);')
(out/'hydrogen4.frag').write_text(header+uniforms+'layout(location=0) out vec4 outColor;\nlayout(location=1) out vec4 outDepth;\n'+glsl(helpers)+'void main(){\n'+glsl(ray)+'}\n')
vert='''layout(location=0) in vec4 posAndInfo;
layout(location=1) in vec4 normalAndTwist;
out vec3 worldPos;
out float radius, twist, camDist;
void main(){
 worldPos=posAndInfo.xyz; worldPos.z+=gridParams.y;
 gl_Position=viewProj*vec4(worldPos,1.0);
 // Native Metal projection [0,1] to OpenGL [-1,1].
 gl_Position.z=2.0*gl_Position.z-gl_Position.w;
 radius=posAndInfo.w;twist=normalAndTwist.w;
 camDist=length(worldPos-camPosTime.xyz);
}
'''
(out/'hydrogen4-grid.vert').write_text(header+uniforms+vert)
grid=s[s.index('    if (ubo.gridParams.x',s.index('fragment float4 fragmentSpacetimeGrid')):s.index('// ============================================================================\n// Pass 3')]
grid=grid[:grid.rindex('}')]
start=grid.index('    uint texW');end=grid.index('    float occlusion')
grid=grid[:start]+'    float sceneHitDist = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;\n\n'+grid[end:]
grid=grid.replace('discard_fragment();','discard;')
for a,b in [('in.worldPos','worldPos'),('in.camDist','camDist'),('in.twist','twist'),('in.r','radius')]:grid=grid.replace(a,b)
grid=grid.replace('return float4(color * alpha, clamp(alpha, 0.0f, 1.0f));','fragColor = vec4(color * alpha, clamp(alpha, 0.0, 1.0));')
(out/'hydrogen4-grid.frag').write_text(header+uniforms+'in vec3 worldPos;\nin float radius, twist, camDist;\nuniform highp sampler2D depthTex;\nout vec4 fragColor;\nvoid main(){\n'+glsl(grid)+'}\n')
extract=s[s.index('    float3 c0 =',s.index('kernel void extractBloomBright')):s.index('struct QuadVaryings')]
extract=extract[:extract.rindex('}')]
extract=extract.replace('inHDR.read(src)','texelFetch(inHDR, src, 0)').replace('inHDR.read(min(src + uint2(1, 0), uint2(inW - 1, inH - 1)))','texelFetch(inHDR, clamp(src + ivec2(1, 0), ivec2(0), size-1), 0)').replace('inHDR.read(min(src + uint2(0, 1), uint2(inW - 1, inH - 1)))','texelFetch(inHDR, clamp(src + ivec2(0, -1), ivec2(0), size-1), 0)').replace('inHDR.read(min(src + uint2(1, 1), uint2(inW - 1, inH - 1)))','texelFetch(inHDR, clamp(src + ivec2(1, -1), ivec2(0), size-1), 0)')
extract=extract.replace('outBloom.write(float4(col * clamp(weight, 0.0f, 3.5f), 1.0f), gid);','fragColor = vec4(col * clamp(weight, 0.0f, 3.5f), 1.0f);')
(out/'hydrogen4-extract.frag').write_text(header+'''uniform sampler2D inHDR;
uniform vec2 outputSize;
out vec4 fragColor;
void main(){
 ivec2 size=textureSize(inHDR,0);
 vec2 uv=vec2(gl_FragCoord.x,outputSize.y-gl_FragCoord.y)/outputSize;
 ivec2 src=ivec2(uv*vec2(size));src.y=size.y-1-src.y;
'''+glsl(extract)+'}\n')
aces=s[s.index('inline float3 acesFilmic'):s.index('fragment float4 fragmentCompositeFinal')]
comp=s[s.index('    float3 hdr =',s.index('fragment float4 fragmentCompositeFinal')):]
comp=comp[:comp.rindex('}')]
comp=comp.replace('sceneHDR.sample(linSampler, in.uv)','texture(sceneHDR, vUV)').replace('bloomTight.sample(linSampler, in.uv)','texture(bloomTight, vUV)').replace('bloomWide.sample(linSampler, in.uv)','texture(bloomWide, vUV)').replace('in.uv','vUV').replace('return float4(srgb, 1.0f);','fragColor=vec4(srgb,1.0f);')
(out/'hydrogen4-composite.frag').write_text(header+uniforms+'in vec2 vUV;\nuniform sampler2D sceneHDR, bloomTight, bloomWide;\nout vec4 fragColor;\n'+glsl(aces)+'void main(){\n'+glsl(comp)+'}\n')
(out/'hydrogen4-blur.frag').write_text(header+'''// Separable normalized Gaussian approximation of MPSImageGaussianBlur.
// Native sigmas are 5 and 20 pixels at half resolution; clamp edges, 3-sigma support.
in vec2 vUV;
uniform sampler2D sourceTex;
uniform vec2 direction;
uniform float sigma;
out vec4 fragColor;
void main(){
 vec3 sum=vec3(0.0);float weight=0.0;
 for(int i=-60;i<=60;i++){
  if(abs(float(i))>3.0*sigma)continue;
  float w=exp(-0.5*float(i*i)/(sigma*sigma));
  sum+=texture(sourceTex,vUV+direction*float(i)).rgb*w;weight+=w;
 }
 fragColor=vec4(sum/weight,1.0);
}
''')
print('Generated six shaders from unchanged BlackHole.metal')
