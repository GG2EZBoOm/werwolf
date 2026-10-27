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

// Bitmaske geschlossener Türen (Bit i = Tür von Haus i ist zu)
using DoorMask = uint32_t;

struct Collider {
    enum class Shape : uint8_t { Circle, Box };
    Shape shape = Shape::Circle;
    Vec2 center;
    float radius = 0.5f;  // Kreis
    Vec2 half;            // Box: halbe Ausdehnung (lokal x/z)
    float yaw = 0.0f;     // Box: Drehung um Y
    float bound = 0.5f;   // Umkreis für schnelle Vorabprüfung
    bool opaque = false;  // verdeckt die Sicht (Wände)
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
    bool chapel = false; // die Kapelle: gehört niemandem, Steinboden, Bänke und Altar
    int variant = 0;
    Vec3 door;         // Weltposition vor der Tür (draußen)
    Vec3 doorInside;   // direkt hinter der Tür (drinnen)
    Vec3 hearth;       // Kaminfeuer bzw. Altarkerzen (Lichtquelle)
    float floorY = 0;  // Höhe des Fußbodens innen
    float doorWidth = kDoorWidth;
    float ramp = 1.3f; // Länge der Rampe vor der Tür

    static constexpr float kWall = 0.24f;      // Wandstärke
    static constexpr float kDoorWidth = 1.15f;
    static constexpr float kFloorOffset = 0.35f;
};

// Grundriss der Kapelle (lokal: x quer, z zur Tür hin). Kollision und Grafik nutzen dieselben Maße.
namespace chapel {
constexpr float kWidth = 8.4f, kDepth = 14.4f, kWallHeight = 5.6f, kRoofHeight = 4.6f;
constexpr float kDoorWidth = 1.8f, kDoorHeight = 2.9f;
constexpr float kChancelZ = -3.5f;   // dahinter (Richtung Altar) liegt der Altarraum eine Stufe höher
constexpr float kChancelStep = 0.22f;
constexpr int kPewRows = 6;
constexpr float kPewFirstZ = -2.3f, kPewSpacing = 1.4f; // Mitte der ersten Bankreihe, Abstand der Reihen
constexpr float kPewInner = 0.9f, kPewOuter = 3.0f, kPewHalfDepth = 0.27f; // Mittelgang bis Seitengang
constexpr Vec2 kAltar{0.0f, -5.7f}, kAltarHalf{1.0f, 0.42f};
constexpr Vec2 kCandleStand{2.2f, -5.0f}; // gespiegelt auf beiden Seiten
constexpr Vec2 kLectern{-2.7f, -4.3f};
constexpr Vec2 kFont{-2.9f, 6.0f};        // Taufbecken neben dem Eingang
constexpr float kTowerSize = 4.0f;        // Turm hinter der Altarwand
constexpr float pewZ(int row) { return kPewFirstZ + kPewSpacing * static_cast<float>(row); }
} // namespace chapel

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

    // Höhe des Bodens: Gelände bzw. Fußboden in Häusern (mit Rampe an der Tür)
    float height(float x, float z) const;
    // Nur das Gelände (Dreiecke des Höhengitters)
    float terrainHeight(float x, float z) const;
    float gridHeight(int i, int j) const;
    Vec3 ground(float x, float z) const { return {x, height(x, z), z}; }

    // Bodenart für die Einfärbung: 0..1 Anteile
    float pathFactor(float x, float z) const;
    float squareFactor(float x, float z) const;
    float fieldFactor(float x, float z) const;

    // Schiebt einen Kreis aus allen Hindernissen (inkl. geschlossener Türen) heraus.
    Vec2 resolveCollision(Vec2 pos, float radius, DoorMask closedDoors = 0) const;
    bool blocked(Vec2 pos, float radius, DoorMask closedDoors = 0) const;
    // Sichtlinie durch Wände oder geschlossene Türen verdeckt?
    bool lineBlocked(Vec2 a, Vec2 b, DoorMask closedDoors = 0) const;
    // Kreuzt die Bewegung von a nach b eine geschlossene Tür?
    bool crossesClosedDoor(Vec2 a, Vec2 b, DoorMask closedDoors) const;
    // Liegt der Punkt in einer Wand oder einem geschlossenen Gebäude (für die Kamera)?
    bool insideBuilding(Vec2 p, float margin = 0.2f) const;
    // Index des Hauses, in dessen Innenraum der Punkt liegt, sonst -1
    int houseAt(Vec2 p, float margin = 0.0f) const;
    // Mittelpunkt der Türöffnung eines Hauses
    Vec2 doorCenter(int house) const;
    // Lokale Hauskoordinaten (x quer, z zur Tür hin) in Weltkoordinaten
    Vec2 houseToWorld(int house, Vec2 local) const;
    Vec2 worldToHouse(int house, Vec2 world) const;
    // Nächster Wegpunkt innerhalb eines Gebäudes, um die Einrichtung herum (z. B. die Kirchenbänke)
    Vec2 routeInside(int house, Vec2 from, Vec2 to) const;
    // Index der Kapelle in houses(), sonst -1
    int chapelIndex() const { return m_chapelIndex; }

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
    float naturalHeight(float x, float z) const;
    float rawHeight(float x, float z) const;
    void buildHeights();
    void placeVillage();
    void placeChapel(Vec2 at, float yaw);
    void placeNature();
    void addCircle(Vec2 c, float r);
    void addBox(Vec2 c, Vec2 half, float yaw, bool opaque = false);
    void addHouseColliders(const House& h);
    Collider doorCollider(int house) const;
    void buildGrid();
    void buildChapelRoutes();
    bool freeSpot(Vec2 p, float r) const;
    bool pushOut(const Collider& c, Vec2& pos, float radius) const;
    // Frei von den Hindernissen im Bereich [first, last) der Kollisionsliste?
    bool segmentClear(Vec2 a, Vec2 b, float radius, size_t first, size_t last) const;

    uint32_t m_seed = 0;
    std::vector<float> m_heights;
    float m_yardHeight = 0.0f; // Höhe der Kirchhof-Terrasse
    std::vector<House> m_houses;
    std::vector<Tree> m_trees;
    std::vector<Prop> m_props;
    std::vector<Collider> m_colliders;
    std::vector<Vec2> m_pathPoints; // Polylinien als Paare (Start, Ende)
    Vec3 m_well, m_bell, m_chapel;
    float m_chapelYaw = 0.0f;
    int m_chapelIndex = -1;

    // Wegenetz in der Kapelle: Knoten (Welt), kürzeste Abstände zwischen allen Knoten
    size_t m_chapelColliders[2] = {0, 0};
    std::vector<Vec2> m_routeNodes;
    std::vector<float> m_routeDist;

    // Raster zur schnellen Kollisionssuche
    static constexpr float kGridCell = 8.0f;
    static constexpr int kGridSize = static_cast<int>(kSize / kGridCell);
    std::vector<std::vector<uint32_t>> m_grid;
};

} // namespace ww
