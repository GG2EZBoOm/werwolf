#pragma once

#include "world/World.h"

#include "engine/core/Math3D.h"

#include <algorithm>
#include <cmath>

namespace ww {

// Wie sich Hebel, Falltür, Seil und Körper bewegen; t = Sekunden seit dem Ziehen des Hebels (< 0: alles ruht).
struct GallowsMotion {
    float lever = 0.0f;    // 0 = aufrecht, 1 = umgelegt
    float trapdoor = 0.0f; // Öffnungswinkel der beiden Flügel (Bogenmaß)
    float drop = 0.0f;     // wie weit der Körper gefallen ist (m)
    float swingX = 0.0f, swingZ = 0.0f, twist = 0.0f; // Pendeln unter dem Querbalken und Drehen am Seil
};

constexpr float kLeverPull = 0.12f; // so lange dauert es, bis der Hebel die Falltür freigibt

// Wann das Seil den Fallenden auffängt (Sekunden nach dem Ziehen des Hebels)
inline float gallowsCatchTime() { return kLeverPull + std::sqrt(2.0f * gallows::kDrop / 9.81f); }

inline GallowsMotion gallowsMotion(float t) {
    GallowsMotion m;
    if (t < 0.0f) return m;
    constexpr float kPull = kLeverPull, kG = 9.81f, kOpen = 1.5708f, kFlap = 48.0f;
    m.lever = std::min(1.0f, t / kPull);
    const float s = t - kPull;
    if (s <= 0.0f) return m;
    // Die Flügel schlagen nach unten und federn an den Pfosten nach
    const float flap = 0.5f * kFlap * s * s;
    if (flap < kOpen) {
        m.trapdoor = flap;
    } else {
        const float after = s - std::sqrt(2.0f * kOpen / kFlap);
        m.trapdoor = kOpen - 0.2f * std::exp(-5.0f * after) * std::fabs(std::sin(11.0f * after));
    }
    const float fall = std::sqrt(2.0f * gallows::kDrop / kG);
    if (s < fall) {
        m.drop = 0.5f * kG * s * s;
        return m;
    }
    // Das Seil fängt den Körper und federt nach; danach pendelt er langsam aus und dreht sich am Seil
    const float hung = s - fall;
    m.drop = gallows::kDrop + 0.07f * std::exp(-4.5f * hung) * std::sin(16.0f * hung);
    const float fade = std::exp(-hung / 8.0f);
    m.swingX = 0.08f * fade * std::sin(1.7f * hung);
    m.swingZ = 0.04f * fade * std::sin(1.7f * hung + 1.3f);
    m.twist = 0.5f * std::sin(0.42f * hung) * (1.0f - std::exp(-hung / 1.5f));
    return m;
}

// Lage des Halses einer Figur der Größe `height`: stehend auf der Falltür oder hängend (ohne Skalierung)
inline eng::Mat4 gallowsNeckFrame(const World& world, float height, const GallowsMotion& m, bool hanging) {
    const Vec3 standing = world.gallowsTrapdoor() + Vec3{0.0f, gallows::kNeck * height, 0.0f};
    const float yaw = world.gallows().yaw;
    if (!hanging) return eng::trs(standing, yaw);
    // Der Körper pendelt als Ganzes um den Querbalken, das Seil bleibt gespannt
    const Vec3 anchor = world.gallowsRopeAnchor();
    const float rope = anchor.y - (standing.y - m.drop);
    const eng::Mat4 swing = eng::Mat4::rotationY(yaw) * eng::Mat4::rotationX(m.swingX) * eng::Mat4::rotationZ(m.swingZ);
    const Vec3 neck = anchor + swing.transformDir({0.0f, -rope, 0.0f});
    return eng::Mat4::translation(neck) * swing * eng::Mat4::rotationY(m.twist);
}

// Der Knoten der Schlinge sitzt seitlich hinter dem Ohr (im Halsrahmen); dort setzt das Seil an
constexpr Vec3 kNooseKnot{-0.2f, 0.03f, -0.02f};
constexpr float kNooseRadius = 0.2f;

} // namespace ww
