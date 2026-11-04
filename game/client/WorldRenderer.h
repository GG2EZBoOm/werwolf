#pragma once

#include "client/GallowsMotion.h"
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

    void drawMud(eng::Renderer3D& r, const World& world, const GameView& view, Vec3 observer, float time) const;

    // Türen und Feuer aus dem Spielzustand übernehmen (Index = Haus) und Türen animieren
    void updateHouses(const std::vector<DoorState>& doors, float dt);
    // Dach dieses Hauses ausblenden, weil die Kamera drinnen ist (-1 = keines)
    void setHiddenRoof(int house) { m_hiddenRoof = house; }

    // timeOfDay: 0 = Mitternacht, 0.25 = Sonnenaufgang, 0.5 = Mittag, 0.75 = Sonnenuntergang
    eng::Environment environment(float timeOfDay, Vec3 focus, float time) const;
    void draw(eng::Renderer3D& r, const eng::Environment& env, float time, float candleTime = 0.f) const;

    // Galgen: Falltür und Hebel bewegen sich nach gallowsMotion; Seil und Schlinge zeichnet, wer weiß, wo der Hals ist
    void drawGallows(eng::Renderer3D& r, const GallowsMotion& motion) const;
    void drawRope(eng::Renderer3D& r, Vec3 from, Vec3 to) const;
    void drawNoose(eng::Renderer3D& r, const eng::Mat4& frame) const; // Ring um den Hals; frame wie gallowsNeckFrame
    void drawRopeCoil(eng::Renderer3D& r, Vec3 at) const;             // aufgeschossenes Seil unter dem Querbalken

private:
    struct Chunk {
        eng::Mesh mesh;
    };
    struct HouseGfx {
        eng::Mesh shell; // Fundament, Wände, Einrichtung
        eng::Mesh roof;  // Dach und Giebel (ausblendbar)
        eng::Mesh glass; // Buntglasfenster der Kapelle (leuchten leicht)
        bool chapel = false;
        Vec3 position;
        float yaw = 0.0f;
        Vec3 hinge;      // Türangel (lokal), bei der Kapelle die des linken Flügels
        float doorWidth = House::kDoorWidth;
        Vec3 hearth;     // Kaminfeuer bzw. Altarkerzen (Welt)
        Vec3 naveLight;  // Kerzenschein im Kirchenschiff (Welt)
        std::vector<Vec3> flames; // Kerzenflammen (Welt)
        Vec3 center;     // Mitte auf Traufhöhe (Welt) für Feuer
        float width = 6.0f, depth = 5.0f, height = 3.0f;
        float door = 0.0f; // 0 = zu, 1 = offen (animiert)
        uint8_t flags = 0;
    };

    void drawHouses(eng::Renderer3D& r, float night, float time) const;

    std::vector<Chunk> m_chunks;
    std::vector<HouseGfx> m_houses;
    eng::Mesh m_footprint[2], m_raindrop;
    eng::Mesh m_door;
    eng::Mesh m_doorReinforced;
    eng::Mesh m_chapelDoor;
    eng::Mesh m_chapelDoorReinforced;
    eng::Mesh m_trapLeaf, m_lever, m_ropeUnit, m_noose, m_ropeCoil;
    eng::Mat4 m_gallowsFrame;
    float m_gallowsTop = 0.0f; // Plattform über dem Fuß des Galgens
    bool m_hasGallows = false;
    int m_hiddenRoof = -1;
    std::vector<Vec3> m_lanterns;
    std::vector<Vec3> m_fireflies;
    Vec3 m_bonfire;
    std::vector<Vec3> m_squareWicks;
    eng::Mesh m_candleFlame;
};

// Wiederverwendbare Bauteile, z. B. für Figuren
void buildTree(eng::MeshBuilder& b, const Tree& tree);

} // namespace ww
