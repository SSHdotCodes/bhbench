#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

// -----------------------------------------------------------------------------
// Mathematical & Physical Constants
// -----------------------------------------------------------------------------
constant float PI = 3.14159265358979323846;
constant float ESCAPE_RADIUS = 50.0; // Distance where space is approximately flat

// -----------------------------------------------------------------------------
// Procedural Celestial Sky (Stars, Nebula, Milky Way band)
// -----------------------------------------------------------------------------
float hash(float3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(float3 x) {
    float3 p = floor(x);
    float3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(p + float3(0,0,0)), hash(p + float3(1,0,0)), f.x),
                   mix(hash(p + float3(0,1,0)), hash(p + float3(1,1,0)), f.x), f.y),
               mix(mix(hash(p + float3(0,0,1)), hash(p + float3(1,0,1)), f.x),
                   mix(hash(p + float3(0,1,1)), hash(p + float3(1,1,1)), f.x), f.y), f.z);
}

float fbm(float3 p) {
    float v = 0.0;
    float a = 0.5;
    float3 shift = float3(100.0);
    for (int i = 0; i < 4; ++i) {
        v += a * noise(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

float3 getSkyColor(float3 dir) {
    // Coordinate on celestial sphere
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    float phi = atan2(dir.z, dir.x);
    
    // Milky Way galactic plane tilted at ~60 degrees
    float3 galacticNormal = normalize(float3(0.5, 0.8, 0.2));
    float galacticDist = abs(dot(dir, galacticNormal));
    
    // Deep cosmic dark background
    float3 col = float3(0.003, 0.005, 0.012);
    
    // Nebula clouds along galactic plane
    float nebulaNoise = fbm(dir * 3.5);
    float band = exp(-galacticDist * 6.0) * (0.6 + 0.4 * nebulaNoise);
    float3 nebulaCol = mix(float3(0.15, 0.08, 0.25), float3(0.05, 0.15, 0.3), fbm(dir * 5.0));
    col += nebulaCol * band * 1.8;
    
    // Dense starry field
    float3 starCell = floor(dir * 180.0);
    float starRnd = hash(starCell);
    if (starRnd > 0.985) {
        float3 starCenter = (starCell + 0.5) / 180.0;
        float d = length(dir - normalize(starCenter));
        float brightness = pow(fract(starRnd * 12345.67), 12.0) * 1.5;
        float starIntensity = exp(-d * 800.0) * brightness;
        // Star color temperature variation
        float3 starHue = mix(float3(0.8, 0.9, 1.0), float3(1.0, 0.8, 0.5), fract(starRnd * 77.1));
        col += starHue * starIntensity * 25.0;
    }
    
    // Distant background grid / coordinate reference lines
    float gridU = abs(fract(phi * (8.0 / PI) + 0.5) - 0.5);
    float gridV = abs(fract(theta * (8.0 / PI) + 0.5) - 0.5);
    float grid = smoothstep(0.04, 0.01, min(gridU, gridV));
    col += float3(0.04, 0.08, 0.12) * grid * 0.4;
    
    return col;
}

// -----------------------------------------------------------------------------
// Planckian Blackbody Radiation to RGB approximation
// -----------------------------------------------------------------------------
float3 blackbody(float Temp) {
    // Temperature in Kelvin (scaled)
    float t = Temp / 100.0;
    float r, g, b;
    
    // Red
    if (t <= 66.0) {
        r = 255.0;
    } else {
        r = t - 60.0;
        r = 329.698727446 * pow(r, -0.1332047592);
        r = clamp(r, 0.0, 255.0);
    }
    
    // Green
    if (t <= 66.0) {
        g = t;
        g = 99.4708025861 * log(g) - 161.1195681661;
        g = clamp(g, 0.0, 255.0);
    } else {
        g = t - 60.0;
        g = 288.1221695283 * pow(g, -0.0755148492);
        g = clamp(g, 0.0, 255.0);
    }
    
    // Blue
    if (t >= 66.0) {
        b = 255.0;
    } else if (t <= 19.0) {
        b = 0.0;
    } else {
        b = t - 10.0;
        b = 138.5177312231 * log(b) - 305.0447927307;
        b = clamp(b, 0.0, 255.0);
    }
    
    return float3(r, g, b) / 255.0;
}

// -----------------------------------------------------------------------------
// Flamm's Paraboloid Embedding Spacetime Curvature Visualizer
// z_embed(r) = 2 * sqrt(2M * (r - 2M))
// We render the 2D spatial slice (equatorial plane or offset plane) embedded into 3D
// displaying the "trapdoor" / gravity funnel that leads into the event horizon.
// -----------------------------------------------------------------------------
float4 sampleSpacetimeGrid(float3 pos, constant BlackHoleParams &params) {
    if (params.renderGrid == 0) return float4(0.0);
    
    float M = params.mass;
    float rH = params.rH;
    // Embedding coordinates: let the grid live around y = params.gridHeightOffset
    // For Flamm's paraboloid, the embedding depth is Z(r) = -2.5 * sqrt(2.0*M*(r - 2.0*M))
    // We compute the distance of pos to the embedded funnel surface:
    float r_plane = length(pos.xz);
    if (r_plane < rH * 1.001 || r_plane > 25.0 * M) {
        return float4(0.0);
    }
    
    // Funnel height profile (Flamm's paraboloid)
    float delta_r = max(r_plane - 2.0 * M, 0.0);
    float funnel_y = params.gridHeightOffset - 2.5 * sqrt(2.0 * M * delta_r);
    
    // Distance from current photon ray position to the funnel surface
    float distToFunnel = abs(pos.y - funnel_y);
    
    // If ray is within the thin sheet of the embedding surface:
    float sheetThickness = 0.12 * (1.0 + 0.05 * r_plane);
    if (distToFunnel < sheetThickness) {
        // Compute polar grid coordinates on the funnel
        float phi = atan2(pos.z, pos.x);
        // Radial grid lines spaced logarithmically near horizon, linear far away
        float rCoord = log(r_plane / (2.0 * M) + 0.1);
        float radialLine = abs(fract(rCoord * 6.0) - 0.5);
        float angularLine = abs(fract(phi * (12.0 / PI)) - 0.5);
        
        float lineIntensity = smoothstep(0.08, 0.02, min(radialLine, angularLine));
        
        // Color shifts from high-energy neon cyan/electric blue far out to glowing red/orange near horizon
        float depthFactor = clamp((r_plane - 2.0 * M) / (10.0 * M), 0.0, 1.0);
        float3 gridColor = mix(float3(1.0, 0.2, 0.05), float3(0.1, 0.7, 1.0), depthFactor);
        
        // Edge glow / rim highlight
        float alpha = (1.0 - distToFunnel / sheetThickness) * (lineIntensity * 0.85 + 0.15);
        // Fade out near outer edge
        alpha *= smoothstep(25.0 * M, 20.0 * M, r_plane);
        // Brighten up the funnel neck near horizon
        float neckGlow = 1.0 / (depthFactor + 0.15);
        gridColor *= (1.0 + 0.3 * neckGlow);
        
        return float4(gridColor, alpha * 0.75);
    }
    
    return float4(0.0);
}

// -----------------------------------------------------------------------------
// Accretion Disk Physics: Novikov-Thorne Temperature & Relativistic Beaming
// -----------------------------------------------------------------------------
float4 sampleAccretionDisk(float3 oldPos, float3 newPos, constant BlackHoleParams &params) {
    if (params.renderDisk == 0) return float4(0.0);
    
    // Check if the geodesic segment intersects the disk plane (y = 0)
    if ((oldPos.y > 0.0 && newPos.y < 0.0) || (oldPos.y < 0.0 && newPos.y > 0.0) || abs(newPos.y) < 0.08) {
        // Linear interpolation to equatorial plane y = 0
        float t = (abs(oldPos.y - newPos.y) > 1e-6) ? (0.0 - oldPos.y) / (newPos.y - oldPos.y) : 0.0;
        t = clamp(t, 0.0, 1.0);
        float3 hitPos = mix(oldPos, newPos, t);
        
        float r = length(hitPos.xz);
        float r_in = params.diskInner;
        float r_out = params.diskOuter;
        
        if (r >= r_in && r <= r_out) {
            float phi = atan2(hitPos.z, hitPos.x);
            float M = params.mass;
            float a = params.spin;
            
            // Standard Page-Thorne / Novikov-Thorne thin accretion disk model:
            // T(r)^4 ~ (M / r^3) * [1 - sqrt(r_ISCO / r)]
            float rRel = r / r_in;
            float f_profile = max(1.0 - sqrt(1.0 / rRel), 0.0);
            float T_eff = pow(f_profile / (rRel * rRel * rRel), 0.25) * params.diskTempScale * 12000.0;
            T_eff = max(T_eff, 800.0);
            
            // Keplerian orbital velocity in Kerr spacetime:
            // Omega = sqrt(M) / (r^(3/2) + a * sqrt(M))
            float Omega = sqrt(M) / (pow(r, 1.5) + a * sqrt(M));
            // Disk rotates counter-clockwise around y-axis:
            // Velocity vector v = Omega * r * (-sin(phi), 0, cos(phi))
            float v_phi = Omega * r;
            // Cap to sub-luminal velocity
            v_phi = clamp(v_phi, 0.0, 0.85);
            
            float3 v_disk = float3(-sin(phi), 0.0, cos(phi)) * v_phi;
            
            // Photon direction vector in disk frame
            float3 rayDir = normalize(newPos - oldPos);
            // Relativistic Doppler factor g = 1 / (gamma * (1 - v . n))
            // where n is unit vector pointing FROM disk element TO observer (i.e. -rayDir)
            float gamma = 1.0 / sqrt(max(1.0 - v_phi * v_phi, 1e-4));
            float cosTheta = dot(v_disk / v_phi, -rayDir);
            
            // Gravitational redshift factor: sqrt(1 - 2M/r)
            float g_grav = sqrt(max(1.0 - 2.0 * M / r, 0.01));
            
            // Combined relativistic frequency shift factor g = nu_obs / nu_emit:
            float g_factor = g_grav / (gamma * (1.0 - v_phi * cosTheta));
            g_factor = clamp(g_factor, 0.05, 4.0);
            
            // Relativistic Beaming: Specific intensity transforms as I_obs = g^3.5 * I_emit
            float beaming = pow(g_factor, 3.5);
            
            // Temperature shifted by Doppler + Gravitational redshift:
            float T_obs = T_eff * g_factor;
            
            // Procedural turbulent shearing flows, spirals, and clumps in the accretion disk:
            float shearPhase = phi - Omega * params.time * 2.0;
            float diskNoise = fbm(float3(r * 2.5, shearPhase * 3.0, 0.5));
            float ringDetail = sin(r * 18.0 + fbm(hitPos * 1.5) * 4.0) * 0.5 + 0.5;
            float turbulence = 0.7 + 0.3 * diskNoise + 0.15 * ringDetail;
            
            // Blackbody color from shifted temperature
            float3 baseColor = blackbody(T_obs);
            
            if (params.colorMode == 1) {
                // False-color temperature mode
                baseColor = mix(float3(0.0, 0.2, 1.0), float3(1.0, 0.1, 0.0), clamp(T_obs / 25000.0, 0.0, 1.0));
            } else if (params.colorMode == 2) {
                // Relativistic g-factor visualization (blue = approaching, red = receding)
                baseColor = mix(float3(1.0, 0.05, 0.05), float3(0.05, 0.5, 1.0), clamp((g_factor - 0.5) / 1.5, 0.0, 1.0));
            }
            
            // Optical depth / density profile across the disk
            float edgeFade = smoothstep(r_in, r_in + 0.3 * M, r) * smoothstep(r_out, r_out - 1.5 * M, r);
            float diskDensity = params.diskAlpha * edgeFade * turbulence;
            
            // Luminous emission
            float3 diskRadiance = baseColor * beaming * turbulence * 2.2;
            
            return float4(diskRadiance, clamp(diskDensity, 0.0, 1.0));
        }
    }
    
    return float4(0.0);
}

// -----------------------------------------------------------------------------
// General Relativity Geodesic Equations of Motion (Null Geodesics)
// -----------------------------------------------------------------------------
float3 computeGeodesicAcceleration(float3 pos, float3 vel, float M, float a) {
    float r = length(pos);
    float r2 = r * r;
    float r3 = r2 * r;
    float r5 = r3 * r2;
    
    // Angular momentum vector L = x x v
    float3 L = cross(pos, vel);
    float L2 = dot(L, L);
    
    // Classical + General Relativistic light deflection term:
    float3 acc = - (3.0 * M * L2 / (r5 + 1e-5)) * pos;
    
    // Kerr Frame-Dragging (Lense-Thirring effect) for rotating black hole:
    // Spin vector J is along the positive y-axis: J = (0, a, 0)
    if (abs(a) > 1e-4) {
        float3 J = float3(0.0, a, 0.0);
        float3 n = pos / r;
        float3 B_gm = (2.0 * M / r3) * (3.0 * dot(J, n) * n - J);
        float3 acc_drag = cross(vel, B_gm);
        acc += acc_drag;
    }
    
    return acc;
}

// -----------------------------------------------------------------------------
// Main Compute Kernel: Raymarch null geodesics backwards from camera
// -----------------------------------------------------------------------------
kernel void raytraceBlackHole(
    texture2d<float, access::write> outTexture [[texture(0)]],
    constant BlackHoleParams &params [[buffer(0)]],
    uint2 id [[thread_position_in_grid]])
{
    if (id.x >= uint(params.resolution.x) || id.y >= uint(params.resolution.y)) {
        return;
    }
    
    // Normalized device coordinates with aspect ratio correction
    float2 uv = (float2(id) + 0.5) / params.resolution;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Match screen coordinates
    float aspect = params.resolution.x / params.resolution.y;
    ndc.x *= aspect;
    
    // Construct initial camera primary ray
    float tanHalfFov = tan(params.fov * 0.5);
    float3 rayDir = normalize(params.camForward + ndc.x * tanHalfFov * params.camRight + ndc.y * tanHalfFov * params.camUp);
    float3 rayPos = params.camPos;
    
    float M = params.mass;
    float a = params.spin;
    float rH = params.rH;
    
    float4 accumulatedColor = float4(0.0);
    bool hitHorizon = false;
    
    // Adaptive Step Runge-Kutta 4th Order Geodesic Integrator (RK4)
    float baseStep = 0.08 * params.stepSizeScale;
    int maxSteps = int(params.maxSteps);
    
    float3 pos = rayPos;
    float3 vel = rayDir; // speed of light c = 1
    
    // If lensing is disabled (for scientific comparison), do straight ray:
    if (params.renderLensing == 0) {
        float tMin = 0.0;
        float tMax = 60.0;
        float dt = 0.1;
        float3 p = pos;
        for (float t = tMin; t < tMax; t += dt) {
            float3 nextP = p + vel * dt;
            float r = length(nextP);
            if (r < rH) {
                hitHorizon = true;
                break;
            }
            float4 diskCol = sampleAccretionDisk(p, nextP, params);
            if (diskCol.a > 0.0) {
                accumulatedColor.rgb += (1.0 - accumulatedColor.a) * diskCol.rgb * diskCol.a;
                accumulatedColor.a += (1.0 - accumulatedColor.a) * diskCol.a;
            }
            p = nextP;
        }
        if (!hitHorizon) {
            float3 sky = getSkyColor(vel);
            accumulatedColor.rgb += (1.0 - accumulatedColor.a) * sky;
        }
        outTexture.write(float4(accumulatedColor.rgb, 1.0), id);
        return;
    }
    
    for (int step = 0; step < maxSteps; ++step) {
        float r = length(pos);
        
        // Check if captured by Event Horizon:
        if (r <= rH * 1.01) {
            hitHorizon = true;
            break;
        }
        
        // Check if photon escaped to asymptotic infinity:
        if (r > ESCAPE_RADIUS) {
            break;
        }
        
        // Adaptive step size: photon moves much faster far away, needs ultra-fine steps near horizon/ISCO
        float dt = baseStep * clamp(pow(r / (2.0 * M), 1.2), 0.15, 6.0);
        
        // RK4 Integration
        float3 k1_v = dt * computeGeodesicAcceleration(pos, vel, M, a);
        float3 k1_x = dt * vel;
        
        float3 k2_v = dt * computeGeodesicAcceleration(pos + 0.5 * k1_x, vel + 0.5 * k1_v, M, a);
        float3 k2_x = dt * (vel + 0.5 * k1_v);
        
        float3 k3_v = dt * computeGeodesicAcceleration(pos + 0.5 * k2_x, vel + 0.5 * k2_v, M, a);
        float3 k3_x = dt * (vel + 0.5 * k2_v);
        
        float3 k4_v = dt * computeGeodesicAcceleration(pos + k3_x, vel + k3_v, M, a);
        float3 k4_x = dt * (vel + k3_v);
        
        float3 nextPos = pos + (k1_x + 2.0 * k2_x + 2.0 * k3_x + k4_x) / 6.0;
        float3 nextVel = normalize(vel + (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v) / 6.0);
        
        // 1. Sample Spacetime Curvature Funnel Grid (Flamm's Paraboloid visualizer)
        float4 gridCol = sampleSpacetimeGrid(nextPos, params);
        if (gridCol.a > 0.0) {
            accumulatedColor.rgb += (1.0 - accumulatedColor.a) * gridCol.rgb * gridCol.a;
            accumulatedColor.a += (1.0 - accumulatedColor.a) * gridCol.a;
        }
        
        // 2. Sample Accretion Disk volumetric / sheet radiance
        float4 diskCol = sampleAccretionDisk(pos, nextPos, params);
        if (diskCol.a > 0.0) {
            accumulatedColor.rgb += (1.0 - accumulatedColor.a) * diskCol.rgb * diskCol.a;
            accumulatedColor.a += (1.0 - accumulatedColor.a) * diskCol.a;
        }
        
        // Early termination if fully opaque
        if (accumulatedColor.a >= 0.99) {
            break;
        }
        
        pos = nextPos;
        vel = nextVel;
    }
    
    // Background celestial sphere
    if (!hitHorizon) {
        float3 skyColor = getSkyColor(vel);
        accumulatedColor.rgb += (1.0 - accumulatedColor.a) * skyColor;
    } else {
        // Pure black singularity / event horizon shadow
        accumulatedColor.rgb += (1.0 - accumulatedColor.a) * float3(0.0, 0.0, 0.0);
    }
    
    // ACES Film Tone Mapping & Gamma Correction for High Dynamic Range (HDR) radiance
    float3 col = accumulatedColor.rgb;
    float a_cur = 2.51;
    float b_cur = 0.03;
    float c_cur = 2.43;
    float d_cur = 0.59;
    float e_cur = 0.14;
    col = clamp((col * (a_cur * col + b_cur)) / (col * (c_cur * col + d_cur) + e_cur), 0.0, 1.0);
    col = pow(col, float3(1.0 / 2.2));
    
    outTexture.write(float4(col, 1.0), id);
}

// -----------------------------------------------------------------------------
// Fullscreen Quad Blit Pipeline with HUD alpha blending
// -----------------------------------------------------------------------------
struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut blitVertex(uint vertexID [[vertex_id]]) {
    VertexOut out;
    float2 grid = float2((vertexID << 1) & 2, vertexID & 2);
    out.position = float4(grid * 2.0 - 1.0, 0.0, 1.0);
    out.uv = grid;
    return out;
}

fragment float4 blitFragment(VertexOut in [[stage_in]],
                             texture2d<float> renderTex [[texture(0)]],
                             texture2d<float> hudTex [[texture(1)]]) {
    constexpr sampler linearSampler(coord::normalized, filter::linear, address::clamp_to_edge);
    constexpr sampler pointSampler(coord::normalized, filter::nearest, address::clamp_to_edge);
    
    float4 base = renderTex.sample(linearSampler, in.uv);
    float4 hud = hudTex.sample(pointSampler, in.uv);
    
    // Alpha blend HUD on top of raytraced simulation
    float3 finalRgb = mix(base.rgb, hud.rgb, hud.a);
    return float4(finalRgb, 1.0);
}
