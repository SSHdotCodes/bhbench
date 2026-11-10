#version 410 core
// Progressive supersampling: running mean of jittered frames while the view
// is frozen.  Blended by the fixed-function blender with alpha = 1/N.
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform float uWeight;
void main() { oColor = vec4(texture(uSrc, vUV).rgb, uWeight); }
