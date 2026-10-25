#include "engine/gfx/Renderer2D.h"

#include "engine/core/Log.h"
#include "engine/core/Utf8.h"
#include "engine/gfx/Font.h"
#include "engine/gfx/GL.h"

#include <cstddef>

namespace eng {

namespace {

constexpr size_t kMaxVertices = 6 * 8192;

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aLocal;
layout(location = 4) in vec2 aHalf;
layout(location = 5) in vec3 aParams;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
out vec2 vLocal;
out vec2 vHalf;
out vec3 vParams;
void main() {
    vUV = aUV;
    vColor = aColor;
    vLocal = aLocal;
    vHalf = aHalf;
    vParams = aParams;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in vec2 vLocal;
in vec2 vHalf;
in vec3 vParams;
uniform sampler2D uTex;
out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    int mode = int(vParams.z + 0.5);
    if (mode == 0) {
        fragColor = vColor * texture(uTex, vUV);
    } else if (mode == 1) {
        float d = sdRoundBox(vLocal, vHalf, vParams.x);
        float w = max(fwidth(d), 1e-4);
        float a = clamp(0.5 - d / w, 0.0, 1.0);
        if (vParams.y > 0.0) {
            float inner = clamp(0.5 - (d + vParams.y) / w, 0.0, 1.0);
            a = max(a - inner, 0.0);
        }
        fragColor = vec4(vColor.rgb, vColor.a * a);
    } else if (mode == 2) {
        float t = clamp(1.0 - length(vLocal / vHalf), 0.0, 1.0);
        fragColor = vec4(vColor.rgb, vColor.a * t * t);
    } else {
        fragColor = vColor;
    }
}
)";

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048];
        glGetShaderInfoLog(shader, sizeof info, nullptr, info);
        log::error("Shader-Fehler: {}", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Renderer2D::~Renderer2D() { shutdown(); }

bool Renderer2D::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[2048];
        glGetProgramInfoLog(m_program, sizeof info, nullptr, info);
        log::error("Shader-Linkfehler: {}", info);
        return false;
    }
    m_projLocation = glGetUniformLocation(m_program, "uProj");
    glUseProgram(m_program);
    glUniform1i(glGetUniformLocation(m_program, "uTex"), 0);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    const GLsizei stride = sizeof(Vertex);
    auto attrib = [&](GLuint index, GLint count, GLenum type, bool normalized, size_t offset) {
        glVertexAttribPointer(index, count, type, normalized ? GL_TRUE : GL_FALSE, stride,
                              reinterpret_cast<const void*>(offset));
        glEnableVertexAttribArray(index);
    };
    attrib(0, 2, GL_FLOAT, false, offsetof(Vertex, x));
    attrib(1, 2, GL_FLOAT, false, offsetof(Vertex, u));
    attrib(2, 4, GL_UNSIGNED_BYTE, true, offsetof(Vertex, color));
    attrib(3, 2, GL_FLOAT, false, offsetof(Vertex, lx));
    attrib(4, 2, GL_FLOAT, false, offsetof(Vertex, hx));
    attrib(5, 3, GL_FLOAT, false, offsetof(Vertex, radius));

    const uint32_t white = 0xFFFFFFFFu;
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_currentTexture = m_whiteTexture;

    m_vertices.reserve(kMaxVertices);
    return true;
}

void Renderer2D::shutdown() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_whiteTexture) glDeleteTextures(1, &m_whiteTexture);
    if (m_program) glDeleteProgram(m_program);
    m_vbo = m_vao = m_whiteTexture = m_program = 0;
}

void Renderer2D::beginFrame(int framebufferWidth, int framebufferHeight, float dpiScale) {
    m_fbWidth = std::max(1, framebufferWidth);
    m_fbHeight = std::max(1, framebufferHeight);
    m_scale = dpiScale;
    m_width = static_cast<float>(m_fbWidth) / m_scale;
    m_height = static_cast<float>(m_fbHeight) / m_scale;
    m_drawCalls = 0;
    m_clipStack.clear();

    glViewport(0, 0, m_fbWidth, m_fbHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    m_blend = BlendMode::Alpha;
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Orthografische Projektion: (0,0) oben links, (w,h) unten rechts
    const float proj[16] = {2.0f / m_width, 0, 0, 0, 0, -2.0f / m_height, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
    glUseProgram(m_program);
    glUniformMatrix4fv(m_projLocation, 1, GL_FALSE, proj);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glActiveTexture(GL_TEXTURE0);
    m_currentTexture = m_whiteTexture;
}

void Renderer2D::endFrame() { flush(); }

std::vector<uint8_t> Renderer2D::readPixels() const {
    const size_t rowBytes = static_cast<size_t>(m_fbWidth) * 4;
    std::vector<uint8_t> flipped(rowBytes * static_cast<size_t>(m_fbHeight));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_fbWidth, m_fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    // OpenGL liefert die unterste Zeile zuerst
    std::vector<uint8_t> out(flipped.size());
    for (int y = 0; y < m_fbHeight; ++y)
        std::copy_n(flipped.data() + static_cast<size_t>(m_fbHeight - 1 - y) * rowBytes, rowBytes,
                    out.data() + static_cast<size_t>(y) * rowBytes);
    return out;
}

void Renderer2D::clear(Color c) {
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer2D::flush() {
    if (m_vertices.empty()) return;
    glBindTexture(GL_TEXTURE_2D, m_currentTexture);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)), m_vertices.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    m_vertices.clear();
    ++m_drawCalls;
}

void Renderer2D::push(const Vertex& v) {
    if (m_vertices.size() >= kMaxVertices && m_vertices.size() % 3 == 0) flush();
    m_vertices.push_back(v);
}

void Renderer2D::setTexture(unsigned texture) {
    if (texture == m_currentTexture) return;
    flush();
    m_currentTexture = texture;
}

void Renderer2D::setBlendMode(BlendMode mode) {
    if (mode == m_blend) return;
    flush();
    m_blend = mode;
    if (mode == BlendMode::Additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
    else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer2D::applyClip() {
    if (m_clipStack.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const Rect& r = m_clipStack.back();
    const int x0 = static_cast<int>(std::floor(r.x * m_scale));
    const int y0 = static_cast<int>(std::floor(r.y * m_scale));
    const int x1 = static_cast<int>(std::ceil(r.right() * m_scale));
    const int y1 = static_cast<int>(std::ceil(r.bottom() * m_scale));
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, m_fbHeight - y1, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

void Renderer2D::pushClip(const Rect& r) {
    flush();
    Rect clip = m_clipStack.empty() ? r : Rect::intersect(m_clipStack.back(), r);
    m_clipStack.push_back(clip);
    applyClip();
}

void Renderer2D::popClip() {
    flush();
    if (!m_clipStack.empty()) m_clipStack.pop_back();
    applyClip();
}

void Renderer2D::shape(const Rect& r, float radius, float border, float mode, Color top, Color bottom) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float pad = (mode == 1.0f) ? 1.0f : 0.0f; // Platz für die Kantenglättung
    const float hx = r.w * 0.5f;
    const float hy = r.h * 0.5f;
    const float cx = r.x + hx;
    const float cy = r.y + hy;
    const float x0 = r.x - pad, y0 = r.y - pad, x1 = r.right() + pad, y1 = r.bottom() + pad;
    const uint32_t ct = top.packed();
    const uint32_t cb = bottom.packed();
    radius = std::min(radius, std::min(hx, hy));

    auto vtx = [&](float x, float y, uint32_t c) {
        return Vertex{x, y, 0, 0, c, x - cx, y - cy, hx, hy, radius, border, mode};
    };
    const Vertex a = vtx(x0, y0, ct), b = vtx(x1, y0, ct), c = vtx(x1, y1, cb), d = vtx(x0, y1, cb);
    push(a);
    push(b);
    push(c);
    push(a);
    push(c);
    push(d);
}

void Renderer2D::rect(const Rect& r, Color c) { shape(r, 0.0f, 0.0f, 3.0f, c, c); }

void Renderer2D::gradientV(const Rect& r, Color top, Color bottom) { shape(r, 0.0f, 0.0f, 3.0f, top, bottom); }

void Renderer2D::roundRect(const Rect& r, float radius, Color c) { shape(r, radius, 0.0f, 1.0f, c, c); }

void Renderer2D::roundRectOutline(const Rect& r, float radius, float thickness, Color c) {
    shape(r, radius, thickness, 1.0f, c, c);
}

void Renderer2D::circle(Vec2 center, float radius, Color c) {
    shape({center.x - radius, center.y - radius, radius * 2, radius * 2}, radius, 0.0f, 1.0f, c, c);
}

void Renderer2D::circleOutline(Vec2 center, float radius, float thickness, Color c) {
    shape({center.x - radius, center.y - radius, radius * 2, radius * 2}, radius, thickness, 1.0f, c, c);
}

void Renderer2D::glow(Vec2 center, float radius, Color c) {
    shape({center.x - radius, center.y - radius, radius * 2, radius * 2}, 0.0f, 0.0f, 2.0f, c, c);
}

void Renderer2D::line(Vec2 a, Vec2 b, float thickness, Color c) {
    const Vec2 d = b - a;
    const float len = d.length();
    if (len < 1e-3f) return;
    const Vec2 dir = d / len;
    const Vec2 nrm{-dir.y, dir.x};
    const float ht = thickness * 0.5f;
    const float hx = len * 0.5f + ht; // Halbachse entlang der Linie inkl. runder Kappen
    const float pad = 1.0f;
    const Vec2 mid = (a + b) * 0.5f;
    const uint32_t col = c.packed();

    auto vtx = [&](float lx, float ly) {
        const Vec2 p = mid + dir * lx + nrm * ly;
        return Vertex{p.x, p.y, 0, 0, col, lx, ly, hx, ht, ht, 0.0f, 1.0f};
    };
    const Vertex v0 = vtx(-hx - pad, -ht - pad), v1 = vtx(hx + pad, -ht - pad), v2 = vtx(hx + pad, ht + pad),
                 v3 = vtx(-hx - pad, ht + pad);
    push(v0);
    push(v1);
    push(v2);
    push(v0);
    push(v2);
    push(v3);
}

void Renderer2D::triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
    const uint32_t col = color.packed();
    push(Vertex{a.x, a.y, 0, 0, col, 0, 0, 0, 0, 0, 0, 3.0f});
    push(Vertex{b.x, b.y, 0, 0, col, 0, 0, 0, 0, 0, 0, 3.0f});
    push(Vertex{c.x, c.y, 0, 0, col, 0, 0, 0, 0, 0, 0, 3.0f});
}

void Renderer2D::textBaseline(Font& font, Vec2 pos, std::string_view s, Color c) {
    if (!font.isLoaded() || s.empty()) return;
    const float scale = font.dpiScale();
    const float startX = std::round(pos.x * scale);
    float penX = startX;
    float baseline = std::round(pos.y * scale);
    const float lineStep = std::round(font.lineHeight() * scale);
    const uint32_t col = c.packed();

    for (size_t i = 0; i < s.size();) {
        const char32_t cp = utf8::next(s, i);
        if (cp == '\n') {
            penX = startX;
            baseline += lineStep;
            continue;
        }
        const Glyph* g = font.glyph(cp);
        if (!g) continue;
        if (g->width > 0) {
            setTexture(font.texture());
            const float x0 = (std::round(penX) + static_cast<float>(g->bearingX)) / scale;
            const float y0 = (baseline - static_cast<float>(g->bearingY)) / scale;
            const float x1 = x0 + static_cast<float>(g->width) / scale;
            const float y1 = y0 + static_cast<float>(g->height) / scale;
            const Vertex a{x0, y0, g->u0, g->v0, col, 0, 0, 0, 0, 0, 0, 0};
            const Vertex b{x1, y0, g->u1, g->v0, col, 0, 0, 0, 0, 0, 0, 0};
            const Vertex cc{x1, y1, g->u1, g->v1, col, 0, 0, 0, 0, 0, 0, 0};
            const Vertex d{x0, y1, g->u0, g->v1, col, 0, 0, 0, 0, 0, 0, 0};
            push(a);
            push(b);
            push(cc);
            push(a);
            push(cc);
            push(d);
        }
        penX += g->advance;
    }
}

void Renderer2D::text(Font& font, Vec2 pos, std::string_view s, Color c) {
    textBaseline(font, {pos.x, pos.y + font.ascent()}, s, c);
}

void Renderer2D::textCentered(Font& font, const Rect& r, std::string_view s, Color c) {
    const float w = font.measure(s);
    const float x = r.x + (r.w - w) * 0.5f;
    const float baseline = r.y + r.h * 0.5f + font.capHeight() * 0.5f;
    textBaseline(font, {x, baseline}, s, c);
}

void Renderer2D::textRight(Font& font, Vec2 topRight, std::string_view s, Color c) {
    text(font, {topRight.x - font.measure(s), topRight.y}, s, c);
}

} // namespace eng
