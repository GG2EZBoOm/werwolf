#pragma once

#include "world/World.h"

namespace ww {

constexpr float kWalkSpeed = 3.6f;
constexpr float kSprintSpeed = 6.2f;
constexpr float kWolfSpeed = 7.6f;
constexpr float kGhostSpeed = 7.0f;
constexpr float kPlayerRadius = 0.35f;

// Bewegungszustand einer Figur (lokal gesteuert, vom Netzwerk interpoliert oder ein Bot).
struct Mover {
    Vec3 position;
    float yaw = 0.0f;
    Vec2 velocity{0, 0};
    float walkPhase = 0.0f;
    float speed = 0.0f; // 0 = steht, 1 = geht, 2 = rennt (für die Animation)

    // Bewegt die Figur in Richtung `wish` (Länge 0..1). Ohne Welt-Kollision, wenn `collide` false ist (Geister).
    void move(Vec2 wish, float maxSpeed, float dt, const World& world, bool collide = true);
    // Für Figuren, deren Position von außen gesetzt wird: Animation aus der Bewegung ableiten.
    void animateTo(Vec3 newPosition, float newYaw, float dt);
};

float wrapAngle(float a);

} // namespace ww
