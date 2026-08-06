#version 330 core
// Flamm paraboloid: isometric embedding of the Schwarzschild spatial slice
//   z(r) = 2 * sqrt( Rs * (r - Rs) ) ,  r >= Rs
// The "trapdoor in spacetime": space stretches into a funnel whose throat
// is the event horizon.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aGrid;    // (r, theta)
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vGrid;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
void main(){
    vWorldPos = (uModel * vec4(aPos,1.0)).xyz;
    vNormal   = normalize((uModel * vec4(aNormal,0.0)).xyz);
    vGrid     = aGrid;
    gl_Position = uProj * uView * vec4(vWorldPos, 1.0);
}
