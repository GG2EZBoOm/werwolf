#include "world/World.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::kPi;

namespace {

uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// Eigener Zufallsgenerator (SplitMix64), damit die Welt auf allen Plattformen gleich entsteht.
struct WorldRng {
    uint64_t state;
    explicit WorldRng(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL) {}
    uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    bool chance(float p) { return uniform() < p; }
    template <class T>
    const T& pick(const std::vector<T>& v) {
        return v[static_cast<size_t>(next() % v.size())];
    }
};

float smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float deg(float d) { return d * kPi / 180.0f; }

// Drehung um Y wie Mat4::rotationY: lokal -> Welt (x/z)
Vec2 rotate(Vec2 local, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * local.x + s * local.y, -s * local.x + c * local.y};
}

Vec2 unrotate(Vec2 world, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * world.x - s * world.y, s * world.x + c * world.y};
}

// yaw, bei dem die lokale +Z-Achse in Richtung (dx, dz) zeigt
float yawToward(float dx, float dz) { return std::atan2(dx, dz); }

float distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = len2 > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vec2 q = a + ab * t;
    return (p - q).length();
}

// Felder im Osten
constexpr float kFieldX0 = 48.0f, kFieldX1 = 74.0f, kFieldZ0 = -15.0f, kFieldZ1 = 15.0f;
// Holzfällerlager im Westen
const Vec2 kCamp{-52.0f, 8.0f};

} // namespace

float hash01(int x, int y, uint32_t seed) {
    const uint32_t h = mix32(static_cast<uint32_t>(x) * 0x8da6b343U ^ static_cast<uint32_t>(y) * 0xd8163841U ^
                             seed * 0xcb1ab31fU);
    return static_cast<float>(h) * (1.0f / 4294967296.0f);
}

float valueNoise(float x, float y, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed);
    const float c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
    return eng::lerp(eng::lerp(a, b, tx), eng::lerp(c, d, tx), ty);
}

float fbm(float x, float y, uint32_t seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x, y, seed + static_cast<uint32_t>(i) * 101u) * amp;
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// --- Gelände -------------------------------------------------------------------

float World::rawHeight(float x, float z) const {
    const float r = std::sqrt(x * x + z * z);
    const float n = fbm(x * 0.018f, z * 0.018f, m_seed, 4) * 2.0f - 1.0f;
    const float detail = fbm(x * 0.07f, z * 0.07f, m_seed + 17u, 2) * 2.0f - 1.0f;
    const float outside = smooth(34.0f, 62.0f, r);
    float h = n * (1.2f + 4.5f * outside) + detail * 0.35f;
    // Berge am Rand der Welt
    const float edge = smooth(78.0f, 125.0f, r);
    h += edge * edge * 34.0f * (0.75f + 0.5f * fbm(x * 0.03f, z * 0.03f, m_seed + 3u, 2));
    // Kapellenhügel im Norden
    h += 2.6f * std::exp(-((x * x) + (z + 54.0f) * (z + 54.0f)) / (2.0f * 15.0f * 15.0f));
    // ebener Dorfplatz
    h *= smooth(kSquareRadius - 2.0f, kSquareRadius + 12.0f, r);
    // ebene Felder
    if (x > kFieldX0 - 6.0f && x < kFieldX1 + 6.0f && z > kFieldZ0 - 6.0f && z < kFieldZ1 + 6.0f) {
        const float fx = std::min(smooth(kFieldX0 - 6.0f, kFieldX0, x), 1.0f - smooth(kFieldX1, kFieldX1 + 6.0f, x));
        const float fz = std::min(smooth(kFieldZ0 - 6.0f, kFieldZ0, z), 1.0f - smooth(kFieldZ1, kFieldZ1 + 6.0f, z));
        const float flat = std::min(fx, fz);
        h = eng::lerp(h, 0.2f + n * 0.4f, flat);
    }
    return h;
}

void World::buildHeights() {
    const int n = kCells + 1;
    m_heights.assign(static_cast<size_t>(n * n), 0.0f);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = -kSize * 0.5f + static_cast<float>(i) * kCell;
            const float z = -kSize * 0.5f + static_cast<float>(j) * kCell;
            m_heights[static_cast<size_t>(j * n + i)] = rawHeight(x, z) - 0.12f * pathFactor(x, z);
        }
    }
}

float World::gridHeight(int i, int j) const {
    const int n = kCells + 1;
    i = std::clamp(i, 0, kCells);
    j = std::clamp(j, 0, kCells);
    return m_heights[static_cast<size_t>(j * n + i)];
}

float World::height(float x, float z) const {
    if (m_heights.empty()) return 0.0f;
    const float gx = (x + kSize * 0.5f) / kCell;
    const float gz = (z + kSize * 0.5f) / kCell;
    const int i = std::clamp(static_cast<int>(std::floor(gx)), 0, kCells - 1);
    const int j = std::clamp(static_cast<int>(std::floor(gz)), 0, kCells - 1);
    const float fx = std::clamp(gx - static_cast<float>(i), 0.0f, 1.0f);
    const float fz = std::clamp(gz - static_cast<float>(j), 0.0f, 1.0f);
    const float h00 = gridHeight(i, j), h10 = gridHeight(i + 1, j);
    const float h01 = gridHeight(i, j + 1), h11 = gridHeight(i + 1, j + 1);
    // gleiche Dreiecksaufteilung wie das Geländemesh (Diagonale 00 -> 11)
    if (fx > fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

float World::pathFactor(float x, float z) const {
    float best = 1e9f;
    const Vec2 p{x, z};
    for (size_t i = 0; i + 1 < m_pathPoints.size(); i += 2)
        best = std::min(best, distToSegment(p, m_pathPoints[i], m_pathPoints[i + 1]));
    const float wobble = (valueNoise(x * 0.4f, z * 0.4f, m_seed + 99u) - 0.5f) * 0.8f;
    return 1.0f - smooth(1.0f, 2.1f, best + wobble);
}

float World::squareFactor(float x, float z) const {
    const float r = std::sqrt(x * x + z * z);
    const float wobble = (valueNoise(x * 0.5f, z * 0.5f, m_seed + 5u) - 0.5f) * 1.2f;
    return 1.0f - smooth(kSquareRadius - 1.0f, kSquareRadius + 0.5f, r + wobble);
}

float World::fieldFactor(float x, float z) const {
    return (x > kFieldX0 && x < kFieldX1 && z > kFieldZ0 && z < kFieldZ1) ? 1.0f : 0.0f;
}

// --- Erzeugung -------------------------------------------------------------------

void World::addCircle(Vec2 c, float r) {
    Collider col;
    col.shape = Collider::Shape::Circle;
    col.center = c;
    col.radius = r;
    col.bound = r;
    m_colliders.push_back(col);
}

void World::addBox(Vec2 c, Vec2 half, float yaw) {
    Collider col;
    col.shape = Collider::Shape::Box;
    col.center = c;
    col.half = half;
    col.yaw = yaw;
    col.bound = half.length();
    m_colliders.push_back(col);
}

bool World::freeSpot(Vec2 p, float r) const {
    for (const Collider& c : m_colliders)
        if ((p - c.center).length() < c.bound + r) return false;
    return true;
}

void World::generate(uint32_t seed) {
    m_seed = seed;
    m_houses.clear();
    m_trees.clear();
    m_props.clear();
    m_colliders.clear();
    m_pathPoints.clear();

    // Wege: Speichen vom Platz nach außen und ein Ring vor den Häusern
    auto path = [&](std::initializer_list<Vec2> pts) {
        const Vec2* prev = nullptr;
        for (const Vec2& p : pts) {
            if (prev) {
                m_pathPoints.push_back(*prev);
                m_pathPoints.push_back(p);
            }
            prev = &p;
        }
    };
    path({{11, 0}, {26, 1.5f}, {40, -1}, {48, 0}});
    path({{-11, 0}, {-26, 2}, {-40, 5}, {-48, 7}});
    path({{0, -11}, {1.5f, -26}, {0, -40}, {0, -46}});
    path({{0, 11}, {-2, 28}, {2, 48}, {-3, 72}, {0, 98}});
    for (int k = 0; k < 24; ++k) {
        const float a0 = deg(15.0f * static_cast<float>(k)), a1 = deg(15.0f * static_cast<float>(k + 1));
        m_pathPoints.push_back({std::cos(a0) * 19.0f, std::sin(a0) * 19.0f});
        m_pathPoints.push_back({std::cos(a1) * 19.0f, std::sin(a1) * 19.0f});
    }

    buildHeights();
    placeVillage();
    placeNature();
    buildGrid();
}

void World::placeVillage() {
    WorldRng rng(m_seed ^ 0xA11CEu);

    // Lagerfeuer mit Sitzstämmen
    addCircle({0, 0}, 1.4f);
    for (int k = 0; k < 6; ++k) {
        const float a = deg(60.0f * static_cast<float>(k) + 30.0f);
        const Vec2 p{std::cos(a) * 4.8f, std::sin(a) * 4.8f};
        const float yaw = yawToward(p.x, p.y); // lokale X-Achse (Stamm) liegt tangential zum Kreis
        m_props.push_back({PropKind::LogSeat, ground(p.x, p.y), yaw, 1.0f});
        addBox(p, {1.15f, 0.32f}, yaw);
    }

    // Brunnen und Glocke
    m_well = ground(7.5f, -6.0f);
    addCircle({m_well.x, m_well.z}, 1.25f);
    m_bell = ground(-7.0f, -6.5f);
    addBox({m_bell.x, m_bell.z}, {1.1f, 0.9f}, kBellYaw);

    // Häuserring
    const uint32_t walls[] = {0xD9C9A3, 0xCDB892, 0xE3D6BC, 0xC9AE86, 0xBFB5A0, 0xD4BFA0};
    const uint32_t roofs[] = {0x7A3B2E, 0x5E4A3A, 0x6B2E25, 0x55604A, 0x8A5A3C, 0x4E3B30};
    const uint32_t timbers[] = {0x4A3426, 0x3B2A1E, 0x5A3E2A};
    for (int k = 0; k < 12; ++k) {
        const float a = deg(15.0f + 30.0f * static_cast<float>(k) + rng.range(-5.0f, 5.0f));
        const float radius = rng.range(26.0f, 30.0f);
        const Vec2 p{std::cos(a) * radius, std::sin(a) * radius};
        House h;
        const Vec2 toCenter = Vec2{-p.x, -p.y} / radius;
        h.yaw = yawToward(toCenter.x, toCenter.y);
        h.width = rng.range(5.6f, 7.2f);
        h.depth = rng.range(4.6f, 5.6f);
        h.wallHeight = rng.range(2.8f, 3.4f);
        h.roofHeight = rng.range(1.9f, 2.5f);
        h.wallColor = walls[rng.next() % 6];
        h.roofColor = roofs[rng.next() % 6];
        h.timberColor = timbers[rng.next() % 3];
        h.chimney = rng.chance(0.75f);
        h.variant = static_cast<int>(rng.next() % 4);
        // Fundament auf der tiefsten Ecke, damit nichts schwebt
        float lowest = 1e9f;
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cz = -1; cz <= 1; cz += 2) {
                const Vec2 corner = p + rotate({h.width * 0.5f * static_cast<float>(cx), h.depth * 0.5f * static_cast<float>(cz)}, h.yaw);
                lowest = std::min(lowest, height(corner.x, corner.y));
            }
        h.position = {p.x, lowest, p.y};
        const Vec2 door = p + toCenter * (h.depth * 0.5f + 1.2f);
        h.door = ground(door.x, door.y);
        m_houses.push_back(h);
        addBox(p, {h.width * 0.5f + 0.1f, h.depth * 0.5f + 0.1f}, h.yaw);

        // Kleinkram neben dem Haus
        const Vec2 side = p + rotate({h.width * 0.5f + 0.9f, h.depth * 0.25f}, h.yaw);
        if (rng.chance(0.6f)) {
            const PropKind kind = rng.chance(0.5f) ? PropKind::Barrel : PropKind::Crate;
            m_props.push_back({kind, ground(side.x, side.y), h.yaw + rng.range(-0.4f, 0.4f), 1.0f});
            addCircle(side, 0.55f);
        }
        const Vec2 back = p + rotate({rng.range(-1.5f, 1.5f), -(h.depth * 0.5f + 1.1f)}, h.yaw);
        if (rng.chance(0.55f)) {
            m_props.push_back({PropKind::WoodPile, ground(back.x, back.y), h.yaw + kPi * 0.5f, 0.8f});
            addBox(back, {0.9f, 0.6f}, h.yaw + kPi * 0.5f);
        }
    }

    // Kapelle mit Friedhof im Norden
    m_chapel = ground(0.0f, -55.0f);
    m_chapelYaw = 0.0f; // Tür zeigt nach Süden zum Dorf
    addBox({0.0f, -55.0f}, {4.2f, 7.2f}, 0.0f);
    addBox({0.0f, -63.0f}, {2.0f, 2.0f}, 0.0f); // Turm
    for (int gx = 0; gx < 4; ++gx) {
        for (int gz = 0; gz < 4; ++gz) {
            const Vec2 p{9.0f + static_cast<float>(gx) * 2.6f + rng.range(-0.3f, 0.3f),
                         -60.0f + static_cast<float>(gz) * 3.0f + rng.range(-0.3f, 0.3f)};
            const PropKind kind = rng.chance(0.5f) ? PropKind::Tombstone : PropKind::GraveCross;
            m_props.push_back({kind, ground(p.x, p.y), rng.range(-0.15f, 0.15f), rng.range(0.85f, 1.15f)});
            addCircle(p, 0.35f);
        }
    }
    // Friedhofszaun
    auto fenceLine = [&](Vec2 a, Vec2 b) {
        const Vec2 d = b - a;
        const float len = d.length();
        const int pieces = std::max(1, static_cast<int>(len / 2.5f));
        for (int i = 0; i < pieces; ++i) {
            const Vec2 c = a + d * ((static_cast<float>(i) + 0.5f) / static_cast<float>(pieces));
            const float yaw = std::atan2(-d.y, d.x); // lokale X-Achse entlang des Zauns
            m_props.push_back({PropKind::Fence, ground(c.x, c.y), yaw, len / static_cast<float>(pieces)});
            addBox(c, {len / static_cast<float>(pieces) * 0.5f, 0.12f}, yaw);
        }
    };
    fenceLine({7.0f, -62.5f}, {19.5f, -62.5f});
    fenceLine({19.5f, -62.5f}, {19.5f, -48.5f});
    fenceLine({19.5f, -48.5f}, {12.0f, -48.5f});

    // Felder im Osten: Getreidereihen mit Zaun, Eingang im Westen
    for (float z = kFieldZ0 + 1.2f; z < kFieldZ1 - 0.8f; z += 1.7f)
        m_props.push_back({PropKind::CropRow, ground((kFieldX0 + kFieldX1) * 0.5f, z), 0.0f, kFieldX1 - kFieldX0 - 2.0f});
    fenceLine({kFieldX0 - 1.0f, kFieldZ0 - 1.0f}, {kFieldX1 + 1.0f, kFieldZ0 - 1.0f});
    fenceLine({kFieldX1 + 1.0f, kFieldZ0 - 1.0f}, {kFieldX1 + 1.0f, kFieldZ1 + 1.0f});
    fenceLine({kFieldX1 + 1.0f, kFieldZ1 + 1.0f}, {kFieldX0 - 1.0f, kFieldZ1 + 1.0f});
    fenceLine({kFieldX0 - 1.0f, kFieldZ1 + 1.0f}, {kFieldX0 - 1.0f, 2.5f});
    fenceLine({kFieldX0 - 1.0f, -2.5f}, {kFieldX0 - 1.0f, kFieldZ0 - 1.0f});
    for (int i = 0; i < 5; ++i) {
        const Vec2 p{rng.range(kFieldX0 - 6.0f, kFieldX0 - 3.0f), rng.range(-12.0f, 12.0f)};
        if (std::fabs(p.y) < 3.5f) continue;
        m_props.push_back({PropKind::HayBale, ground(p.x, p.y), rng.range(0.0f, kPi), 1.0f});
        addCircle(p, 0.9f);
    }
    m_props.push_back({PropKind::Cart, ground(42.0f, 6.0f), 0.4f, 1.0f});
    addBox({42.0f, 6.0f}, {1.6f, 0.9f}, 0.4f);

    // Holzfällerlager im Westen
    m_props.push_back({PropKind::ChoppingBlock, ground(kCamp.x, kCamp.y), 0.0f, 1.0f});
    addCircle(kCamp, 0.6f);
    for (int i = 0; i < 3; ++i) {
        const Vec2 p = kCamp + Vec2{-3.5f + static_cast<float>(i) * 3.5f, -4.0f};
        m_props.push_back({PropKind::WoodPile, ground(p.x, p.y), 0.0f, 1.2f});
        addBox(p, {1.3f, 0.7f}, 0.0f);
    }
    for (int i = 0; i < 6; ++i) {
        const Vec2 p = kCamp + Vec2{rng.range(-8.0f, 8.0f), rng.range(2.0f, 9.0f)};
        m_props.push_back({PropKind::Stump, ground(p.x, p.y), rng.range(0.0f, kPi), rng.range(0.8f, 1.2f)});
        addCircle(p, 0.5f);
    }

    // Laternen entlang der Wege
    for (size_t i = 0; i + 1 < m_pathPoints.size(); i += 2) {
        const Vec2 a = m_pathPoints[i], b = m_pathPoints[i + 1];
        const Vec2 mid = (a + b) * 0.5f;
        const float r = mid.length();
        if (r < 14.0f || r > 50.0f) continue;
        const bool ringSegment = r > 17.0f && r < 21.0f && std::fabs(a.length() - b.length()) < 0.5f;
        if (ringSegment && (i / 2) % 4 != 0) continue;
        const Vec2 d = (b - a);
        const Vec2 n = Vec2{-d.y, d.x}.normalized();
        const Vec2 p = mid + n * 2.4f;
        if (!freeSpot(p, 1.0f)) continue;
        m_props.push_back({PropKind::Lantern, ground(p.x, p.y), 0.0f, 1.0f});
        addCircle(p, 0.25f);
    }
}

void World::placeNature() {
    WorldRng rng(m_seed ^ 0x7EE5u);

    // Bäume: Raster mit Zufallsversatz, Dichte nimmt nach außen zu
    const float step = 4.2f;
    for (float gz = -kSize * 0.5f + 4.0f; gz < kSize * 0.5f - 4.0f; gz += step) {
        for (float gx = -kSize * 0.5f + 4.0f; gx < kSize * 0.5f - 4.0f; gx += step) {
            const Vec2 p{gx + rng.range(-1.8f, 1.8f), gz + rng.range(-1.8f, 1.8f)};
            const float r = p.length();
            float density = r < 22.0f ? 0.0f : r < 38.0f ? 0.05f : r < 48.0f ? 0.25f : 0.8f;
            if (r > 112.0f) density = 0.95f;
            const bool inField = p.x > kFieldX0 - 8.0f && p.x < kFieldX1 + 8.0f && p.y > kFieldZ0 - 8.0f && p.y < kFieldZ1 + 8.0f;
            const bool nearChapel = std::fabs(p.x) < 24.0f && p.y < -40.0f && p.y > -72.0f;
            const bool nearCamp = (p - kCamp).length() < 13.0f;
            if (inField || nearChapel || nearCamp) density *= 0.05f;
            if (!rng.chance(density)) continue;
            if (pathFactor(p.x, p.y) > 0.02f) continue;
            const float trunk = 0.4f;
            if (r < kBoundary + 4.0f && !freeSpot(p, 1.6f)) continue;

            Tree t;
            const float kindRoll = rng.uniform();
            t.kind = r < 46.0f ? (kindRoll < 0.55f ? TreeKind::Broadleaf : TreeKind::Pine)
                               : (kindRoll < 0.78f ? TreeKind::Pine : TreeKind::Broadleaf);
            if (rng.chance(0.03f)) t.kind = TreeKind::Dead;
            t.scale = rng.range(0.8f, 1.25f) * (r > 60.0f ? rng.range(1.0f, 1.45f) : 1.0f);
            t.yaw = rng.range(0.0f, 2.0f * kPi);
            t.tint = rng.range(-1.0f, 1.0f);
            t.position = ground(p.x, p.y);
            m_trees.push_back(t);
            if (r < kBoundary + 4.0f) addCircle(p, trunk * t.scale);
        }
    }

    // Felsen und Büsche
    for (int i = 0; i < 90; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float r = rng.range(16.0f, 105.0f);
        const Vec2 p{std::cos(a) * r, std::sin(a) * r};
        if (pathFactor(p.x, p.y) > 0.05f || fieldFactor(p.x, p.y) > 0.0f) continue;
        const float scale = rng.range(0.35f, 1.7f);
        if (!freeSpot(p, scale + 0.5f)) continue;
        m_props.push_back({PropKind::Rock, ground(p.x, p.y), rng.range(0.0f, 2.0f * kPi), scale});
        if (scale > 0.6f) addCircle(p, scale * 0.8f);
    }
    for (int i = 0; i < 160; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float r = rng.range(14.0f, 100.0f);
        const Vec2 p{std::cos(a) * r, std::sin(a) * r};
        if (pathFactor(p.x, p.y) > 0.05f || fieldFactor(p.x, p.y) > 0.0f || !freeSpot(p, 0.8f)) continue;
        m_props.push_back({PropKind::Bush, ground(p.x, p.y), rng.range(0.0f, 2.0f * kPi), rng.range(0.6f, 1.3f)});
    }
}

void World::buildGrid() {
    m_grid.assign(static_cast<size_t>(kGridSize * kGridSize), {});
    for (size_t idx = 0; idx < m_colliders.size(); ++idx) {
        const Collider& c = m_colliders[idx];
        const int x0 = static_cast<int>(std::floor((c.center.x - c.bound + kSize * 0.5f) / kGridCell));
        const int x1 = static_cast<int>(std::floor((c.center.x + c.bound + kSize * 0.5f) / kGridCell));
        const int z0 = static_cast<int>(std::floor((c.center.y - c.bound + kSize * 0.5f) / kGridCell));
        const int z1 = static_cast<int>(std::floor((c.center.y + c.bound + kSize * 0.5f) / kGridCell));
        for (int z = std::max(0, z0); z <= std::min(kGridSize - 1, z1); ++z)
            for (int x = std::max(0, x0); x <= std::min(kGridSize - 1, x1); ++x)
                m_grid[static_cast<size_t>(z * kGridSize + x)].push_back(static_cast<uint32_t>(idx));
    }
}

// --- Kollision -------------------------------------------------------------------

Vec2 World::resolveCollision(Vec2 pos, float radius) const {
    for (int iter = 0; iter < 3; ++iter) {
        const int cx = static_cast<int>(std::floor((pos.x + kSize * 0.5f) / kGridCell));
        const int cz = static_cast<int>(std::floor((pos.y + kSize * 0.5f) / kGridCell));
        bool moved = false;
        for (int z = cz - 1; z <= cz + 1; ++z) {
            for (int x = cx - 1; x <= cx + 1; ++x) {
                if (x < 0 || z < 0 || x >= kGridSize || z >= kGridSize) continue;
                for (uint32_t idx : m_grid[static_cast<size_t>(z * kGridSize + x)]) {
                    const Collider& c = m_colliders[idx];
                    const Vec2 d = pos - c.center;
                    if (d.length() > c.bound + radius) continue;
                    if (c.shape == Collider::Shape::Circle) {
                        const float dist = d.length();
                        const float minDist = c.radius + radius;
                        if (dist < minDist) {
                            pos = dist > 1e-4f ? c.center + d / dist * minDist : c.center + Vec2{minDist, 0};
                            moved = true;
                        }
                    } else {
                        Vec2 local = unrotate(d, c.yaw);
                        const Vec2 clamped{std::clamp(local.x, -c.half.x, c.half.x), std::clamp(local.y, -c.half.y, c.half.y)};
                        const Vec2 diff = local - clamped;
                        const float dist = diff.length();
                        if (dist >= radius) continue;
                        if (dist > 1e-4f) {
                            local = clamped + diff / dist * radius;
                        } else {
                            // Mittelpunkt in der Box: entlang der geringsten Eindringtiefe hinausschieben
                            const float px = c.half.x - std::fabs(local.x);
                            const float pz = c.half.y - std::fabs(local.y);
                            if (px < pz) local.x = (local.x < 0 ? -1.0f : 1.0f) * (c.half.x + radius);
                            else local.y = (local.y < 0 ? -1.0f : 1.0f) * (c.half.y + radius);
                        }
                        pos = c.center + rotate(local, c.yaw);
                        moved = true;
                    }
                }
            }
        }
        if (!moved) break;
    }
    const float r = pos.length();
    if (r > kBoundary - radius) pos = pos * ((kBoundary - radius) / r);
    return pos;
}

bool World::blocked(Vec2 pos, float radius) const {
    const Vec2 resolved = resolveCollision(pos, radius);
    return (resolved - pos).length() > 0.01f;
}

bool World::lineBlockedByBuildings(Vec2 a, Vec2 b) const {
    for (const Collider& c : m_colliders) {
        if (c.shape != Collider::Shape::Box || c.half.x < 1.5f || c.half.y < 1.5f) continue;
        if (distToSegment(c.center, a, b) > c.bound) continue;
        // Segment im lokalen Raum gegen die Box (Slab-Test)
        const Vec2 la = unrotate(a - c.center, c.yaw);
        const Vec2 lb = unrotate(b - c.center, c.yaw);
        const Vec2 d = lb - la;
        float t0 = 0.0f, t1 = 1.0f;
        bool hit = true;
        const float pa[2] = {la.x, la.y}, pd[2] = {d.x, d.y}, ph[2] = {c.half.x, c.half.y};
        for (int k = 0; k < 2 && hit; ++k) {
            if (std::fabs(pd[k]) < 1e-6f) {
                if (pa[k] < -ph[k] || pa[k] > ph[k]) hit = false;
            } else {
                float e0 = (-ph[k] - pa[k]) / pd[k], e1 = (ph[k] - pa[k]) / pd[k];
                if (e0 > e1) std::swap(e0, e1);
                t0 = std::max(t0, e0);
                t1 = std::min(t1, e1);
                if (t0 > t1) hit = false;
            }
        }
        if (hit) return true;
    }
    return false;
}

bool World::insideBuilding(Vec2 p, float margin) const {
    for (const Collider& c : m_colliders) {
        if (c.shape != Collider::Shape::Box || c.half.x < 1.5f || c.half.y < 1.5f) continue;
        if ((p - c.center).length() > c.bound + margin) continue;
        const Vec2 local = unrotate(p - c.center, c.yaw);
        if (std::fabs(local.x) < c.half.x + margin && std::fabs(local.y) < c.half.y + margin) return true;
    }
    return false;
}

Vec3 World::spawnPoint(int index, int count) const {
    const float a = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count)) + 0.3f;
    return ground(std::cos(a) * 8.0f, std::sin(a) * 8.0f);
}

} // namespace ww
