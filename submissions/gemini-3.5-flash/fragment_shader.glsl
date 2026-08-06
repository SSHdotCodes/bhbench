#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_camera_pos;
uniform vec3 u_camera_dir;
uniform vec3 u_camera_up;
uniform vec3 u_camera_right;

uniform float u_mass;
uniform bool u_enable_disk;
uniform bool u_enable_halos;
uniform bool u_enable_grid;
uniform float u_disk_intensity;
uniform float u_halo_intensity;
uniform float u_grid_intensity;

const float PI = 3.14159265358979323846;

// A simple hash function to generate random floats
float hash3(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

// 3D value noise
float noise3(vec3 p) {
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp); // smoothstep interpolation
    
    float n000 = hash3(ip + vec3(0.0, 0.0, 0.0));
    float n100 = hash3(ip + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(ip + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(ip + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(ip + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(ip + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(ip + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(ip + vec3(1.0, 1.0, 1.0));
    
    float nx00 = mix(n000, n100, fp.x);
    float nx10 = mix(n010, n110, fp.x);
    float nx01 = mix(n001, n101, fp.x);
    float nx11 = mix(n011, n111, fp.x);
    
    float nxy0 = mix(nx00, nx10, fp.y);
    float nxy1 = mix(nx01, nx11, fp.y);
    
    return mix(nxy0, nxy1, fp.z);
}

// 4-octave fBm noise
float fbm(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    vec3 shift = vec3(100.0);
    for (int i = 0; i < 4; ++i) {
        v += a * noise3(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// Procedural nebula generator
vec3 get_nebula(vec3 dir) {
    // Sample fBm at multiple scales
    float n1 = fbm(dir * 2.5 + vec3(0.0, u_time * 0.005, 0.0));
    float n2 = fbm(dir * 5.0 + vec3(u_time * 0.003, 0.0, 0.0));
    
    vec3 col1 = vec3(0.18, 0.03, 0.35); // deep purple
    vec3 col2 = vec3(0.35, 0.08, 0.03); // burning orange/red
    
    float intensity = smoothstep(0.25, 0.75, n1 * 0.65 + n2 * 0.35);
    vec3 nebula_color = mix(col1, col2, n2) * intensity * 0.65;
    
    return nebula_color;
}

// Procedural stars generator
vec3 get_stars(vec3 dir) {
    vec3 p = dir * 200.0;
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    
    float h = hash3(ip);
    
    vec3 star_color = vec3(0.0);
    if (h > 0.982) { // cells with stars
        vec3 offset = vec3(hash3(ip + 1.0), hash3(ip + 2.0), hash3(ip + 3.0));
        vec3 star_pos = (offset - 0.5) * 0.6;
        
        float d = length(fp - 0.5 - star_pos);
        float size = mix(0.012, 0.055, hash3(ip + 4.0));
        float intensity = smoothstep(size, 0.0, d);
        
        vec3 color = vec3(0.85) + 0.15 * vec3(hash3(ip + 5.0), hash3(ip + 6.0), hash3(ip + 7.0));
        star_color = color * intensity * mix(0.6, 2.2, hash3(ip + 8.0));
    }
    return star_color;
}

// Mitchell Charity blackbody approximation
vec3 blackbody(float Temp) {
    Temp /= 100.0;
    float r, g, b;
    if (Temp <= 66.0) {
        r = 255.0;
        g = Temp;
        g = 99.4708025861 * log(max(1.0, g)) - 161.1195681661;
        if (Temp <= 19.0) {
            b = 0.0;
        } else {
            b = Temp - 10.0;
            b = 138.5177312231 * log(max(1.0, b)) - 305.0447927307;
        }
    } else {
        r = Temp - 60.0;
        r = 329.698727446 * pow(max(1.0, r), -0.1332047592);
        g = Temp - 60.0;
        g = 288.1221695283 * pow(max(1.0, g), -0.0755148492);
        b = 255.0;
    }
    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}

// Warp grid verification function
float check_grid(vec3 pos, float M) {
    float r = length(pos.xy);
    float phi = atan(pos.y, pos.x);
    
    if (r < 0.5 * M) return 0.0;
    
    float r_spacing = 1.0 * M;
    float phi_spacing = PI / 12.0; // 24 radial segments
    
    float line_width = 0.02 * M;
    
    float dr = abs(fract(r / r_spacing + 0.5) - 0.5) * r_spacing;
    float dphi = abs(fract(phi / phi_spacing + 0.5) - 0.5) * phi_spacing * r;
    
    float val_r = smoothstep(line_width, 0.0, dr);
    float val_phi = smoothstep(line_width, 0.0, dphi);
    
    return clamp(val_r + val_phi, 0.0, 1.0);
}

// Derivative equations for geodesics in isotropic Schwarzschild metric
void get_derivatives(vec3 x, vec3 p, float M, out vec3 dx, out vec3 dp) {
    float r = length(x);
    float r_minus = r - 0.5 * M;
    if (r_minus <= 0.0001) {
        dx = vec3(0.0);
        dp = vec3(0.0);
        return;
    }
    float r_plus = r + 0.5 * M;
    
    // A(r) = (r_minus / r_plus)^2
    float A = (r_minus * r_minus) / (r_plus * r_plus);
    
    // B(r) = (r_plus / r)^4
    float r_p_over_r = r_plus / r;
    float B = r_p_over_r * r_p_over_r * r_p_over_r * r_p_over_r;
    
    // dx/dlambda = p / B(r)
    dx = p / B;
    
    // dp/dlambda = - [ 2*M*(4*r - M) / (A * r^2 * (4*r^2 - M^2)) ] * x
    // where 4*r^2 - M^2 = 4 * r_minus * r_plus
    float den = A * r * r * 4.0 * r_minus * r_plus;
    float num = 2.0 * M * (4.0 * r - M);
    dp = - (num / den) * x;
}

// RK4 Geodesic Step
void rk4_step(inout vec3 x, inout vec3 p, float M, float dt) {
    vec3 dx1, dp1;
    get_derivatives(x, p, M, dx1, dp1);
    
    vec3 x2 = x + 0.5 * dt * dx1;
    vec3 p2 = p + 0.5 * dt * dp1;
    vec3 dx2, dp2;
    get_derivatives(x2, p2, M, dx2, dp2);
    
    vec3 x3 = x + 0.5 * dt * dx2;
    vec3 p3 = p + 0.5 * dt * dp2;
    vec3 dx3, dp3;
    get_derivatives(x3, p3, M, dx3, dp3);
    
    vec3 x4 = x + dt * dx3;
    vec3 p4 = p + dt * dp3;
    vec3 dx4, dp4;
    get_derivatives(x4, p4, M, dx4, dp4);
    
    x += (dt / 6.0) * (dx1 + 2.0 * dx2 + 2.0 * dx3 + dx4);
    p += (dt / 6.0) * (dp1 + 2.0 * dp2 + 2.0 * dp3 + dp4);
}

void main() {
    // Normalised device coordinates [-0.5, 0.5] corrected for aspect ratio
    vec2 uv = (gl_FragCoord.xy / u_resolution - 0.5) * vec2(u_resolution.x / u_resolution.y, 1.0);
    
    // Camera ray direction
    float fov_scale = 1.0;
    vec3 ray_dir = normalize(u_camera_dir + uv.x * u_camera_right * fov_scale + uv.y * u_camera_up * fov_scale);
    
    vec3 x = u_camera_pos;
    
    float M = u_mass;
    
    // Initial momentum p satisfying H = 0: |p| = sqrt(B(r0)/A(r0))
    float r0 = length(x);
    float r0_plus = r0 + 0.5 * M;
    float r0_minus = r0 - 0.5 * M;
    float A0 = (r0_minus * r0_minus) / (r0_plus * r0_plus);
    float r0_p_over_r = r0_plus / r0;
    float B0 = r0_p_over_r * r0_p_over_r * r0_p_over_r * r0_p_over_r;
    vec3 p = ray_dir * sqrt(B0 / A0);
    
    float accumulated_transmittance = 1.0;
    vec3 accumulated_color = vec3(0.0);
    
    // Accretion disk specifications
    float r_in = 2.914 * M; // ISCO radius in isotropic coordinates
    float r_out = 8.5 * M;
    
    int max_steps = 150;
    float min_dt = 0.02;
    float max_dt = 0.35;
    
    for (int i = 0; i < max_steps; ++i) {
        vec3 x_prev = x;
        float r_curr = length(x);
        
        // Adaptive step size: smaller near event horizon, larger far away
        float dt = mix(min_dt, max_dt, clamp((r_curr - 0.5 * M) / 4.0, 0.0, 1.0));
        
        // Advance geodesic
        rk4_step(x, p, M, dt);
        
        float r_new = length(x);
        
        // 1. Check Horizon collision (horizon at r_h = 0.5 * M)
        if (r_new <= 0.5 * M * 1.01) {
            accumulated_transmittance = 0.0;
            break;
        }
        
        // 2. Corona Halo accumulation (spherical diffuse corona)
        if (u_enable_halos && r_new > 0.5 * M && r_new < 9.0 * M) {
            float density = exp(-r_new / (2.2 * M)) * u_halo_intensity * 0.07;
            vec3 halo_col = vec3(1.0, 0.58, 0.28); // orange corona glow
            accumulated_color += accumulated_transmittance * halo_col * density * dt;
            accumulated_transmittance *= exp(-density * dt);
        }
        
        // 3. Equatorial Plane Crossing (Accretion Disk & Spacetime Grid)
        if (x_prev.z * x.z < 0.0) {
            float t_plane = -x_prev.z / (x.z - x_prev.z);
            vec3 x_int = x_prev + t_plane * (x - x_prev);
            float r_int = length(x_int);
            
            // A. Accretion Disk intersection
            if (u_enable_disk && r_int >= r_in && r_int <= r_out) {
                // Schwarzschild equivalent radius (coordinate transformation)
                float r_sch = r_int * pow(1.0 + M / (2.0 * r_int), 2.0);
                
                // Gas Keplerian velocity vector
                float v_orb = sqrt(M / r_sch) / (1.0 + M / (2.0 * r_int));
                vec3 t_gas = normalize(vec3(-x_int.y, x_int.x, 0.0));
                vec3 v_gas = v_orb * t_gas;
                
                // Photon ray direction at intersection
                vec3 u_ray = normalize(p);
                
                // Relativistic Doppler Shift & Gravitational Redshift (Doppler factor D)
                float v_dot_u = dot(v_gas, u_ray);
                float D = sqrt(max(0.0, 1.0 - 3.0 * M / r_sch)) / (1.0 - v_dot_u);
                
                // Local temperature profile (Novikov-Thorne disk style)
                float temp_base = 7200.0; // max temperature
                float r_ratio = r_in / r_int;
                float T_local = temp_base * pow(r_ratio, 0.75) * sqrt(max(0.0, 1.0 - sqrt(r_ratio)));
                
                // Shift temperature by Doppler factor
                float T_obs = D * T_local;
                vec3 disk_color = blackbody(T_obs);
                
                // Relativistic Beaming (integrated flux scales as D^4)
                float beaming = pow(D, 4.0);
                vec3 emission = disk_color * beaming * u_disk_intensity;
                
                // Density / Opacity falloff
                float opacity = 0.65 * pow(r_ratio, 1.6);
                opacity = clamp(opacity, 0.0, 0.95);
                
                // Blend
                accumulated_color += accumulated_transmittance * emission * opacity;
                accumulated_transmittance *= (1.0 - opacity);
            }
            
            // B. Spacetime Grid intersection
            if (u_enable_grid && r_int >= 0.5 * M && r_int <= r_out * 1.5) {
                float grid_val = check_grid(x_int, M);
                if (grid_val > 0.0) {
                    vec3 grid_color = vec3(0.0, 0.95, 0.55); // glowing cyan grid
                    float grid_opacity = 0.85 * grid_val * smoothstep(0.5 * M, 0.58 * M, r_int);
                    
                    accumulated_color += accumulated_transmittance * grid_color * 1.6 * grid_opacity * u_grid_intensity;
                    accumulated_transmittance *= (1.0 - grid_opacity);
                }
            }
        }
        
        // 4. Escape to Infinity
        if (r_new >= 35.0) {
            break;
        }
        
        if (accumulated_transmittance < 0.01) {
            accumulated_transmittance = 0.0;
            break;
        }
    }
    
    // Add skybox at infinity
    if (accumulated_transmittance > 0.0) {
        vec3 final_dir = normalize(p);
        vec3 stars = get_stars(final_dir);
        vec3 nebula = get_nebula(final_dir);
        accumulated_color += accumulated_transmittance * (stars * 1.2 + nebula);
    }
    
    // Tone mapping and gamma correction
    vec3 mapped_color = accumulated_color / (accumulated_color + vec3(1.0)); // Reinhard tone mapping
    mapped_color = pow(mapped_color, vec3(1.0 / 2.2)); // Gamma correction
    
    FragColor = vec4(mapped_color, 1.0);
}
