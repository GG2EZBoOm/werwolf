#pragma once

#include "world/World.h"

#include "engine/gfx/Mesh.h"
#include "engine/gfx/Renderer3D.h"

#include <vector>

namespace ww {

// Macht aus der Weltbeschreibung Low-Poly-Geometrie und zeichnet sie samt
// Tageszeit-Stimmung, Lagerfeuer, Laternen und nächtlichen Glühwürmchen.
class WorldRenderer {
public:
    void build(const World& world);
    bool built() const { return !m_chunks.empty(); }

    // timeOfDay: 0 = Mitternacht, 0.25 = Sonnenaufgang, 0.5 = Mittag, 0.75 = Sonnenuntergang
    eng::Environment environment(float timeOfDay, Vec3 focus, float time) const;
    void draw(eng::Renderer3D& r, const eng::Environment& env, float time) const;

private:
    struct Chunk {
        eng::Mesh mesh;
    };

    std::vector<Chunk> m_chunks;
    std::vector<Vec3> m_lanterns;
    std::vector<Vec3> m_fireflies;
    Vec3 m_bonfire;
};

// Wiederverwendbare Bauteile, z. B. für Figuren
void buildTree(eng::MeshBuilder& b, const Tree& tree);

} // namespace ww
