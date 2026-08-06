#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <cmath>
#include <string>

#include "camera.h"
#include "shader.h"

// Embedded shaders -----------------------------------------------------------
const char* blackholeVertSrc = R"GLSL(
#version 410 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main(){
    vUV = aPos*0.5+0.5;
    gl_Position = vec4(aPos,0.0,1.0);
}
)GLSL";

const char* blackholeFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 camPos;
uniform vec3 camForward;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float fov;          // degrees
uniform float aspect;
uniform vec2 resolution;
uniform float rs;           // Schwarzschild radius
uniform vec3 bhPos;
uniform float time;
uniform int showDisk;       // 0/1
uniform int gridMode;       // 0 off, 1 flat lensed, 2 flamm paraboloid
uniform float diskInner;
uniform float diskOuter;
uniform float gridY;
uniform float exposure;
uniform float timeScale;

const float PI = 3.14159265359;
const int MAX_STEPS = 300;
const float INF = 1e6;

// Hash functions ------------------------------------------------------------
float hash1(float p){
    return fract(sin(p*12.9898)*43758.5453);
}
float hash3(vec3 p){
    p = fract(p*0.3183099+0.1);
    p *= 17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
float hash2(vec2 p){
    return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453);
}

// Blackbody chromaticity - Tanner Helland approximation
vec3 blackbody(float T){
    // T in Kelvin, return normalized color [0,1]
    // Clamp to 1000..40000
    T = clamp(T, 1000.0, 40000.0);
    float t = T/100.0;
    vec3 col;
    // Red
    if(t <= 66.0) col.r = 1.0;
    else {
        float r = t - 60.0;
        r = 329.698727446 * pow(r, -0.1332047592);
        col.r = clamp(r/255.0,0.0,1.0);
    }
    // Green
    if(t <= 66.0){
        float g = t;
        g = 99.4708025861 * log(max(g,0.001)) - 161.1195681661;
        col.g = clamp(g/255.0,0.0,1.0);
    } else {
        float g = t - 60.0;
        g = 288.1221695283 * pow(g, -0.0755148492);
        col.g = clamp(g/255.0,0.0,1.0);
    }
    // Blue
    if(t >= 66.0) col.b = 1.0;
    else if(t <= 19.0) col.b = 0.0;
    else {
        float b = t - 10.0;
        b = 138.5177312231 * log(max(b,0.001)) - 305.0447927307;
        col.b = clamp(b/255.0,0.0,1.0);
    }
    return col;
}

// Starfield: procedural stars + Milky Way band
vec3 sampleStars(vec3 dir){
    dir = normalize(dir);
    // spherical coordinates
    float phi = atan(dir.z, dir.x); // -PI..PI
    float theta = asin(clamp(dir.y, -1.0, 1.0)); // -PI/2..PI/2
    float u = phi / (2.0*PI) + 0.5;
    float v = theta / PI + 0.5;

    const float Nx = 180.0;
    const float Ny = 90.0;

    float cellU = floor(u*Nx);
    float cellV = floor(v*Ny);

    vec3 col = vec3(0.0);

    // Search 3x3 neighborhood for stars
    for(int dy=-1; dy<=1; ++dy){
        for(int dx=-1; dx<=1; ++dx){
            vec2 cell = vec2(cellU + float(dx), cellV + float(dy));
            // wrap U
            cell.x = mod(cell.x, Nx);
            if(cell.y < 0.0 || cell.y >= Ny) continue;

            float h = hash3(vec3(cell, 0.0));
            const float starDensity = 0.3; // probability
            if(h < (1.0-starDensity)) continue;

            float hx = hash3(vec3(cell, 1.0));
            float hy = hash3(vec3(cell, 2.0));
            vec2 starUV = (cell + vec2(hx, hy)) / vec2(Nx, Ny);

            float du = abs(u - starUV.x);
            du = min(du, 1.0 - du); // wrap seam
            float dv = v - starUV.y;

            // correct for latitude stretch - longitude lines converge at poles
            float cosT = cos(theta);
            cosT = max(cosT, 0.05);
            du *= cosT;

            float dist = sqrt(du*du + dv*dv);

            float starSize = 0.0008 + hash3(vec3(cell,3.0))*0.0015;
            float brightness = hash3(vec3(cell,4.0));

            // Gaussian star profile
            float star = exp(-dist*dist / (starSize*starSize)) * 1.5;

            // star color variation
            vec3 starColor = vec3(1.0);
            float tr = hash3(vec3(cell,5.0));
            if(tr < 0.2) starColor = vec3(1.0,0.6,0.5);        // reddish
            else if(tr < 0.4) starColor = vec3(0.6,0.7,1.0);  // bluish
            else starColor = vec3(1.0);

            col += star * starColor * (0.3 + brightness*0.7);
        }
    }

    // Milky Way band - defined by galactic plane
    vec3 galPole = normalize(vec3(0.25, 0.9, 0.15));
    float galLat = dot(dir, galPole); // sin latitude
    float galBand = exp(-galLat*galLat / 0.025) ; // narrow band

    // Add noise along band for clumpiness
    float lonNoise = hash3(dir*7.0);
    float bandNoise = hash3(dir*15.0);
    float bandDetail = 0.5 + 0.5*sin(phi*5.0 + theta*3.0)*0.5 + bandNoise*0.3;

    vec3 milkyCol = vec3(0.55, 0.6, 0.9) * 0.18;
    col += milkyCol * galBand * bandDetail * (0.5 + lonNoise*0.5);

    // Faint background
    col += vec3(0.015,0.015,0.02) * (0.5 + 0.5*sin(u*20.0));

    return col;
}

// Main
void main(){
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / resolution; // 0..1

    // Camera ray construction
    float tanHalfFov = tan(radians(fov)*0.5);
    // NDC from -1..1
    float ndcX = (uv.x*2.0 - 1.0) * aspect * tanHalfFov;
    float ndcY = (uv.y*2.0 - 1.0) * tanHalfFov;

    vec3 rayDir = normalize(camForward + ndcX*camRight + ndcY*camUp);

    // Position relative to BH
    vec3 ro = camPos - bhPos;

    // Orbital plane basis (spherical symmetry allows reduction to 2D plane)
    vec3 n = cross(ro, rayDir);
    float nLen = length(n);
    if(nLen < 1e-4){
        // near radial, pick arbitrary orthogonal
        if(abs(ro.x) < abs(ro.y)) n = cross(ro, vec3(1,0,0));
        else n = cross(ro, vec3(0,1,0));
        nLen = length(n);
    }
    n = n / max(nLen, 1e-6);
    vec3 u_basis = normalize(ro);
    vec3 v_basis = cross(n, u_basis);
    // v_basis already orthogonal, normalize
    v_basis = normalize(v_basis);

    float rd_u = dot(rayDir, u_basis);
    float rd_v = dot(rayDir, v_basis);

    float r_init = length(ro);
    float u_cur = 1.0 / max(r_init, 0.1);
    float phi_cur = 0.0;
    float w_cur;
    if(abs(rd_v) < 1e-5) w_cur = 0.0;
    else w_cur = -(rd_u / rd_v) * u_cur;

    float signPhi = sign(rd_v);
    if(signPhi == 0.0) signPhi = 1.0;

    float r_cur = r_init;
    vec3 pos_old = ro;

    vec3 finalColor = vec3(0.0);
    bool hit = false;

    float Mbh = rs * 0.5;

    for(int i=0; i<MAX_STEPS; ++i){
        float stepAbs;
        if(r_cur < 3.0*rs) stepAbs = 0.02;
        else if(r_cur < 8.0*rs) stepAbs = 0.05;
        else if(r_cur < 20.0*rs) stepAbs = 0.12;
        else stepAbs = 0.25;
        float step = stepAbs * signPhi;

        // RK4 for second order ODE: d^2u/dphi^2 + u = 3 M u^2
        // System: du/dphi = w, dw/dphi = -u + 3M u^2
        float k1_u = w_cur;
        float k1_w = -u_cur + 3.0*Mbh*u_cur*u_cur;

        float u2 = u_cur + k1_u*step*0.5;
        float w2 = w_cur + k1_w*step*0.5;
        float k2_u = w2;
        float k2_w = -u2 + 3.0*Mbh*u2*u2;

        float u3 = u_cur + k2_u*step*0.5;
        float w3 = w_cur + k2_w*step*0.5;
        float k3_u = w3;
        float k3_w = -u3 + 3.0*Mbh*u3*u3;

        float u4 = u_cur + k3_u*step;
        float w4 = w_cur + k3_w*step;
        float k4_u = w4;
        float k4_w = -u4 + 3.0*Mbh*u4*u4;

        float u_new = u_cur + (k1_u + 2.0*k2_u + 2.0*k3_u + k4_u)/6.0 * step;
        float w_new = w_cur + (k1_w + 2.0*k2_w + 2.0*k3_w + k4_w)/6.0 * step;
        float phi_new = phi_cur + step;

        float r_new;
        if(u_new <= 1e-7) r_new = 1e6;
        else r_new = 1.0 / u_new;

        vec3 pos_new = r_new * (cos(phi_new)*u_basis + sin(phi_new)*v_basis);

        // ---- Event horizon capture (black) ----
        if(r_new < rs*1.005){
            finalColor = vec3(0.0);
            hit = true;
            break;
        }

        // ---- Accretion disk intersection ----
        if(showDisk==1){
            float y_old = pos_old.y;
            float y_new = pos_new.y;
            if((y_old > 0.0 && y_new < 0.0) || (y_old < 0.0 && y_new > 0.0)){
                float t_cross = y_old / (y_old - y_new);
                vec3 pos_int = mix(pos_old, pos_new, t_cross);
                float r_disk = length(pos_int.xz); // cylindrical radius
                if(r_disk >= diskInner && r_disk <= diskOuter){
                    float r_e = r_disk;
                    // Temperature profile: Shakura-Sunyaev thin disk
                    float T0 = 1.2e6; // peak ~1e6 K for stellar mass BH
                    float temp = 0.0;
                    if(r_e > diskInner){
                        float term = 1.0 - sqrt(diskInner / r_e);
                        term = max(term, 0.0);
                        float prof = pow(diskInner / r_e, 0.75) * pow(term, 0.25);
                        temp = T0 * prof;
                    }
                    // Add Keplerian shear turbulence / hot spots
                    float th = atan(pos_int.z, pos_int.x);
                    float orbFreq = sqrt(Mbh / (pow(r_e,3.0)+1e-6));
                    float turb = 1.0 + 0.15*sin(th*3.0 + time*orbFreq*2.0 + r_e*0.5)*sin(r_e*2.0);
                    temp *= turb;

                    // Orbital velocity (GR corrected for static observer)
                    float v = 0.0;
                    float denom = r_e - 2.0*Mbh;
                    if(denom > 0.1) v = sqrt(Mbh/denom);
                    v = clamp(v, 0.0, 0.85);

                    vec3 v_dir = vec3(0.0);
                    if(r_disk > 0.01) v_dir = normalize(vec3(-pos_int.z, 0.0, pos_int.x));
                    vec3 v_vec = v * v_dir;

                    vec3 dir_back = normalize(pos_new - pos_old);
                    vec3 dir_to_cam = -dir_back;

                    float grav = sqrt(max(0.0, 1.0 - rs / max(r_e, rs*1.01)));
                    float gamma = 1.0 / sqrt(max(0.001, 1.0 - v*v));
                    float ndotv = dot(v_vec, dir_to_cam);
                    float doppler = 1.0 / (gamma * (1.0 - ndotv + 1e-4));
                    float gfac = grav * doppler;

                    float T_obs = temp * gfac;
                    vec3 bb = blackbody(max(T_obs, 1000.0));

                    // Bolometric brightness ~ T^4, scaled for display
                    float flux = pow(T_obs/8000.0, 2.5);
                    flux = clamp(flux, 0.0, 50.0);

                    float intensity = pow(gfac, 3.2); // relativistic beaming I_nu ~ g^3
                    float cosTheta = abs(dot(vec3(0,1,0), dir_to_cam));
                    vec3 emit = bb * flux * intensity * (0.2 + 0.8*cosTheta);

                    // Add inner edge highlight (photon ring stacking approximation)
                    float ringBoost = 1.0;
                    if(abs(phi_cur) > 3.0) ringBoost = 1.5; // lensed secondary image brighter

                    finalColor = emit * ringBoost;
                    hit = true;
                    break;
                }
            }
        }

        // ---- Spacetime grid visualisation ----
        if(gridMode==1){
            float gy = gridY;
            float y_old = pos_old.y;
            float y_new = pos_new.y;
            if((y_old - gy)*(y_new - gy) < 0.0){
                float t_cross = (y_old - gy)/(y_old - y_new);
                vec3 pos_int = mix(pos_old, pos_new, t_cross);
                float dist = length(pos_int.xz);
                if(dist < 50.0*rs && dist > rs*0.9){
                    float spacing = 2.0*rs;
                    float lineW = 0.04;
                    float fx = abs(fract(pos_int.x / spacing) - 0.5);
                    float fz = abs(fract(pos_int.z / spacing) -0.5);
                    if(fx < lineW || fz < lineW){
                        float potential = rs / max(dist, rs);
                        vec3 gridCol = mix(vec3(0.1,0.5,1.0), vec3(0.9,0.2,1.2), potential);
                        float fade = 1.0 / (1.0 + dist*0.02);
                        // show bending: intensity increases near BH
                        finalColor = gridCol * fade * 0.8;
                        hit = true;
                        break;
                    }
                }
            }
        } else if(gridMode==2){
            // Flamm's paraboloid embedding: z_emb = 2*sqrt(rs*(r - rs))
            // Surface: y = -2* sqrt(rs*(rc - rs)), rc = cylindrical radius
            float rc_old = length(pos_old.xz);
            float rc_new = length(pos_new.xz);
            float f_old = -1000.0;
            float f_new = -1000.0;
            if(rc_old > rs) f_old = -2.0*sqrt(rs*(rc_old - rs));
            else f_old = 0.0; // inside cut
            if(rc_new > rs) f_new = -2.0*sqrt(rs*(rc_new - rs));
            else f_new = 0.0;

            float d_old = pos_old.y - f_old;
            float d_new = pos_new.y - f_new;
            if(d_old * d_new < 0.0 && rc_old > rs*0.99 && rc_new < 60.0*rs){
                float t_cross = d_old/(d_old - d_new);
                vec3 pos_int = mix(pos_old, pos_new, t_cross);
                float rc_int = length(pos_int.xz);
                if(rc_int > rs){
                    float th = atan(pos_int.z, pos_int.x);
                    float spacingR = 1.5*rs;
                    float spacingTh = PI/12.0;
                    float fr = abs(fract(rc_int/spacingR)-0.5);
                    float ft = abs(fract(th/spacingTh)-0.5);
                    float lw = 0.045;
                    if(fr < lw || ft < 0.03){
                        float depth = clamp(-pos_int.y/(12.0*rs),0.0,1.0);
                        vec3 gridCol = mix(vec3(0.2,0.7,1.0), vec3(1.0,0.4,0.7), depth);
                        float fade = 1.0/(1.0+rc_int*0.02);
                        finalColor = gridCol * fade * 1.2;
                        hit = true;
                        break;
                    }
                }
            }
        }

        // advance
        pos_old = pos_new;
        r_cur = r_new;
        phi_cur = phi_new;
        u_cur = u_new;
        w_cur = w_new;

        // Escaped to infinity -> sample starfield background
        if(r_cur > 80.0*rs){
            vec3 dir_inf = normalize(pos_new - pos_old);
            // dir_inf points outward (along backward ray) -> star direction
            vec3 starCol = sampleStars(dir_inf);
            // Lensing magnification near photon sphere creates Einstein ring brightening
            // approximate magnification factor ~ 1/ (b - b_crit)
            finalColor = starCol;
            hit = true;
            break;
        }

        if(abs(phi_cur) > 30.0){
            finalColor = vec3(0.0);
            hit = true;
            break;
        }
    }

    if(!hit) finalColor = vec3(0.0);

    // Tone mapping (Reinhard) and exposure + gamma
    finalColor = finalColor * exposure;
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0/2.2));

    // Add subtle vignette
    float vign = 1.0 - 0.2*length(uv-0.5)*2.0;
    finalColor *= vign;

    FragColor = vec4(finalColor,1.0);
}
)GLSL";

const char* gridVertSrc = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 view;
uniform mat4 proj;
uniform mat4 model;
void main(){
    gl_Position = proj*view*model*vec4(aPos,1.0);
}
)GLSL";

const char* gridFragSrc = R"GLSL(
#version 410 core
out vec4 FragColor;
uniform vec3 color;
uniform float alpha;
void main(){
    FragColor = vec4(color, alpha);
}
)GLSL";

// Globals for input handling
Camera camera;
bool keys[1024] = {false};
double lastX = 400, lastY = 300;
bool firstMouse = true;
bool mousePressed = false;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Simulation params
float rs = 2.0f; // Schwarzschild radius (world units, 2M)
int showDisk = 1;
int gridMode = 0; // 0 off, 1 flat, 2 flamm
float gridY = -3.0f;
float diskInner = 0.0f;
float diskOuter = 0.0f;
float exposure = 2.5f;
float timeAccum = 0.0f;
bool autoOrbit = true;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods){
    if(key==GLFW_KEY_ESCAPE && action==GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if(action==GLFW_PRESS){
        if(key==GLFW_KEY_G){
            gridMode = (gridMode+1)%3;
            std::cout << "GridMode: " << gridMode << " (0=off,1=flat lensed grid,2=Flamm paraboloid)" << std::endl;
        }
        if(key==GLFW_KEY_D){
            showDisk = 1-showDisk;
            std::cout << "ShowDisk: " << showDisk << std::endl;
        }
        if(key==GLFW_KEY_SPACE){
            autoOrbit = !autoOrbit;
            std::cout << "AutoOrbit: " << autoOrbit << std::endl;
        }
        if(key==GLFW_KEY_R){
            // reset camera
            camera.distance = 22.0f;
            camera.yaw = 0.0f;
            camera.pitch = 0.3f;
        }
        if(key==GLFW_KEY_UP) exposure *= 1.2f;
        if(key==GLFW_KEY_DOWN) exposure /= 1.2f;
        if(key==GLFW_KEY_O) rs *= 1.1f;
        if(key==GLFW_KEY_P) rs *= 0.9f;
    }
    if(key>=0 && key<1024){
        if(action==GLFW_PRESS) keys[key]=true;
        else if(action==GLFW_RELEASE) keys[key]=false;
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods){
    if(button==GLFW_MOUSE_BUTTON_LEFT){
        if(action==GLFW_PRESS) mousePressed = true;
        else mousePressed = false;
    }
}

void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos){
    if(firstMouse){ lastX=xpos; lastY=ypos; firstMouse=false; }
    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    if(mousePressed){
        float sensitivity = 0.005f;
        camera.yaw += xoffset * sensitivity;
        camera.pitch += yoffset * sensitivity;
    }
}

void scroll_callback(GLFWwindow* window, double xoff, double yoff){
    camera.distance -= yoff * 1.0f;
    if(camera.distance < 3.0f) camera.distance = 3.0f;
    if(camera.distance > 100.0f) camera.distance = 100.0f;
}

void processInput(float dt){
    float speed = 5.0f * dt;
    if(keys[GLFW_KEY_W]) camera.distance -= speed;
    if(keys[GLFW_KEY_S]) camera.distance += speed;
    if(keys[GLFW_KEY_A]) camera.yaw -= speed*0.5f;
    if(keys[GLFW_KEY_D]) camera.yaw += speed*0.5f;
    if(keys[GLFW_KEY_Q]) camera.pitch -= speed*0.5f;
    if(keys[GLFW_KEY_E]) camera.pitch += speed*0.5f;
    if(camera.distance < 3.0f) camera.distance = 3.0f;
    if(camera.distance > 120.0f) camera.distance = 120.0f;
}

int main(){
    std::cout << "=== Black Hole Ray-Tracing Simulation ===" << std::endl;
    std::cout << "Scientifically accurate Schwarzschild black hole with:" << std::endl;
    std::cout << " - Gravitational lensing via null geodesic integration" << std::endl;
    std::cout << " - Thin accretion disk (Shakura-Sunyaev) with Doppler & gravitational redshift" << std::endl;
    std::cout << " - Spacetime curvature grid (Flamm paraboloid + lensed flat grid)" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << " Mouse drag: orbit camera" << std::endl;
    std::cout << " Scroll / W-S: zoom" << std::endl;
    std::cout << " A-D / Q-E: yaw / pitch" << std::endl;
    std::cout << " G: toggle spacetime grid (0=off,1=flat lensed,2=Flamm funnel)" << std::endl;
    std::cout << " D: toggle accretion disk" << std::endl;
    std::cout << " Space: toggle auto-orbit" << std::endl;
    std::cout << " R: reset camera" << std::endl;
    std::cout << " Up/Down: exposure +/-" << std::endl;
    std::cout << " O/P: increase/decrease BH mass (rs)" << std::endl;
    std::cout << " ESC: exit\n" << std::endl;

    if(!glfwInit()){
        std::cerr << "Failed to init GLFW" << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Black Hole - GR Ray Tracing (Schwarzschild)", NULL, NULL);
    if(!window){
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSwapInterval(1); // vsync

#ifndef __APPLE__
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        std::cerr << "Failed to init GLAD" << std::endl;
        return -1;
    }
#endif

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    // Setup fullscreen triangle
    float quadVerts[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Compile shaders
    GLuint bhProgram = createProgram(blackholeVertSrc, blackholeFragSrc);
    GLuint gridProgram = createProgram(gridVertSrc, gridFragSrc);

    glUseProgram(bhProgram);
    // Enable blending not needed for main pass
    glDisable(GL_DEPTH_TEST);

    // Camera init
    camera.target = glm::vec3(0,0,0);
    camera.distance = 22.0f;
    camera.yaw = 0.5f;
    camera.pitch = 0.35f;
    camera.fov = 60.0f;
    camera.update();

    // Main loop
    while(!glfwWindowShouldClose(window)){
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        timeAccum += deltaTime;

        processInput(deltaTime);

        if(autoOrbit){
            camera.yaw += deltaTime * 0.15f; // slow auto orbit
        }

        camera.update();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        camera.aspect = (float)fbW / (float)fbH;

        glViewport(0,0,fbW,fbH);
        glClearColor(0.0f,0.0f,0.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---- Black hole ray tracing pass (fullscreen) ----
        glUseProgram(bhProgram);
        glBindVertexArray(quadVAO);
        glDisable(GL_DEPTH_TEST);

        diskInner = rs * 3.0f; // ISCO at 6M = 3 rs
        diskOuter = rs * 12.0f;

        // Set uniforms
        GLint loc;
        loc = glGetUniformLocation(bhProgram, "camPos");
        glUniform3fv(loc,1, glm::value_ptr(camera.position));
        loc = glGetUniformLocation(bhProgram, "camForward");
        glUniform3fv(loc,1, glm::value_ptr(camera.forward));
        loc = glGetUniformLocation(bhProgram, "camRight");
        glUniform3fv(loc,1, glm::value_ptr(camera.right));
        loc = glGetUniformLocation(bhProgram, "camUp");
        glUniform3fv(loc,1, glm::value_ptr(camera.up));
        loc = glGetUniformLocation(bhProgram, "fov");
        glUniform1f(loc, camera.fov);
        loc = glGetUniformLocation(bhProgram, "aspect");
        glUniform1f(loc, camera.aspect);
        loc = glGetUniformLocation(bhProgram, "resolution");
        glUniform2f(loc, (float)fbW, (float)fbH);
        loc = glGetUniformLocation(bhProgram, "rs");
        glUniform1f(loc, rs);
        loc = glGetUniformLocation(bhProgram, "bhPos");
        glUniform3f(loc, 0,0,0);
        loc = glGetUniformLocation(bhProgram, "time");
        glUniform1f(loc, timeAccum);
        loc = glGetUniformLocation(bhProgram, "showDisk");
        glUniform1i(loc, showDisk);
        loc = glGetUniformLocation(bhProgram, "gridMode");
        glUniform1i(loc, gridMode);
        loc = glGetUniformLocation(bhProgram, "diskInner");
        glUniform1f(loc, diskInner);
        loc = glGetUniformLocation(bhProgram, "diskOuter");
        glUniform1f(loc, diskOuter);
        loc = glGetUniformLocation(bhProgram, "gridY");
        glUniform1f(loc, gridY);
        loc = glGetUniformLocation(bhProgram, "exposure");
        glUniform1f(loc, exposure);
        loc = glGetUniformLocation(bhProgram, "timeScale");
        glUniform1f(loc, 1.0f);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- Optional: render auxiliary grid mesh as overlay for extra clarity ----
        // When gridMode==2, we could render a wireframe Flamm paraboloid on top with alpha for illustration
        // We will render it as transparent overlay after main pass, without depth test, but with blending
        if(gridMode==2){
            // Generate simple funnel mesh on the fly? For simplicity, we skip heavy generation
            // Instead, we rely on the ray-traced Flamm intersection already done in fragment shader
            // This block is placeholder for future mesh rendering
        }

        glBindVertexArray(0);

        // Title update
        char title[256];
        snprintf(title, sizeof(title), "Black Hole GR | rs=%.2f | Grid:%d (G) Disk:%d (D) | Dist:%.1f | FPS:%.0f | Exp:%.2f",
                 rs, gridMode, showDisk, camera.distance, 1.0/deltaTime, exposure);
        glfwSetWindowTitle(window, title);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(bhProgram);
    glDeleteProgram(gridProgram);

    glfwTerminate();
    return 0;
}
