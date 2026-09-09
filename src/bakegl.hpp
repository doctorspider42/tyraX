#pragma once
#include <GLFW/glfw3.h>
#include <string>

// Shared by GPU GI and impostor capture. Create on the main thread; each baker
// owns a separate context, so a background GI bake cannot race a capture.
namespace bakegl {
inline GLFWwindow* create(int major, int minor, const char* name, std::string& why) {
    if (!glfwInit()) { why = "GLFW could not initialise"; return nullptr; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(1, 1, name, nullptr, nullptr);
    glfwDefaultWindowHints();
    if (!win) why = "Cannot create OpenGL " + std::to_string(major) + "." + std::to_string(minor) + " context";
    return win;
}
struct ScopedCurrent {
    GLFWwindow* prev;
    explicit ScopedCurrent(GLFWwindow* win) : prev(glfwGetCurrentContext()) { glfwMakeContextCurrent(win); }
    ~ScopedCurrent() { glfwMakeContextCurrent(prev); }
    ScopedCurrent(const ScopedCurrent&) = delete;
    ScopedCurrent& operator=(const ScopedCurrent&) = delete;
};
}
