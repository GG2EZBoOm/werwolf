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
    X(void, glBlendFuncSeparate, GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)

#define ENG_GL_DECLARE(ret, name, ...)                                                                             \
    using PFN_##name = ret(APIENTRY*)(__VA_ARGS__);                                                                \
    extern PFN_##name name;
ENG_GL_FUNCTIONS(ENG_GL_DECLARE)
#undef ENG_GL_DECLARE

namespace eng::gl {
// Muss mit aktivem Kontext aufgerufen werden.
bool loadFunctions();
} // namespace eng::gl
