#include "client/Characters.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::Mat4;
using eng::MeshBuilder;
using eng::kPi;

namespace {

Mat4 T(float x, float y, float z) { return Mat4::translation({x, y, z}); }
Mat4 S(float x, float y, float z) { return Mat4::scale({x, y, z}); }
Mat4 Rx(float a) { return Mat4::rotationX(a); }
Mat4 Ry(float a) { return Mat4::rotationY(a); }
Mat4 Rz(float a) { return Mat4::rotationZ(a); }

Color shade(Color c, float f) { return {c.r * f, c.g * f, c.b * f, c.a}; }

struct LookRng {
    uint32_t s;
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    template <size_t N>
    uint32_t pick(const uint32_t (&arr)[N]) {
        return arr[next() % N];
    }
};

} // namespace

CharacterLook characterLook(uint8_t id) {
    static const uint32_t skins[] = {0xF1C9A5, 0xE0AC84, 0xC68A62, 0x8D5A3B, 0xF5D7C0, 0xD9A27A};
    static const uint32_t hairs[] = {0x2B1E16, 0x5A3A22, 0x9A6A3A, 0xC9A35A, 0x8A8A86, 0x6B2E1E, 0x1A1A1A};
    static const uint32_t shirts[] = {0x8E3B32, 0x3F5E7A, 0x5E7A3F, 0xB08D4F, 0x6A4C7A,
                                      0x7A6A55, 0x3E6E6A, 0xA35A2A, 0xC9B79A, 0x7A2E4A};
    static const uint32_t pants[] = {0x4A3B2E, 0x3A3F4A, 0x5A4A36, 0x2E3438, 0x6A5A44};
    static const uint32_t accents[] = {0x3A2A1E, 0x5A2E2A, 0x2E3A2A, 0x4A4A52, 0x7A5A3A, 0x6E3B5A};

    LookRng rng{static_cast<uint32_t>(id) * 2654435761u + 977u};
    for (int i = 0; i < 3; ++i) rng.next();
    CharacterLook look;
    look.skin = Color::hex(rng.pick(skins));
    look.hair = Color::hex(rng.pick(hairs));
    // Hemdfarbe nach ID verteilt, damit sich Spieler gut unterscheiden lassen
    look.shirt = Color::hex(shirts[(id * 7u + 3u) % (sizeof shirts / sizeof shirts[0])]);
    look.pants = Color::hex(rng.pick(pants));
    look.boots = Color::hex(0x2E241C);
    look.accent = Color::hex(rng.pick(accents));
    look.headwear = static_cast<Headwear>(rng.next() % 6);
    look.beard = rng.next() % 3 == 0;
    look.apron = rng.next() % 4 == 0;
    look.height = 0.94f + static_cast<float>(rng.next() % 100) / 100.0f * 0.12f;
    return look;
}

void CharacterRenderer::init() {
    MeshBuilder b;
    b.box({0, 0, 0}, {1, 1, 1});
    b.uploadTo(m_cube);
    b.clear();
    b.cylinder({0, -0.5f, 0}, 0.5f, 1.0f, 9);
    b.uploadTo(m_cylinder);
    b.clear();
    b.cone({0, 0, 0}, 0.5f, 1.0f, 8);
    b.uploadTo(m_cone);
    b.clear();
    b.frustum({0, 0, 0}, 0.5f, 0.38f, 1.0f, 9);
    b.uploadTo(m_frustum);
    b.clear();
    b.sphere({0, 0, 0}, 0.5f, 4, 8);
    b.uploadTo(m_sphere);
    b.clear();

    // Handlaterne mit leuchtendem Glas
    b.setColor(0x2E2824);
    b.box({0, 0.02f, 0}, {0.2f, 0.04f, 0.2f});
    b.box({0, 0.3f, 0}, {0.22f, 0.04f, 0.22f});
    b.cone({0, 0.32f, 0}, 0.13f, 0.1f, 4);
    b.box({0, 0.45f, 0}, {0.03f, 0.12f, 0.03f});
    b.setGlow(1.0f);
    b.setColor(0x3A3020);
    b.box({0, 0.16f, 0}, {0.15f, 0.24f, 0.15f});
    b.uploadTo(m_lantern);
}

float CharacterRenderer::headHeight(const CharacterPose& pose) const {
    if (pose.dead) return 0.6f;
    return eng::lerp(2.0f, 2.55f, pose.wolf);
}

Vec3 CharacterRenderer::lanternPosition(const CharacterPose& pose) const {
    const Mat4 root = eng::trs(pose.position, pose.yaw);
    return root.transformPoint({0.36f, 0.62f, 0.12f});
}

void CharacterRenderer::draw(eng::Renderer3D& r, const CharacterLook& look, const CharacterPose& pose) const {
    Mat4 root = eng::trs(pose.position, pose.yaw);
    if (pose.dead) {
        // Liegend auf dem Rücken
        root = root * T(0, 0.17f, 0.9f) * Rx(-kPi * 0.5f);
        CharacterPose still = pose;
        still.speed = 0.0f;
        still.attack = 0.0f;
        if (pose.wolf > 0.5f) drawWolf(r, still, root * S(1.15f, 1.15f, 1.15f));
        else drawHuman(r, look, still, root * S(look.height, look.height, look.height));
        return;
    }
    if (pose.wolf >= 0.5f) {
        // Verwandlung: kurz aufblähen
        const float pop = 1.0f + std::sin(std::clamp((pose.wolf - 0.5f) * 2.0f, 0.0f, 1.0f) * kPi) * 0.15f;
        drawWolf(r, pose, root * S(1.18f * pop, 1.18f * pop, 1.18f * pop));
    } else if (pose.ghost) {
        // Geister schweben leicht über dem Boden
        const float hover = 0.25f + std::sin(pose.time * 1.8f) * 0.08f;
        drawHuman(r, look, pose, eng::trs(pose.position + Vec3{0, hover, 0}, pose.yaw) * S(look.height, look.height, look.height));
    } else {
        const float shrink = 1.0f - std::sin(std::clamp(pose.wolf * 2.0f, 0.0f, 1.0f) * kPi) * 0.1f;
        const float s = look.height * shrink;
        drawHuman(r, look, pose, root * S(s, s, s));
    }
}

void CharacterRenderer::drawHuman(eng::Renderer3D& r, const CharacterLook& look, const CharacterPose& pose,
                                  const Mat4& root) const {
    auto part = [&](const eng::Mesh& mesh, const Mat4& m, Color c, float glow = 0.0f) {
        if (pose.ghost) {
            const Color spirit = eng::lerp(c, Color::hex(0x6F8FC8), 0.8f);
            r.draw(mesh, m, spirit, 0.1f + glow, false);
            return;
        }
        r.draw(mesh, m, pose.dead ? shade(c, 0.75f) : c, glow);
    };
    const float walk = std::min(pose.speed, 1.0f);
    const float run = std::max(pose.speed - 1.0f, 0.0f);
    const float swing = std::sin(pose.walkPhase) * (0.55f * walk + 0.3f * run);
    const float bob = std::fabs(std::sin(pose.walkPhase)) * (0.05f * walk + 0.04f * run);
    const float breathe = std::sin(pose.time * 2.2f) * 0.012f * (1.0f - walk);
    const float lean = 0.04f * walk + 0.16f * run;
    const float hipY = 0.86f;
    const float legLen = 0.82f;

    const Mat4 body = root * T(0, bob, 0);
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const Mat4 hip = body * T(s * 0.11f, hipY, 0) * Rx(s * swing);
        part(m_cube, hip * T(0, -legLen * 0.5f, 0) * S(0.17f, legLen, 0.19f), look.pants);
        part(m_cube, hip * T(0, -legLen + 0.05f, 0.05f) * S(0.19f, 0.12f, 0.29f), look.boots);
    }

    const Mat4 torso = body * T(0, hipY, 0) * Rx(lean) * S(1.0f + breathe, 1.0f, 1.0f + breathe);
    part(m_cube, torso * T(0, 0.31f, 0) * S(0.48f, 0.62f, 0.28f), look.shirt);
    part(m_cube, torso * T(0, 0.05f, 0) * S(0.5f, 0.08f, 0.3f), look.accent);
    if (look.apron) part(m_cube, torso * T(0, -0.12f, 0.15f) * S(0.36f, 0.5f, 0.02f), Color::hex(0xD8CCB0));

    // Arme schwingen gegengleich zu den Beinen; beim Hieb nach vorne
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const float armSwing = -s * swing * 0.8f;
        const Mat4 shoulder = torso * T(s * 0.31f, 0.56f, 0) * Rx(armSwing) * Rz(s * 0.07f);
        part(m_cube, shoulder * T(0, -0.29f, 0) * S(0.13f, 0.58f, 0.14f), shade(look.shirt, 0.88f));
        part(m_cube, shoulder * T(0, -0.62f, 0) * S(0.11f, 0.12f, 0.11f), look.skin);
        if (side == 1 && pose.lantern) r.draw(m_lantern, shoulder * T(0, -0.95f, 0.02f), {1, 1, 1, 1}, 0.0f);
    }

    const Mat4 head = torso * T(0, 0.62f, 0) * Ry(std::sin(pose.time * 0.7f) * 0.08f * (1.0f - walk));
    part(m_cube, head * T(0, 0.19f, 0) * S(0.33f, 0.36f, 0.32f), look.skin);
    part(m_cube, head * T(0.08f, 0.22f, 0.163f) * S(0.05f, 0.05f, 0.02f), Color::hex(0x1A1410));
    part(m_cube, head * T(-0.08f, 0.22f, 0.163f) * S(0.05f, 0.05f, 0.02f), Color::hex(0x1A1410));
    part(m_cube, head * T(0, 0.16f, 0.175f) * S(0.06f, 0.08f, 0.05f), shade(look.skin, 0.93f));
    part(m_cube, head * T(0, 0.33f, -0.02f) * S(0.35f, 0.1f, 0.35f), look.hair);
    part(m_cube, head * T(0, 0.2f, -0.155f) * S(0.35f, 0.3f, 0.05f), look.hair);
    if (look.beard) part(m_cube, head * T(0, 0.06f, 0.15f) * S(0.3f, 0.14f, 0.07f), look.hair);

    switch (look.headwear) {
    case Headwear::FeltHat:
        part(m_cylinder, head * T(0, 0.39f, 0) * S(0.62f, 0.04f, 0.62f), look.accent);
        part(m_frustum, head * T(0, 0.39f, 0) * S(0.38f, 0.24f, 0.38f), look.accent);
        part(m_cylinder, head * T(0, 0.43f, 0) * S(0.39f, 0.04f, 0.39f), shade(look.accent, 0.6f));
        break;
    case Headwear::Hood:
        part(m_cube, head * T(0, 0.39f, -0.01f) * S(0.41f, 0.08f, 0.4f), look.accent);
        part(m_cube, head * T(0, 0.21f, -0.19f) * S(0.41f, 0.42f, 0.06f), look.accent);
        part(m_cube, head * T(0.2f, 0.21f, 0) * S(0.04f, 0.42f, 0.38f), look.accent);
        part(m_cube, head * T(-0.2f, 0.21f, 0) * S(0.04f, 0.42f, 0.38f), look.accent);
        part(m_cube, torso * T(0, 0.55f, -0.02f) * S(0.58f, 0.12f, 0.34f), look.accent);
        break;
    case Headwear::Scarf:
        part(m_cube, head * T(0, 0.37f, -0.01f) * S(0.37f, 0.07f, 0.36f), look.accent);
        part(m_cube, head * T(0, 0.22f, -0.17f) * S(0.37f, 0.32f, 0.05f), look.accent);
        break;
    case Headwear::Cap:
        part(m_sphere, head * T(0, 0.36f, -0.02f) * S(0.37f, 0.22f, 0.37f), look.accent);
        break;
    case Headwear::StrawHat:
        part(m_cylinder, head * T(0, 0.38f, 0) * S(0.82f, 0.03f, 0.82f), Color::hex(0xD8C07A));
        part(m_cone, head * T(0, 0.38f, 0) * S(0.38f, 0.22f, 0.38f), Color::hex(0xCDB26A));
        break;
    default:
        break;
    }
}

void CharacterRenderer::drawWolf(eng::Renderer3D& r, const CharacterPose& pose, const Mat4& root) const {
    const Color fur = Color::hex(0x3A3532);
    const Color furLight = Color::hex(0x5E5650);
    const Color furDark = Color::hex(0x24201E);
    auto part = [&](const eng::Mesh& mesh, const Mat4& m, Color c, float glow = 0.0f) {
        r.draw(mesh, m, pose.dead ? shade(c, 0.7f) : c, glow);
    };
    const float walk = std::min(pose.speed, 1.0f);
    const float run = std::max(pose.speed - 1.0f, 0.0f);
    const float swing = std::sin(pose.walkPhase) * (0.6f * walk + 0.4f * run);
    const float bob = std::fabs(std::sin(pose.walkPhase)) * (0.07f * walk + 0.06f * run);
    const float breathe = std::sin(pose.time * 3.0f) * 0.02f;
    const Mat4 body = root * T(0, bob, 0);

    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const Mat4 hip = body * T(s * 0.16f, 0.9f, -0.05f) * Rx(s * swing);
        part(m_cube, hip * T(0, -0.25f, 0.04f) * Rx(0.35f) * S(0.24f, 0.5f, 0.3f), fur);
        part(m_cube, hip * T(0, -0.66f, -0.06f) * Rx(-0.3f) * S(0.18f, 0.48f, 0.2f), furDark);
        part(m_cube, hip * T(0, -0.88f, 0.06f) * S(0.2f, 0.08f, 0.32f), furDark);
    }

    const Mat4 torso = body * T(0, 0.92f, 0) * Rx(0.55f + 0.1f * run) * S(1.0f + breathe, 1.0f, 1.0f + breathe);
    part(m_cube, torso * T(0, 0.35f, 0) * S(0.64f, 0.72f, 0.5f), fur);
    part(m_cube, torso * T(0, 0.42f, 0.2f) * S(0.5f, 0.5f, 0.2f), furLight);
    part(m_cube, torso * T(0, 0.74f, -0.05f) * S(0.78f, 0.3f, 0.55f), fur);
    part(m_cube, torso * T(0, -0.05f, -0.2f) * Rx(-0.9f) * T(0, 0, -0.3f) * S(0.12f, 0.12f, 0.65f), fur); // Schwanz

    // Arme: lang, mit Krallen; beim Angriff nach vorne gerissen
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const float armSwing = -s * swing * 0.7f - pose.attack * 2.0f;
        const Mat4 shoulder = torso * T(s * 0.4f, 0.72f, 0) * Rx(armSwing - 0.4f) * Rz(s * 0.12f);
        part(m_cube, shoulder * T(0, -0.3f, 0) * S(0.2f, 0.62f, 0.22f), fur);
        part(m_cube, shoulder * T(0, -0.72f, 0.04f) * S(0.16f, 0.36f, 0.18f), furDark);
        for (int c = -1; c <= 1; ++c)
            part(m_cone, shoulder * T(static_cast<float>(c) * 0.05f, -0.9f, 0.08f) * Rx(kPi) * S(0.05f, 0.14f, 0.05f),
                 Color::hex(0xD8D2C4));
    }

    // Kopf mit Schnauze, Ohren und glühenden Augen
    const Mat4 head = torso * T(0, 0.95f, 0.18f) * Rx(-0.5f) * Ry(std::sin(pose.time * 1.3f) * 0.1f);
    part(m_cube, head * T(0, 0.1f, 0) * S(0.42f, 0.38f, 0.42f), fur);
    part(m_cube, head * T(0, 0.02f, 0.3f) * S(0.24f, 0.2f, 0.32f), furLight);
    part(m_cube, head * T(0, 0.08f, 0.47f) * S(0.1f, 0.08f, 0.06f), Color::hex(0x101010));
    part(m_cube, head * T(0, -0.08f, 0.28f) * S(0.2f, 0.06f, 0.28f), furDark);
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        part(m_cone, head * T(s * 0.13f, 0.26f, -0.05f) * Rz(-s * 0.25f) * S(0.13f, 0.26f, 0.1f), fur);
        part(m_cube, head * T(s * 0.1f, 0.17f, 0.215f) * S(0.07f, 0.04f, 0.02f), Color::hex(0xFFC23A), pose.dead ? 0.0f : 6.0f);
    }
}

} // namespace ww
