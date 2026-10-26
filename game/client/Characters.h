#pragma once

#include "engine/core/Math3D.h"
#include "engine/gfx/Mesh.h"
#include "engine/gfx/Renderer3D.h"

#include <cstdint>

namespace ww {

using eng::Color;
using eng::Vec3;

enum class Headwear : uint8_t { None, FeltHat, Hood, Scarf, Cap, StrawHat };

// Aussehen einer Figur – wird deterministisch aus der Spieler-ID erzeugt.
struct CharacterLook {
    Color skin;
    Color hair;
    Color shirt;
    Color pants;
    Color boots;
    Color accent; // Gürtel, Hut, Kapuze
    Headwear headwear = Headwear::None;
    bool beard = false;
    bool apron = false;
    float height = 1.0f; // Skalierung
};

CharacterLook characterLook(uint8_t id);

struct CharacterPose {
    Vec3 position;
    float yaw = 0.0f;       // Blickrichtung (lokal +Z)
    float walkPhase = 0.0f; // Fortschritt im Schrittzyklus (Bogenmaß)
    float speed = 0.0f;     // 0 = steht, 1 = geht, 2 = rennt
    float wolf = 0.0f;      // 0 = Mensch, 1 = Werwolf (Übergang animiert)
    bool dead = false;
    float attack = 0.0f;    // 0..1 Prankenhieb
    float time = 0.0f;
    bool lantern = false;   // trägt nachts eine Laterne
    bool ghost = false;     // Geist eines Verstorbenen (nur für andere Tote sichtbar)
};

// Zeichnet Figuren aus wiederverwendeten Grundkörpern mit Gelenk-Animation.
class CharacterRenderer {
public:
    void init();
    bool isReady() const { return m_cube.valid(); }
    void draw(eng::Renderer3D& r, const CharacterLook& look, const CharacterPose& pose) const;
    // Position der Laterne in der Hand (für das Punktlicht)
    Vec3 lanternPosition(const CharacterPose& pose) const;
    // Kopfhöhe über dem Boden (für Namensschilder)
    float headHeight(const CharacterPose& pose) const;

private:
    void drawHuman(eng::Renderer3D& r, const CharacterLook& look, const CharacterPose& pose, const eng::Mat4& root) const;
    void drawWolf(eng::Renderer3D& r, const CharacterPose& pose, const eng::Mat4& root) const;

    eng::Mesh m_cube;
    eng::Mesh m_cylinder;
    eng::Mesh m_cone;
    eng::Mesh m_frustum;
    eng::Mesh m_sphere;
    eng::Mesh m_lantern;
};

} // namespace ww
