#pragma once

#include "logic/Types.h"
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

    // Türen und Feuer aus dem Spielzustand übernehmen (Index = Haus) und Türen animieren
    void updateHouses(const std::vector<DoorState>& doors, float dt);
    // Dach dieses Hauses ausblenden, weil die Kamera drinnen ist (-1 = keines)
    void setHiddenRoof(int house) { m_hiddenRoof = house; }

    // timeOfDay: 0 = Mitternacht, 0.25 = Sonnenaufgang, 0.5 = Mittag, 0.75 = Sonnenuntergang
    eng::Environment environment(float timeOfDay, Vec3 focus, float time) const;
    void draw(eng::Renderer3D& r, const eng::Environment& env, float time) const;

private:
    struct Chunk {
        eng::Mesh mesh;
    };
    struct HouseGfx {
        eng::Mesh shell; // Fundament, Wände, Einrichtung
        eng::Mesh roof;  // Dach und Giebel (ausblendbar)
        Vec3 position;
        float yaw = 0.0f;
        Vec3 hinge;      // Türangel (lokal)
        Vec3 hearth;     // Kaminfeuer (Welt)
        Vec3 center;     // Mitte auf Traufhöhe (Welt) für Feuer
        float width = 6.0f, depth = 5.0f, height = 3.0f;
        float door = 0.0f; // 0 = zu, 1 = offen (animiert)
        uint8_t flags = 0;
    };

    void drawHouses(eng::Renderer3D& r, float night, float time) const;

    std::vector<Chunk> m_chunks;
    std::vector<HouseGfx> m_houses;
    eng::Mesh m_door;
    eng::Mesh m_doorReinforced;
    int m_hiddenRoof = -1;
    std::vector<Vec3> m_lanterns;
    std::vector<Vec3> m_fireflies;
    Vec3 m_bonfire;
};

// Wiederverwendbare Bauteile, z. B. für Figuren
void buildTree(eng::MeshBuilder& b, const Tree& tree);

} // namespace ww
