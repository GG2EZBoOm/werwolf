#pragma once

// Minimaler, selbst geschriebener OpenGL-3.3-Loader (ohne GLAD/GLEW).
// Funktionen aus OpenGL 1.1 kommen direkt aus opengl32.lib, alles Neuere
// wird nach dem Anlegen des Kontexts über wglGetProcAddress geladen.

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#include <cstddef>

using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

#ifndef APIENTRY
#define APIENTRY
#endif

#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_R8 0x8229
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#define GL_FUNC_ADD 0x8006
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_RGBA16F 0x881A
#define GL_RGB16F 0x881B
#define GL_R11F_G11F_B10F 0x8C3A
#define GL_HALF_FLOAT 0x140B
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_MAX_SAMPLES 0x8D57
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4

#define ENG_GL_FUNCTIONS(X)                                                                                        \
    X(GLuint, glCreateShader, GLenum type)                                                                         \
    X(void, glShaderSource, GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)        \
    X(void, glCompileShader, GLuint shader)                                                                        \
    X(void, glGetShaderiv, GLuint shader, GLenum pname, GLint* params)                                             \
    X(void, glGetShaderInfoLog, GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)                  \
    X(void, glDeleteShader, GLuint shader)                                                                         \
    X(GLuint, glCreateProgram, void)                                                                               \
    X(void, glAttachShader, GLuint program, GLuint shader)                                                         \
    X(void, glLinkProgram, GLuint program)                                                                         \
    X(void, glGetProgramiv, GLuint program, GLenum pname, GLint* params)                                           \
    X(void, glGetProgramInfoLog, GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)                \
    X(void, glDeleteProgram, GLuint program)                                                                       \
    X(void, glUseProgram, GLuint program)                                                                          \
    X(GLint, glGetUniformLocation, GLuint program, const GLchar* name)                                             \
    X(void, glUniform1i, GLint location, GLint v0)                                                                 \
    X(void, glUniformMatrix4fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)          \
    X(void, glGenVertexArrays, GLsizei n, GLuint* arrays)                                                          \
    X(void, glBindVertexArray, GLuint array)                                                                       \
    X(void, glDeleteVertexArrays, GLsizei n, const GLuint* arrays)                                                 \
    X(void, glGenBuffers, GLsizei n, GLuint* buffers)                                                              \
    X(void, glBindBuffer, GLenum target, GLuint buffer)                                                            \
    X(void, glBufferData, GLenum target, GLsizeiptr size, const void* data, GLenum usage)                          \
    X(void, glDeleteBuffers, GLsizei n, const GLuint* buffers)                                                     \
    X(void, glVertexAttribPointer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,    \
      const void* pointer)                                                                                         \
    X(void, glEnableVertexAttribArray, GLuint index)                                                               \
    X(void, glActiveTexture, GLenum texture)                                                                       \
    X(void, glBlendFuncSeparate, GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)  \
    X(void, glBufferSubData, GLenum target, GLintptr offset, GLsizeiptr size, const void* data)                    \
    X(void, glUniform1f, GLint location, GLfloat v0)                                                               \
    X(void, glUniform2f, GLint location, GLfloat v0, GLfloat v1)                                                   \
    X(void, glUniform3f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)                                       \
    X(void, glUniform4f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)                           \
    X(void, glUniform4fv, GLint location, GLsizei count, const GLfloat* value)                                     \
    X(void, glUniformMatrix3fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)          \
    X(void, glGenFramebuffers, GLsizei n, GLuint* ids)                                                             \
    X(void, glBindFramebuffer, GLenum target, GLuint framebuffer)                                                  \
    X(void, glDeleteFramebuffers, GLsizei n, const GLuint* framebuffers)                                           \
    X(void, glFramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget, GLuint texture,            \
      GLint level)                                                                                                 \
    X(GLenum, glCheckFramebufferStatus, GLenum target)                                                             \
    X(void, glGenRenderbuffers, GLsizei n, GLuint* renderbuffers)                                                  \
    X(void, glBindRenderbuffer, GLenum target, GLuint renderbuffer)                                                \
    X(void, glDeleteRenderbuffers, GLsizei n, const GLuint* renderbuffers)                                         \
    X(void, glRenderbufferStorageMultisample, GLenum target, GLsizei samples, GLenum internalformat,               \
      GLsizei width, GLsizei height)                                                                               \
    X(void, glFramebufferRenderbuffer, GLenum target, GLenum attachment, GLenum renderbuffertarget,                \
      GLuint renderbuffer)                                                                                         \
    X(void, glBlitFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0,       \
      GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)                                                    \
    X(void, glGenerateMipmap, GLenum target)

#define ENG_GL_DECLARE(ret, name, ...)                                                                             \
    using PFN_##name = ret(APIENTRY*)(__VA_ARGS__);                                                                \
    extern PFN_##name name;
ENG_GL_FUNCTIONS(ENG_GL_DECLARE)
#undef ENG_GL_DECLARE

namespace eng::gl {
// Muss mit aktivem Kontext aufgerufen werden.
bool loadFunctions();
} // namespace eng::gl
