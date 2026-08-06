#version 410 core
// Generic colored-geometry shader for the spacetime-curvature view:
// the Flamm-paraboloid wireframe, the horizon, the disk band and infalling
// particles are all drawn with this.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4  uMVP;
uniform float uPointSize;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
