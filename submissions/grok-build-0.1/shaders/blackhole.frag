#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

// Camera
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform float uFov; // vertical half-angle in radians
uniform float uAspect; // width / height

// Physics
uniform float uRs;          // Schwarzschild radius (2M)
uniform float uDiskInner;   // ISCO approx 6M = 3*uRs
uniform float uDiskOuter;
uniform float uTime;
uniform int uMaxSteps;
uniform float uStepSize;
uniform float uExposure;
uniform float uDiskBrightness;

// Ray trace quality
uniform float uEpsilon;

// --- Math helpers ---
const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

float safeSqrt(float x) { return sqrt(max(x, 0.0)); }

vec3 hsv2rgb(vec3 c) {
    vec3 rgb = clamp(abs(mod(c.x*6.0 + vec3(0.0,4.0,2.0), 6.0)-3.0)-1.0, 0.0, 1.0);
    rgb = rgb*rgb*(3.0-2.0*rgb);
    return c.z * mix(vec3(1.0), rgb, c.y);
}

// Procedural starfield (scientifically "random" directions on sphere)
vec3 sampleStars(vec3 dir) {
    // Use spherical coordinates for stable hash
    float phi = atan(dir.y, dir.x);
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    // High frequency noise for stars
    float star = 0.0;
    for (int i = 0; i < 3; ++i) {
        float f = float(i + 1);
        float s = sin(f * 19.0 * theta + phi * (7.0 + f)) * sin(f * 31.0 * phi - theta * 11.0);
        float bright = smoothstep(0.985 - 0.004 * f, 0.995, s);
        star += bright * (0.7 + 0.3 * sin(f * 4.0 + uTime * 0.02));
    }
    // Add milky way like band
    float mw = exp(-abs(dir.z * 1.4 + sin(phi * 0.6) * 0.3) * 2.8);
    vec3 starCol = vec3(0.85, 0.92, 1.0) * star * 1.6;
    starCol += vec3(0.6, 0.75, 1.0) * mw * 0.035; // faint background
    return starCol;
}

// Convert cartesian to spherical
void cartToSph(vec3 p, out float r, out float theta, out float phi) {
    r = length(p);
    if (r < 1e-5) {
        theta = PI * 0.5; phi = 0.0; return;
    }
    theta = acos(clamp(p.z / r, -1.0, 1.0));
    phi = atan(p.y, p.x);
}

// Basis for local frame at position
mat3 localFrame(float theta, float phi) {
    vec3 er = vec3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
    vec3 et = vec3(cos(theta)*cos(phi), cos(theta)*sin(phi), -sin(theta));
    vec3 ep = vec3(-sin(phi), cos(phi), 0.0);
    return mat3(er, et, ep);
}

// Compute conserved E from current state (at init mostly)
float computeE(float r, float theta, float ur, float uth, float uph, float rs) {
    if (r < rs * 0.5) return 0.0;
    float f = 1.0 - rs / r;
    float term = (ur * ur) / f + (r * r * uth * uth) + (r * r * sin(theta) * sin(theta) * uph * uph);
    float ut2 = term / f;
    float ut = safeSqrt(ut2);
    return ut * f;
}

// Disk color at radius (simple blackbody-ish + temperature profile T ~ r^-3/4)
vec3 diskColor(float r, float gFactor) {
    float rin = 6.0; // M units but relative
    float x = (r - 6.0) / (18.0); // normalized 6M-24M ish
    x = clamp(x, 0.0, 1.0);
    // Temperature falls as r increases
    float temp = pow( (6.0 / max(r, 5.5)), 0.75 );
    // Hot inner blueish-white, cooler outer orange/red
    vec3 hot = vec3(0.95, 0.95, 1.0);
    vec3 mid = vec3(1.0, 0.85, 0.5);
    vec3 cool = vec3(0.95, 0.45, 0.15);
    vec3 col = mix(mix(cool, mid, smoothstep(0.0, 0.4, x)), hot, smoothstep(0.35, 0.0, x));
    col *= (0.6 + 0.8 * temp);
    // Relativistic beaming + redshift
    float bright = pow(clamp(gFactor, 0.05, 6.0), 3.8);
    col *= bright * 0.9;
    // Add some turbulence / spiral arm feel
    float spiral = sin(log(r + 0.5) * 5.5 + 2.0 * atan(0.0, r)) * 0.5 + 0.5; // rough
    col *= (0.75 + 0.25 * spiral);
    return col;
}

// Compute redshift / g factor for thin Keplerian disk at given r, theta~pi/2
float computeRedshiftFactor(float r, float theta, float ur, float uth, float uph,
                            float E, float Lz, float rs) {
    if (r < 5.0 || abs(theta - PI*0.5) > 0.35) return 0.0; // only near plane
    float M = rs * 0.5;
    if (r < 6.0 * M) return 0.0; // inside ISCO, no stable disk
    float Omega = sqrt(M / (r*r*r));
    float ut_em = 1.0 / safeSqrt(1.0 - 3.0 * M / r);
    float uph_em = Omega * ut_em;
    // -k.u_em = E * ut_em - Lz * uph_em   (signs for future directed)
    float k_dot_u = E * ut_em - Lz * uph_em;
    float g = E / max(k_dot_u, 1e-4);
    return g;
}

// Main geodesic integration using RK4 + conserved quantities + Christoffel
// Returns color seen along this ray
vec3 traceRay(vec3 ro, vec3 rd, float rs) {
    float r, theta, phi;
    cartToSph(ro, r, theta, phi);

    // Compute initial local velocities from direction
    mat3 frame = localFrame(theta, phi);
    vec3 localVel = vec3(dot(rd, frame[0]), dot(rd, frame[1]), dot(rd, frame[2]));
    // physical 3-vel components approx at large distance
    float ur  = localVel.x;
    float uth = localVel.y / max(r, 0.01);
    float uph = localVel.z / max(r * sin(theta), 0.01);

    float E = computeE(r, theta, ur, uth, uph, rs);
    if (E < 1e-5) E = 1.0;

    float Lz = r * r * sin(theta) * sin(theta) * uph;

    // Prepare for disk crossing detection
    float prevZ = ro.z;
    vec3 prevPos = ro;
    float prevR = r;

    float step = uStepSize;
    vec3 accumColor = vec3(0.0);
    float alpha = 1.0; // for simple transparency if wanted

    bool hitDisk = false;
    vec3 diskHitColor = vec3(0.0);

    for (int s = 0; s < uMaxSteps; ++s) {
        float rr = r;
        if (rr < rs * 0.98) {
            // Captured by horizon
            break;
        }
        if (rr > 180.0) {
            // Escaped to infinity - sample background stars
            // Use the current direction of travel at escape for sky dir
            vec3 escapeDir = normalize( vec3(
                sin(theta)*cos(phi),
                sin(theta)*sin(phi),
                cos(theta)
            ) );
            // Slight correction using current 3-vel for better accuracy
            vec3 skyDir = normalize(escapeDir + 0.15 * vec3(ur, r*uth, r*sin(theta)*uph));
            vec3 sky = sampleStars(normalize(skyDir));
            accumColor = sky;
            break;
        }

        // Adaptive step: smaller when close to photon sphere (1.5 rs) or horizon
        float danger = min(abs(rr - 1.5*rs), abs(rr - rs));
        step = clamp(uStepSize * (0.6 + 0.6 * rr / 20.0), 0.004, 0.6);
        if (danger < rs * 0.6) step = min(step, 0.018);

        // --- RK4 integration of geodesic ---
        // State: r, theta, phi, ur, uth, uph
        // We integrate 5 actually, phi separate but ok.

        // Helper to compute derivatives at a state
        // dr = ur, dth=uth, dph=uph
        // dur, duth, duph from Christoffels

        float f  = 1.0 - rs / rr;
        float ut = E / max(f, 1e-5); // for accel calc

        // Christoffel derived accelerations
        float dur  = -( (rs * f / (2.0 * rr*rr)) * ut*ut ) 
                   - ( - rs / (2.0 * rr * (rr - rs)) ) * ur*ur
                   - ( - rr * f ) * uth*uth
                   - ( - rr * f * sin(theta)*sin(theta) ) * uph*uph ;

        float sinth = sin(theta);
        float costh = cos(theta);
        float cotth = (abs(sinth) > 1e-5) ? costh / sinth : 0.0;

        float duth = - (2.0 / rr) * ur * uth 
                   + sinth * costh * uph * uph;

        float duph = - (2.0 / rr) * ur * uph 
                   - 2.0 * cotth * uth * uph;

        // dr dth dph already known: ur uth uph

        // RK4: k1
        float kr1 = ur;
        float kt1 = uth;
        float kp1 = uph;
        float kur1 = dur;
        float kut1 = duth;
        float kup1 = duph;

        // k2 mid
        float r2 = rr + 0.5 * step * kr1;
        float th2 = theta + 0.5 * step * kt1;
        float ph2 = phi + 0.5 * step * kp1;
        float ur2 = ur + 0.5 * step * kur1;
        float ut2 = uth + 0.5 * step * kut1;
        float up2 = uph + 0.5 * step * kup1;

        float f2 = 1.0 - rs / max(r2, rs*0.5);
        float utt2 = E / max(f2, 1e-5);
        float dur2 = -( (rs * f2 / (2.0 * r2*r2)) * utt2*utt2 )
                    - ( -rs / (2.0 * r2 * (r2-rs)) ) * ur2*ur2
                    - (-r2 * f2) * ut2*ut2
                    - (-r2 * f2 * sin(th2)*sin(th2)) * up2*up2;
        float sinth2 = sin(th2);
        float costh2 = cos(th2);
        float cot2 = (abs(sinth2)>1e-5) ? costh2/sinth2 : 0.0;
        float duth2 = -(2.0/r2)*ur2*ut2 + sinth2*costh2 * up2*up2;
        float duph2 = -(2.0/r2)*ur2*up2 - 2.0*cot2 * ut2 * up2;

        float kr2 = ur2;
        float kt2 = ut2;
        float kp2 = up2;
        float kur2 = dur2;
        float kut2 = duth2;
        float kup2 = duph2;

        // k3
        float r3 = rr + 0.5 * step * kr2;
        float th3 = theta + 0.5 * step * kt2;
        float ph3 = phi + 0.5 * step * kp2;
        float ur3 = ur + 0.5 * step * kur2;
        float ut3 = uth + 0.5 * step * kut2;
        float up3 = uph + 0.5 * step * kup2;

        float f3 = 1.0 - rs/max(r3,rs*0.5);
        float utt3 = E / max(f3,1e-5);
        float dur3 = -(rs*f3/(2.0*r3*r3))*utt3*utt3 - (-rs/(2.0*r3*(r3-rs)))*ur3*ur3
                    - (-r3*f3)*ut3*ut3 - (-r3*f3*sin(th3)*sin(th3))*up3*up3;
        float sinth3 = sin(th3); float costh3=cos(th3); float cot3 = abs(sinth3)>1e-5 ? costh3/sinth3 : 0.0;
        float duth3 = -(2.0/r3)*ur3*ut3 + sinth3*costh3*up3*up3;
        float duph3 = -(2.0/r3)*ur3*up3 - 2.0*cot3*ut3*up3;

        float kr3 = ur3; float kt3=ut3; float kp3=up3;
        float kur3 = dur3; float kut3 = duth3; float kup3 = duph3;

        // k4
        float r4 = rr + step * kr3;
        float th4 = theta + step * kt3;
        float ph4 = phi + step * kp3;
        float ur4 = ur + step * kur3;
        float ut4 = uth + step * kut3;
        float up4 = uph + step * kup3;

        float f4 = 1.0 - rs / max(r4, rs*0.5);
        float utt4 = E / max(f4, 1e-5);
        float dur4 = -(rs*f4/(2.0*r4*r4))*utt4*utt4 - (-rs/(2.0*r4*(r4-rs)))*ur4*ur4
                    - (-r4*f4)*ut4*ut4 - (-r4*f4*sin(th4)*sin(th4))*up4*up4;
        float sinth4 = sin(th4); float costh4 = cos(th4); float cot4 = abs(sinth4)>1e-5 ? costh4/sinth4 : 0.0;
        float duth4 = -(2.0/r4)*ur4*ut4 + sinth4*costh4*up4*up4;
        float duph4 = -(2.0/r4)*ur4*up4 - 2.0*cot4*ut4*up4;

        float kr4 = ur4; float kt4 = ut4; float kp4 = up4;
        float kur4 = dur4; float kut4 = duth4; float kup4 = duph4;

        // Combine
        float drAvg = (kr1 + 2.0*kr2 + 2.0*kr3 + kr4) / 6.0;
        float dthAvg = (kt1 + 2.0*kt2 + 2.0*kt3 + kt4) / 6.0;
        float dphAvg = (kp1 + 2.0*kp2 + 2.0*kp3 + kp4) / 6.0;
        float durAvg = (kur1 + 2.0*kur2 + 2.0*kur3 + kur4) / 6.0;
        float duthAvg= (kut1 + 2.0*kut2 + 2.0*kut3 + kut4) / 6.0;
        float duphAvg= (kup1 + 2.0*kup2 + 2.0*kup3 + kup4) / 6.0;

        // Advance state
        float new_r = rr + step * drAvg;
        float new_theta = theta + step * dthAvg;
        float new_phi = phi + step * dphAvg;
        float new_ur = ur + step * durAvg;
        float new_uth = uth + step * duthAvg;
        float new_uph = uph + step * duphAvg;

        // Update
        float old_r = r;
        r = max(new_r, rs * 0.5);
        theta = clamp(new_theta, 1e-4, PI - 1e-4);
        phi = new_phi;
        ur = new_ur;
        uth = new_uth;
        uph = new_uph;

        // Current cartesian z for crossing
        float cz = r * cos(theta);
        vec3 cpos = vec3(r * sin(theta) * cos(phi), r * sin(theta) * sin(phi), cz);

        // Check disk plane crossing (z sign change)
        if (!hitDisk && prevZ * cz < 0.0 && old_r > rs * 1.02) {
            // Interpolate fraction where z==0
            float frac = abs(prevZ) / max(abs(prevZ - cz), 1e-6);
            float hit_r = mix(old_r, r, frac);
            float hit_theta = mix(acos(clamp(prevPos.z/ max(old_r,0.01),-1.,1.)), theta, frac); // approx

            if (hit_r > uDiskInner && hit_r < uDiskOuter) {
                // Recompute local state at hit approx using averaged
                float g = computeRedshiftFactor(hit_r, PI*0.5, ur, uth, uph, E, Lz, rs);
                if (g > 0.001) {
                    vec3 dc = diskColor(hit_r, g);
                    diskHitColor = dc * uDiskBrightness;
                    hitDisk = true;
                    // We take the first (closest) hit
                    accumColor = diskHitColor;
                    break;
                }
            }
        }

        // Update prev
        prevZ = cz;
        prevPos = cpos;
        prevR = r;

        // Extra: photon ring / strong deflection glow (near photon sphere)
        if (abs(r - 1.5 * rs) < 0.25 * rs && length(vec3(ur, uth, uph)) > 0.2) {
            // Add faint ring glow contribution when ray skims photon sphere
            float prGlow = exp( -pow( (r - 1.5*rs) / (0.12*rs) , 2.0) ) * 0.035;
            accumColor += vec3(0.7, 0.85, 1.0) * prGlow;
        }
    }

    if (hitDisk) {
        accumColor = diskHitColor;
    }

    // Apply simple exposure / tone
    accumColor *= uExposure;

    // Very faint halo around whole shadow from scattered light / disk
    // (simple additive)
    return accumColor;
}

void main() {
    // NDC uv from -1..1 with correct aspect
    vec2 uv = vTexCoord * 2.0 - 1.0;
    uv.x *= uAspect;

    float scale = tan(uFov * 0.5);
    vec3 rd = normalize( uCamForward + uCamRight * (uv.x * scale) + uCamUp * (uv.y * scale) );

    vec3 col = traceRay(uCamPos, rd, uRs);

    // Very subtle vignette
    float vig = smoothstep(1.4, 0.6, length(uv));
    col *= (0.6 + 0.4 * vig);

    // Clamp
    col = clamp(col, 0.0, 12.0);

    FragColor = vec4(col, 1.0);
}
