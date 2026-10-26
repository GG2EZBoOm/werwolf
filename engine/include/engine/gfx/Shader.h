#pragma once

#include "engine/core/Math3D.h"

#include <string>
#include <unordered_map>

namespace eng {

// GLSL-Programm mit zwischengespeicherten Uniform-Positionen.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool load(const char* vertexSource, const char* fragmentSource, const std::string& name);
    void destroy();
    void bind() const;
    unsigned id() const { return m_program; }

    int location(const char* name);
    void set(const char* name, int v);
    void set(const char* name, float v);
    void set(const char* name, Vec2 v);
    void set(const char* name, Vec3 v);
    void set(const char* name, Vec4 v);
    void set(const char* name, const Mat4& v);
    void setArray(const char* name, const Vec4* values, int count);

private:
    unsigned m_program = 0;
    std::string m_name;
    std::unordered_map<std::string, int> m_locations;
};

} // namespace eng
