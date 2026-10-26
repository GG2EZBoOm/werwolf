#pragma once

#include "engine/core/Math3D.h"

#include <cstdint>
#include <vector>

namespace ww {

using eng::Vec2;
using eng::Vec3;

// Deterministische Rauschfunktionen (gleiches Ergebnis auf Server und Clients).
float hash01(int x, int y, uint32_t seed);
float valueNoise(float x, float y, uint32_t seed);
float fbm(float x, float y, uint32_t seed, int octaves);

struct Collider {
    enum class Shape : uint8_t { Circle, Box };
    Shape shape = Shape::Circle;
    Vec2 center;
    float radius = 0.5f;  // Kreis
    Vec2 half;            // Box: halbe Ausdehnung (lokal x/z)
    float yaw = 0.0f;     // Box: Drehung um Y
    float bound = 0.5f;   // Umkreis für schnelle Vorabprüfung
};

struct House {
    Vec3 position;
    float yaw = 0.0f; // Tür zeigt in lokale +Z-Richtung
    float width = 6.0f;
    float depth = 5.0f;
    float wallHeight = 3.0f;
    float roofHeight = 2.2f;
    uint32_t wallColor = 0xD8C8A8;
    uint32_t roofColor = 0x7A3B2E;
    uint32_t timberColor = 0x4A3426;
    bool chimney = true;
    int variant = 0;
    Vec3 door; // Weltposition vor der Tür
};

enum class TreeKind : uint8_t { Pine, Broadleaf, Dead };

struct Tree {
    Vec3 position;
    float scale = 1.0f;
    float yaw = 0.0f;
    TreeKind kind = TreeKind::Pine;
    float tint = 0.0f; // Farbabweichung -1..1
};

enum class PropKind : uint8_t {
    Rock,
    Fence,       // Zaunstück, scale = Länge
    Lantern,
    LogSeat,     // Baumstamm als Sitzbank am Feuer
    WoodPile,
    ChoppingBlock,
    Barrel,
    Crate,
    HayBale,
    Cart,
    Tombstone,
    GraveCross,
    CropRow,     // Getreidereihe, scale = Länge
    Bush,
    Stump,
};

struct Prop {
    PropKind kind;
    Vec3 position;
    float yaw = 0.0f;
    float scale = 1.0f;
};

// Das Dorf: Gelände, Gebäude, Vegetation und Kollision. Wird aus einem Seed erzeugt.
class World {
public:
    static constexpr float kSize = 240.0f;   // Kantenlänge des Geländes in Metern
    static constexpr int kCells = 120;       // Gitterzellen pro Kante
    static constexpr float kCell = kSize / kCells;
    static constexpr float kBoundary = 96.0f; // begehbarer Radius
    static constexpr float kSquareRadius = 12.0f;
    static constexpr float kBellYaw = 0.6f;

    void generate(uint32_t seed);
    uint32_t seed() const { return m_seed; }

    // Höhe exakt auf den Dreiecken des Geländegitters
    float height(float x, float z) const;
    float gridHeight(int i, int j) const;
    Vec3 ground(float x, float z) const { return {x, height(x, z), z}; }

    // Bodenart für die Einfärbung: 0..1 Anteile
    float pathFactor(float x, float z) const;
    float squareFactor(float x, float z) const;
    float fieldFactor(float x, float z) const;

    // Schiebt einen Kreis aus allen Hindernissen heraus.
    Vec2 resolveCollision(Vec2 pos, float radius) const;
    bool blocked(Vec2 pos, float radius) const;
    // Prüft, ob die Sichtlinie zwischen zwei Punkten von Gebäuden verdeckt wird.
    bool lineBlockedByBuildings(Vec2 a, Vec2 b) const;
    // Liegt der Punkt in einem Gebäude (große Box-Hindernisse)?
    bool insideBuilding(Vec2 p, float margin = 0.2f) const;

    Vec3 spawnPoint(int index, int count) const;
    Vec3 bonfire() const { return ground(0.0f, 0.0f); }
    Vec3 well() const { return m_well; }
    Vec3 bell() const { return m_bell; }
    Vec3 chapel() const { return m_chapel; }
    float chapelYaw() const { return m_chapelYaw; }

    const std::vector<House>& houses() const { return m_houses; }
    const std::vector<Tree>& trees() const { return m_trees; }
    const std::vector<Prop>& props() const { return m_props; }
    const std::vector<Collider>& colliders() const { return m_colliders; }
    const std::vector<Vec2>& pathPoints() const { return m_pathPoints; }

private:
    float rawHeight(float x, float z) const;
    void buildHeights();
    void placeVillage();
    void placeNature();
    void addCircle(Vec2 c, float r);
    void addBox(Vec2 c, Vec2 half, float yaw);
    void buildGrid();
    bool freeSpot(Vec2 p, float r) const;

    uint32_t m_seed = 0;
    std::vector<float> m_heights;
    std::vector<House> m_houses;
    std::vector<Tree> m_trees;
    std::vector<Prop> m_props;
    std::vector<Collider> m_colliders;
    std::vector<Vec2> m_pathPoints; // Polylinien als Paare (Start, Ende)
    Vec3 m_well, m_bell, m_chapel;
    float m_chapelYaw = 0.0f;

    // Raster zur schnellen Kollisionssuche
    static constexpr float kGridCell = 8.0f;
    static constexpr int kGridSize = static_cast<int>(kSize / kGridCell);
    std::vector<std::vector<uint32_t>> m_grid;
};

} // namespace ww
