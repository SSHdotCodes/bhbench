// Real-time Schwarzschild null-geodesic visualizer.
// All lengths are in gravitational units G = c = M = 1.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
  float x{}, y{}, z{};
  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(Vec3 v) {
  const float len = std::sqrt(std::max(dot(v, v), 1.0e-12f));
  return v * (1.0f / len);
}

struct Mat4 {
  std::array<float, 16> m{}; // Column-major OpenGL layout.
  static Mat4 perspective(float verticalFov, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 r{};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return r;
  }
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r{};
    r.m = {s.x, u.x, -f.x, 0.0f, s.y, u.y, -f.y, 0.0f,
           s.z, u.z, -f.z, 0.0f, -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0f};
    return r;
  }
};

Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
  return r;
}

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == GL_TRUE) return shader;
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
  glGetShaderInfoLog(shader, length, nullptr, log.data());
  std::cerr << "Shader compilation failed:\n" << log << '\n';
  std::exit(EXIT_FAILURE);
}

GLuint link(GLuint vertex, GLuint fragment) {
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok == GL_TRUE) return program;
  GLint length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
  glGetProgramInfoLog(program, length, nullptr, log.data());
  std::cerr << "Program link failed:\n" << log << '\n';
  std::exit(EXIT_FAILURE);
}

constexpr const char* kRayVertex = R"GLSL(#version 330 core
const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() {
  vec2 p = positions[gl_VertexID];
  uv = 0.5 * (p + 1.0);
  gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

// The integration variable is azimuth phi.  In Schwarzschild coordinates,
// u = 1/r follows u'' = 3u^2 - u for every null geodesic orbital plane.
constexpr const char* kRayFragment = R"GLSL(#version 330 core
in vec2 uv;
out vec4 fragment;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCamera;
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uFov;

const float M = 1.0;
const float HORIZON = 2.0;
const float ISCO = 6.0;
const float FAR_SPHERE = 95.0;
const int MAX_STEPS = 700;
const float DPHI = 0.012;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float hash21(vec2 p) {
  p = fract(p * vec2(234.34, 435.345));
  p += dot(p, p + 34.23);
  return fract(p.x * p.y);
}
float hash31(vec3 p) { return hash21(p.xy + hash21(p.yz)); }

vec3 starfield(vec3 d) {
  d = normalize(d);
  vec3 color = vec3(0.0012, 0.002, 0.006) + 0.012 * pow(max(d.y, 0.0), 2.0) * vec3(0.15, 0.25, 0.65);
  float longitude = atan(d.z, d.x);
  float latitude = asin(clamp(d.y, -1.0, 1.0));
  vec2 cellUv = vec2(longitude / (2.0 * 3.14159265), latitude / 3.14159265);
  vec2 cells = cellUv * vec2(1300.0, 660.0);
  vec2 id = floor(cells);
  vec2 f = fract(cells) - 0.5;
  float seed = hash21(id);
  float radius = mix(0.018, 0.13, pow(hash21(id + 17.0), 6.0));
  float star = 1.0 - smoothstep(radius, radius * 1.7, length(f));
  star *= step(0.992, seed);
  vec3 temperature = mix(vec3(1.0, 0.42, 0.18), vec3(0.35, 0.62, 1.0), hash21(id + 9.0));
  color += star * temperature * mix(0.6, 2.4, hash21(id + 3.0));
  // Dim, structured Milky Way band, evaluated on the escaped ray direction.
  float band = exp(-pow((d.y + 0.28 * sin(2.0 * longitude)) * 4.0, 2.0));
  color += band * vec3(0.009, 0.007, 0.014) * (0.3 + 0.7 * hash21(floor(cells * 0.12)));
  return color;
}

vec3 blackbodyApprox(float t) {
  // Compact artistic fit to the normalized 1,500--12,000 K blackbody locus.
  float hot = saturate((t - 0.25) / 0.75);
  return mix(vec3(1.0, 0.055, 0.003), vec3(1.0, 0.82, 0.35), smoothstep(0.05, 0.55, t))
       + 0.42 * hot * vec3(0.25, 0.44, 1.0);
}

float diskTurbulence(vec3 p, float r) {
  float a = sin(8.0 * atan(p.y, p.x) + 1.8 * log(r) - 0.7 * uTime);
  float b = sin(21.0 * atan(p.y, p.x) - 4.5 * log(r) + 1.2 * uTime);
  float speckle = hash31(floor(p * 8.0));
  return 0.58 + 0.25 * a + 0.12 * b + 0.20 * speckle;
}

vec3 diskEmission(vec3 p, vec3 photonTowardCamera) {
  float r = length(p.xy);
  float x = ISCO / max(r, ISCO);
  // Zero-torque, thin-disk-inspired radial heating profile with its peak outside ISCO.
  float temperature = pow(x, 0.75) * pow(max(1.0 - sqrt(ISCO / r), 0.0), 0.25);
  temperature *= 2.9;
  vec3 tangent = normalize(vec3(-p.y, p.x, 0.0));
  // Circular-geodesic velocity measured by a static Schwarzschild observer.
  float beta = sqrt(1.0 / max(r - 2.0, 1.0e-3));
  float gamma = inversesqrt(max(1.0 - beta * beta, 1.0e-4));
  float doppler = 1.0 / (gamma * max(1.0 - beta * dot(tangent, photonTowardCamera), 0.08));
  float gravitational = sqrt(max(1.0 - HORIZON / r, 0.0));
  float redshift = doppler * gravitational;
  float texture = diskTurbulence(p, r);
  float halo = exp(-0.65 * abs(p.z)) * exp(-0.025 * r);
  return blackbodyApprox(temperature * redshift) * texture * (0.70 + 2.0 * pow(redshift, 3.0)) * halo;
}

void derivatives(float u, float up, out float du, out float dup) {
  du = up;
  dup = 3.0 * M * u * u - u;
}

void rk4(inout float u, inout float up, float h) {
  float a, b, c, d, e, f, g, j;
  derivatives(u, up, a, b);
  derivatives(u + 0.5 * h * a, up + 0.5 * h * b, c, d);
  derivatives(u + 0.5 * h * c, up + 0.5 * h * d, e, f);
  derivatives(u + h * e, up + h * f, g, j);
  u += h * (a + 2.0 * c + 2.0 * e + g) / 6.0;
  up += h * (b + 2.0 * d + 2.0 * f + j) / 6.0;
}

void main() {
  vec2 screen = (2.0 * gl_FragCoord.xy - uResolution.xy) / uResolution.y;
  float focal = 1.0 / tan(0.5 * uFov);
  vec3 localRay = normalize(vec3(screen, -focal));
  vec3 ray = normalize(uRight * localRay.x + uUp * localRay.y + uForward * (-localRay.z));

  vec3 er = normalize(uCamera);
  float r0 = length(uCamera);
  float radialDirection = dot(ray, er); // Measured in the observer's local frame.
  vec3 tangentDirection = ray - radialDirection * er;
  float tangential = length(tangentDirection);
  if (tangential < 1.0e-5) {
    fragment = vec4(radialDirection < 0.0 ? vec3(0.0) : starfield(ray), 1.0);
    return;
  }
  vec3 ephi = tangentDirection / tangential;
  float lapse0 = sqrt(1.0 - HORIZON / r0);
  float impact = r0 * tangential / lapse0; // b = L/E
  float u = 1.0 / r0;
  float potential = max(2.0 * M * u * u * u - u * u + 1.0 / (impact * impact), 0.0);
  float up = (radialDirection > 0.0 ? -1.0 : 1.0) * sqrt(potential);
  float phi = 0.0;
  vec3 oldP = uCamera;
  vec3 color = vec3(0.0);
  bool escaped = false;
  bool hitDisk = false;

  for (int i = 0; i < MAX_STEPS; ++i) {
    rk4(u, up, DPHI);
    phi += DPHI;
    if (u <= 0.0 || u > 0.501) break; // u > 1/2 has crossed the horizon.
    float r = 1.0 / u;
    vec3 p = r * (cos(phi) * er + sin(phi) * ephi);
    // A thin equatorial disk: ray is tested after each curved geodesic step.
    if (oldP.z * p.z <= 0.0 && abs(oldP.z - p.z) > 1.0e-5) {
      float t = oldP.z / (oldP.z - p.z);
      vec3 crossing = mix(oldP, p, t);
      float diskRadius = length(crossing.xy);
      if (diskRadius >= ISCO && diskRadius <= 62.0) {
        vec3 pathDirection = normalize(p - oldP);
        color = diskEmission(crossing, -pathDirection);
        hitDisk = true;
        break;
      }
    }
    if (r > FAR_SPHERE && up < 0.0) {
      color = starfield(normalize(p));
      escaped = true;
      break;
    }
    oldP = p;
  }

  if (!escaped && !hitDisk) color = vec3(0.0);
  // Tone mapping retains the black shadow while controlling relativistic disk highlights.
  color = 1.0 - exp(-1.35 * color);
  color = pow(color, vec3(0.4545));
  fragment = vec4(color, 1.0);
}
)GLSL";

constexpr const char* kGridVertex = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPosition, 1.0); }
)GLSL";

constexpr const char* kGridFragment = R"GLSL(#version 330 core
out vec4 fragment;
void main() { fragment = vec4(0.18, 0.70, 1.0, 0.88); }
)GLSL";

struct App {
  GLFWwindow* window{};
  int width{1280};
  int height{800};
  GLuint rayProgram{};
  GLuint gridProgram{};
  GLuint emptyVao{};
  GLuint gridVao{};
  GLuint gridVbo{};
  GLsizei gridVertexCount{};
  float yaw{0.0f};
  float pitch{0.24f};
  float fov{48.0f * kPi / 180.0f};
  float distance{15.0f};
  bool dragging{};
  bool orbiting{true};
  bool showGrid{true};
  double lastMouseX{};
  double lastMouseY{};
};

void cursorCallback(GLFWwindow* window, double x, double y) {
  auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
  if (app->dragging) {
    app->yaw += static_cast<float>((x - app->lastMouseX) * 0.005);
    app->pitch = std::clamp(app->pitch + static_cast<float>((y - app->lastMouseY) * 0.005), -1.1f, 1.1f);
  }
  app->lastMouseX = x;
  app->lastMouseY = y;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
  auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
  if (button == GLFW_MOUSE_BUTTON_LEFT) app->dragging = action == GLFW_PRESS;
}

void scrollCallback(GLFWwindow* window, double, double yoffset) {
  auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
  app->fov = std::clamp(app->fov - static_cast<float>(yoffset) * 0.045f, 20.0f * kPi / 180.0f, 85.0f * kPi / 180.0f);
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
  if (action != GLFW_PRESS) return;
  auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
  if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
  if (key == GLFW_KEY_SPACE) app->orbiting = !app->orbiting;
  if (key == GLFW_KEY_G) app->showGrid = !app->showGrid;
}

void buildEmbeddingGrid(App& app) {
  std::vector<float> vertices;
  constexpr int radialLines = 32;
  constexpr int ringSegments = 96;
  constexpr int rings = 22;
  constexpr float rMin = 2.015f;
  constexpr float rMax = 24.0f;
  const auto point = [](float r, float angle) {
    // Flamm paraboloid: z = -2 sqrt(2M(r-2M)), M = 1.  The vertical axis is y.
    return Vec3{r * std::cos(angle), -2.0f * std::sqrt(2.0f * (r - 2.0f)), r * std::sin(angle)};
  };
  const auto add = [&vertices](Vec3 p) { vertices.insert(vertices.end(), {p.x, p.y, p.z}); };
  for (int i = 0; i < radialLines; ++i) {
    const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(radialLines);
    for (int j = 0; j < rings; ++j) {
      const float a = static_cast<float>(j) / rings;
      const float b = static_cast<float>(j + 1) / rings;
      add(point(rMin + (rMax - rMin) * a, angle));
      add(point(rMin + (rMax - rMin) * b, angle));
    }
  }
  for (int j = 0; j <= rings; ++j) {
    const float r = rMin + (rMax - rMin) * static_cast<float>(j) / rings;
    for (int i = 0; i < ringSegments; ++i) {
      const float a = 2.0f * kPi * static_cast<float>(i) / ringSegments;
      const float b = 2.0f * kPi * static_cast<float>(i + 1) / ringSegments;
      add(point(r, a));
      add(point(r, b));
    }
  }
  app.gridVertexCount = static_cast<GLsizei>(vertices.size() / 3);
  glGenVertexArrays(1, &app.gridVao);
  glGenBuffers(1, &app.gridVbo);
  glBindVertexArray(app.gridVao);
  glBindBuffer(GL_ARRAY_BUFFER, app.gridVbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  glBindVertexArray(0);
}

void drawEmbeddingGrid(const App& app, float time) {
  const int insetW = std::max(220, app.width * 32 / 100);
  const int insetH = std::max(180, app.height * 32 / 100);
  const int x = 20;
  const int y = 20;
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, insetW, insetH);
  glClearColor(0.002f, 0.006f, 0.014f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  glViewport(x, y, insetW, insetH);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(app.gridProgram);
  const float spin = 0.18f * std::sin(time * 0.18f);
  const Vec3 eye{31.0f * std::sin(0.72f + spin), 21.0f, 31.0f * std::cos(0.72f + spin)};
  const Mat4 projection = Mat4::perspective(42.0f * kPi / 180.0f, static_cast<float>(insetW) / insetH, 0.1f, 100.0f);
  const Mat4 view = Mat4::lookAt(eye, {0.0f, -4.8f, 0.0f}, {0.0f, 1.0f, 0.0f});
  glUniformMatrix4fv(glGetUniformLocation(app.gridProgram, "uMvp"), 1, GL_FALSE, (projection * view).m.data());
  glBindVertexArray(app.gridVao);
  glLineWidth(1.2f);
  glDrawArrays(GL_LINES, 0, app.gridVertexCount);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
}

Vec3 cameraPosition(const App& app) {
  return {app.distance * std::cos(app.pitch) * std::sin(app.yaw),
          app.distance * std::sin(app.pitch),
          app.distance * std::cos(app.pitch) * std::cos(app.yaw)};
}

void drawFrame(App& app, float time) {
  glfwGetFramebufferSize(app.window, &app.width, &app.height);
  if (app.width <= 0 || app.height <= 0) return;
  glViewport(0, 0, app.width, app.height);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  const Vec3 eye = cameraPosition(app);
  const Vec3 forward = normalize(Vec3{} - eye);
  const Vec3 worldUp{0.0f, 1.0f, 0.0f};
  const Vec3 right = normalize(cross(forward, worldUp));
  const Vec3 up = cross(right, forward);
  glUseProgram(app.rayProgram);
  const auto uniform = [&app](const char* name) { return glGetUniformLocation(app.rayProgram, name); };
  glUniform2f(uniform("uResolution"), static_cast<float>(app.width), static_cast<float>(app.height));
  glUniform1f(uniform("uTime"), time);
  glUniform3f(uniform("uCamera"), eye.x, eye.y, eye.z);
  glUniform3f(uniform("uForward"), forward.x, forward.y, forward.z);
  glUniform3f(uniform("uRight"), right.x, right.y, right.z);
  glUniform3f(uniform("uUp"), up.x, up.y, up.z);
  glUniform1f(uniform("uFov"), app.fov);
  glBindVertexArray(app.emptyVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  if (app.showGrid) drawEmbeddingGrid(app, time);
}

} // namespace

int main() {
  if (glfwInit() != GLFW_TRUE) {
    std::cerr << "Unable to initialize GLFW.\n";
    return EXIT_FAILURE;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  App app;
  app.window = glfwCreateWindow(app.width, app.height, "Schwarzschild Black Hole — GPU Geodesic Ray Tracer", nullptr, nullptr);
  if (!app.window) {
    std::cerr << "Unable to create an OpenGL window.\n";
    glfwTerminate();
    return EXIT_FAILURE;
  }
  glfwMakeContextCurrent(app.window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(app.window, &app);
  glfwSetCursorPosCallback(app.window, cursorCallback);
  glfwSetMouseButtonCallback(app.window, mouseButtonCallback);
  glfwSetScrollCallback(app.window, scrollCallback);
  glfwSetKeyCallback(app.window, keyCallback);

  app.rayProgram = link(compile(GL_VERTEX_SHADER, kRayVertex), compile(GL_FRAGMENT_SHADER, kRayFragment));
  app.gridProgram = link(compile(GL_VERTEX_SHADER, kGridVertex), compile(GL_FRAGMENT_SHADER, kGridFragment));
  glGenVertexArrays(1, &app.emptyVao);
  buildEmbeddingGrid(app);

  std::cout << "Controls: drag to orbit, scroll to zoom, Space pause orbit, G grid, Esc quit\n";
  double oldTime = glfwGetTime();
  while (!glfwWindowShouldClose(app.window)) {
    const double now = glfwGetTime();
    const float delta = static_cast<float>(std::min(now - oldTime, 0.1));
    oldTime = now;
    if (app.orbiting && !app.dragging) app.yaw += delta * 0.065f;
    drawFrame(app, static_cast<float>(now));
    glfwSwapBuffers(app.window);
    glfwPollEvents();
  }

  glDeleteBuffers(1, &app.gridVbo);
  glDeleteVertexArrays(1, &app.gridVao);
  glDeleteVertexArrays(1, &app.emptyVao);
  glDeleteProgram(app.rayProgram);
  glDeleteProgram(app.gridProgram);
  glfwDestroyWindow(app.window);
  glfwTerminate();
  return EXIT_SUCCESS;
}
