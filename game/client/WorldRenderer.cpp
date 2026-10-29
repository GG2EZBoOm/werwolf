#include "client/WorldRenderer.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::Color;
using eng::Mat4;
using eng::MeshBuilder;
using eng::kPi;

namespace {

constexpr int kChunkGrid = 6;
constexpr float kChunkSize = World::kSize / kChunkGrid;

Color hex(uint32_t c) { return Color::hex(c); }

Color mixColor(Color a, Color b, float t) { return eng::lerp(a, b, eng::clamp01(t)); }

float smooth(float e0, float e1, float x) { return eng::smoothstep(e0, e1, x); }

int chunkOf(float x, float z) {
    const int cx = std::clamp(static_cast<int>((x + World::kSize * 0.5f) / kChunkSize), 0, kChunkGrid - 1);
    const int cz = std::clamp(static_cast<int>((z + World::kSize * 0.5f) / kChunkSize), 0, kChunkGrid - 1);
    return cz * kChunkGrid + cx;
}

float rnd(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
}

// --- Gelände -------------------------------------------------------------------

Color terrainColor(const World& w, Vec3 c, Vec3 normal) {
    const float n = valueNoise(c.x * 0.08f, c.z * 0.08f, w.seed() + 11u);
    const float n2 = valueNoise(c.x * 0.3f, c.z * 0.3f, w.seed() + 12u);
    const float r = std::sqrt(c.x * c.x + c.z * c.z);
    Color col = mixColor(hex(0x4C6C35), hex(0x617F3E), n);
    col = mixColor(col, hex(0x7C8A48), smooth(0.7f, 0.95f, n2) * 0.5f);
    col = mixColor(col, hex(0x3E5A2F), smooth(44.0f, 70.0f, r));
    col = mixColor(col, hex(0x6A665C), smooth(0.22f, 0.45f, 1.0f - normal.y));
    col = mixColor(col, hex(0x7E7A72), smooth(16.0f, 26.0f, c.y));
    col = mixColor(col, hex(0x5A422C), w.fieldFactor(c.x, c.z));
    col = mixColor(col, hex(0x8C7655), smooth(0.25f, 0.7f, w.pathFactor(c.x, c.z)));
    col = mixColor(col, hex(0x6F6B64), w.squareFactor(c.x, c.z));
    return col;
}

void buildTerrain(std::vector<MeshBuilder>& chunks, const World& w) {
    for (int j = 0; j < World::kCells; ++j) {
        for (int i = 0; i < World::kCells; ++i) {
            const float x0 = -World::kSize * 0.5f + static_cast<float>(i) * World::kCell;
            const float z0 = -World::kSize * 0.5f + static_cast<float>(j) * World::kCell;
            const float x1 = x0 + World::kCell, z1 = z0 + World::kCell;
            const Vec3 p00{x0, w.gridHeight(i, j), z0}, p10{x1, w.gridHeight(i + 1, j), z0};
            const Vec3 p01{x0, w.gridHeight(i, j + 1), z1}, p11{x1, w.gridHeight(i + 1, j + 1), z1};
            MeshBuilder& b = chunks[static_cast<size_t>(chunkOf(x0 + World::kCell * 0.5f, z0 + World::kCell * 0.5f))];
            b.setColorJitter(0.045f);
            const Vec3 c1 = (p00 + p11 + p10) / 3.0f;
            b.setColor(terrainColor(w, c1, eng::cross(p11 - p00, p10 - p00).normalized()));
            b.triangle(p00, p11, p10);
            const Vec3 c2 = (p00 + p01 + p11) / 3.0f;
            b.setColor(terrainColor(w, c2, eng::cross(p01 - p00, p11 - p00).normalized()));
            b.triangle(p00, p01, p11);
        }
    }
}

void buildGrass(std::vector<MeshBuilder>& chunks, const World& w) {
    uint32_t state = w.seed() * 7u + 3u;
    for (int i = 0; i < 9000; ++i) {
        const float a = rnd(state) * 2.0f * kPi;
        const float r = 6.0f + std::sqrt(rnd(state)) * 90.0f;
        const float x = std::cos(a) * r, z = std::sin(a) * r;
        if (w.pathFactor(x, z) > 0.2f || w.squareFactor(x, z) > 0.2f || w.fieldFactor(x, z) > 0.0f) continue;
        if (w.blocked({x, z}, 0.3f) || w.houseAt({x, z}, 0.6f) >= 0) continue;
        MeshBuilder& b = chunks[static_cast<size_t>(chunkOf(x, z))];
        const Vec3 base = w.ground(x, z);
        const float h = 0.25f + rnd(state) * 0.35f;
        b.setColorJitter(0.15f);
        b.setColor(mixColor(hex(0x6B8C40), hex(0x9AA850), rnd(state)));
        for (int k = 0; k < 3; ++k) {
            const float ba = rnd(state) * 2.0f * kPi;
            const Vec3 side{std::cos(ba) * 0.06f, 0, std::sin(ba) * 0.06f};
            const Vec3 tip = base + Vec3{std::cos(ba + 1.3f) * 0.12f, h, std::sin(ba + 1.3f) * 0.12f};
            b.triangle(base - side, base + side, tip);
            b.triangle(base + side, base - side, tip);
        }
    }
    // Blumen im Dorf
    const uint32_t flowers[] = {0xD9473B, 0xE8C547, 0xEDE6D6, 0x9A6AC8};
    for (int i = 0; i < 500; ++i) {
        const float a = rnd(state) * 2.0f * kPi;
        const float r = 13.0f + rnd(state) * 30.0f;
        const float x = std::cos(a) * r, z = std::sin(a) * r;
        if (w.pathFactor(x, z) > 0.1f || w.blocked({x, z}, 0.3f) || w.houseAt({x, z}, 0.6f) >= 0) continue;
        MeshBuilder& b = chunks[static_cast<size_t>(chunkOf(x, z))];
        b.setColorJitter(0.1f);
        b.setColor(flowers[static_cast<size_t>(rnd(state) * 3.99f)]);
        b.box(w.ground(x, z) + Vec3{0, 0.18f, 0}, {0.12f, 0.1f, 0.12f});
    }
}

// --- Bäume -------------------------------------------------------------------

void buildPine(MeshBuilder& b, float s, float tint) {
    b.setColorJitter(0.06f);
    b.setColor(0x5A4030);
    b.cylinder({0, 0, 0}, 0.2f * s, 1.6f * s, 6, false);
    const Color green = mixColor(hex(0x2C462C), hex(0x40613A), tint * 0.5f + 0.5f);
    for (int k = 0; k < 4; ++k) {
        b.setColorJitter(0.09f);
        b.setColor(mixColor(green, hex(0x4C6E40), static_cast<float>(k) * 0.12f));
        const float y = (1.0f + static_cast<float>(k) * 1.15f) * s;
        b.cone({0, y, 0}, (2.1f - static_cast<float>(k) * 0.45f) * s, 2.1f * s, 7);
    }
}

void buildBroadleaf(MeshBuilder& b, float s, float tint) {
    b.setColorJitter(0.06f);
    b.setColor(0x5E4632);
    b.frustum({0, 0, 0}, 0.3f * s, 0.18f * s, 2.8f * s, 6, false);
    b.beam(Vec3{0, 2.0f, 0} * s, Vec3{0.9f, 2.9f, 0.3f} * s, 0.16f * s);
    const Color green = mixColor(hex(0x4B6E2F), hex(0x6C8E3A), tint * 0.5f + 0.5f);
    b.setColorJitter(0.1f);
    b.setColor(green);
    b.sphere(Vec3{0, 3.6f, 0} * s, 1.75f * s, 3, 7);
    b.sphere(Vec3{1.0f, 3.1f, 0.35f} * s, 1.25f * s, 3, 6);
    b.sphere(Vec3{-0.8f, 3.2f, -0.5f} * s, 1.35f * s, 3, 6);
    b.setColor(mixColor(green, hex(0x86A44A), 0.3f));
    b.sphere(Vec3{0.15f, 4.5f, -0.2f} * s, 1.1f * s, 3, 6);
}

void buildDeadTree(MeshBuilder& b, float s) {
    b.setColorJitter(0.08f);
    b.setColor(0x4E4640);
    b.frustum({0, 0, 0}, 0.28f * s, 0.12f * s, 3.6f * s, 6, false);
    b.beam(Vec3{0, 2.2f, 0} * s, Vec3{1.2f, 3.4f, 0.2f} * s, 0.12f * s);
    b.beam(Vec3{0, 2.8f, 0} * s, Vec3{-1.0f, 3.9f, -0.4f} * s, 0.1f * s);
    b.beam(Vec3{0, 1.6f, 0} * s, Vec3{-0.4f, 2.3f, 1.0f} * s, 0.1f * s);
}

// --- Gebäude -------------------------------------------------------------------

// Fenster in der Wandmitte c: außen leuchtet die Scheibe nachts warm, innen sieht man die dunkle Nacht.
void buildWindow(MeshBuilder& b, Vec3 c, bool sideX, uint32_t frame, float outward) {
    const float pane = House::kWall * 0.5f + 0.02f;
    const Vec3 half = sideX ? Vec3{pane * 0.5f, 0, 0} : Vec3{0, 0, pane * 0.5f};
    const Vec3 size = sideX ? Vec3{pane, 0.78f, 0.72f} : Vec3{0.72f, 0.78f, pane};
    b.setColorJitter(0.0f);
    b.setGlow(1.0f);
    b.setColor(0x2B3340);
    b.box(c + half * outward, size);
    b.setGlow(0.0f);
    b.setColor(0x1C2433);
    b.box(c - half * outward, size);
    b.setColor(frame);
    const float face = House::kWall * 0.5f + 0.03f;
    for (float side : {outward, -outward}) {
        const Vec3 o = sideX ? Vec3{side * face, 0, 0} : Vec3{0, 0, side * face};
        if (sideX) {
            b.box(c + o, {0.06f, 0.82f, 0.08f});
            b.box(c + o, {0.06f, 0.08f, 0.76f});
        } else {
            b.box(c + o, {0.08f, 0.82f, 0.06f});
            b.box(c + o, {0.76f, 0.08f, 0.06f});
        }
    }
    // Fensterbank und Läden außen
    const Vec3 o = sideX ? Vec3{outward * face, 0, 0} : Vec3{0, 0, outward * face};
    if (sideX) b.box(c + o + Vec3{0, -0.45f, 0}, {0.14f, 0.1f, 0.9f});
    else b.box(c + o + Vec3{0, -0.45f, 0}, {0.9f, 0.1f, 0.14f});
    b.setColorJitter(0.05f);
    b.setColor(0x4D5E44);
    if (sideX) {
        b.box(c + o + Vec3{0, 0, 0.58f}, {0.05f, 0.82f, 0.36f});
        b.box(c + o + Vec3{0, 0, -0.58f}, {0.05f, 0.82f, 0.36f});
    } else {
        b.box(c + o + Vec3{0.58f, 0, 0}, {0.36f, 0.82f, 0.05f});
        b.box(c + o + Vec3{-0.58f, 0, 0}, {0.36f, 0.82f, 0.05f});
    }
}

constexpr float kDoorTop = 2.05f; // Höhe der Türöffnung über dem Fußboden

// Fundament, hohle Wände mit Türöffnung, Fachwerk, Fenster und Einrichtung (lokale Koordinaten).
void buildHouseShell(MeshBuilder& b, const House& h) {
    const float w = h.width, d = h.depth, wh = h.wallHeight;
    const float fy = h.floorY - h.position.y;
    const float t = House::kWall, hw = w * 0.5f, hd = d * 0.5f, gap = House::kDoorWidth * 0.5f;

    // Fundament und Dielenboden
    b.setColorJitter(0.08f);
    b.setColor(0x726C64);
    b.box({0, fy - 1.15f, 0}, {w + 0.4f, 2.3f, d + 0.4f});
    b.setColorJitter(0.05f);
    const int planks = std::max(1, static_cast<int>((d - 2.0f * t) / 0.42f));
    const float plank = (d - 2.0f * t) / static_cast<float>(planks);
    for (int i = 0; i < planks; ++i) {
        b.setColor(i % 2 ? 0x83643F : 0x8E6E48);
        b.box({0, fy + 0.01f, -hd + t + plank * (static_cast<float>(i) + 0.5f)}, {w - 2.0f * t, 0.02f, plank - 0.015f});
    }

    // Wände (die Seitenwände liegen zwischen Vorder- und Rückwand, damit sich nichts überlappt)
    b.setColorJitter(0.025f);
    b.setColor(h.wallColor);
    const float wy = fy + wh * 0.5f;
    const float seg = hw - gap;
    b.box({0, wy, -hd + t * 0.5f}, {w, wh, t});
    b.box({-hw + t * 0.5f, wy, 0}, {t, wh, d - 2.0f * t});
    b.box({hw - t * 0.5f, wy, 0}, {t, wh, d - 2.0f * t});
    b.box({-(gap + seg * 0.5f), wy, hd - t * 0.5f}, {seg, wh, t});
    b.box({gap + seg * 0.5f, wy, hd - t * 0.5f}, {seg, wh, t});
    b.box({0, fy + (kDoorTop + wh) * 0.5f, hd - t * 0.5f}, {gap * 2.0f, wh - kDoorTop, t});

    // Fachwerk
    b.setColorJitter(0.07f);
    b.setColor(h.timberColor);
    const float tt = 0.2f, o = 0.03f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            b.box({static_cast<float>(sx) * (hw - tt * 0.5f + o), wy, static_cast<float>(sz) * (hd - tt * 0.5f + o)}, {tt, wh, tt});
    for (float y : {fy + 0.08f, fy + wh * 0.5f + 0.25f, fy + wh - 0.08f}) {
        if (y < fy + kDoorTop + 0.1f) {
            // vorne nicht quer durch die Türöffnung
            const float len = hw + 0.02f - 0.69f;
            b.box({-(0.69f + len * 0.5f), y, hd + o}, {len, 0.16f, 0.08f});
            b.box({0.69f + len * 0.5f, y, hd + o}, {len, 0.16f, 0.08f});
        } else {
            b.box({0, y, hd + o}, {w + 0.04f, 0.16f, 0.08f});
        }
        b.box({0, y, -hd - o}, {w + 0.04f, 0.16f, 0.08f});
        b.box({hw + o, y, 0}, {0.08f, 0.16f, d + 0.04f});
        b.box({-hw - o, y, 0}, {0.08f, 0.16f, d + 0.04f});
    }
    b.beam({-hw + 0.2f, fy + 0.1f, -hd - o}, {-w * 0.2f, fy + wh * 0.5f + 0.2f, -hd - o}, 0.12f);
    b.beam({hw - 0.2f, fy + 0.1f, -hd - o}, {w * 0.2f, fy + wh * 0.5f + 0.2f, -hd - o}, 0.12f);
    b.beam({hw + o, fy + wh * 0.5f + 0.3f, -d * 0.45f}, {hw + o, fy + wh - 0.1f, -d * 0.1f}, 0.12f);
    b.beam({-hw - o, fy + wh * 0.5f + 0.3f, d * 0.45f}, {-hw - o, fy + wh - 0.1f, d * 0.1f}, 0.12f);

    // Türrahmen (außen und innen) und Stufe
    b.setColor(h.timberColor);
    for (float z : {hd + 0.07f, hd - t - 0.05f}) {
        b.box({0, fy + kDoorTop + 0.02f, z}, {1.4f, 0.14f, 0.12f});
        b.box({0.62f, fy + 1.0f, z}, {0.14f, 2.0f, 0.12f});
        b.box({-0.62f, fy + 1.0f, z}, {0.14f, 2.0f, 0.12f});
    }
    b.setColor(0x7A746A);
    b.box({0, fy - 0.12f, hd + 0.45f}, {1.5f, 0.24f, 0.7f});

    // Fenster
    const float winY = fy + wh * 0.6f;
    buildWindow(b, {-w * 0.3f, winY, hd - t * 0.5f}, false, h.timberColor, 1.0f);
    buildWindow(b, {w * 0.3f, winY, hd - t * 0.5f}, false, h.timberColor, 1.0f);
    buildWindow(b, {hw - t * 0.5f, winY, d * 0.25f}, true, h.timberColor, 1.0f);
    buildWindow(b, {-hw + t * 0.5f, winY, d * 0.1f}, true, h.timberColor, -1.0f);
    buildWindow(b, {0, winY, -hd + t * 0.5f}, false, h.timberColor, -1.0f);

    // --- Einrichtung (passend zu den Kollisionsboxen in World::addHouseColliders) ---
    const uint32_t blankets[] = {0x8C3B33, 0x3E5A7A, 0x5E7A3E, 0x7A5A8C};
    const uint32_t accent = blankets[static_cast<size_t>(h.variant) % 4];

    // Bett an der linken Rückwand
    const Vec2 bed{-hw + t + 0.5f, -hd + t + 1.0f};
    b.setColorJitter(0.06f);
    b.setColor(0x6B4A30);
    b.box({bed.x, fy + 0.2f, bed.y}, {0.96f, 0.4f, 1.96f});
    b.box({bed.x, fy + 0.5f, bed.y - 0.94f}, {0.96f, 0.95f, 0.08f});
    b.setColor(0xD8D0BC);
    b.box({bed.x, fy + 0.47f, bed.y - 0.66f}, {0.72f, 0.12f, 0.38f});
    b.setColor(accent);
    b.box({bed.x, fy + 0.45f, bed.y + 0.24f}, {0.92f, 0.1f, 1.42f});

    // Tisch mit Hocker, Krug und Kerze
    const Vec2 table{hw * 0.25f, 0.15f};
    b.setColor(0x7A5A3C);
    b.box({table.x, fy + 0.74f, table.y}, {1.1f, 0.07f, 0.8f});
    for (float lx : {-0.47f, 0.47f})
        for (float lz : {-0.32f, 0.32f}) b.box({table.x + lx, fy + 0.36f, table.y + lz}, {0.07f, 0.72f, 0.07f});
    b.setColor(0x6E5038);
    b.box({table.x - 0.1f, fy + 0.22f, table.y - 0.62f}, {0.36f, 0.44f, 0.36f});
    b.setColor(0xB89A70);
    b.cylinder({table.x + 0.25f, fy + 0.78f, table.y + 0.1f}, 0.08f, 0.16f, 6);
    b.setColor(0x9A9288);
    b.cylinder({table.x - 0.2f, fy + 0.78f, table.y}, 0.15f, 0.02f, 8);
    b.setColor(0xE8E0C8);
    b.box({table.x + 0.05f, fy + 0.84f, table.y - 0.2f}, {0.05f, 0.14f, 0.05f});
    b.setGlow(1.0f);
    b.setColor(0x3A2A10);
    b.box({table.x + 0.05f, fy + 0.94f, table.y - 0.2f}, {0.04f, 0.06f, 0.04f});
    b.setGlow(0.0f);

    // Kamin an der rechten Wand mit Rauchfang bis unters Dach
    const Vec2 fire{hw - t - 0.32f, -hd * 0.25f};
    b.setColorJitter(0.1f);
    b.setColor(0x6E665E);
    b.box({fire.x, fy + 0.06f, fire.y}, {0.64f, 0.12f, 1.2f});
    b.box({fire.x, fy + 0.55f, fire.y - 0.5f}, {0.64f, 0.9f, 0.2f});
    b.box({fire.x, fy + 0.55f, fire.y + 0.5f}, {0.64f, 0.9f, 0.2f});
    b.box({fire.x + 0.22f, fy + 0.55f, fire.y}, {0.2f, 0.9f, 0.8f});
    b.setColor(0x5A4030);
    b.box({fire.x - 0.04f, fy + 1.06f, fire.y}, {0.74f, 0.12f, 1.3f});
    b.setColor(0x6E665E);
    b.box({fire.x + 0.08f, fy + (1.12f + wh) * 0.5f, fire.y}, {0.48f, wh - 1.12f, 0.9f});
    b.setColor(0x221A14);
    b.box({fire.x - 0.02f, fy + 0.13f, fire.y}, {0.4f, 0.02f, 0.7f});
    b.setColorJitter(0.12f);
    b.setColor(0x4A3424);
    b.beam({fire.x - 0.1f, fy + 0.17f, fire.y - 0.25f}, {fire.x + 0.05f, fy + 0.2f, fire.y + 0.25f}, 0.09f);
    b.beam({fire.x + 0.05f, fy + 0.17f, fire.y - 0.25f}, {fire.x - 0.1f, fy + 0.22f, fire.y + 0.25f}, 0.09f);

    // Truhe in der rechten hinteren Ecke
    const Vec2 chest{hw - t - 0.45f, -hd + t + 0.3f};
    b.setColorJitter(0.08f);
    b.setColor(0x7A5534);
    b.box({chest.x, fy + 0.25f, chest.y}, {0.9f, 0.5f, 0.56f});
    b.setColor(0x3A3430);
    b.box({chest.x - 0.3f, fy + 0.26f, chest.y}, {0.06f, 0.52f, 0.58f});
    b.box({chest.x + 0.3f, fy + 0.26f, chest.y}, {0.06f, 0.52f, 0.58f});

    // Wandbord mit Töpfen an der Rückwand und ein Teppich
    b.setColor(0x6B4A30);
    b.box({-0.15f, fy + 1.4f, -hd + t + 0.13f}, {1.3f, 0.06f, 0.26f});
    const uint32_t pots[] = {0x9A6A4A, 0x5E6E7A, 0xB8A878, 0x7A4A3A};
    for (int i = 0; i < 4; ++i) {
        b.setColor(pots[i]);
        b.cylinder({-0.6f + static_cast<float>(i) * 0.3f, fy + 1.43f, -hd + t + 0.13f}, 0.08f, 0.14f + 0.04f * static_cast<float>(i % 2), 6);
    }
    b.setColorJitter(0.04f);
    b.setColor(eng::lerp(hex(accent), hex(0xC8B890), 0.45f));
    b.box({-0.35f, fy + 0.025f, 0.75f}, {1.5f, 0.012f, 1.1f});

    // Varianten außen
    if (h.variant == 0) {
        b.setColorJitter(0.07f);
        b.setColor(0x5C3E28);
        b.box({-w * 0.3f, winY - 0.55f, hd + 0.22f}, {0.9f, 0.2f, 0.25f});
        b.box({w * 0.3f, winY - 0.55f, hd + 0.22f}, {0.9f, 0.2f, 0.25f});
        b.setColorJitter(0.2f);
        b.setColor(0xC8423B);
        for (float x : {-0.35f, -0.15f, 0.05f, 0.25f}) {
            b.box({-w * 0.3f + x * 1.2f + 0.05f, winY - 0.4f, hd + 0.22f}, {0.12f, 0.12f, 0.12f});
            b.box({w * 0.3f + x * 1.2f + 0.05f, winY - 0.4f, hd + 0.22f}, {0.12f, 0.12f, 0.12f});
        }
    } else if (h.variant == 1) {
        b.setColorJitter(0.07f);
        b.setColor(0x6B5038);
        b.box({w * 0.32f, fy + 0.45f, hd + 0.45f}, {1.4f, 0.1f, 0.4f});
        b.box({w * 0.32f - 0.55f, fy + 0.2f, hd + 0.45f}, {0.1f, 0.4f, 0.35f});
        b.box({w * 0.32f + 0.55f, fy + 0.2f, hd + 0.45f}, {0.1f, 0.4f, 0.35f});
    } else if (h.variant == 2) {
        // kleiner Anbau
        b.setColorJitter(0.03f);
        b.setColor(h.wallColor);
        b.box({-hw - 1.0f, fy + 1.1f, -d * 0.15f}, {2.0f, 2.2f, d * 0.6f});
        b.setColor(h.roofColor);
        b.setColorJitter(0.08f);
        b.quad({-hw - 2.2f, fy + 2.0f, d * 0.2f}, {-hw, fy + 2.9f, d * 0.2f}, {-hw, fy + 2.9f, -d * 0.5f},
               {-hw - 2.2f, fy + 2.0f, -d * 0.5f});
    }
}

// Dach, Giebel und Schornstein (lokale Koordinaten) – wird ausgeblendet, wenn man drinnen ist.
void buildHouseRoof(MeshBuilder& b, const House& h) {
    const float w = h.width, d = h.depth, wh = h.wallHeight, rh = h.roofHeight;
    const float fy = h.floorY - h.position.y;
    const float hw = w * 0.5f, hd = d * 0.5f, o = 0.03f;
    b.setColorJitter(0.025f);
    b.setColor(h.wallColor);
    b.gableWalls({0, fy + wh, 0}, w, d, rh, 0.45f);
    b.setColorJitter(0.07f);
    b.setColor(h.timberColor);
    b.beam({-hw, fy + wh + 0.05f, hd + o}, {0, fy + wh + rh * 0.85f, hd + o}, 0.12f);
    b.beam({hw, fy + wh + 0.05f, hd + o}, {0, fy + wh + rh * 0.85f, hd + o}, 0.12f);
    b.setColorJitter(0.08f);
    b.setColor(h.roofColor);
    b.gableRoof({0, fy + wh, 0}, w, d, rh, 0.45f, false);
    b.setColor(eng::lerp(hex(h.roofColor), hex(0x000000), 0.35f));
    b.box({0, fy + wh + rh + 0.03f, 0}, {0.2f, 0.16f, d + 0.95f});
    // Schornstein direkt über dem Kamin
    const Vec2 fire{hw - House::kWall - 0.24f, -hd * 0.25f};
    const float top = fy + wh + rh * 0.55f + 0.9f;
    b.setColor(0x6E665E);
    b.box({fire.x, fy + wh + (top - fy - wh) * 0.5f, fire.y}, {0.5f, top - fy - wh, 0.6f});
    b.setColor(0x4E4740);
    b.box({fire.x, top, fire.y}, {0.62f, 0.12f, 0.72f});
}

// Türblatt, Ursprung an der Angel (links), öffnet sich um die Y-Achse nach innen.
void buildDoorLeaf(MeshBuilder& b, bool reinforced, float wd, float height) {
    b.setColorJitter(0.05f);
    b.setColor(0x5A3A22);
    b.box({wd * 0.5f, height * 0.5f, 0}, {wd, height, 0.08f});
    b.setColor(0x4A2E1A);
    for (float y : {0.35f, height - 0.36f})
        for (float z : {0.05f, -0.05f}) b.box({wd * 0.5f, y, z}, {wd - 0.08f, 0.12f, 0.03f});
    b.setColor(0x3A2618);
    b.box({wd - 0.15f, height * 0.485f, 0.08f}, {0.08f, 0.08f, 0.06f});
    b.box({wd - 0.15f, height * 0.485f, -0.08f}, {0.08f, 0.08f, 0.06f});
    if (!reinforced) return;
    b.setColorJitter(0.03f);
    b.setColor(0x4A4E55);
    for (float y : {0.3f, height * 0.5f, height - 0.3f}) {
        for (float z : {0.056f, -0.056f}) b.box({wd * 0.5f, y, z}, {wd + 0.02f, 0.11f, 0.03f});
        b.setColor(0x6A6E75);
        for (float x : {0.12f, wd * 0.5f, wd - 0.12f}) b.box({x, y, 0.075f}, {0.05f, 0.05f, 0.03f});
        b.setColor(0x4A4E55);
    }
}

// --- Kapelle -------------------------------------------------------------------

constexpr uint32_t kStone = 0xB8B0A0;

// Spitzbogenfenster in einer Seitenwand (Wandmitte x, Außenseite s): Laibung und Bleiruten in der Hülle,
// das bunte Glas in einem eigenen Mesh, das von selbst leicht leuchtet.
void buildChapelWindow(MeshBuilder& b, MeshBuilder& glass, float x, float s, float z, float y0, float height, int seed) {
    const float t = House::kWall, wide = 0.9f;
    b.setColorJitter(0.05f);
    b.setColor(kStone);
    for (float face : {1.0f, -1.0f}) {
        const float fx = x + s * face * (t * 0.5f + 0.03f);
        b.box({fx, y0 - 0.08f, z}, {0.1f, 0.16f, wide + 0.4f});
        for (float e : {-1.0f, 1.0f}) {
            b.box({fx, y0 + height * 0.5f, z + e * (wide * 0.5f + 0.09f)}, {0.08f, height, 0.18f});
            b.beam({fx, y0 + height - 0.05f, z + e * (wide * 0.5f + 0.09f)}, {fx, y0 + height + 0.62f, z}, 0.17f);
        }
    }
    // Bleiruten hinter dem Glas (dunkel, etwas weniger dick als das Glas)
    b.setColorJitter(0.0f);
    b.setColor(0x24201C);
    b.box({x, y0 + height * 0.5f, z}, {t + 0.03f, height, wide});
    b.box({x, y0 + height + 0.16f, z}, {t + 0.03f, 0.32f, wide * 0.62f});
    b.box({x, y0 + height + 0.4f, z}, {t + 0.03f, 0.18f, wide * 0.3f});

    const uint32_t colors[] = {0x2E4A9A, 0x9A2A2A, 0xC89A3A, 0x2E7A5A, 0x6A3A8A, 0x3A6AAA};
    glass.setColorJitter(0.08f);
    constexpr int kCols = 3, kRows = 5;
    const float pw = wide / kCols, ph = height / kRows;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            // Mittelbahn mit eigenem Muster, Ränder blau-golden
            const int pick = c == 1 ? (r + seed) % 6 : ((r + seed) % 2 == 0 ? 0 : 2);
            glass.setColor(colors[pick]);
            glass.box({x, y0 + ph * (static_cast<float>(r) + 0.5f), z - wide * 0.5f + pw * (static_cast<float>(c) + 0.5f)},
                      {t + 0.05f, ph - 0.05f, pw - 0.05f});
        }
    }
    glass.setColor(colors[(seed + 1) % 6]);
    glass.box({x, y0 + height + 0.16f, z}, {t + 0.05f, 0.27f, wide * 0.62f - 0.06f});
    glass.setColor(colors[2]);
    glass.box({x, y0 + height + 0.4f, z}, {t + 0.05f, 0.13f, wide * 0.3f - 0.06f});
}

// Kerzenflamme vormerken (lokal) und Kerze bauen
void buildCandle(MeshBuilder& b, Vec3 base, float height, std::vector<Vec3>& flames) {
    b.setColorJitter(0.03f);
    b.setColor(0xF0E8D0);
    b.cylinder(base, 0.035f, height, 6);
    b.setGlow(1.0f);
    b.setColor(0x3A2A10);
    b.box(base + Vec3{0, height + 0.03f, 0}, {0.02f, 0.05f, 0.02f});
    b.setGlow(0.0f);
    flames.push_back(base + Vec3{0, height + 0.07f, 0});
}

// Hülle der Kapelle: Sockel, Steinboden, Wände, Fenster, Turm, Portal und die ganze Einrichtung (lokal).
void buildChapelShell(MeshBuilder& b, MeshBuilder& glass, const House& h, float stepRise, std::vector<Vec3>& flames) {
    using namespace chapel;
    const float w = h.width, d = h.depth, wh = h.wallHeight;
    const float fy = h.floorY - h.position.y;
    const float t = House::kWall, hw = w * 0.5f, hd = d * 0.5f, gap = h.doorWidth * 0.5f;
    const float iw = hw - t, id = hd - t; // halbe Innenmaße
    const float base = -2.5f;
    const float cy = fy + kChancelStep; // Boden des Altarraums

    // Sockel aus Bruchstein, Turmfundament inklusive
    b.setColorJitter(0.08f);
    b.setColor(0x77726A);
    b.box({0, (fy + base) * 0.5f, 0}, {w + 0.5f, fy - base, d + 0.5f});

    // Steinplatten im Kirchenschiff, das Fugenbild ist der Sockel darunter
    const int nx = 9, nz = 15;
    const float sx = 2.0f * iw / static_cast<float>(nx), sz = 2.0f * id / static_cast<float>(nz);
    const uint32_t slabs[] = {0x9A948A, 0x8C867C, 0xA49E92, 0x948D82};
    b.setColorJitter(0.05f);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < nz; ++j) {
            const float z = -id + sz * (static_cast<float>(j) + 0.5f);
            if (z < kChancelZ - sz * 0.5f) continue;
            b.setColor(slabs[static_cast<size_t>((i * 7 + j * 13 + (i * j) % 3) % 4)]);
            b.box({-iw + sx * (static_cast<float>(i) + 0.5f), fy + 0.015f, z}, {sx - 0.035f, 0.03f, sz - 0.035f});
        }
    }
    // Altarraum eine Stufe höher, mit heller Stufenkante
    b.setColorJitter(0.04f);
    b.setColor(0xA8A196);
    b.box({0, fy + kChancelStep * 0.5f, (-id + kChancelZ) * 0.5f}, {2.0f * iw, kChancelStep, kChancelZ + id});
    b.setColor(0xC2BBAD);
    b.box({0, cy - 0.025f, kChancelZ - 0.07f}, {2.0f * iw, 0.06f, 0.16f});

    // Roter Läufer im Mittelgang bis vor den Altar, mit Goldborte
    const float runFront = id - 0.45f, altarFront = kAltar.y + kAltarHalf.y + 0.12f;
    b.setColorJitter(0.03f);
    b.setColor(0x7A2A26);
    b.box({0, fy + 0.036f, (runFront + kChancelZ) * 0.5f}, {1.2f, 0.012f, runFront - kChancelZ});
    b.box({0, cy + 0.006f, (kChancelZ + altarFront) * 0.5f}, {1.2f, 0.012f, kChancelZ - altarFront});
    b.setColor(0xB08A3A);
    for (float s : {-1.0f, 1.0f}) {
        b.box({s * 0.57f, fy + 0.038f, (runFront + kChancelZ) * 0.5f}, {0.05f, 0.012f, runFront - kChancelZ});
        b.box({s * 0.57f, cy + 0.008f, (kChancelZ + altarFront) * 0.5f}, {0.05f, 0.012f, kChancelZ - altarFront});
    }

    // Weiß gekalkte Wände mit Türöffnung
    b.setColorJitter(0.02f);
    b.setColor(h.wallColor);
    const float wy = fy + wh * 0.5f, seg = hw - gap;
    b.box({0, wy, -hd + t * 0.5f}, {w, wh, t});
    b.box({-hw + t * 0.5f, wy, 0}, {t, wh, d - 2.0f * t});
    b.box({hw - t * 0.5f, wy, 0}, {t, wh, d - 2.0f * t});
    b.box({-(gap + seg * 0.5f), wy, hd - t * 0.5f}, {seg, wh, t});
    b.box({gap + seg * 0.5f, wy, hd - t * 0.5f}, {seg, wh, t});
    b.box({0, fy + (kDoorHeight + wh) * 0.5f, hd - t * 0.5f}, {gap * 2.0f, wh - kDoorHeight, t});

    // Holzvertäfelung innen
    b.setColorJitter(0.05f);
    const float panel = 1.15f;
    const float frontSeg = iw - gap - 0.12f;
    auto wainscot = [&](uint32_t color, float height, float y, float inset) {
        b.setColor(color);
        b.box({0, y, -id + inset}, {2.0f * iw, height, 0.04f});
        for (float s : {-1.0f, 1.0f}) {
            b.box({s * (iw - inset), y, 0}, {0.04f, height, 2.0f * id});
            b.box({s * (gap + 0.12f + frontSeg * 0.5f), y, id - inset}, {frontSeg, height, 0.04f});
        }
    };
    wainscot(0x6E5238, panel, fy + panel * 0.5f, 0.02f);
    wainscot(0x58402A, 0.08f, fy + panel + 0.02f, 0.04f);

    // Außen: Sockelband, Eckquader, Traufgesims und Strebepfeiler aus hellem Stein
    b.setColorJitter(0.06f);
    b.setColor(0x8E887C);
    const float course = 0.65f;
    for (float s : {-1.0f, 1.0f}) {
        b.box({s * (hw + 0.05f), fy + course * 0.5f, 0}, {0.12f, course, d + 0.1f});
        b.box({s * (gap + 0.6f + (hw - gap - 0.6f) * 0.5f), fy + course * 0.5f, hd + 0.05f}, {hw - gap - 0.6f, course, 0.12f});
    }
    b.box({0, fy + course * 0.5f, -hd - 0.05f}, {w + 0.1f, course, 0.12f});
    // Eckquader nur als Verblendung außen, damit innen nichts durch die dünne Wand ragt
    b.setColor(kStone);
    for (int k = 0; k < 8; ++k) {
        const float y = fy + course + 0.2f + static_cast<float>(k) * 0.62f;
        const float lx = k % 2 == 0 ? 0.62f : 0.42f, lz = k % 2 == 0 ? 0.42f : 0.62f;
        for (float s : {-1.0f, 1.0f}) {
            for (float e : {-1.0f, 1.0f}) {
                b.box({s * (hw + 0.02f - lx * 0.5f), y, e * (hd - 0.03f)}, {lx, 0.3f, 0.1f});
                b.box({s * (hw - 0.03f), y, e * (hd + 0.02f - lz * 0.5f)}, {0.1f, 0.3f, lz});
            }
        }
    }
    for (float s : {-1.0f, 1.0f}) b.box({s * (hw + 0.08f), fy + wh - 0.12f, 0}, {0.2f, 0.24f, d + 0.2f});
    for (float z : {-hd + 0.3f, -2.3f, 2.3f, hd - 0.3f}) {
        for (float s : {-1.0f, 1.0f}) {
            b.setColor(kStone);
            b.box({s * (hw + 0.22f), fy + 1.4f, z}, {0.44f, 2.8f, 0.6f});
            b.box({s * (hw + 0.15f), fy + 3.6f, z}, {0.3f, 1.6f, 0.5f});
            b.setColor(0xA49C8C);
            b.beam({s * (hw + 0.42f), fy + 2.8f, z}, {s * (hw + 0.16f), fy + 3.15f, z}, 0.42f);
            b.beam({s * (hw + 0.28f), fy + 4.4f, z}, {s * (hw + 0.04f), fy + 4.85f, z}, 0.34f);
        }
    }

    // Spitzbogenfenster in beiden Langseiten
    int seed = 0;
    for (float z : {-4.6f, 0.0f, 4.6f})
        for (float s : {-1.0f, 1.0f}) buildChapelWindow(b, glass, s * (hw - t * 0.5f), s, z, fy + 2.0f, 2.4f, seed++);

    // Portal: Gewände, Säulen, Rundbogen aus Keilsteinen und ein Bogenfeld mit Kreuz
    const float pz = hd + 0.08f;
    b.setColorJitter(0.05f);
    b.setColor(kStone);
    for (float s : {-1.0f, 1.0f}) {
        b.box({s * (gap + 0.15f), fy + kDoorHeight * 0.5f, pz}, {0.3f, kDoorHeight, 0.2f});
        b.box({s * (gap + 0.45f), fy + (kDoorHeight + 0.35f) * 0.5f, pz + 0.05f}, {0.24f, kDoorHeight + 0.35f, 0.28f});
        b.box({s * (gap + 0.45f), fy + kDoorHeight + 0.42f, pz + 0.05f}, {0.34f, 0.14f, 0.36f});
    }
    b.setColor(0x9E9686);
    b.box({0, fy + kDoorHeight + 0.42f, pz - 0.02f}, {gap * 2.0f, 0.84f, 0.14f});
    b.setColor(0x6A5A44);
    b.box({0, fy + kDoorHeight + 0.45f, pz + 0.06f}, {0.08f, 0.56f, 0.04f});
    b.box({0, fy + kDoorHeight + 0.55f, pz + 0.06f}, {0.36f, 0.08f, 0.04f});
    b.setColor(kStone);
    constexpr int kVoussoirs = 9;
    for (int k = 0; k < kVoussoirs; ++k) {
        const float a = kPi * (static_cast<float>(k) + 0.5f) / static_cast<float>(kVoussoirs);
        const float rx = std::cos(a) * (gap + 0.18f), ry = std::sin(a) * 0.95f;
        b.setTransform(Mat4::translation({rx, fy + kDoorHeight + ry, pz + 0.02f}) * Mat4::rotationZ(a - kPi * 0.5f));
        b.box({0, 0, 0}, {0.36f, 0.34f, 0.24f});
    }
    b.setTransform(Mat4::identity());
    // Türrahmen innen
    b.setColor(h.timberColor);
    b.box({0, fy + kDoorHeight + 0.05f, id - 0.05f}, {gap * 2.0f + 0.3f, 0.14f, 0.12f});
    for (float s : {-1.0f, 1.0f}) b.box({s * (gap + 0.08f), fy + kDoorHeight * 0.5f, id - 0.05f}, {0.14f, kDoorHeight, 0.12f});

    // Steinstufen vor dem Portal (die Rampe in World::height folgt ihnen)
    const float stepTop[] = {fy - 0.02f, fy - stepRise * 0.36f, fy - stepRise * 0.7f};
    const float stepDepth = h.ramp / 3.0f;
    for (int k = 0; k < 3; ++k) {
        const float bottom = fy - stepRise - 0.4f;
        b.setColor(k % 2 ? 0xA8A092 : 0xB4AC9E);
        b.box({0, (stepTop[k] + bottom) * 0.5f, hd + stepDepth * (static_cast<float>(k) + 0.5f)},
              {gap * 2.0f + 1.3f, stepTop[k] - bottom, stepDepth});
    }

    // --- Einrichtung (passend zu den Kollisionen in World::addHouseColliders) ---

    // Bankreihen: Sitz, Lehne, Wangen und Kniebank
    for (int row = 0; row < kPewRows; ++row) {
        const float z = pewZ(row);
        for (float s : {-1.0f, 1.0f}) {
            const float cx = s * (kPewInner + kPewOuter) * 0.5f, len = kPewOuter - kPewInner;
            b.setColorJitter(0.05f);
            b.setColor(0x6B4A30);
            b.box({cx, fy + 0.45f, z - 0.05f}, {len - 0.1f, 0.06f, 0.42f});
            b.box({cx, fy + 0.8f, z + 0.2f}, {len - 0.1f, 0.52f, 0.05f});
            b.setColor(0x5A3C26);
            for (float e : {-1.0f, 1.0f}) {
                b.box({cx + e * (len * 0.5f - 0.04f), fy + 0.5f, z}, {0.07f, 1.0f, 0.54f});
                b.box({cx + e * (len * 0.5f - 0.04f), fy + 1.02f, z + 0.12f}, {0.1f, 0.08f, 0.28f});
            }
            b.box({cx, fy + 0.2f, z + 0.14f}, {len - 0.12f, 0.05f, 0.05f});
            b.box({cx, fy + 0.11f, z - 0.2f}, {len - 0.12f, 0.06f, 0.12f});
            // Gesangbuch auf manchen Bänken
            if ((row + (s > 0 ? 1 : 0)) % 3 == 0) {
                b.setColor(0x3A2A4A);
                b.box({cx + s * 0.3f, fy + 0.5f, z - 0.1f}, {0.16f, 0.04f, 0.22f});
            }
        }
    }

    // Altar mit Tuch, Antependium, Kerzen und Kreuz
    b.setColorJitter(0.04f);
    b.setColor(0xCFC8B8);
    b.box({kAltar.x, cy + 0.5f, kAltar.y}, {kAltarHalf.x * 2.0f, 1.0f, kAltarHalf.y * 2.0f});
    b.setColor(kStone);
    b.box({kAltar.x, cy + 1.03f, kAltar.y}, {kAltarHalf.x * 2.0f + 0.12f, 0.06f, kAltarHalf.y * 2.0f + 0.12f});
    b.setColorJitter(0.02f);
    b.setColor(0xEDE8DC);
    b.box({kAltar.x, cy + 1.066f, kAltar.y}, {kAltarHalf.x * 2.0f + 0.16f, 0.012f, kAltarHalf.y * 2.0f + 0.16f});
    b.box({kAltar.x, cy + 0.83f, kAltar.y + kAltarHalf.y + 0.075f}, {kAltarHalf.x * 2.0f + 0.16f, 0.48f, 0.012f});
    b.setColor(0x7A2A26);
    b.box({kAltar.x, cy + 0.8f, kAltar.y + kAltarHalf.y + 0.085f}, {0.8f, 0.42f, 0.012f});
    b.setColor(0xC8A040);
    b.box({kAltar.x, cy + 0.8f, kAltar.y + kAltarHalf.y + 0.093f}, {0.05f, 0.28f, 0.01f});
    b.box({kAltar.x, cy + 0.86f, kAltar.y + kAltarHalf.y + 0.093f}, {0.18f, 0.05f, 0.01f});
    for (float x : {-0.62f, 0.62f}) {
        b.setColor(0xB89A50);
        b.frustum({kAltar.x + x, cy + 1.07f, kAltar.y - 0.12f}, 0.08f, 0.05f, 0.08f, 8);
        buildCandle(b, {kAltar.x + x, cy + 1.15f, kAltar.y - 0.12f}, 0.3f, flames);
    }
    b.setColor(0xB89A50);
    b.box({kAltar.x, cy + 1.1f, kAltar.y - 0.22f}, {0.2f, 0.06f, 0.14f});
    b.box({kAltar.x, cy + 1.36f, kAltar.y - 0.22f}, {0.045f, 0.48f, 0.045f});
    b.box({kAltar.x, cy + 1.46f, kAltar.y - 0.22f}, {0.24f, 0.045f, 0.045f});

    // Altarbild als Triptychon und großes Holzkreuz an der Rückwand
    b.setColorJitter(0.03f);
    b.setColor(0xA88438);
    b.box({0, cy + 2.05f, -id + 0.05f}, {2.7f, 1.5f, 0.06f});
    b.box({0, cy + 2.85f, -id + 0.05f}, {1.0f, 0.3f, 0.06f});
    const uint32_t panels[] = {0x2E4A7A, 0x8A2A2A, 0x2E4A7A};
    for (int i = 0; i < 3; ++i) {
        b.setColor(panels[i]);
        b.box({-0.86f + 0.86f * static_cast<float>(i), cy + 2.0f, -id + 0.085f}, {0.72f, i == 1 ? 1.26f : 1.14f, 0.02f});
    }
    b.setColor(0xD8C890);
    b.box({0, cy + 2.05f, -id + 0.1f}, {0.06f, 0.7f, 0.01f});
    b.box({0, cy + 2.2f, -id + 0.1f}, {0.36f, 0.06f, 0.01f});
    b.setColor(0x5A3A22);
    b.box({0, fy + 4.35f, -id + 0.07f}, {0.18f, 1.9f, 0.1f});
    b.box({0, fy + 4.75f, -id + 0.07f}, {1.05f, 0.18f, 0.1f});

    // Lesepult mit aufgeschlagenem Buch
    b.setColorJitter(0.05f);
    b.setColor(0x5A3C26);
    b.box({kLectern.x, cy + 0.04f, kLectern.y}, {0.5f, 0.08f, 0.5f});
    b.box({kLectern.x, cy + 0.6f, kLectern.y}, {0.14f, 1.1f, 0.14f});
    b.setTransform(Mat4::translation({kLectern.x, cy + 1.18f, kLectern.y}) * Mat4::rotationX(-0.38f));
    b.box({0, 0, 0}, {0.62f, 0.05f, 0.46f});
    b.setColor(0xE8E0C8);
    b.box({-0.1f, 0.04f, 0}, {0.2f, 0.025f, 0.3f});
    b.box({0.1f, 0.04f, 0}, {0.2f, 0.025f, 0.3f});
    b.setColor(0x7A2A26);
    b.box({0, 0.03f, 0.12f}, {0.02f, 0.02f, 0.2f});
    b.setTransform(Mat4::identity());

    // Kerzenständer aus Eisen links und rechts vom Altar
    for (float s : {-1.0f, 1.0f}) {
        const Vec3 p{s * kCandleStand.x, cy, kCandleStand.y};
        b.setColorJitter(0.03f);
        b.setColor(0x3A3430);
        b.cylinder(p, 0.18f, 0.05f, 8);
        b.box(p + Vec3{0, 0.75f, 0}, {0.05f, 1.4f, 0.05f});
        b.cylinder(p + Vec3{0, 1.42f, 0}, 0.11f, 0.03f, 8);
        buildCandle(b, p + Vec3{0, 1.45f, 0}, 0.36f, flames);
    }

    // Taufbecken neben dem Eingang
    b.setColorJitter(0.05f);
    b.setColor(kStone);
    b.frustum({kFont.x, fy, kFont.y}, 0.32f, 0.18f, 0.55f, 8);
    b.frustum({kFont.x, fy + 0.55f, kFont.y}, 0.24f, 0.44f, 0.34f, 10);
    b.setColor(0x2A3A48);
    b.cylinder({kFont.x, fy + 0.84f, kFont.y}, 0.37f, 0.01f, 10);

    // Wandleuchter zwischen den Fenstern
    for (float z : {-2.3f, 2.3f}) {
        for (float s : {-1.0f, 1.0f}) {
            const Vec3 p{s * (iw - 0.1f), fy + 2.2f, z};
            b.setColor(0x3A3430);
            b.box(p + Vec3{s * 0.04f, 0, 0}, {0.08f, 0.32f, 0.12f});
            b.box(p + Vec3{-s * 0.08f, 0.14f, 0}, {0.2f, 0.03f, 0.03f});
            b.cylinder(p + Vec3{-s * 0.16f, 0.15f, 0}, 0.07f, 0.025f, 8);
            buildCandle(b, p + Vec3{-s * 0.16f, 0.17f, 0}, 0.22f, flames);
        }
    }

    // Turm hinter der Altarwand mit Glockenstube, Spitzhelm und goldenem Kreuz
    const float tz = -hd - kTowerSize * 0.5f, ts = kTowerSize, top = fy + 14.0f;
    b.setColorJitter(0.04f);
    b.setColor(0xD8D2C4);
    b.box({0, (base + top) * 0.5f, tz}, {ts, top - base, ts});
    b.setColorJitter(0.06f);
    b.setColor(kStone);
    for (int k = 0; k < 20; ++k) {
        const float y = fy + course + 0.2f + static_cast<float>(k) * 0.62f;
        if (y > top - 0.4f) break;
        const bool wideX = k % 2 == 0;
        const Vec3 size = wideX ? Vec3{0.6f, 0.3f, 0.4f} : Vec3{0.4f, 0.3f, 0.6f};
        for (float s : {-1.0f, 1.0f})
            for (float e : {-1.0f, 1.0f})
                b.box({s * (ts * 0.5f + 0.02f - size.x * 0.5f), y, tz + e * (ts * 0.5f + 0.02f - size.z * 0.5f)}, size);
    }
    b.box({0, top - 1.0f, tz}, {ts + 0.24f, 0.22f, ts + 0.24f});
    b.box({0, top + 0.05f, tz}, {ts + 0.3f, 0.2f, ts + 0.3f});
    // Schallöffnungen mit Lamellen auf allen vier Seiten, Schlitzfenster darunter
    for (int side = 0; side < 4; ++side) {
        const float a = static_cast<float>(side) * kPi * 0.5f;
        b.setTransform(Mat4::translation({0, 0, tz}) * Mat4::rotationY(a));
        b.setColorJitter(0.0f);
        b.setColor(0x1E1A16);
        b.box({0, top - 2.3f, ts * 0.5f + 0.01f}, {1.1f, 1.8f, 0.04f});
        b.box({0, fy + 7.2f, ts * 0.5f + 0.01f}, {0.22f, 0.9f, 0.04f});
        b.setColorJitter(0.05f);
        b.setColor(0x4A3A2A);
        for (int k = 0; k < 5; ++k) b.box({0, top - 3.0f + static_cast<float>(k) * 0.32f, ts * 0.5f + 0.04f}, {1.0f, 0.06f, 0.06f});
        b.setColor(kStone);
        for (float e : {-1.0f, 1.0f}) b.box({e * 0.62f, top - 2.3f, ts * 0.5f + 0.05f}, {0.14f, 1.9f, 0.12f});
        b.box({0, top - 1.35f, ts * 0.5f + 0.05f}, {1.38f, 0.14f, 0.12f});
    }
    b.setColorJitter(0.07f);
    b.setColor(0x3C3F47);
    b.setTransform(Mat4::translation({0, top + 0.15f, tz}) * Mat4::rotationY(kPi * 0.25f));
    b.cone({0, 0, 0}, 3.3f, 5.6f, 4);
    b.setTransform(Mat4::identity());
    b.setColorJitter(0.02f);
    b.setColor(0xC8A848);
    b.cylinder({0, top + 5.6f, tz}, 0.09f, 0.3f, 6);
    b.box({0, top + 6.45f, tz}, {0.12f, 1.4f, 0.12f});
    b.box({0, top + 6.75f, tz}, {0.72f, 0.12f, 0.12f});
}

// Dach, Giebel mit Rosette und Giebelkreuz – wird ausgeblendet, wenn man drinnen ist.
void buildChapelRoof(MeshBuilder& b, const House& h) {
    const float w = h.width, d = h.depth, wh = h.wallHeight, rh = h.roofHeight;
    const float fy = h.floorY - h.position.y, hd = d * 0.5f;
    b.setColorJitter(0.02f);
    b.setColor(h.wallColor);
    b.gableWalls({0, fy + wh, 0}, w, d, rh, 0.5f);
    b.setColorJitter(0.07f);
    b.setColor(h.roofColor);
    b.gableRoof({0, fy + wh, 0}, w, d, rh, 0.5f, false);
    b.setColor(eng::lerp(hex(h.roofColor), hex(0x000000), 0.3f));
    b.box({0, fy + wh + rh + 0.04f, 0}, {0.22f, 0.16f, d + 1.0f});
    // Rosette im Frontgiebel: Steinring, Speichen, bunte Felder (leuchten nachts)
    const Vec3 c{0, fy + wh + 1.55f, hd + 0.03f};
    b.setColorJitter(0.04f);
    b.setColor(kStone);
    for (int k = 0; k < 16; ++k) {
        const float a = static_cast<float>(k) / 16.0f * 2.0f * kPi;
        b.box(c + Vec3{std::cos(a) * 0.82f, std::sin(a) * 0.82f, 0.04f}, {0.3f, 0.3f, 0.12f});
    }
    const uint32_t colors[] = {0x2E4A9A, 0x9A2A2A, 0xC89A3A, 0x2E7A5A};
    b.setGlow(1.0f);
    b.setColorJitter(0.08f);
    b.setColor(0x1E1A16);
    b.box(c, {1.45f, 1.45f, 0.06f});
    for (int k = 0; k < 8; ++k) {
        const float a = (static_cast<float>(k) + 0.5f) / 8.0f * 2.0f * kPi;
        b.setColor(colors[k % 4]);
        b.box(c + Vec3{std::cos(a) * 0.48f, std::sin(a) * 0.48f, 0.035f}, {0.3f, 0.3f, 0.04f});
    }
    b.setColor(colors[2]);
    b.box(c + Vec3{0, 0, 0.04f}, {0.32f, 0.32f, 0.04f});
    b.setGlow(0.0f);
    // Steinkreuz auf der Giebelspitze
    b.setColor(kStone);
    b.box({0, fy + wh + rh + 0.55f, hd + 0.2f}, {0.16f, 0.9f, 0.16f});
    b.box({0, fy + wh + rh + 0.7f, hd + 0.2f}, {0.56f, 0.16f, 0.16f});
}

void buildWell(MeshBuilder& b, Vec3 pos) {
    b.setTransform(Mat4::translation(pos));
    b.setColorJitter(0.1f);
    b.setColor(0x7C776E);
    b.cylinder({0, -0.3f, 0}, 1.05f, 1.15f, 10);
    b.setColor(0x1A2530);
    b.cylinder({0, 0.6f, 0}, 0.82f, 0.26f, 10);
    b.setColor(0x5E4632);
    b.box({0.95f, 1.2f, 0}, {0.18f, 2.4f, 0.18f});
    b.box({-0.95f, 1.2f, 0}, {0.18f, 2.4f, 0.18f});
    b.box({0, 1.95f, 0}, {2.1f, 0.14f, 0.14f});
    b.setColor(0x6B3A2C);
    b.gableRoof({0, 2.3f, 0}, 2.3f, 1.4f, 0.8f, 0.2f);
    b.setColor(0x4E3A28);
    b.frustum({0, 1.0f, 0}, 0.16f, 0.2f, 0.28f, 6);
    b.setColor(0x9A8C70);
    b.box({0, 1.6f, 0}, {0.03f, 0.7f, 0.03f});
    b.setTransform(Mat4::identity());
}

void buildBell(MeshBuilder& b, Vec3 pos) {
    b.setTransform(eng::trs(pos, World::kBellYaw));
    b.setColorJitter(0.06f);
    b.setColor(0x6B5038);
    b.box({0, 0.12f, 0}, {2.5f, 0.24f, 1.9f});
    b.setColor(0x5A4030);
    b.box({0.95f, 1.75f, 0}, {0.26f, 3.3f, 0.26f});
    b.box({-0.95f, 1.75f, 0}, {0.26f, 3.3f, 0.26f});
    b.box({0, 3.2f, 0}, {2.3f, 0.26f, 0.3f});
    b.beam({0.95f, 2.4f, 0}, {0.4f, 3.1f, 0}, 0.14f);
    b.beam({-0.95f, 2.4f, 0}, {-0.4f, 3.1f, 0}, 0.14f);
    b.setColor(0x5E3328);
    b.gableRoof({0, 3.33f, 0}, 2.4f, 1.0f, 0.75f, 0.25f);
    b.setColorJitter(0.04f);
    b.setColor(0xB08D57);
    b.frustum({0, 2.35f, 0}, 0.46f, 0.24f, 0.6f, 9);
    b.cone({0, 2.95f, 0}, 0.25f, 0.18f, 9);
    b.setColor(0x3A3028);
    b.box({0, 1.6f, 0}, {0.04f, 0.8f, 0.04f});
    b.setTransform(Mat4::identity());
}

void buildBonfireBase(MeshBuilder& b, Vec3 pos) {
    b.setTransform(Mat4::translation(pos));
    b.setColorJitter(0.15f);
    b.setColor(0x6A665E);
    for (int i = 0; i < 10; ++i) {
        const float a = static_cast<float>(i) / 10.0f * 2.0f * kPi;
        b.sphere({std::cos(a) * 1.2f, 0.05f, std::sin(a) * 1.2f}, 0.26f, 2, 5);
    }
    b.setColor(0x2A2018);
    b.cylinder({0, -0.02f, 0}, 1.0f, 0.06f, 10);
    b.setColorJitter(0.1f);
    b.setColor(0x3D2C20);
    for (int i = 0; i < 7; ++i) {
        const float a = static_cast<float>(i) / 7.0f * 2.0f * kPi + 0.3f;
        b.beam({std::cos(a) * 0.8f, 0.05f, std::sin(a) * 0.8f}, {std::cos(a) * 0.08f, 1.15f, std::sin(a) * 0.08f}, 0.17f);
    }
    b.setTransform(Mat4::identity());
}

// --- Kleinteile ----------------------------------------------------------------

void buildProp(MeshBuilder& b, const Prop& p, uint32_t& state) {
    const Mat4 base = eng::trs(p.position, p.yaw);
    b.setTransform(base);
    const float s = p.scale;
    switch (p.kind) {
    case PropKind::Rock:
        b.setTransform(base * Mat4::scale({s * 1.25f, s * 0.75f, s}));
        b.setColorJitter(0.12f);
        b.setColor(mixColor(hex(0x77746C), hex(0x8E8A80), rnd(state)));
        b.sphere({0, 0.15f, 0}, 1.0f, 3, 6);
        break;
    case PropKind::Fence:
        b.setColorJitter(0.1f);
        b.setColor(0x6B5A48);
        b.box({s * 0.5f, 0.55f, 0}, {0.13f, 1.1f, 0.13f});
        b.box({-s * 0.5f, 0.55f, 0}, {0.13f, 1.1f, 0.13f});
        b.box({0, 0.42f, 0}, {s, 0.1f, 0.06f});
        b.box({0, 0.85f, 0}, {s, 0.1f, 0.06f});
        break;
    case PropKind::Lantern:
        b.setColorJitter(0.05f);
        b.setColor(0x3A3028);
        b.box({0, 1.3f, 0}, {0.14f, 2.6f, 0.14f});
        b.box({0.22f, 2.55f, 0}, {0.55f, 0.08f, 0.08f});
        b.setGlow(1.0f);
        b.setColor(0x2E3036);
        b.box({0.45f, 2.22f, 0}, {0.28f, 0.36f, 0.28f});
        b.setGlow(0.0f);
        b.setColor(0x2A2520);
        b.cone({0.45f, 2.4f, 0}, 0.24f, 0.18f, 4);
        break;
    case PropKind::LogSeat:
        b.setTransform(base * Mat4::translation({0, 0.27f, 0}) * Mat4::rotationZ(kPi * 0.5f));
        b.setColorJitter(0.08f);
        b.setColor(0x5B4330);
        b.cylinder({0, -1.1f, 0}, 0.28f, 2.2f, 7);
        break;
    case PropKind::WoodPile: {
        b.setColorJitter(0.12f);
        b.setColor(0x7A5A3C);
        const int rows[] = {4, 3, 2, 1};
        for (int row = 0; row < 4; ++row) {
            for (int i = 0; i < rows[row]; ++i) {
                const float x = (static_cast<float>(i) - static_cast<float>(rows[row] - 1) * 0.5f) * 0.36f * s;
                b.setTransform(base * Mat4::translation({0, (0.17f + static_cast<float>(row) * 0.3f) * s, x}) *
                               Mat4::rotationZ(kPi * 0.5f));
                b.cylinder({0, -0.9f * s, 0}, 0.17f * s, 1.8f * s, 6);
            }
        }
        break;
    }
    case PropKind::ChoppingBlock:
        b.setColorJitter(0.08f);
        b.setColor(0x6E5238);
        b.frustum({0, 0, 0}, 0.48f, 0.42f, 0.6f, 8);
        b.setColor(0xB08A60);
        b.cylinder({0, 0.6f, 0}, 0.4f, 0.03f, 8);
        b.setColor(0x6E5038);
        b.beam({0.05f, 0.62f, 0.05f}, {0.42f, 1.25f, 0.12f}, 0.07f);
        b.setColor(0x8A8E92);
        b.box({0.08f, 0.66f, 0.06f}, {0.3f, 0.12f, 0.05f});
        break;
    case PropKind::Barrel:
        b.setColorJitter(0.08f);
        b.setColor(0x7A5534);
        b.frustum({0, 0, 0}, 0.38f, 0.46f, 0.45f, 9, false);
        b.frustum({0, 0.45f, 0}, 0.46f, 0.38f, 0.45f, 9, true);
        b.setColor(0x3A3430);
        b.cylinder({0, 0.15f, 0}, 0.44f, 0.06f, 9, false);
        b.cylinder({0, 0.72f, 0}, 0.44f, 0.06f, 9, false);
        break;
    case PropKind::Crate:
        b.setColorJitter(0.1f);
        b.setColor(0x8B6B45);
        b.box({0, 0.4f, 0}, {0.8f, 0.8f, 0.8f});
        b.setColor(0x5E462C);
        b.beam({-0.38f, 0.05f, 0.42f}, {0.38f, 0.75f, 0.42f}, 0.07f);
        b.beam({0.42f, 0.05f, -0.38f}, {0.42f, 0.75f, 0.38f}, 0.07f);
        break;
    case PropKind::HayBale:
        b.setTransform(base * Mat4::translation({0, 0.6f, 0}) * Mat4::rotationZ(kPi * 0.5f));
        b.setColorJitter(0.1f);
        b.setColor(0xD9B95C);
        b.cylinder({0, -0.6f, 0}, 0.6f, 1.2f, 9);
        break;
    case PropKind::Cart:
        b.setColorJitter(0.08f);
        b.setColor(0x7A5A3C);
        b.box({0, 0.85f, 0}, {2.4f, 0.12f, 1.3f});
        b.box({0, 1.05f, 0.62f}, {2.4f, 0.35f, 0.06f});
        b.box({0, 1.05f, -0.62f}, {2.4f, 0.35f, 0.06f});
        b.box({-1.17f, 1.05f, 0}, {0.06f, 0.35f, 1.3f});
        b.beam({1.2f, 0.85f, 0.4f}, {2.6f, 0.5f, 0.35f}, 0.08f);
        b.beam({1.2f, 0.85f, -0.4f}, {2.6f, 0.5f, -0.35f}, 0.08f);
        b.setColor(0x4E3A28);
        for (float x : {-0.75f, 0.75f}) {
            for (float z : {-0.72f, 0.72f}) {
                b.setTransform(base * Mat4::translation({x, 0.45f, z}) * Mat4::rotationX(kPi * 0.5f));
                b.cylinder({0, -0.06f, 0}, 0.45f, 0.12f, 10);
            }
        }
        b.setTransform(base);
        b.setColor(0xC9AA5A);
        b.box({0.2f, 1.1f, 0}, {1.6f, 0.4f, 1.0f});
        break;
    case PropKind::Tombstone:
        b.setColorJitter(0.1f);
        b.setColor(mixColor(hex(0x85847E), hex(0x9C9A92), rnd(state)));
        b.box({0, 0.35f * s, 0}, {0.6f * s, 0.7f * s, 0.16f * s});
        b.setTransform(base * Mat4::translation({0, 0.7f * s, 0}) * Mat4::rotationX(kPi * 0.5f));
        b.cylinder({0, -0.08f * s, 0}, 0.3f * s, 0.16f * s, 8);
        b.setTransform(base);
        b.setColor(0x4A3B2C);
        b.box({0, 0.05f, -0.95f}, {0.9f, 0.16f, 1.7f});
        break;
    case PropKind::GraveCross:
        b.setColorJitter(0.1f);
        b.setColor(0x5A4A3A);
        b.box({0, 0.55f * s, 0}, {0.12f, 1.1f * s, 0.1f});
        b.box({0, 0.8f * s, 0}, {0.56f * s, 0.1f, 0.1f});
        b.setColor(0x4A3B2C);
        b.box({0, 0.05f, -0.95f}, {0.9f, 0.16f, 1.7f});
        break;
    case PropKind::CropRow: {
        b.setColorJitter(0.05f);
        b.setColor(0x4E3A26);
        b.box({0, 0.04f, 0}, {s, 0.14f, 0.75f});
        b.setColorJitter(0.14f);
        for (float x = -s * 0.5f + 0.3f; x < s * 0.5f - 0.2f; x += 0.45f) {
            const float hgt = 0.55f + rnd(state) * 0.4f;
            b.setColor(mixColor(hex(0xC9A94F), hex(0xA7A848), rnd(state) * 0.6f));
            b.box({x + rnd(state) * 0.1f, 0.1f + hgt * 0.5f, (rnd(state) - 0.5f) * 0.2f}, {0.32f, hgt, 0.32f});
        }
        break;
    }
    case PropKind::Bush:
        b.setColorJitter(0.12f);
        b.setColor(mixColor(hex(0x3B6532), hex(0x52783A), rnd(state)));
        b.setTransform(base * Mat4::scale({s, s * 0.8f, s}));
        b.sphere({0, 0.4f, 0}, 0.65f, 3, 6);
        b.sphere({0.5f, 0.3f, 0.2f}, 0.45f, 3, 5);
        b.sphere({-0.4f, 0.3f, -0.25f}, 0.5f, 3, 5);
        break;
    case PropKind::Stump:
        b.setColorJitter(0.08f);
        b.setColor(0x5E4632);
        b.frustum({0, 0, 0}, 0.42f * s, 0.34f * s, 0.45f * s, 7);
        b.setColor(0xA7845C);
        b.cylinder({0, 0.45f * s, 0}, 0.33f * s, 0.02f, 7);
        break;
    }
    b.setTransform(Mat4::identity());
}

} // namespace

void buildTree(MeshBuilder& b, const Tree& tree) {
    b.setTransform(eng::trs(tree.position - Vec3{0, 0.1f, 0}, tree.yaw));
    switch (tree.kind) {
    case TreeKind::Pine: buildPine(b, tree.scale, tree.tint); break;
    case TreeKind::Broadleaf: buildBroadleaf(b, tree.scale, tree.tint); break;
    case TreeKind::Dead: buildDeadTree(b, tree.scale); break;
    }
    b.setTransform(Mat4::identity());
}

void WorldRenderer::build(const World& world) {
    std::vector<MeshBuilder> builders(static_cast<size_t>(kChunkGrid * kChunkGrid));
    buildTerrain(builders, world);
    buildGrass(builders, world);

    for (const Tree& t : world.trees()) buildTree(builders[static_cast<size_t>(chunkOf(t.position.x, t.position.z))], t);
    m_houses.clear();
    m_houses.resize(world.houses().size());
    for (size_t i = 0; i < world.houses().size(); ++i) {
        const House& h = world.houses()[i];
        HouseGfx& g = m_houses[i];
        MeshBuilder shell, roof, glass;
        g.flames.clear();
        if (h.chapel) {
            const Vec2 rampEnd = world.houseToWorld(static_cast<int>(i), {0, h.depth * 0.5f + h.ramp});
            const float rise = std::max(0.0f, h.floorY - world.terrainHeight(rampEnd.x, rampEnd.y));
            buildChapelShell(shell, glass, h, rise, g.flames);
            buildChapelRoof(roof, h);
        } else {
            buildHouseShell(shell, h);
            buildHouseRoof(roof, h);
        }
        shell.uploadTo(g.shell);
        roof.uploadTo(g.roof);
        if (!glass.empty()) glass.uploadTo(g.glass);
        g.chapel = h.chapel;
        g.position = h.position;
        g.yaw = h.yaw;
        g.doorWidth = h.doorWidth;
        g.hinge = {-h.doorWidth * 0.5f + 0.05f, h.floorY - h.position.y, h.depth * 0.5f - House::kWall * 0.5f};
        g.hearth = h.hearth;
        g.center = {h.position.x, h.floorY + h.wallHeight, h.position.z};
        g.width = h.width;
        g.depth = h.depth;
        g.height = h.wallHeight;
        if (h.chapel) {
            const Vec2 nave = world.houseToWorld(static_cast<int>(i), {0, 0.6f});
            g.naveLight = {nave.x, h.floorY + 2.6f, nave.y};
            const Mat4 m = eng::trs(h.position, h.yaw);
            for (Vec3& f : g.flames) f = m.transformPoint(f);
        }
    }
    MeshBuilder door, reinforced, chapelDoor, chapelReinforced;
    buildDoorLeaf(door, false, House::kDoorWidth - 0.1f, 1.96f);
    buildDoorLeaf(reinforced, true, House::kDoorWidth - 0.1f, 1.96f);
    buildDoorLeaf(chapelDoor, false, chapel::kDoorWidth * 0.5f - 0.06f, chapel::kDoorHeight - 0.08f);
    buildDoorLeaf(chapelReinforced, true, chapel::kDoorWidth * 0.5f - 0.06f, chapel::kDoorHeight - 0.08f);
    door.uploadTo(m_door);
    reinforced.uploadTo(m_doorReinforced);
    chapelDoor.uploadTo(m_chapelDoor);
    chapelReinforced.uploadTo(m_chapelDoorReinforced);
    buildWell(builders[static_cast<size_t>(chunkOf(world.well().x, world.well().z))], world.well());
    buildBell(builders[static_cast<size_t>(chunkOf(world.bell().x, world.bell().z))], world.bell());
    m_bonfire = world.bonfire();
    buildBonfireBase(builders[static_cast<size_t>(chunkOf(0, 0))], m_bonfire);

    uint32_t state = world.seed() + 77u;
    m_lanterns.clear();
    for (const Prop& p : world.props()) {
        buildProp(builders[static_cast<size_t>(chunkOf(p.position.x, p.position.z))], p, state);
        if (p.kind == PropKind::Lantern) m_lanterns.push_back(p.position + eng::Mat4::rotationY(p.yaw).transformDir({0.45f, 2.22f, 0}));
    }

    m_fireflies.clear();
    for (int i = 0; i < 90; ++i) {
        const float a = rnd(state) * 2.0f * kPi;
        const float r = 30.0f + rnd(state) * 45.0f;
        const float x = std::cos(a) * r, z = std::sin(a) * r;
        m_fireflies.push_back(world.ground(x, z) + Vec3{0, 0.6f + rnd(state) * 1.6f, 0});
    }

    m_chunks.clear();
    m_chunks.resize(builders.size());
    for (size_t i = 0; i < builders.size(); ++i)
        if (!builders[i].empty()) builders[i].uploadTo(m_chunks[i].mesh);
}

eng::Environment WorldRenderer::environment(float timeOfDay, Vec3 focus, float time) const {
    using eng::Vec3;
    eng::Environment env;
    const float a = (timeOfDay - 0.25f) * 2.0f * kPi;
    const float elevation = std::sin(a);
    const Vec3 sun = Vec3{std::cos(a), elevation * 0.92f, 0.42f}.normalized();
    const Vec3 moon = Vec3{-std::cos(a) * 0.8f, std::max(-elevation, 0.0f) * 0.75f + 0.28f, -0.55f}.normalized();

    const float day = smooth(-0.1f, 0.3f, elevation);
    const float twilight = std::exp(-(elevation / 0.17f) * (elevation / 0.17f));
    const float night = 1.0f - smooth(-0.22f, 0.02f, elevation);

    const Vec3 noonLight{3.5f, 3.25f, 2.85f};
    const Vec3 lowLight{3.2f, 1.45f, 0.55f};
    const Vec3 moonLight{0.2f, 0.28f, 0.52f};
    if (elevation > -0.04f) {
        env.lightDirection = -sun;
        env.lightColor = eng::lerp(lowLight, noonLight, smooth(0.0f, 0.45f, elevation)) * smooth(-0.04f, 0.12f, elevation);
    } else {
        env.lightDirection = -moon;
        env.lightColor = moonLight * smooth(-0.04f, -0.25f, elevation);
    }

    const Vec3 dayTop{0.08f, 0.24f, 0.8f}, dayHorizon{0.55f, 0.7f, 0.92f};
    const Vec3 duskTop{0.18f, 0.16f, 0.36f}, duskHorizon{1.25f, 0.52f, 0.24f};
    const Vec3 nightTop{0.0025f, 0.004f, 0.014f}, nightHorizon{0.018f, 0.026f, 0.055f};
    Vec3 top = eng::lerp(nightTop, dayTop, day);
    Vec3 horizon = eng::lerp(nightHorizon, dayHorizon, day);
    top = eng::lerp(top, duskTop, twilight * 0.6f);
    horizon = eng::lerp(horizon, duskHorizon, twilight * 0.85f);
    env.skyTop = top;
    env.skyHorizon = horizon;
    env.skyGround = horizon * 0.35f;

    env.sunDirection = sun;
    env.sunColor = eng::lerp(Vec3{5.0f, 2.2f, 0.9f}, Vec3{6.0f, 5.4f, 4.4f}, smooth(0.0f, 0.4f, elevation));
    env.sunVisible = smooth(-0.08f, 0.02f, elevation);
    env.moonDirection = moon;
    env.moonVisible = night;
    env.stars = night;

    env.skyAmbient = eng::lerp(Vec3{0.02f, 0.028f, 0.058f}, Vec3{0.3f, 0.34f, 0.44f}, day) + Vec3{0.1f, 0.05f, 0.04f} * twilight;
    env.groundAmbient = eng::lerp(Vec3{0.005f, 0.006f, 0.011f}, Vec3{0.13f, 0.11f, 0.08f}, day);

    env.fogColor = eng::lerp(Vec3{0.01f, 0.015f, 0.032f}, Vec3{0.55f, 0.65f, 0.8f}, day);
    env.fogColor = eng::lerp(env.fogColor, Vec3{0.62f, 0.36f, 0.24f}, twilight * 0.45f);
    env.fogDensity = eng::lerp(0.0028f, 0.011f, night);
    env.fogHeightFalloff = 0.06f;

    env.emissive = 1.0f - smooth(-0.12f, 0.18f, elevation);
    env.exposure = eng::lerp(1.35f, 1.0f, day);
    env.bloomStrength = eng::lerp(0.85f, 0.4f, day);
    env.vignette = eng::lerp(1.35f, 0.75f, day);
    env.saturation = eng::lerp(0.6f, 1.05f, day);
    env.gradeTint = eng::lerp(Vec3{0.82f, 0.92f, 1.18f}, Vec3{1.02f, 1.0f, 0.96f}, day);
    env.time = time;
    env.shadowFocus = focus;
    env.shadowRadius = 48.0f;
    return env;
}

void WorldRenderer::updateHouses(const std::vector<DoorState>& doors, float dt) {
    for (size_t i = 0; i < m_houses.size(); ++i) {
        HouseGfx& g = m_houses[i];
        g.flags = i < doors.size() ? doors[i].flags : 0;
        const bool open = g.flags & (DoorOpen | DoorBroken);
        g.door = eng::approach(g.door, open ? 1.0f : 0.0f, dt * 3.5f);
    }
}

void WorldRenderer::drawHouses(eng::Renderer3D& r, float night, float time) const {
    const Color ruin{0.2f, 0.18f, 0.17f, 0.0f};
    for (size_t i = 0; i < m_houses.size(); ++i) {
        const HouseGfx& g = m_houses[i];
        const Mat4 m = eng::trs(g.position, g.yaw);
        const bool burnt = g.flags & DoorBurnt;
        const bool burning = g.flags & DoorBurning;
        r.draw(g.shell, m, burnt ? ruin : Color{1, 1, 1, 1});
        if (burnt) {
            // Glutnester in der Ruine
            for (int k = 0; k < 6; ++k) {
                const float fk = static_cast<float>(k);
                const Vec3 p = m.transformPoint({std::sin(fk * 2.7f) * g.width * 0.3f, g.hinge.y + 0.15f, std::cos(fk * 1.9f) * g.depth * 0.3f});
                r.particle(p, 0.25f, Vec3{2.2f, 0.6f, 0.15f} * (0.5f + 0.5f * std::sin(time * 1.3f + fk)), 0.8f);
            }
            continue;
        }
        if (static_cast<int>(i) != m_hiddenRoof) r.draw(g.roof, m, burning ? Color{0.55f, 0.45f, 0.4f, 1.0f} : Color{1, 1, 1, 1});
        // Buntglas leuchtet von selbst und wirft keinen Schatten: tagsüber von innen hell, nachts von außen (Kerzenschein)
        if (g.glass.valid()) {
            const bool within = static_cast<int>(i) == m_hiddenRoof;
            const float glow = within ? eng::lerp(0.32f, 0.05f, night) : eng::lerp(0.16f, 0.36f, night);
            r.draw(g.glass, m, burning ? Color{0.7f, 0.55f, 0.45f, 1.0f} : Color{1, 1, 1, 1}, glow, false);
        }

        // Tür: offen, zu oder eingeschlagen (die Kapelle hat zwei Flügel)
        const bool reinforced = g.flags & DoorReinforced;
        const eng::Mesh& leaf = g.chapel ? (reinforced ? m_chapelDoorReinforced : m_chapelDoor) : (reinforced ? m_doorReinforced : m_door);
        if (g.flags & DoorBroken) {
            const Color splinters{0.75f, 0.7f, 0.65f, 1.0f};
            if (g.chapel) {
                for (float s : {-1.0f, 1.0f})
                    r.draw(leaf, m * Mat4::translation({s * 0.95f - 0.4f, g.hinge.y + 0.05f, g.hinge.z - 0.4f - 0.5f * (s + 1.0f)}) *
                                     Mat4::rotationY(0.35f * s) * Mat4::rotationX(-kPi * 0.5f),
                           splinters);
            } else {
                r.draw(leaf, m * Mat4::translation({-0.45f, g.hinge.y + 0.05f, g.hinge.z - 0.3f}) * Mat4::rotationY(0.35f) *
                                 Mat4::rotationX(-kPi * 0.5f),
                       splinters);
            }
        } else {
            const float angle = eng::smoothstep(0.0f, 1.0f, g.door) * kPi * 0.5f * 0.94f;
            r.draw(leaf, m * Mat4::translation(g.hinge) * Mat4::rotationY(angle));
            if (g.chapel) r.draw(leaf, m * Mat4::translation({-g.hinge.x, g.hinge.y, g.hinge.z}) * Mat4::rotationY(kPi - angle));
        }

        // Kapelle: Kerzen auf dem Altar, an den Ständern und Wänden, nachts heller
        if (g.chapel && !burning) {
            const float fi = static_cast<float>(i);
            const float flicker = 0.88f + 0.12f * std::sin(time * 9.0f + fi) * std::sin(time * 5.3f + fi * 1.7f);
            r.light({g.hearth, 8.5f, {1.0f, 0.55f, 0.22f}, (0.7f + 2.6f * night) * flicker});
            r.light({g.naveLight, 10.5f, {1.0f, 0.5f, 0.18f}, (0.4f + 2.0f * night) * flicker});
            for (size_t k = 0; k < g.flames.size(); ++k) {
                const float fk = static_cast<float>(k);
                const float sway = std::sin(time * 7.0f + fk * 1.9f) * 0.006f;
                const float pulse = 0.85f + 0.15f * std::sin(time * 13.0f + fk * 2.3f);
                r.particle(g.flames[k] + Vec3{sway, 0, sway * 0.5f}, 0.09f * pulse, Vec3{4.5f, 2.6f, 0.9f}, 1.0f);
                r.particle(g.flames[k] + Vec3{0, 0.02f, 0}, 0.4f, Vec3{1.6f, 0.75f, 0.22f} * (0.3f + 0.7f * night), 0.45f * pulse);
            }
            continue;
        }

        // Kaminfeuer: brennt immer, nachts heller
        if (!burning) {
            const float flicker = 0.85f + 0.15f * std::sin(time * 11.0f + static_cast<float>(i)) * std::sin(time * 6.1f + static_cast<float>(i) * 2.0f);
            r.light({g.hearth + Vec3{-0.25f, 0.1f, 0}, 5.5f, {1.0f, 0.52f, 0.2f}, (0.7f + 1.5f * night) * flicker});
            for (int k = 0; k < 8; ++k) {
                const float fk = static_cast<float>(k);
                const float life = std::fmod(time * (1.1f + std::fmod(fk * 0.31f, 0.5f)) + fk * 0.618f, 1.0f);
                const Vec3 base = m.transformPoint({g.width * 0.5f - House::kWall - 0.34f, g.hinge.y + 0.2f, -g.depth * 0.125f});
                const Vec3 p = base + Vec3{std::sin(fk * 2.1f) * 0.12f * (1.0f - life), life * 0.45f, std::cos(fk * 1.7f) * 0.18f * (1.0f - life)};
                r.particle(p, (1.0f - life) * 0.32f + 0.08f, eng::lerp(Vec3{4.0f, 2.6f, 1.0f}, Vec3{2.8f, 0.6f, 0.1f}, life), 1.0f - life);
            }
            continue;
        }

        // Das Haus brennt lichterloh
        const float flicker = 0.8f + 0.2f * std::sin(time * 9.0f + static_cast<float>(i)) * std::sin(time * 4.3f);
        r.light({g.center + Vec3{0, 1.0f, 0}, 16.0f, {1.0f, 0.45f, 0.14f}, 4.5f * flicker});
        for (int k = 0; k < 70; ++k) {
            const float fk = static_cast<float>(k);
            const float life = std::fmod(time * (0.7f + std::fmod(fk * 0.23f, 0.6f)) + fk * 0.618f, 1.0f);
            const float lx = (std::fmod(fk * 0.377f, 1.0f) - 0.5f) * g.width;
            const float lz = (std::fmod(fk * 0.611f, 1.0f) - 0.5f) * g.depth;
            const float roofY = g.height * (0.4f + 0.6f * std::fmod(fk * 0.13f, 1.0f));
            const Vec3 base = m.transformPoint({lx, g.hinge.y + roofY, lz});
            const Vec3 p = base + Vec3{std::sin(time * 2.0f + fk) * 0.3f, life * 2.8f, std::cos(time * 1.7f + fk) * 0.3f};
            r.particle(p, (1.0f - life) * 1.5f + 0.3f, eng::lerp(Vec3{5.0f, 3.0f, 1.0f}, Vec3{3.0f, 0.5f, 0.08f}, life), 1.0f - life);
        }
    }
}

void WorldRenderer::draw(eng::Renderer3D& r, const eng::Environment& env, float time) const {
    for (const Chunk& c : m_chunks) r.draw(c.mesh, Mat4::identity());

    const float night = env.emissive;
    drawHouses(r, night, time);

    // Lagerfeuer: Licht, Flammen und Funken
    const float flicker = 0.85f + 0.1f * std::sin(time * 13.0f) * std::sin(time * 7.3f + 1.0f) + 0.05f * std::sin(time * 23.0f);
    r.light({m_bonfire + Vec3{0, 1.3f, 0}, 11.0f + 4.0f * night, {1.0f, 0.5f, 0.18f}, (1.2f + 2.4f * night) * flicker});
    for (int i = 0; i < 40; ++i) {
        const float fi = static_cast<float>(i);
        const float speed = 0.9f + std::fmod(fi * 0.37f, 0.6f);
        const float life = std::fmod(time * speed + fi * 0.618f, 1.0f);
        const float angle = fi * 2.39996f;
        const float radius = (0.45f + 0.15f * std::sin(fi)) * (1.0f - life);
        const Vec3 p = m_bonfire + Vec3{std::cos(angle) * radius + std::sin(time * 3.0f + fi) * 0.08f, 0.35f + life * 1.7f,
                                        std::sin(angle) * radius};
        const Vec3 hot{4.5f, 3.0f, 1.2f}, cool{3.2f, 0.7f, 0.12f};
        r.particle(p, (1.0f - life) * 0.9f + 0.2f, eng::lerp(hot, cool, life), (1.0f - life));
    }
    for (int i = 0; i < 18; ++i) {
        const float fi = static_cast<float>(i);
        const float life = std::fmod(time * (0.3f + std::fmod(fi * 0.13f, 0.2f)) + fi * 0.37f, 1.0f);
        const Vec3 p = m_bonfire + Vec3{std::sin(fi * 7.1f + life * 6.0f) * 0.6f * life, 1.0f + life * 4.0f,
                                        std::cos(fi * 3.3f + life * 5.0f) * 0.6f * life};
        r.particle(p, 0.08f, {6.0f, 2.5f, 0.6f}, 1.0f - life);
    }

    // Laternen und Glühwürmchen in der Nacht
    if (night > 0.02f) {
        for (const Vec3& l : m_lanterns) {
            r.light({l, 7.0f, {1.0f, 0.6f, 0.26f}, 1.2f * night});
            r.particle(l, 1.0f, Vec3{2.4f, 1.4f, 0.55f} * night, 0.7f);
        }
        if (night > 0.4f) {
            for (size_t i = 0; i < m_fireflies.size(); ++i) {
                const float fi = static_cast<float>(i);
                const Vec3 p = m_fireflies[i] + Vec3{std::sin(time * 0.4f + fi) * 1.5f, std::sin(time * 0.9f + fi * 2.0f) * 0.4f,
                                                     std::cos(time * 0.35f + fi * 1.3f) * 1.5f};
                const float blink = std::max(0.0f, std::sin(time * 1.7f + fi * 4.1f));
                r.particle(p, 0.14f, Vec3{2.0f, 3.2f, 0.9f} * blink * night, 1.0f);
            }
        }
    }
}

} // namespace ww
