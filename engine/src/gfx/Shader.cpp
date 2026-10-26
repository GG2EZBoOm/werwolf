#include "engine/gfx/Shader.h"

#include "engine/core/Log.h"
#include "engine/gfx/GL.h"

namespace eng {

namespace {

GLuint compileStage(GLenum type, const char* source, const std::string& name) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[4096];
        glGetShaderInfoLog(shader, sizeof info, nullptr, info);
        log::error("Shader '{}' ({}): {}", name, type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::~Shader() { destroy(); }

bool Shader::load(const char* vertexSource, const char* fragmentSource, const std::string& name) {
    destroy();
    m_name = name;
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, name);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, name);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[4096];
        glGetProgramInfoLog(m_program, sizeof info, nullptr, info);
        log::error("Shader '{}' Linkfehler: {}", name, info);
        destroy();
        return false;
    }
    return true;
}

void Shader::destroy() {
    if (m_program) glDeleteProgram(m_program);
    m_program = 0;
    m_locations.clear();
}

void Shader::bind() const { glUseProgram(m_program); }

int Shader::location(const char* name) {
    auto it = m_locations.find(name);
    if (it != m_locations.end()) return it->second;
    const int loc = glGetUniformLocation(m_program, name);
    m_locations.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, int v) { glUniform1i(location(name), v); }
void Shader::set(const char* name, float v) { glUniform1f(location(name), v); }
void Shader::set(const char* name, Vec2 v) { glUniform2f(location(name), v.x, v.y); }
void Shader::set(const char* name, Vec3 v) { glUniform3f(location(name), v.x, v.y, v.z); }
void Shader::set(const char* name, Vec4 v) { glUniform4f(location(name), v.x, v.y, v.z, v.w); }
void Shader::set(const char* name, const Mat4& v) { glUniformMatrix4fv(location(name), 1, GL_FALSE, v.m); }

void Shader::setArray(const char* name, const Vec4* values, int count) {
    if (count > 0) glUniform4fv(location(name), count, &values[0].x);
}

} // namespace eng
