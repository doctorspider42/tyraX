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

void glUploadTexRgba(int width, int height, const void* rgba) {
    if (width < 1 || height < 1) return;
    // Allocate empty, then fill - see the header for why this is not one call.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    if (rgba)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}
