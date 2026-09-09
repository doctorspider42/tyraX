// Minimal OpenGL 3.3 function loader - only what the editor viewport needs.
// Loaded via glfwGetProcAddress after context creation (glInit()).
#pragma once

#include <cstddef>
#include <cstdint>

// Base GL types
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_LEQUAL 0x0203
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
// PS2-shading viewport mode: the per-vertex lighting pass runs in a geometry
// shader (docs/ps2-viewport.md) - core since GL 3.2, no extension needed.
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_LINE_SMOOTH 0x0B20
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
// Additive blending, the GS's Cs*FIX + Cd - the day/night sun disc.
#define GL_ONE 1
#define GL_MULTISAMPLE 0x809D
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_FRONT_AND_BACK 0x0408
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_RED 0x1903
#define GL_R32F 0x822E
#define GL_RGB 0x1907
#define GL_NEAREST 0x2600
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9

#define TYRA_GL_FUNCS(X) \
    X(void, Clear, GLbitfield) \
    X(void, ClearColor, GLclampf, GLclampf, GLclampf, GLclampf) \
    X(void, Enable, GLenum) \
    X(void, Disable, GLenum) \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei) \
    X(void, DepthFunc, GLenum) \
    X(void, DepthMask, GLboolean) \
    X(void, LineWidth, GLfloat) \
    X(void, BlendFunc, GLenum, GLenum) \
    X(void, PolygonOffset, GLfloat, GLfloat) \
    X(void, PolygonMode, GLenum, GLenum) \
    X(void, GenTextures, GLsizei, GLuint*) \
    X(void, DeleteTextures, GLsizei, const GLuint*) \
    X(void, BindTexture, GLenum, GLuint) \
    X(void, ActiveTexture, GLenum) \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, TexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
    X(void, TexImage3D, GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, TexSubImage3D, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*) \
    X(void, CopyTexImage2D, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) \
    X(void, TexParameteri, GLenum, GLenum, GLint) \
    X(void, DrawArrays, GLenum, GLint, GLsizei) \
    X(void, DrawElements, GLenum, GLsizei, GLenum, const void*) \
    X(GLuint, CreateShader, GLenum) \
    X(void, DeleteShader, GLuint) \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
    X(void, CompileShader, GLuint) \
    X(void, GetShaderiv, GLuint, GLenum, GLint*) \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    X(GLuint, CreateProgram, void) \
    X(void, DeleteProgram, GLuint) \
    X(void, AttachShader, GLuint, GLuint) \
    X(void, LinkProgram, GLuint) \
    X(void, GetProgramiv, GLuint, GLenum, GLint*) \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    X(void, UseProgram, GLuint) \
    X(GLint, GetUniformLocation, GLuint, const GLchar*) \
    X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*) \
    X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void, Uniform4fv, GLint, GLsizei, const GLfloat*) \
    X(void, Uniform3f, GLint, GLfloat, GLfloat, GLfloat) \
    X(void, Uniform2f, GLint, GLfloat, GLfloat) \
    X(void, Uniform1f, GLint, GLfloat) \
    X(void, Uniform1fv, GLint, GLsizei, const GLfloat*) \
    X(void, Uniform1i, GLint, GLint) \
    X(void, Uniform3i, GLint, GLint, GLint, GLint) \
    X(void, Uniform1iv, GLint, GLsizei, const GLint*) \
    X(void, GenVertexArrays, GLsizei, GLuint*) \
    X(void, DeleteVertexArrays, GLsizei, const GLuint*) \
    X(void, BindVertexArray, GLuint) \
    X(void, GenBuffers, GLsizei, GLuint*) \
    X(void, DeleteBuffers, GLsizei, const GLuint*) \
    X(void, BindBuffer, GLenum, GLuint) \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum) \
    X(void, EnableVertexAttribArray, GLuint) \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
    X(void, GenFramebuffers, GLsizei, GLuint*) \
    X(void, DeleteFramebuffers, GLsizei, const GLuint*) \
    X(void, BindFramebuffer, GLenum, GLuint) \
    X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) \
    X(void, GenRenderbuffers, GLsizei, GLuint*) \
    X(void, DeleteRenderbuffers, GLsizei, const GLuint*) \
    X(void, BindRenderbuffer, GLenum, GLuint) \
    X(void, RenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei) \
    X(void, FramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint) \
    X(GLenum, CheckFramebufferStatus, GLenum) \
    X(void, BlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) \
    X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) \
    X(void, PixelStorei, GLenum, GLint)

#define TYRA_GL_DECLARE(ret, name, ...) \
    typedef ret (*PFN_gl##name)(__VA_ARGS__); \
    extern PFN_gl##name gl##name;

TYRA_GL_FUNCS(TYRA_GL_DECLARE)
#undef TYRA_GL_DECLARE

// Loads all pointers via glfwGetProcAddress. Returns false if any is missing.
bool glInit();

// Fills the currently bound GL_TEXTURE_2D with RGBA8 pixels (pass nullptr to
// only allocate) and sets the editor's standard LINEAR/REPEAT sampling.
//
// It deliberately allocates the level empty and then fills it with
// glTexSubImage2D instead of handing the pixels straight to glTexImage2D:
// the single-call form segfaults inside the AMD GL driver (atio6axx.dll,
// 0xc0000005 at a fixed offset) on at least one setup, reproducibly and with
// entirely valid arguments - 128x128 RGBA8, power-of-two, non-null data. The
// allocate-then-fill path does not fault. **Route every RGBA texture upload
// through here** rather than calling glTexImage2D with data directly.
void glUploadTexRgba(int width, int height, const void* rgba);
