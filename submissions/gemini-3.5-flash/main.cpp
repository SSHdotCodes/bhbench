#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

#include "Shader.h"
#include "Grid.h"
#include "ParticleSystem.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Math Helpers (To avoid external GLM dependencies) ---

struct Vec3 {
    float x, y, z;
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
};

Vec3 normalize(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.0f) return Vec3(v.x / len, v.y / len, v.z / len);
    return Vec3(0, 0, 0);
}

Vec3 cross(Vec3 a, Vec3 b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void mat4_identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_perspective(float* m, float fov_deg, float aspect, float zNear, float zFar) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    float f = 1.0f / std::tan(fov_deg * 0.5f * (float)M_PI / 180.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}

void mat4_lookAt(float* m, Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 r = normalize(cross(f, up));
    Vec3 u = cross(r, f);
    
    m[0] = r.x;  m[1] = u.x;  m[2] = -f.x; m[3] = 0.0f;
    m[4] = r.y;  m[5] = u.y;  m[6] = -f.y; m[7] = 0.0f;
    m[8] = r.z;  m[9] = u.z;  m[10] = -f.z; m[11] = 0.0f;
    m[12] = -dot(r, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
    m[15] = 1.0f;
}

// --- Global Simulation Variables ---

int current_mode = 0; // 0 = Relativistic Ray-Tracer, 1 = 3D Spacetime Grid Funnel
float mass = 1.0f;    // Black hole mass parameter
bool enable_disk = true;
bool enable_halos = true;
bool enable_grid = false; // warped equatorial grid toggle for ray tracer
float disk_intensity = 1.0f;
float halo_intensity = 1.0f;
float grid_intensity = 1.0f;
bool simulation_paused = false;

// Camera settings
float camera_distance = 12.0f;
float camera_theta = (float)M_PI / 2.0f - 0.06f; // slightly above the equatorial plane
float camera_phi = 0.0f;

// Window resolution
int window_width = 1280;
int window_height = 720;

// --- Function Declarations ---

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void process_continuous_input(GLFWwindow* window, float dt);
void print_controls();

int main() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // Set OpenGL Version to 3.3 Core Profile (perfect compatibility on macOS)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create window
    GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Relativistic Black Hole 3D Simulation", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    
    // Set callbacks
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    
    // Vsync enabled
    glfwSwapInterval(1);

    // Initialize GLEW if not on Apple
#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }
#endif

    // Print helper instructions to console
    print_controls();

    // Compile Shaders
    // 1. Ray-Tracer Shader Program
    Shader rayTracerShader("vertex_shader.glsl", "fragment_shader.glsl", false);
    
    // 2. Spacetime Grid Shader Program
    Shader gridShader("grid_vertex_shader.glsl", "grid_fragment_shader.glsl", false);
    
    // 3. Particle Shader Program
    Shader particleShader("particle_vertex_shader.glsl", "particle_fragment_shader.glsl", false);

    // Set up full screen quad for ray tracer
    float quadVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,

        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };
    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Instantiate Spacetime Grid and Particle System
    // Grid: Rs = 2.0 * mass. Rmax = 18.0.
    float Rs = 2.0f * mass;
    float Rmax = 18.0f;
    Grid spacetimeGrid(Rs, Rmax, 36, 24);
    ParticleSystem particles(150, Rs, Rmax);

    // OpenGL settings
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Enable custom point size and blending
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float lastFrameTime = 0.0f;
    float totalTime = 0.0f;

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = (float)glfwGetTime();
        float dt = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        // Limit dt to avoid massive physics jumps on lag spikes
        if (dt > 0.1f) dt = 0.1f;

        if (!simulation_paused) {
            totalTime += dt;
            particles.update(dt);
        }

        // Handle user input
        process_continuous_input(window, dt);

        // Reset buffer and clear screen
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update grid parameters if mass changed
        Rs = 2.0f * mass;

        // Camera calculations
        Vec3 cameraPos;
        cameraPos.x = camera_distance * std::sin(camera_theta) * std::cos(camera_phi);
        cameraPos.y = camera_distance * std::sin(camera_theta) * std::sin(camera_phi);
        cameraPos.z = camera_distance * std::cos(camera_theta);

        Vec3 target(0.0f, 0.0f, 0.0f);
        Vec3 forward = normalize(target - cameraPos);
        Vec3 worldUp(0.0f, 0.0f, 1.0f);
        if (std::abs(forward.z) > 0.999f) {
            worldUp = Vec3(0.0f, 1.0f, 0.0f);
        }
        Vec3 right = normalize(cross(forward, worldUp));
        Vec3 up = cross(right, forward);

        if (current_mode == 0) {
            // --- MODE 0: GENERAL RELATIVISTIC RAY-TRACED BLACK HOLE ---
            glDisable(GL_DEPTH_TEST);
            
            rayTracerShader.use();
            rayTracerShader.setVec2("u_resolution", (float)window_width, (float)window_height);
            rayTracerShader.setFloat("u_time", totalTime);
            rayTracerShader.setVec3("u_camera_pos", cameraPos.x, cameraPos.y, cameraPos.z);
            rayTracerShader.setVec3("u_camera_dir", forward.x, forward.y, forward.z);
            rayTracerShader.setVec3("u_camera_up", up.x, up.y, up.z);
            rayTracerShader.setVec3("u_camera_right", right.x, right.y, right.z);
            
            rayTracerShader.setFloat("u_mass", mass);
            rayTracerShader.setBool("u_enable_disk", enable_disk);
            rayTracerShader.setBool("u_enable_halos", enable_halos);
            rayTracerShader.setBool("u_enable_grid", enable_grid);
            rayTracerShader.setFloat("u_disk_intensity", disk_intensity);
            rayTracerShader.setFloat("u_halo_intensity", halo_intensity);
            rayTracerShader.setFloat("u_grid_intensity", grid_intensity);

            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
        else {
            // --- MODE 1: 3D SPACETIME CURVATURE GRID FUNNEL ---
            glEnable(GL_DEPTH_TEST);

            // Compute projection and view matrices
            float proj[16];
            float aspect = (float)window_width / (float)window_height;
            mat4_perspective(proj, 45.0f, aspect, 0.1f, 100.0f);

            float view[16];
            mat4_lookAt(view, cameraPos, target, worldUp);

            float model[16];
            mat4_identity(model);

            // 1. Draw Spacetime Curvature Grid Funnel
            gridShader.use();
            gridShader.setMat4("u_proj", proj);
            gridShader.setMat4("u_view", view);
            gridShader.setMat4("u_model", model);
            
            gridShader.setBool("u_is_horizon", false);
            gridShader.setVec3("u_color", 0.0f, 0.85f, 1.0f); // glowing light blue/cyan lines
            
            glLineWidth(1.0f);
            spacetimeGrid.drawGrid();

            // 2. Draw Horizon Sphere at the bottom of the funnel
            // The throat is at z_bottom = 2 * sqrt(Rs) * (sqrt(Rs - Rs) - sqrt(Rmax - Rs))
            //                        = -2 * sqrt(Rs) * sqrt(Rmax - Rs)
            float z_bottom = -2.0f * std::sqrt(Rs) * std::sqrt(Rmax - Rs);
            
            float modelHorizon[16];
            mat4_translate(modelHorizon, 0.0f, 0.0f, z_bottom);
            
            gridShader.setMat4("u_model", modelHorizon);
            gridShader.setBool("u_is_horizon", true);
            gridShader.setVec3("u_light_pos", cameraPos.x, cameraPos.y, cameraPos.z + 5.0f);
            gridShader.setVec3("u_view_pos", cameraPos.x, cameraPos.y, cameraPos.z);
            
            spacetimeGrid.drawHorizon();

            // 3. Draw Orbiting/Falling Particles sliding down the grid
            glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending for glowing particle sparks
            
            particleShader.use();
            particleShader.setMat4("u_proj", proj);
            particleShader.setMat4("u_view", view);
            
            particles.draw();
            
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Restore standard alpha blending
        }

        // Swap buffers and poll window events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean up
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// --- GLFW Callback Implementations ---

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    window_width = width;
    window_height = height;
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, true);
            break;
        case GLFW_KEY_1:
            current_mode = 0;
            std::cout << "Switched to Mode 1: Relativistic Ray-Traced Black Hole.\n";
            break;
        case GLFW_KEY_2:
            current_mode = 1;
            std::cout << "Switched to Mode 2: 3D Spacetime Curvature Grid.\n";
            break;
        case GLFW_KEY_G:
            enable_grid = !enable_grid;
            std::cout << "Equatorial Spacetime Grid Warp: " << (enable_grid ? "ENABLED" : "DISABLED") << "\n";
            break;
        case GLFW_KEY_K:
            enable_disk = !enable_disk;
            std::cout << "Accretion Disk: " << (enable_disk ? "ENABLED" : "DISABLED") << "\n";
            break;
        case GLFW_KEY_L:
            enable_halos = !enable_halos;
            std::cout << "Corona Halos: " << (enable_halos ? "ENABLED" : "DISABLED") << "\n";
            break;
        case GLFW_KEY_SPACE:
            simulation_paused = !simulation_paused;
            std::cout << "Simulation " << (simulation_paused ? "PAUSED" : "RESUMED") << "\n";
            break;
        case GLFW_KEY_H:
            print_controls();
            break;
        default:
            break;
    }
}

void process_continuous_input(GLFWwindow* window, float dt) {
    float orbit_speed = 1.3f;
    float zoom_speed = 8.0f;

    // Orbit Left/Right (Azimuthal phi)
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        camera_phi -= orbit_speed * dt;
        if (camera_phi < -2.0f * (float)M_PI) camera_phi += 2.0f * (float)M_PI;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        camera_phi += orbit_speed * dt;
        if (camera_phi > 2.0f * (float)M_PI) camera_phi -= 2.0f * (float)M_PI;
    }

    // Orbit Up/Down (Polar theta)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        camera_theta -= orbit_speed * dt;
        if (camera_theta < 0.02f) camera_theta = 0.02f; // prevent flipping at north pole
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        camera_theta += orbit_speed * dt;
        if (camera_theta > (float)M_PI - 0.02f) camera_theta = (float)M_PI - 0.02f; // prevent flipping at south pole
    }

    // Zoom (Q = zoom out, E = zoom in)
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        camera_distance += zoom_speed * dt;
        if (camera_distance > 40.0f) camera_distance = 40.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        camera_distance -= zoom_speed * dt;
        float min_dist = 1.5f * mass;
        if (camera_distance < min_dist) camera_distance = min_dist;
    }

    // Mass adjustments (M = increase, N = decrease)
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        mass += 0.4f * dt;
        if (mass > 4.5f) mass = 4.5f;
        std::cout << "Mass: " << mass << " (Horizon Radius Rs = " << 2.0f * mass << ")\n";
    }
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
        mass -= 0.4f * dt;
        if (mass < 0.25f) mass = 0.25f;
        std::cout << "Mass: " << mass << " (Horizon Radius Rs = " << 2.0f * mass << ")\n";
    }

    // Accretion disk intensity adjustments (U = hotter/brighter, J = dimmer)
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) {
        disk_intensity += 1.0f * dt;
        if (disk_intensity > 5.0f) disk_intensity = 5.0f;
        std::cout << "Disk Intensity: " << disk_intensity << "\n";
    }
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) {
        disk_intensity -= 1.0f * dt;
        if (disk_intensity < 0.0f) disk_intensity = 0.0f;
        std::cout << "Disk Intensity: " << disk_intensity << "\n";
    }
}

void print_controls() {
    std::cout << "=========================================================\n";
    std::cout << "         RELATIVISTIC BLACK HOLE 3D SIMULATION           \n";
    std::cout << "=========================================================\n";
    std::cout << " Controls:\n";
    std::cout << "  [1]           : Mode 1 - Relativistic Ray-Traced View\n";
    std::cout << "  [2]           : Mode 2 - 3D Spacetime Curvature Grid\n";
    std::cout << "  [W/S] / [Up/Dn]: Orbit camera vertically (Polar angle)\n";
    std::cout << "  [A/D] / [Lt/Rt]: Orbit camera horizontally (Azimuth)\n";
    std::cout << "  [Q/E]         : Zoom Out / Zoom In\n";
    std::cout << "  [M/N]         : Increase / Decrease Black Hole Mass\n";
    std::cout << "  [U/J]         : Increase / Decrease Accretion Disk Intensity\n";
    std::cout << "  [K]           : Toggle Accretion Disk rendering\n";
    std::cout << "  [L]           : Toggle Corona Halos rendering\n";
    std::cout << "  [G]           : Toggle Warped Spacetime Grid (Mode 1 only)\n";
    std::cout << "  [Space]       : Pause / Resume simulation animation\n";
    std::cout << "  [H]           : Print this help message\n";
    std::cout << "  [ESC]         : Close application\n";
    std::cout << "=========================================================\n";
}
