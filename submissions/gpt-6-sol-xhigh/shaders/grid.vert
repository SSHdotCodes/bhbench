#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
uniform float uTurn;
uniform float uAspect;

void main() {
    // Camera orbiting a real 3D mesh of Flamm's equatorial embedding.
    vec3 eye = vec3(35.0 * sin(uTurn), 18.0, 35.0 * cos(uTurn));
    vec3 target = vec3(0.0, -5.5, 0.0);
    vec3 forward = normalize(target - eye);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    vec3 q = aPosition - eye;
    float depth = dot(q, forward);
    float focal = 1.05;
    gl_Position = vec4(dot(q, right) * focal / uAspect,
                       dot(q, up) * focal,
                       0.5 * depth, depth);
    vColor = aColor;
}
