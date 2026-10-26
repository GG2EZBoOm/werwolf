#pragma once

#include "engine/core/Math3D.h"

#include <cstdint>
#include <vector>

namespace eng {

struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    uint32_t color = 0xFFFFFFFFu; // RGBA8 (sRGB)
    float glow = 0.0f;            // Eigenleuchten (z. B. Fenster bei Nacht)
};

struct Bounds {
    Vec3 min{1e9f, 1e9f, 1e9f};
    Vec3 max{-1e9f, -1e9f, -1e9f};
    void add(Vec3 p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    Vec3 center() const { return (min + max) * 0.5f; }
    float radius() const { return (max - min).length() * 0.5f; }
};

// Geometrie auf der Grafikkarte (indiziert, Dreiecke).
class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept;
    Mesh& operator=(Mesh&& o) noexcept;

    void upload(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);
    void destroy();
    void draw() const;
    bool valid() const { return m_vao != 0 && m_indexCount > 0; }
    const Bounds& bounds() const { return m_bounds; }
    int triangleCount() const { return m_indexCount / 3; }

private:
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_ibo = 0;
    int m_indexCount = 0;
    Bounds m_bounds;
};

// Baut prozedurale Low-Poly-Geometrie mit flacher Schattierung (eine Normale pro Fläche).
class MeshBuilder {
public:
    void setColor(Color c) { m_color = c; }
    void setColor(uint32_t rgb) { m_color = Color::hex(rgb); }
    void setGlow(float g) { m_glow = g; }
    // Leichte, zufällige Farbabweichung pro Fläche für einen handgemachten Look
    void setColorJitter(float amount) { m_jitter = amount; }
    void setTransform(const Mat4& t) { m_transform = t; }
    const Mat4& transform() const { return m_transform; }

    void triangle(Vec3 a, Vec3 b, Vec3 c);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d); // gegen den Uhrzeigersinn von außen gesehen
    void box(Vec3 center, Vec3 size);
    void cylinder(Vec3 base, float radius, float height, int segments, bool caps = true);
    void cone(Vec3 base, float radius, float height, int segments);
    // Kegelstumpf: unten radiusBottom, oben radiusTop
    void frustum(Vec3 base, float radiusBottom, float radiusTop, float height, int segments, bool caps = true);
    void sphere(Vec3 center, float radius, int rings, int segments);
    // Satteldach über einem Rechteck (Breite x, Tiefe z), First entlang der Tiefe
    void gableRoof(Vec3 baseCenter, float width, float depth, float height, float overhang, bool withGables = true);
    // Nur die beiden Giebelwände (passend zu gableRoof), z. B. in Wandfarbe
    void gableWalls(Vec3 baseCenter, float width, float depth, float height, float overhang);
    // Balken mit quadratischem Querschnitt zwischen zwei beliebigen Punkten
    void beam(Vec3 a, Vec3 b, float thickness);

    void append(const MeshBuilder& other);
    void clear();
    bool empty() const { return m_indices.empty(); }
    size_t vertexCount() const { return m_vertices.size(); }

    const std::vector<Vertex3D>& vertices() const { return m_vertices; }
    const std::vector<uint32_t>& indices() const { return m_indices; }
    void uploadTo(Mesh& mesh) const { mesh.upload(m_vertices, m_indices); }

private:
    uint32_t faceColor(Vec3 center);
    void pushFace(const Vec3* points, int count);

    std::vector<Vertex3D> m_vertices;
    std::vector<uint32_t> m_indices;
    Color m_color{1, 1, 1, 1};
    float m_glow = 0.0f;
    float m_jitter = 0.0f;
    Mat4 m_transform;
};

} // namespace eng
