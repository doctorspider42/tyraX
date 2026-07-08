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
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
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
#define GL_MULTISAMPLE 0x809D
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_FRONT_AND_BACK 0x0408
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_REPEAT 0x2901
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803

#define TYRA_GL_FUNCS(X) \
    X(void, Clear, GLbitfield) \
    X(void, ClearColor, GLclampf, GLclampf, GLclampf, GLclampf) \
    X(void, Enable, GLenum) \
    X(void, Disable, GLenum) \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei) \
    X(void, DepthFunc, GLenum) \
    X(void, LineWidth, GLfloat) \
    X(void, BlendFunc, GLenum, GLenum) \
    X(void, PolygonOffset, GLfloat, GLfloat) \
    X(void, PolygonMode, GLenum, GLenum) \
    X(void, GenTextures, GLsizei, GLuint*) \
    X(void, DeleteTextures, GLsizei, const GLuint*) \
    X(void, BindTexture, GLenum, GLuint) \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
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
    X(void, Uniform1i, GLint, GLint) \
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
    X(GLenum, CheckFramebufferStatus, GLenum)

#define TYRA_GL_DECLARE(ret, name, ...) \
    typedef ret (*PFN_gl##name)(__VA_ARGS__); \
    extern PFN_gl##name gl##name;

TYRA_GL_FUNCS(TYRA_GL_DECLARE)
#undef TYRA_GL_DECLARE

// Loads all pointers via glfwGetProcAddress. Returns false if any is missing.
bool glInit();
