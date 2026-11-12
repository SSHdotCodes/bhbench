#version 300 es
precision highp float;
precision highp int;
uniform vec4 camPosTime, camForwardFov, camRightAspect, camUpDummy, camBL;
uniform vec4 bhParams, diskParams, gridParams, visualParams, screenParams;
uniform mat4 viewProj;
layout(location=0) in vec4 posAndInfo;
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
