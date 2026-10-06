#version 410 core
in vec4 tint;
out vec4 frag;
uniform int roundPoints;
void main() {
    if(roundPoints!=0 && length(gl_PointCoord-.5)>.5) discard;
    frag=tint;
}
