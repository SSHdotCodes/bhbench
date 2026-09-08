#version 410 core
out vec4 outputColor;uniform vec3 color;uniform float alpha;
void main(){outputColor=vec4(color,alpha);}
