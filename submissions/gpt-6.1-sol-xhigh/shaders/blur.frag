#version 410 core
in vec2 uv;
out vec4 frag;
uniform sampler2D image;
uniform vec2 axis;
uniform int extractBright;
void main() {
    vec3 c=vec3(0);
    float weights[5]=float[]( .227027,.194595,.121622,.054054,.016216 );
    for(int i=-4;i<=4;++i) {
        vec3 v=texture(image,uv+axis*float(i)).rgb;
        if(extractBright!=0) v*=smoothstep(.65,1.6,max(v.r,max(v.g,v.b)));
        c+=v*weights[abs(i)];
    }
    frag=vec4(c,1);
}
