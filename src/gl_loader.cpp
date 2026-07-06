#include "gl_loader.h"

#include <GLFW/glfw3.h>

#define TYRA_GL_DEFINE(ret, name, ...) PFN_gl##name gl##name = nullptr;
TYRA_GL_FUNCS(TYRA_GL_DEFINE)
#undef TYRA_GL_DEFINE

bool glInit() {
    bool ok = true;
#define TYRA_GL_LOAD(ret, name, ...) \
    gl##name = (PFN_gl##name)glfwGetProcAddress("gl" #name); \
    if (!gl##name) ok = false;
    TYRA_GL_FUNCS(TYRA_GL_LOAD)
#undef TYRA_GL_LOAD
    return ok;
}
