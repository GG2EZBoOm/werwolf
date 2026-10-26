#include "engine/gfx/Mesh.h"

#include "engine/gfx/GL.h"

#include <cstddef>

namespace eng {

// --- Mesh --------------------------------------------------------------------

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& o) noexcept { *this = std::move(o); }

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        destroy();
        m_vao = o.m_vao;
        m_vbo = o.m_vbo;
        m_ibo = o.m_ibo;
        m_indexCount = o.m_indexCount;
        m_bounds = o.m_bounds;
        o.m_vao = o.m_vbo = o.m_ibo = 0;
        o.m_indexCount = 0;
    }
    return *this;
}

void Mesh::upload(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices) {
    destroy();
    if (vertices.empty() || indices.empty()) return;
    m_bounds = Bounds{};
    for (const Vertex3D& v : vertices) m_bounds.add(v.position);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex3D)), vertices.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);

    const GLsizei stride = sizeof(Vertex3D);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex3D, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex3D, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<const void*>(offsetof(Vertex3D, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex3D, glow)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    m_indexCount = static_cast<int>(indices.size());
}

void Mesh::destroy() {
    if (m_ibo) glDeleteBuffers(1, &m_ibo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ibo = 0;
    m_indexCount = 0;
}

void Mesh::draw() const {
    if (!valid()) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

// --- MeshBuilder -------------------------------------------------------------

namespace {

float hash3(Vec3 p) {
    // deterministischer Pseudozufall aus einer Position, Ergebnis in [-1, 1]
    const float h = std::sin(p.x * 12.9898f + p.y * 78.233f + p.z * 37.719f) * 43758.5453f;
    return (h - std::floor(h)) * 2.0f - 1.0f;
}

} // namespace

uint32_t MeshBuilder::faceColor(Vec3 center) {
    Color c = m_color;
    if (m_jitter > 0.0f) {
        const float f = 1.0f + m_jitter * hash3(center);
        c = {c.r * f, c.g * f, c.b * f, c.a};
    }
    return c.packed();
}

void MeshBuilder::pushFace(const Vec3* points, int count) {
    if (count < 3) return;
    Vec3 p[16];
    count = std::min(count, 16);
    Vec3 center{0, 0, 0};
    for (int i = 0; i < count; ++i) {
        p[i] = m_transform.transformPoint(points[i]);
        center += p[i];
    }
    center = center / static_cast<float>(count);
    // Normale über alle Kanten (robust auch bei doppelten Punkten)
    Vec3 n{0, 0, 0};
    for (int i = 0; i < count; ++i) n += cross(p[i] - center, p[(i + 1) % count] - center);
    n = n.normalized();
    if (n.lengthSq() < 0.5f) return;

    const uint32_t color = faceColor(center);
    const uint32_t base = static_cast<uint32_t>(m_vertices.size());
    for (int i = 0; i < count; ++i) m_vertices.push_back(Vertex3D{p[i], n, color, m_glow});
    for (int i = 1; i + 1 < count; ++i) {
        m_indices.push_back(base);
        m_indices.push_back(base + static_cast<uint32_t>(i));
        m_indices.push_back(base + static_cast<uint32_t>(i + 1));
    }
}

void MeshBuilder::triangle(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 pts[3] = {a, b, c};
    pushFace(pts, 3);
}

void MeshBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    const Vec3 pts[4] = {a, b, c, d};
    pushFace(pts, 4);
}

void MeshBuilder::box(Vec3 c, Vec3 size) {
    const float x0 = c.x - size.x * 0.5f, x1 = c.x + size.x * 0.5f;
    const float y0 = c.y - size.y * 0.5f, y1 = c.y + size.y * 0.5f;
    const float z0 = c.z - size.z * 0.5f, z1 = c.z + size.z * 0.5f;
    quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}); // +X
    quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}); // -X
    quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}); // +Y
    quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}); // -Y
    quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}); // +Z
    quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}); // -Z
}

void MeshBuilder::frustum(Vec3 base, float rb, float rt, float h, int segments, bool caps) {
    segments = std::clamp(segments, 3, 15);
    Vec3 bottom[16], top[16];
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(a), s = std::sin(a);
        bottom[i] = base + Vec3{c * rb, 0, s * rb};
        top[i] = base + Vec3{c * rt, h, s * rt};
    }
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        if (rt <= 1e-4f) triangle(bottom[j], bottom[i], top[i]);
        else quad(bottom[j], bottom[i], top[i], top[j]);
    }
    if (caps) {
        pushFace(bottom, segments);
        if (rt > 1e-4f) {
            Vec3 rev[16];
            for (int i = 0; i < segments; ++i) rev[i] = top[segments - 1 - i];
            pushFace(rev, segments);
        }
    }
}

void MeshBuilder::cylinder(Vec3 base, float radius, float height, int segments, bool caps) {
    frustum(base, radius, radius, height, segments, caps);
}

void MeshBuilder::cone(Vec3 base, float radius, float height, int segments) {
    frustum(base, radius, 0.0f, height, segments, true);
}

void MeshBuilder::sphere(Vec3 center, float radius, int rings, int segments) {
    rings = std::clamp(rings, 2, 32);
    segments = std::clamp(segments, 3, 32);
    auto point = [&](int i, int j) {
        const float theta = 2.0f * kPi * static_cast<float>(i % segments) / static_cast<float>(segments);
        const float phi = kPi * static_cast<float>(j) / static_cast<float>(rings);
        return center + Vec3{std::cos(theta) * std::sin(phi), std::cos(phi), std::sin(theta) * std::sin(phi)} * radius;
    };
    for (int j = 0; j < rings; ++j) {
        for (int i = 0; i < segments; ++i) {
            const Vec3 b1 = point(i + 1, j + 1), b0 = point(i, j + 1), t0 = point(i, j), t1 = point(i + 1, j);
            if (j == 0) triangle(b1, b0, t0);
            else if (j == rings - 1) triangle(b0, t0, t1);
            else quad(b1, b0, t0, t1);
        }
    }
}

void MeshBuilder::gableRoof(Vec3 c, float width, float depth, float height, float overhang, bool withGables) {
    const float W = width * 0.5f + overhang;
    const float D = depth * 0.5f + overhang;
    const float y = c.y;
    const float H = c.y + height;
    quad({c.x + W, y, c.z + D}, {c.x + W, y, c.z - D}, {c.x, H, c.z - D}, {c.x, H, c.z + D});
    quad({c.x - W, y, c.z - D}, {c.x - W, y, c.z + D}, {c.x, H, c.z + D}, {c.x, H, c.z - D});
    // Unterseite der Dachflächen, damit der Überstand von unten nicht durchsichtig ist
    quad({c.x, H - 0.02f, c.z + D}, {c.x, H - 0.02f, c.z - D}, {c.x + W, y - 0.02f, c.z - D}, {c.x + W, y - 0.02f, c.z + D});
    quad({c.x, H - 0.02f, c.z - D}, {c.x, H - 0.02f, c.z + D}, {c.x - W, y - 0.02f, c.z + D}, {c.x - W, y - 0.02f, c.z - D});
    if (withGables) gableWalls(c, width, depth, height, overhang);
}

void MeshBuilder::gableWalls(Vec3 c, float width, float depth, float height, float overhang) {
    const float W = width * 0.5f + overhang;
    const float w = width * 0.5f;
    const float d = depth * 0.5f;
    const float y = c.y;
    const float H = c.y + height;
    const float y1 = y + height * overhang / W;
    const Vec3 front[5] = {{c.x - w, y, c.z + d}, {c.x + w, y, c.z + d}, {c.x + w, y1, c.z + d}, {c.x, H, c.z + d},
                           {c.x - w, y1, c.z + d}};
    pushFace(front, 5);
    const Vec3 back[5] = {{c.x + w, y, c.z - d}, {c.x - w, y, c.z - d}, {c.x - w, y1, c.z - d}, {c.x, H, c.z - d},
                          {c.x + w, y1, c.z - d}};
    pushFace(back, 5);
}

void MeshBuilder::beam(Vec3 a, Vec3 b, float thickness) {
    const Vec3 axis = b - a;
    const float len = axis.length();
    if (len < 1e-4f) return;
    const Vec3 f = axis / len;
    const Vec3 up = std::fabs(f.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 r = cross(f, up).normalized();
    const Vec3 u = cross(r, f);
    const float h = thickness * 0.5f;
    // Eckpunkte im Uhrzeigersinn um die Achse (r -> u)
    const Vec3 ring[4] = {-r * h - u * h, r * h - u * h, r * h + u * h, -r * h + u * h};
    Vec3 ca[4], cb[4];
    for (int i = 0; i < 4; ++i) {
        ca[i] = a + ring[i];
        cb[i] = b + ring[i];
    }
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        quad(ca[i], cb[i], cb[j], ca[j]);
    }
    quad(ca[0], ca[1], ca[2], ca[3]);
    quad(cb[3], cb[2], cb[1], cb[0]);
}

void MeshBuilder::append(const MeshBuilder& other) {
    const uint32_t base = static_cast<uint32_t>(m_vertices.size());
    m_vertices.insert(m_vertices.end(), other.m_vertices.begin(), other.m_vertices.end());
    for (uint32_t i : other.m_indices) m_indices.push_back(base + i);
}

void MeshBuilder::clear() {
    m_vertices.clear();
    m_indices.clear();
}

} // namespace eng
