#include <GLFW/glfw3.h>
#include "shader.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <vector>

struct Camera {
    float yaw   = -90.0f;
    float pitch = -20.0f;
    float distance = 18.0f;
    glm::vec3 front, right, up;

    void updateVectors() {
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front   = glm::normalize(front);
        right   = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        up      = glm::normalize(glm::cross(right, front));
    }

    glm::mat4 viewMatrix() const {
        return glm::lookAt(glm::vec3(0.0f), front * distance, up);
    }
};

Camera camera;

float lastX = 800.0f / 2.0f;
float lastY = 600.0f / 2.0f;
bool firstMouse = true;

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {}

void mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoff = xpos - lastX, yoff = lastY - ypos;
    lastX = xpos; lastY = ypos;
    float sensitivity = 0.3f;
    camera.yaw   += xoff * sensitivity;
    camera.pitch += yoff * sensitivity;
    if (camera.pitch > 89.0f) camera.pitch = 89.0f;
    if (camera.pitch < -89.0f) camera.pitch = -89.0f;
    camera.updateVectors();
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    camera.distance += static_cast<float>(yoffset) * 2.0f;
    if (camera.distance < 6.0f) camera.distance = 6.0f;
    if (camera.distance > 50.0f) camera.distance = 50.0f;
}

void processInput(GLFWwindow* window, float& dt) {
    const float speed = 8.0f * dt;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.front += camera.up * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.front -= camera.up * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.front -= camera.right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.front += camera.right * speed;
}

static const int FBO_W = 1280, FBO_H = 720;

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Black Hole Simulation", nullptr, nullptr);
    if (!window) { std::cerr << "GLFW window creation failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    camera.updateVectors(); // initialize view vectors after context creation

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glViewport(0, 0, FBO_W, FBO_H);
    glEnable(GL_DEPTH_TEST);

    // ---- FBO for bloom post-processing ----
    unsigned int fboMain, fbobloom, fboBlur1, fboBlur2;
    glGenFramebuffers(1, &fboMain);
    glBindFramebuffer(GL_FRAMEBUFFER, fboMain);
    unsigned int texColorMain;
    glGenTextures(1, &texColorMain);
    glBindTexture(GL_TEXTURE_2D, texColorMain);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, FBO_W, FBO_H, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColorMain, 0);

    glGenFramebuffers(1, &fbobloom);
    glBindFramebuffer(GL_FRAMEBUFFER, fbobloom);
    unsigned int texBloom;
    glGenTextures(1, &texBloom);
    glBindTexture(GL_TEXTURE_2D, texBloom);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, FBO_W / 2, FBO_H / 2, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texBloom, 0);

    glGenFramebuffers(1, &fboBlur1);
    glBindFramebuffer(GL_FRAMEBUFFER, fboBlur1);
    unsigned int texBlur1;
    glGenTextures(1, &texBlur1);
    glBindTexture(GL_TEXTURE_2D, texBlur1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, FBO_W / 4, FBO_H / 4, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texBlur1, 0);

    glGenFramebuffers(1, &fboBlur2);
    glBindFramebuffer(GL_FRAMEBUFFER, fboBlur2);
    unsigned int texBlur2;
    glGenTextures(1, &texBlur2);
    glBindTexture(GL_TEXTURE_2D, texBlur2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, FBO_W / 8, FBO_H / 8, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texBlur2, 0);

    // ---- Shaders ----
    Shader shaderGrid("shaders/grid.vert", "shaders/grid.frag");
    Shader shaderBlackhole("shaders/blackhole.vert", "shaders/blackhole.frag");
    Shader shaderBloom("shaders/bloom.vert", "shaders/bloom.frag");

    // Fullscreen quad for post-processing
    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    unsigned int VAOquad, VBOquad;
    glGenVertexArrays(1, &VAOquad);
    glBindVertexArray(VAOquad);
    glGenBuffers(1, &VBOquad);
    glBindBuffer(GL_ARRAY_BUFFER, VBOquad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Spacetime grid mesh - create a large plane for the curvature visualization
    const int GRID_RES = 120;
    std::vector<float> gridVerts, gridUVs;
    for (int j = 0; j <= GRID_RES; ++j) {
        for (int i = 0; i <= GRID_RES; ++i) {
            float u = static_cast<float>(i) / GRID_RES;
            float v = static_cast<float>(j) / GRID_RES;
            float x = (u - 0.5f) * 60.0f;
            float z = (v - 0.5f) * 60.0f;
            gridVerts.push_back(x); gridVerts.push_back(-8.0f); gridVerts.push_back(z);
            gridUVs.push_back(u);   gridUVs.push_back(v);
        }
    }
    std::vector<unsigned int> gridIndices;
    for (int j = 0; j < GRID_RES; ++j) {
        for (int i = 0; i < GRID_RES; ++i) {
            unsigned int a = j * (GRID_RES + 1) + i;
            unsigned int b = a + GRID_RES + 1;
            gridIndices.push_back(a); gridIndices.push_back(b); gridIndices.push_back(a + 1);
            gridIndices.push_back(a + 1); gridIndices.push_back(b); gridIndices.push_back(b + 1);
        }
    }

    unsigned int VAOgrid, VBOgrid, EBOgrid;
    glGenVertexArrays(1, &VAOgrid);
    glBindVertexArray(VAOgrid);
    glGenBuffers(1, &VBOgrid);
    glBindBuffer(GL_ARRAY_BUFFER, VBOgrid);
    glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &EBOgrid);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOgrid);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, gridIndices.size() * sizeof(unsigned int), gridIndices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(3 * sizeof(float)));

    unsigned int VAOgridLine;
    glGenVertexArrays(1, &VAOgridLine);
    glBindVertexArray(VAOgridLine);
    glBindBuffer(GL_ARRAY_BUFFER, VBOgrid);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOgrid);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // ---- Render loop ----
    float dt = 0.0f, prevTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        float curTime = glfwGetTime();
        dt = curTime - prevTime;
        prevTime = curTime;

        processInput(window, dt);

        // --- Pass 1: Render scene to FBO (bloom + regular) ---
        glBindFramebuffer(GL_FRAMEBUFFER, fboMain);
        glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Spacetime grid (behind black hole)
        shaderGrid.use();
        glm::mat4 proj = glm::perspective(glm::radians(65.0f),
            static_cast<float>(FBO_W) / FBO_H, 0.1f, 200.0f);
        glm::mat4 view = camera.viewMatrix();

        shaderGrid.setMat4("view", view);
        shaderGrid.setMat4("projection", proj);
        shaderGrid.setVec3("cameraPos",
            camera.front.x * camera.distance,
            camera.front.y * camera.distance,
            camera.front.z * camera.distance);
        shaderGrid.setFloat("time", curTime);

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(VAOgrid);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gridIndices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        // Accretion disk via ray-traced fullscreen pass (rendered into main FBO)
        shaderBlackhole.use();
        shaderBlackhole.setMat4("view", view);
        shaderBlackhole.setMat4("projection", proj);
        shaderBlackhole.setVec3("cameraPos",
            camera.front.x * camera.distance,
            camera.front.y * camera.distance,
            camera.front.z * camera.distance);
        shaderBlackhole.setFloat("time", curTime);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindVertexArray(VAOquad);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        // --- Pass 2: Downsample + bloom H blur (half res) from main scene --> fbobloom ---
        glBindFramebuffer(GL_FRAMEBUFFER, fbobloom);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderBloom.use();
        shaderBloom.setInt("direction", 1);
        shaderBloom.setVec2("texelSize", 1.0f / (FBO_W / 2), 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texColorMain);
        glBindVertexArray(VAOquad);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Pass 3: Bloom V blur (quarter res) --> fboBlur1 ---
        glBindFramebuffer(GL_FRAMEBUFFER, fboBlur1);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderBloom.use();
        shaderBloom.setInt("direction", 0);
        shaderBloom.setVec2("texelSize", 0.0f, 1.0f / (FBO_H / 4));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBloom);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Pass 4: Bloom H blur (eighth res) --> fboBlur2 ---
        glBindFramebuffer(GL_FRAMEBUFFER, fboBlur2);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderBloom.use();
        shaderBloom.setInt("direction", 1);
        shaderBloom.setVec2("texelSize", 1.0f / (FBO_W / 8), 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBlur1);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Pass 5: Bloom V blur back to quarter res --> fboBlur1 ---
        glBindFramebuffer(GL_FRAMEBUFFER, fboBlur1);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderBloom.use();
        shaderBloom.setInt("direction", 0);
        shaderBloom.setVec2("texelSize", 0.0f, 1.0f / (FBO_H / 4));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBlur2);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Pass 6: Composite bloom + scene --> fbobloom (half res) ---
        glBindFramebuffer(GL_FRAMEBUFFER, fbobloom);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderBloom.use();
        shaderBloom.setInt("direction", -1); // composite mode
        glBindTexture(GL_TEXTURE_2D, texColorMain);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Final: blit fbobloom to default framebuffer (window) ---
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbobloom);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, FBO_W / 2, FBO_H / 2, 0, 0, FBO_W, FBO_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &VAOquad); glDeleteBuffers(1, &VBOquad);
    glDeleteVertexArrays(1, &VAOgrid); glDeleteVertexArrays(1, &VAOgridLine);
    glDeleteBuffers(1, &VBOgrid); glDeleteBuffers(1, &EBOgrid);
    glDeleteTextures(5, &texColorMain);
    glfwTerminate();
    return 0;
}
