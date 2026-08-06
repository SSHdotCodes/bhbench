#include "Window.h"
#include <stdexcept>

Window::Window(int width, int height, const std::string& title)
    : m_window(nullptr), m_width(width), m_height(height) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // VSync for realtime stability
}

Window::~Window() {
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }
void Window::swapBuffers() const { glfwSwapBuffers(m_window); }
void Window::pollEvents() const { glfwPollEvents(); }
GLFWwindow* Window::getGLFWWindow() const { return m_window; }
int Window::getWidth() const { return m_width; }
int Window::getHeight() const { return m_height; }
