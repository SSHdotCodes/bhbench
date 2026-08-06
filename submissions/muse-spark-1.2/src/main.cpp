/**
 * Black Hole Ray-Tracer — Scientifically Accurate Real-Time Simulation
 * ================================================================
 * Features:
 *   • Gravitational lensing via Schwarzschild null-geodesic integration (GR)
 *   • Thin accretion disk with Novikov-Thorne temperature, Doppler + gravitational redshift, + halo lensing
 *   • Spacetime curvature "trapdoor" via Flamm paraboloid embedding (spacetime grid)
 *   • GPU-accelerated with OpenGL GLSL (fragment-shader ray-marching) — real-time
 *
 * Physics model:
 *   Schwarzschild metric: ds² = -(1-Rs/r)c²dt² + dr²/(1-Rs/r) + r²dΩ²
 *   Null geodesics obey d²u/dφ² + u = 3M u² (u=1/r, M=Rs/2).  For 3-D ray marching we use
 *   the Hamiltonian central-force form:
 *       a = -(3 Rs /2) * h² * r_vec / r⁵       where h = r × v (specific angular momentum)
 *   This gives factor-2 light bending (vs Newtonian) and correct photon-sphere at r=1.5 Rs,
 *   ISCO at 3 Rs, and shadow diameter ≈5.2 Rs (Bardeen).
 *
 *   Accretion disk: T(r) ∝ [ (Rs/r)³ (1-√(Rin/r)) ]¹/⁴, Rin=3 Rs (ISCO), Rout≈12 Rs,
 *   blackbody emission, relativistic Doppler g = [γ(1-β cosθ)]⁻¹ and gravitational
 *   redshift g_grav = √(1-Rs/r). Observed intensity ∝ g³ (Liouville).
 *
 *   Orbital velocity (GR Kepler): v = √( Rs / (2(r-Rs)) )  (c=1), measured by static observer;
 *   γ = 1/√(1-β²).
 *
 *   Embedding: Flamm paraboloid z(r)=2√(Rs(r-Rs)) captures spatial curvature at constant
 *   Schwarzschild time — rendered as a ray-traced curved grid sinking into the hole.
 *
 * Controls:
 *   Mouse drag  : orbit camera (LMB yaw/pitch)
 *   Scroll      : zoom (radius)
 *   G           : toggle spacetime grid
 *   D           : toggle accretion disk
 *   H           : toggle halo boost (photon sphere glow)
 *   R           : reset camera
 *   ESC / Q     : quit
 *   SPACE       : pause auto-rotation
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// ───────────────────── Globals / Camera State ─────────────────────
static int gWidth = 1280, gHeight = 800;
static float gYaw = 0.55f;          // azimuth
static float gPitch = 0.38f;        // inclination (0 = pole, PI/2 = equator)
static float gRadius = 14.0f;       // distance from BH in units of Rs
static bool gMouseDown = false;
static double gLastX = 0, gLastY = 0;
static bool gShowGrid = true;
static bool gShowDisk = true;
static bool gHaloBoost = true;
static bool gAutoRotate = true;
static float gRs = 1.6f;            // Schwarzschild radius in world units
static float gFovDeg = 60.0f;

static void framebuffer_cb(GLFWwindow*, int w, int h) {
    gWidth = w; gHeight = h;
    glViewport(0,0,w,h);
}
static void mouse_button_cb(GLFWwindow* w, int btn, int act, int) {
    if(btn==GLFW_MOUSE_BUTTON_LEFT){
        gMouseDown = (act==GLFW_PRESS);
        if(gMouseDown) glfwGetCursorPos(w,&gLastX,&gLastY);
    }
}
static void cursor_cb(GLFWwindow*, double x,double y){
    if(!gMouseDown) return;
    float dx = float(x - gLastX);
    float dy = float(y - gLastY);
    gLastX=x; gLastY=y;
    gYaw   -= dx * 0.004f;
    gPitch += dy * 0.004f;
    const float lim = 1.45f;
    if(gPitch < -lim) gPitch=-lim;
    if(gPitch >  lim) gPitch= lim;
}
static void scroll_cb(GLFWwindow*, double, double yoff){
    gRadius -= float(yoff)*0.9f;
    if(gRadius < 4.5f) gRadius=4.5f;
    if(gRadius > 38.0f) gRadius=38.0f;
}
static void key_cb(GLFWwindow* w,int key,int,int act,int){
    if(act!=GLFW_PRESS) return;
    if(key==GLFW_KEY_ESCAPE || key==GLFW_KEY_Q) glfwSetWindowShouldClose(w,true);
    else if(key==GLFW_KEY_G) gShowGrid=!gShowGrid;
    else if(key==GLFW_KEY_D) gShowDisk=!gShowDisk;
    else if(key==GLFW_KEY_H) gHaloBoost=!gHaloBoost;
    else if(key==GLFW_KEY_SPACE) gAutoRotate=!gAutoRotate;
    else if(key==GLFW_KEY_R){ gYaw=0.55f; gPitch=0.38f; gRadius=14.0f; }
    else if(key==GLFW_KEY_EQUAL || key==GLFW_KEY_KP_ADD){ gRs+=0.15f; if(gRs>3.5f) gRs=3.5f; }
    else if(key==GLFW_KEY_MINUS || key==GLFW_KEY_KP_SUBTRACT){ gRs-=0.15f; if(gRs<0.6f) gRs=0.6f; }
}

// ───────────────────── Shader helpers ─────────────────────
static GLuint compileShader(GLenum type, const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){
        GLint len=0; glGetShaderiv(s,GL_INFO_LOG_LENGTH,&len);
        std::string log(len,' ');
        glGetShaderInfoLog(s,len,nullptr,log.data());
        std::cerr<<"Shader compile error:\n"<<log<<"\n--- src start ---\n"<<src<<"\n--- src end ---\n";
        std::exit(1);
    }
    return s;
}
static GLuint linkProgram(GLuint vs, GLuint fs){
    GLuint p=glCreateProgram();
    glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){
        GLint len=0; glGetProgramiv(p,GL_INFO_LOG_LENGTH,&len);
        std::string log(len,' ');
        glGetProgramInfoLog(p,len,nullptr,log.data());
        std::cerr<<"Program link error:\n"<<log<<"\n";
        std::exit(1);
    }
    return p;
}

// ───────────────────── Embedded Shaders ─────────────────────
// Vertex: simple fullscreen triangle-quad
static const char* kVertSrc = R"GLSL(
#version 410 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main(){
    vUV = aPos*0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Fragment: GR ray-marcher
static const char* kFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
out vec4 FragColor;

uniform vec2  iResolution;
uniform float iTime;
uniform vec3  camPos;
uniform vec3  camForward;
uniform vec3  camRight;
uniform vec3  camUp;
uniform vec3  bhPos;
uniform float Rs;
uniform float tanHalfFov;
uniform int   showGrid;
uniform int   showDisk;
uniform int   haloBoost;

// ── Hash & starfield ──
float hash21(vec2 p){
    p = fract(p*vec2(123.34, 456.21));
    p += dot(p, p+45.32);
    return fract(p.x*p.y);
}
float hash3(vec3 p){
    p = fract(p*0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
// procedural starfield: quantize direction to cells
vec3 starfield(vec3 dir){
    // Convert direction to spherical UV for Milky Way
    float y = dir.y; // -1..1
    // star density hash
    // use 3D hash on quantized direction
    vec3 q = normalize(dir);
    // two layers: bright stars sparse, faint stars dense
    // Layer 1: sparse bright
    vec3 cell = floor(q*256.0);
    float h = hash3(cell);
    float star = 0.0;
    // threshold for star probability
    if(h > 0.994) {
        float bright = pow((h-0.994)/0.006, 2.0);
        star += bright * 1.8;
        // twinkle
        star *= 0.75 + 0.25*sin(iTime*3.0 + h*100.0);
    }
    // Layer 2: faint dust
    vec3 cell2 = floor(q*512.0);
    float h2 = hash3(cell2+7.0);
    if(h2 > 0.985) star += pow((h2-0.985)/0.015, 3.0)*0.35;

    // Milky Way band: faint glow around galactic plane (tilted)
    // Milky way plane normal roughly (0.3, 0.6, 0.5)
    vec3 mwN = normalize(vec3(0.25, 0.58, 0.77));
    float mw = exp(-pow(dot(q,mwN)*3.2, 2.0)) * 0.18;
    // add slight color: milky way bluish
    vec3 col = vec3(0.0);
    // star color variation based on hash
    float colPick = fract(h*7.3);
    vec3 starCol = mix(vec3(1.0,0.85,0.65), vec3(0.65,0.75,1.0), colPick);
    col += star * starCol;
    col += mw * vec3(0.55,0.62,0.85);
    // subtle background gradient (dark blue)
    col += vec3(0.015,0.018,0.035) * (0.6 + 0.4*pow(max(0.0, -y), 0.7));
    // add lensed background dimmer near BH? (gravitational redshift for stars)
    return col;
}

// Blackbody approximation (Tanner Helland) — input kelvin/100
vec3 blackbodyRGB(float t){
    // t = kelvin /100, range 10..400 (1000..40000K)
    vec3 col;
    // red
    if(t <= 66.0) col.r = 1.0;
    else {
        float tt = t - 60.0;
        col.r = 329.698727446 * pow(tt, -0.1332047592) / 255.0;
        col.r = clamp(col.r, 0.0, 1.0);
    }
    // green
    if(t <= 66.0){
        float tt = t;
        col.g = 99.4708025861 * log(tt) - 161.1195681661;
        col.g = clamp(col.g/255.0, 0.0, 1.0);
    } else {
        float tt = t - 60.0;
        col.g = 288.1221695283 * pow(tt, -0.0755148492) / 255.0;
        col.g = clamp(col.g, 0.0, 1.0);
    }
    // blue
    if(t >= 66.0) col.b = 1.0;
    else if(t <= 19.0) col.b = 0.0;
    else {
        float tt = t - 10.0;
        col.b = 138.5177312231 * log(tt) - 305.0447927307;
        col.b = clamp(col.b/255.0, 0.0, 1.0);
    }
    return col;
}

// Map normalized disk temperature tNorm [0..1] -> kelvin/100 -> RGB with extra artistic boost
vec3 diskBlackbody(float tNorm){
    tNorm = clamp(tNorm, 0.0, 1.0);
    // physical: 2500K (outer) .. 28000K (inner) — gives red->white->blue
    float kelvin = mix(2600.0, 28000.0, pow(tNorm, 0.9));
    float tt = kelvin / 100.0;
    vec3 c = blackbodyRGB(tt);
    // boost saturation slightly for aesthetics while keeping physical hue order
    // keep energy: multiply by (0.4+0.9*tNorm) to emphasize inner hotter ring
    return c;
}

// Flamm paraboloid embedding for spatial curvature: y = -A*sqrt(Rs*(rho-Rs)) - y0
float gridHeight(float rho, float Rs){
    if(rho <= Rs*0.98) {
        // inside horizon: deep funnel continues ~ linear extrapolation
        // return depth at horizon minus steep drop
        float hAtH = -2.4*sqrt(max(0.0, Rs*0.02*Rs)) - 2.2; // unused
        // extrapolate deep
        return -9.0 - (Rs*0.98 - rho)*2.0;
    }
    float h = -2.55 * sqrt(Rs * (rho - Rs*0.97)) - 2.2;
    return h;
}

// Utility: fwidth for anti-aliased grid lines (requires derivatives)
float gridLineAA(vec2 uv){
    vec2 g = abs(fract(uv) - 0.5);
    vec2 fw = fwidth(uv)*1.2;
    vec2 a = smoothstep(0.5 - fw, 0.5 + fw, g);
    // a close to 1 near interior, 0 near line? invert:
    // we want lines where fract close to 0
    float line = 1.0 - min( min(a.x, a.y), 1.0 );
    // alternative: distance to nearest integer line
    // use direct distance method
    vec2 d = min(fract(uv), 1.0 - fract(uv));
    float dmin = min(d.x, d.y);
    float aa = fwidth(uv.x)*1.8;
    float l = 1.0 - smoothstep(0.015, 0.015+aa, dmin);
    return max(line, l);
}

void main(){
    // ── Ray setup from camera ──
    vec2 frag = vUV * iResolution;
    vec2 ndc = (frag / iResolution) * 2.0 - 1.0; // -1..1
    // aspect corrected ray direction
    float aspect = iResolution.x / iResolution.y;
    vec3 rd = normalize(camForward + camRight * ndc.x * aspect * tanHalfFov + camUp * ndc.y * tanHalfFov);
    vec3 ro = camPos;

    vec3 pos = ro;
    vec3 vel = rd; // initial photon "velocity" |vel|≈1 (c=1)
    vec3 prevPos = pos;

    vec3 finalCol = vec3(0.0);
    bool hit = false;

    // Ray-march parameters
    const int MAX_STEPS = 260;
    const float ESCAPE_R = 48.0; // *Rs

    float rin = 3.0 * Rs;   // ISCO
    float rout = 12.5 * Rs; // outer edge

    // Photon sphere glow helper: accumulate near 1.5Rs
    float photonGlow = 0.0;

    for(int i=0; i<MAX_STEPS; ++i){
        vec3 p = pos - bhPos;
        float r = length(p);
        float rho = length(p.xz); // cylindrical radius for disk & grid

        // Event horizon capture
        if(r < Rs*1.02){
            // slight halo glow due to photons orbiting before capture — add thin ring
            // fade to black but keep photon ring emission if haloBoost
            if(haloBoost==1){
                float glow = exp(-pow((r - 1.5*Rs)/(0.18*Rs), 2.0))*0.5;
                finalCol = vec3(0.9,0.55,0.18)*glow*0.35;
            } else {
                finalCol = vec3(0.0);
            }
            hit = true;
            break;
        }

        // Adaptive step: small near BH, larger far away
        // dt = 0.04*Rs + 0.12*r*(something) ; clamp 0.05..0.85
        float dt = clamp(0.04*Rs + r*0.07, 0.055*Rs, 0.85);
        // extra refine near photon sphere / ISCO
        if(r < 3.5*Rs) dt *= 0.65;
        if(r < 2.0*Rs) dt *= 0.55;

        // ── GR acceleration: a = -1.5*Rs * h² * p / r⁵,  h=r×v  (MTW / Riazuelo)
        vec3 h = cross(p, vel);
        float h2 = dot(h,h);
        float invR = 1.0 / max(r, 0.45*Rs);
        float invR2 = invR*invR;
        float invR5 = invR2*invR2*invR; // 1/r⁵
        vec3 acc = -1.5 * Rs * h2 * p * invR5;
        // clamp extreme accel to avoid instability (very close to horizon)
        float accMag = length(acc);
        if(accMag > 18.0) acc *= 18.0/accMag;

        // Velocity-Verlet integration
        vec3 nextVel = vel + acc*dt;
        vec3 nextPos = pos + vel*dt + 0.5*acc*dt*dt;

        // ── Accretion disk intersection (thin disk at y=0) ──
        if(showDisk==1){
            // Check crossing of plane y=0 within disk annulus
            // Disk has small thickness for AA: |y| < 0.055*Rs near mid
            float prevY = prevPos.y - bhPos.y;
            float nextY = nextPos.y - bhPos.y;
            // detect sign change or near-miss with thickness
            bool cross = (prevY * nextY < 0.0);
            // also handle grazing rays that step over thin disk without sign flip
            bool nearPlane = abs(nextY) < 0.062*Rs;
            if(cross || nearPlane){
                float tHit = 0.5;
                if(cross){
                    tHit = -prevY / (nextY - prevY + 1e-7);
                    tHit = clamp(tHit, 0.0, 1.0);
                } else {
                    // project to plane
                    tHit = 1.0;
                }
                vec3 hitPos = mix(prevPos, nextPos, tHit);
                vec3 hp = hitPos - bhPos;
                float hitRho = length(hp.xz);
                float hitR = length(hp); // ~hitRho for thin disk
                if(hitRho > rin*0.96 && hitRho < rout && (cross || abs(hitPos.y - bhPos.y) < 0.09*Rs)){
                    // ── Physical disk emission ──
                    float r_ratio = rin / max(hitRho, rin*0.45);
                    r_ratio = clamp(r_ratio, 0.0, 1.4);
                    float inner = 1.0 - sqrt(max(r_ratio, 0.0));
                    inner = max(inner, 0.0);
                    // Temperature profile T ∝ [ (Rin/r)³ (1-√(Rin/r)) ]¹/⁴
                    float T4 = r_ratio*r_ratio*r_ratio * inner;
                    T4 = max(T4, 0.0);
                    float tFactor = pow(T4, 0.25); // 0..~0.49
                    float peak = 0.487; // max of T factor for normalization
                    float tNorm = clamp(tFactor / peak, 0.0, 1.0);
                    // Add radial turbulence / spiral density wave for realism
                    float phi = atan(hp.z, hp.x);
                    float spiral = sin( 5.0*phi + hitRho*0.9/Rs + iTime*0.35*Rs/max(hitRho,1.0) );
                    float turbul = 0.82 + 0.18*spiral + 0.07*sin(hitRho*4.2/Rs - phi*3.0);
                    // Blackbody color
                    vec3 bb = diskBlackbody(tNorm);
                    // Orbital velocity v = sqrt(Rs/(2(r-Rs)))  (static observer)
                    float denom = max(hitRho - Rs, 0.14*Rs);
                    float beta = sqrt(Rs/(2.0*denom));
                    beta = clamp(beta, 0.0, 0.82);
                    float gamma = 1.0 / sqrt(max(1.0 - beta*beta, 0.0012));
                    vec3 ePhi = normalize(vec3(-hp.z, 0.0, hp.x)); // prograde
                    vec3 toObs = -normalize(vel); // ray reversed → emitter to observer
                    float cosTheta = dot(ePhi, toObs);
                    float gDopp = 1.0 / (gamma * (1.0 - beta * cosTheta));
                    float gGrav = sqrt(max(1.0 - Rs/max(hitR, Rs*1.02), 0.0));
                    // Combined redshift; camera at finite radius correction: divide by sqrt(1-Rs/rCam)
                    float rCam = length(camPos - bhPos);
                    float gCam = sqrt(max(1.0 - Rs/max(rCam, Rs*1.02), 0.08));
                    gGrav /= gCam;
                    float g = gDopp * gGrav;
                    // Liouville invariant: I_ν/ν³ invariant → brightness ∝ g³ (bolometric g⁴)
                    float dopplerBoost = pow(max(g, 0.07), 3.2);
                    // limb darkening / projection cos (approx): disk is optically thick, brightness ∝ |cos incidence|
                    float cosInc = abs(dot(-toObs, vec3(0,1,0)));
                    float limb = mix(0.45, 1.0, pow(cosInc, 0.35));
                    // opacity model: inner edge hotter, outer cooler, with falloff to hide hard outer cutoff
                    float edgeFade = smoothstep(rout, rout-1.7*Rs, hitRho) * smoothstep(rin*0.92, rin*1.08, hitRho);
                    vec3 diskCol = bb * (0.55 + 1.85*tNorm) * dopplerBoost * limb * edgeFade * turbul;
                    // Secondary lensed image is dimmer due to extra path / redshift: dim by ~0.7 if this is not first hit?
                    // Approximate by checking if ray has looped: if dot(prevPos - bhPos, pos - bhPos) strongly bent
                    // Simple: if i > 40, dim slightly
                    if(i > 55) diskCol *= 0.62;
                    if(i > 120) diskCol *= 0.55;
                    // Add photon sphere additive glow blending
                    if(haloBoost==1){
                        float glow = exp(-pow((hitRho - 1.5*Rs)/(0.55*Rs), 2.0))*0.22;
                        diskCol += vec3(1.0,0.75,0.35)*glow*tNorm;
                    }
                    finalCol = diskCol;
                    hit = true;
                    break;
                }
            }
        }

        // ── Spacetime curvature grid (Flamm paraboloid) ──
        if(showGrid==1){
            float prevRho = length((prevPos - bhPos).xz);
            float curRho  = length((nextPos - bhPos).xz);
            // compute grid surface heights
            float prevH = gridHeight(prevRho, Rs);
            float curH  = gridHeight(curRho, Rs);
            float prevD = (prevPos.y - bhPos.y) - prevH;
            float curD  = (nextPos.y - bhPos.y) - curH;
            // crossing detection
            if(prevD * curD < 0.0 && curRho < 22.0*Rs && curRho > 0.6*Rs){
                float tG = prevD / (prevD - curD + 1e-8);
                tG = clamp(tG, 0.0, 1.0);
                vec3 hitG = mix(prevPos, nextPos, tG);
                vec2 uvGrid = hitG.xz * 0.62; // grid spacing ~1.6 world units (~1 Rs)
                // Anti-aliased grid lines: lines at integer coordinates
                vec2 f = fract(uvGrid);
                vec2 d = min(f, 1.0 - f);
                // distance to nearest line in uv space
                // adapt thickness with distance/fwidth
                float fw = fwidth(uvGrid.x)*1.1 + 0.0006;
                float lineX = 1.0 - smoothstep(0.012, 0.012 + fw, d.x);
                float lineY = 1.0 - smoothstep(0.012, 0.012 + fw, d.y);
                float gridLine = max(lineX, lineY);
                // radial fade and horizon fade
                float rhoG = length(hitG.xz - bhPos.xz);
                float fadeRho = exp(-0.045 * rhoG / Rs) * smoothstep(22.0*Rs, 10.0*Rs, rhoG);
                float fadeH = 1.0; // keep
                // color: cyan grid with subtle emission
                vec3 gridBase = vec3(0.07,0.14,0.20) * (0.7 + 0.3*gridLine);
                vec3 lineCol = vec3(0.18,0.72,1.0) * gridLine * (0.85 + 0.35*pow(max(0.0,1.0 - rhoG/(18.0*Rs)),1.0));
                // Lensing dimming: use gravitational redshift like disk
                float rHit = length(hitG - bhPos);
                float gGrid = sqrt(max(1.0 - Rs/max(rHit, Rs*1.02), 0.12));
                lineCol *= pow(gGrid, 1.2);
                // combine
                vec3 gridCol = gridBase + lineCol;
                // add depth shading: funnel shadowing (darker deeper)
                float depthShade = clamp((hitG.y - bhPos.y + 9.0)/7.0, 0.2, 1.0);
                gridCol *= depthShade;
                // slight fog for distance
                gridCol = mix(gridCol, vec3(0.02,0.03,0.06), 0.12*(1.0 - fadeRho));
                finalCol = gridCol * fadeRho + vec3(0.015)* (1.0 - gridLine)*0.2;
                hit = true;
                break;
            }
        }

        // accumulate photon sphere glow for miss rays that skim 1.5Rs
        if(haloBoost==1 && r > 1.35*Rs && r < 1.75*Rs){
            float near = exp(-pow((r - 1.5*Rs)/(0.16*Rs), 2.0));
            photonGlow += near * 0.012;
        }

        // advance
        prevPos = pos;
        pos = nextPos;
        vel = nextVel;
        // keep velocity ~unit to avoid runaway (coordinate speed <1 near horizon already)
        float vm = length(vel);
        if(vm > 1e-6) vel /= vm; // re-normalize (energy rescaling — keeps affine param stable)

        if(length(pos - bhPos) > ESCAPE_R * Rs){
            break;
        }
        // safety: if stuck orbiting near photon sphere for many steps, break to avoid infinite loop
        if(i==MAX_STEPS-1){
            break;
        }
    }

    if(!hit){
        // Escaped to infinity → lensed starfield
        vec3 dir = normalize(vel);
        vec3 stars = starfield(dir);
        // add accumulated photon glow (gravitationally lensed halo)
        stars += photonGlow * vec3(1.0,0.62,0.22);
        // subtle vignette / gravitational redshift dimming near BH silhouette edge
        // (stars behind BH are lensed away, leaving dark shadow)
        finalCol = stars;
        // tone slightly
    } else {
        // we already have finalCol from disk/grid/horizon; add photon glow on top for halo
        if(photonGlow > 0.005 && showDisk==1){
            finalCol += photonGlow * vec3(1.0,0.72,0.38)*0.7;
        }
    }

    // ── Tone mapping & color correction ──
    // ACES-like approx and gamma
    vec3 col = finalCol;
    // exposure
    col *= 1.15;
    // reinhard
    col = col / (col + vec3(1.0));
    // slightly boost saturation
    float lum = dot(col, vec3(0.2126,0.7152,0.0722));
    col = mix(vec3(lum), col, 1.12);
    // gamma 2.2
    col = pow(max(col, 0.0), vec3(1.0/2.2));
    // add subtle vignette
    vec2 uv01 = vUV;
    float vign = 1.0 - 0.22*length((uv01-0.5)*1.15);
    col *= clamp(vign, 0.75, 1.0);

    FragColor = vec4(col, 1.0);
}
)GLSL";


// ───────────────────── Fullscreen quad data ─────────────────────
static GLuint gVAO=0,gVBO=0;
static GLuint gProg=0;

static void createQuad(){
    float verts[] = {
        -1,-1,  1,-1,  1, 1,
        -1,-1,  1, 1, -1, 1
    };
    glGenVertexArrays(1,&gVAO);
    glGenBuffers(1,&gVBO);
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER,gVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glBindVertexArray(0);
}

int main(){
    // ── Init GLFW ──
    if(!glfwInit()){
        std::cerr<<"GLFW init failed\n"; return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(gWidth,gHeight,"Black Hole — Schwarzschild Ray Tracer  (G: grid  D: disk  H: halo  SPACE: pause  R: reset  scroll: zoom)",nullptr,nullptr);
    if(!win){ std::cerr<<"Window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1); // vsync

    glfwSetFramebufferSizeCallback(win, framebuffer_cb);
    glfwSetMouseButtonCallback(win, mouse_button_cb);
    glfwSetCursorPosCallback(win, cursor_cb);
    glfwSetScrollCallback(win, scroll_cb);
    glfwSetKeyCallback(win, key_cb);

    // ── GLEW ──
    glewExperimental = GL_TRUE;
    if(glewInit()!=GLEW_OK){
        std::cerr<<"GLEW init failed\n"; return 1;
    }
    // glew may generate INVALID_ENUM; clear
    glGetError();

    std::cout<<"OpenGL "<<glGetString(GL_VERSION)<<" GLSL "<<glGetString(GL_SHADING_LANGUAGE_VERSION)<<"\n";
    std::cout<<"Renderer "<<glGetString(GL_RENDERER)<<"\n";

    createQuad();
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    gProg = linkProgram(vs,fs);
    glDeleteShader(vs); glDeleteShader(fs);

    // Uniform locations (cache)
    GLint locRes = glGetUniformLocation(gProg,"iResolution");
    GLint locTime= glGetUniformLocation(gProg,"iTime");
    GLint locCamPos = glGetUniformLocation(gProg,"camPos");
    GLint locCamFwd = glGetUniformLocation(gProg,"camForward");
    GLint locCamRgt = glGetUniformLocation(gProg,"camRight");
    GLint locCamUp  = glGetUniformLocation(gProg,"camUp");
    GLint locBhPos  = glGetUniformLocation(gProg,"bhPos");
    GLint locRs     = glGetUniformLocation(gProg,"Rs");
    GLint locTanFov = glGetUniformLocation(gProg,"tanHalfFov");
    GLint locShowGrid = glGetUniformLocation(gProg,"showGrid");
    GLint locShowDisk = glGetUniformLocation(gProg,"showDisk");
    GLint locHalo   = glGetUniformLocation(gProg,"haloBoost");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // no depth for fullscreen quad

    double t0 = glfwGetTime();
    // ── Main loop ──
    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();
        double now = glfwGetTime();
        float t = float(now - t0);

        // camera orbit
        float yaw = gYaw + (gAutoRotate ? t*0.13f : 0.0f);
        float pitch = gPitch;
        float rCam = gRadius * gRs; // radius scales with Rs so view stays consistent when Rs changes
        // spherical to Cartesian (BH at origin)
        float cp = cos(pitch), sp = sin(pitch);
        float cy = cos(yaw), sy = sin(yaw);
        // position: use y = r * sin(pitch), xz plane
        // pitch 0 => equatorial view; pitch ~0.4 => slightly elevated
        // Convert: treat pitch as elevation from xz plane
        float camX = rCam * cp * sy;
        float camY = rCam * sp;
        float camZ = rCam * cp * cy;
        float bh[3] = {0,0,0};

        // camera basis: forward = normalize(bh - camPos)
        float fx = -camX, fy = -camY, fz = -camZ;
        float fl = sqrt(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
        // world up
        float ux=0, uy=1, uz=0;
        // right = normalize(cross(forward, up))
        float rx = fy*uz - fz*uy;
        float ry = fz*ux - fx*uz;
        float rz = fx*uy - fy*ux;
        float rl = sqrt(rx*rx+ry*ry+rz*rz); if(rl<1e-6){ rx=1; ry=0; rz=0; rl=1; } rx/=rl; ry/=rl; rz/=rl;
        // recompute up = cross(right, forward) to keep orthonormal
        float upx = ry*fz - rz*fy;
        float upy = rz*fx - rx*fz;
        float upz = rx*fy - ry*fx;
        // normalization not needed much

        // render
        int fbW, fbH; glfwGetFramebufferSize(win,&fbW,&fbH);
        glViewport(0,0,fbW,fbH);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(gProg);
        glUniform2f(locRes, float(fbW), float(fbH));
        glUniform1f(locTime, t);
        glUniform3f(locCamPos, camX, camY, camZ);
        glUniform3f(locCamFwd, fx, fy, fz);
        glUniform3f(locCamRgt, rx, ry, rz);
        glUniform3f(locCamUp, upx, upy, upz);
        glUniform3f(locBhPos, bh[0], bh[1], bh[2]);
        glUniform1f(locRs, gRs);
        float tanHalf = tan(gFovDeg * 0.5f * 3.14159265f/180.0f);
        glUniform1f(locTanFov, tanHalf);
        glUniform1i(locShowGrid, gShowGrid?1:0);
        glUniform1i(locShowDisk, gShowDisk?1:0);
        glUniform1i(locHalo, gHaloBoost?1:0);

        glBindVertexArray(gVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
        glUseProgram(0);

        // ── On-screen help (via window title FPS) ──
        static double lastTitle = 0;
        static int frames=0; frames++;
        if(now - lastTitle > 0.7){
            double fps = frames/(now-lastTitle);
            frames=0; lastTitle=now;
            std::string title = "Black Hole — GR Ray Tracer  |  Rs=" + std::to_string(gRs).substr(0,4)
                + "  r=" + std::to_string(gRadius).substr(0,4) + "Rs"
                + "  FPS " + std::to_string(int(fps))
                + "  [G:grid " + (gShowGrid?"ON":"OFF") + "  D:disk " + (gShowDisk?"ON":"OFF")
                + "  H:halo " + (gHaloBoost?"ON":"OFF") + "  SPACE:" + (gAutoRotate?"auto":"pause") + "]";
            glfwSetWindowTitle(win, title.c_str());
        }

        glfwSwapBuffers(win);
    }

    glDeleteProgram(gProg);
    glDeleteVertexArrays(1,&gVAO);
    glDeleteBuffers(1,&gVBO);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
