#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <math.h>

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

const float rs = 1.0;
const float rs_isotropic = rs / 4.0;
const float max_dist = 100.0;
const int max_steps = 200;

const float disk_in = 3.0 * rs;
const float disk_out = 10.0 * rs;

uniform vec3 camPos;
uniform vec3 camFront;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float time;
uniform int showGrid;

float refractiveIndex(float rho) {
    float m = rs_isotropic;
    if (rho <= m) return 1e5;
    float ratio = m / rho;
    float n = pow(1.0 + ratio, 3.0) / pow(1.0 - ratio, 2.0);
    return n;
}

vec3 refractiveGradient(vec3 pos) {
    float rho = length(pos);
    if (rho <= rs_isotropic) return vec3(0.0);
    float m = rs_isotropic;
    float ratio = m / rho;
    float dn_drho = (m / (rho * rho)) * pow(1.0 + ratio, 2.0) * pow(1.0 - ratio, -3.0) * (-1.0 + 5.0 * ratio);
    return dn_drho * (pos / rho);
}

vec3 getStarField(vec3 dir) {
    vec3 c = vec3(0.01, 0.01, 0.03);
    float n1 = fract(sin(dot(dir.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float n2 = fract(sin(dot(dir.xy + dir.z, vec2(34.56, 87.1))) * 12345.6789);
    if (n1 > 0.98) c += vec3(0.7, 0.8, 1.0) * pow(n1, 4.0);
    if (n2 > 0.99) c += vec3(1.0, 0.9, 0.8) * pow(n2, 3.0);
    return c;
}

vec3 getDiskEmission(vec3 pos, vec3 rayDir) {
    if (abs(pos.z) > 0.3) return vec3(0.0);
    float rho = length(pos.xy);
    if (rho < disk_in || rho > disk_out) return vec3(0.0);
    float T = pow(rho / disk_in, -0.75);
    float v = sqrt(rs / (2.0 * rho));
    vec3 diskPos = vec3(pos.x, pos.y, 0.0);
    vec3 diskVel = normalize(cross(vec3(0.0, 0.0, 1.0), diskPos));
    diskVel *= v;
    vec3 viewDir = -normalize(rayDir);
    float gamma = 1.0 / sqrt(max(0.01, 1.0 - v*v));
    float doppler = 1.0 / (gamma * (1.0 - dot(viewDir, diskVel)));
    doppler = pow(max(0.0, doppler), 3.0);
    vec3 color = mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 0.9, 0.7), T);
    color *= T * doppler * 5.0;
    return color;
}

void main() {
    vec2 uv = (gl_FragCoord.xy / vec2(800.0, 600.0)) * 2.0 - 1.0;
    uv.x *= 800.0/600.0;

    vec3 rayDir = normalize(camFront + uv.x * camRight + uv.y * camUp);
    vec3 rayPos = camPos;

    vec3 finalColor = vec3(0.0);
    float diskOpacity = 0.0;

    for(int i = 0; i < max_steps; i++) {
        float rho = length(rayPos);
        if (rho < rs) {
            finalColor = vec3(0.0);
            break;
        }
        if (rho > max_dist) {
            finalColor = getStarField(rayDir);
            break;
        }

        float n = refractiveIndex(rho);
        vec3 gradN = refractiveGradient(rayPos);
        vec3 u = rayDir;
        float dot_grad = dot(u, gradN);
        vec3 du_ds = (1.0/n) * (gradN - dot_grad * u);
        float stepSize = max(0.05 * rho, 0.02);
        rayDir += du_ds * stepSize;
        rayDir = normalize(rayDir);
        rayPos += rayDir * stepSize;

        if (abs(rayPos.z) < 0.3 && diskOpacity < 0.95) {
            vec3 diskEm = getDiskEmission(rayPos, rayDir);
            if (dot(diskEm, diskEm) > 0.01) {
                float alpha = 0.3 * (1.0 - diskOpacity);
                finalColor = finalColor * (1.0 - alpha) + diskEm * alpha;
                diskOpacity += alpha;
            }
        }
    }

    FragColor = vec4(finalColor, 1.0);
}
)";

struct Vec3 {
    float x, y, z;
};

std::vector<float> generateGridMesh(float rs) {
    std::vector<float> vertices;
    float rs_iso = rs / 4.0;
    
    auto distort = [&](float x, float y, float z) {
        float rho = sqrt(x*x + y*y + z*z);
        if (rho < rs_iso) rho = rs_iso;
        float psi = 1.0 + rs_iso / rho;
        float dist = psi * psi;
        return Vec3{x * dist, y * dist, z * dist};
    };

    float range = 12.0;
    float step = 1.0;

    for (float z = -range; z <= range; z += step) {
        for (float y = -range; y <= range; y += step) {
            if (abs(y) < 0.01 && abs(z) < 0.01) continue;
            Vec3 p1 = distort(-range, y, z);
            Vec3 p2 = distort(range, y, z);
            vertices.push_back(p1.x); vertices.push_back(p1.y); vertices.push_back(p1.z);
            vertices.push_back(p2.x); vertices.push_back(p2.y); vertices.push_back(p2.z);
        }
    }

    for (float z = -range; z <= range; z += step) {
        for (float x = -range; x <= range; x += step) {
            if (abs(x) < 0.01 && abs(z) < 0.01) continue;
            Vec3 p1 = distort(x, -range, z);
            Vec3 p2 = distort(x, range, z);
            vertices.push_back(p1.x); vertices.push_back(p1.y); vertices.push_back(p1.z);
            vertices.push_back(p2.x); vertices.push_back(p2.y); vertices.push_back(p2.z);
        }
    }

    for (float y = -range; y <= range; y += step) {
        for (float x = -range; x <= range; x += step) {
            if (abs(x) < 0.01 && abs(y) < 0.01) continue;
            Vec3 p1 = distort(x, y, -range);
            Vec3 p2 = distort(x, y, range);
            vertices.push_back(p1.x); vertices.push_back(p1.y); vertices.push_back(p1.z);
            vertices.push_back(p2.x); vertices.push_back(p2.y); vertices.push_back(p2.z);
        }
    }

    return vertices;
}

unsigned int compileShader(unsigned int type, const std::string& source) {
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        int length;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> message(length);
        glGetShaderInfoLog(id, length, &length, message.data());
        std::cerr << "Shader compilation failed for type " << type << "\n" << &message[0] << std::endl;
        glDeleteShader(id);
        return 0;
    }
    return id;
}

unsigned int createShader(const std::string& vertexShader, const std::string& fragmentShader) {
    unsigned int program = glCreateProgram();
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vertexShader);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader);

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glValidateProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

float camPosArr[3] = {0.0f, 2.0f, 10.0f};
float camFrontArr[3] = {0.0f, -0.1f, -1.0f};
float camRightArr[3] = {1.0f, 0.0f, 0.0f};
float camUpArr[3] = {0.0f, 1.0f, 0.0f};

double lastX = 400, lastY = 300;
bool firstMouse = true;
float yaw = -90.0f, pitch = -5.0f;
float sensitivity = 0.1f;
bool cameraControlEnabled = true;

void updateCamera() {
    float frontX = cos(yaw * 3.14159265f / 180.0f) * cos(pitch * 3.14159265f / 180.0f);
    float frontY = sin(pitch * 3.14159265f / 180.0f);
    float frontZ = sin(yaw * 3.14159265f / 180.0f) * cos(pitch * 3.14159265f / 180.0f);
    camFrontArr[0] = frontX; camFrontArr[1] = frontY; camFrontArr[2] = frontZ;

    float rightX = camFrontArr[2];
    float rightZ = -camFrontArr[0];
    camRightArr[0] = rightX; camRightArr[1] = 0.0f; camRightArr[2] = rightZ;
}

void cursorCallback(GLFWwindow* win, double xpos, double ypos) {
    if (!cameraControlEnabled) return;

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    xoffset *= sensitivity;
    yoffset *= sensitivity;
    yaw += xoffset;
    pitch += yoffset;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Black Hole Simulation - Gravitational Lensing & Accretion Disk", NULL, NULL);
    if (!window) {
        std::cerr << "GLFW window creation failed" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    unsigned int shaderProgram = createShader(vertexShaderSource, fragmentShaderSource);

    float quadVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    std::vector<float> gridVertices = generateGridMesh(1.0f);
    unsigned int gridVAO, gridVBO;
    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glfwSetCursorPosCallback(window, cursorCallback);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            if (cameraControlEnabled) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                cameraControlEnabled = false;
            } else {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cameraControlEnabled = true;
                firstMouse = true;
                glfwGetCursorPos(win, &lastX, &lastY);
            }
        }
    });

    bool showGrid = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float currentSpeed = 0.05f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            camPosArr[0] += camFrontArr[0] * currentSpeed;
            camPosArr[1] += camFrontArr[1] * currentSpeed;
            camPosArr[2] += camFrontArr[2] * currentSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            camPosArr[0] -= camFrontArr[0] * currentSpeed;
            camPosArr[1] -= camFrontArr[1] * currentSpeed;
            camPosArr[2] -= camFrontArr[2] * currentSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            camPosArr[0] -= camRightArr[0] * currentSpeed;
            camPosArr[1] -= camRightArr[1] * currentSpeed;
            camPosArr[2] -= camRightArr[2] * currentSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            camPosArr[0] += camRightArr[0] * currentSpeed;
            camPosArr[1] += camRightArr[1] * currentSpeed;
            camPosArr[2] += camRightArr[2] * currentSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            camPosArr[1] += currentSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            camPosArr[1] -= currentSpeed;
        }

        updateCamera();
        glfwGetCursorPos(window, &lastX, &lastY);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glUseProgram(shaderProgram);
        glUniform3f(glGetUniformLocation(shaderProgram, "camPos"), camPosArr[0], camPosArr[1], camPosArr[2]);
        glUniform3f(glGetUniformLocation(shaderProgram, "camFront"), camFrontArr[0], camFrontArr[1], camFrontArr[2]);
        glUniform3f(glGetUniformLocation(shaderProgram, "camRight"), camRightArr[0], camRightArr[1], camRightArr[2]);
        glUniform3f(glGetUniformLocation(shaderProgram, "camUp"), camUpArr[0], camUpArr[1], camUpArr[2]);
        glUniform1f(glGetUniformLocation(shaderProgram, "time"), (float)glfwGetTime());
        glUniform1i(glGetUniformLocation(shaderProgram, "showGrid"), showGrid ? 1 : 0);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (showGrid) {
            unsigned int gridShaderProg = glCreateProgram();
            unsigned int gvs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
            const char* gfsSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.2, 0.4, 0.8, 0.3);
}
)";
            unsigned int gfs = compileShader(GL_FRAGMENT_SHADER, gfsSource);
            glAttachShader(gridShaderProg, gvs);
            glAttachShader(gridShaderProg, gfs);
            glLinkProgram(gridShaderProg);
            glDeleteShader(gvs);
            glDeleteShader(gfs);

            glUseProgram(gridShaderProg);
            glBindVertexArray(gridVAO);
            glDisable(GL_DEPTH_TEST);
            glLineWidth(1.5f);
            glDrawArrays(GL_LINES, 0, gridVertices.size() / 3);
            glEnable(GL_DEPTH_TEST);

            glDeleteProgram(gridShaderProg);
        }

        if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
            showGrid = !showGrid;
        }

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);
    glfwTerminate();
    return 0;
}
