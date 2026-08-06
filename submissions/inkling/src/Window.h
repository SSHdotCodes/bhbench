#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glew.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();
    bool shouldClose() const;
    void swapBuffers() const;
    void pollEvents() const;
    GLFWwindow* getGLFWWindow() const;
    int getWidth() const;
    int getHeight() const;
private:
    GLFWwindow* m_window;
    int m_width, m_height;
};
