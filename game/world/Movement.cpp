#include "world/Movement.h"

#include <algorithm>
#include <cmath>

namespace ww {

float wrapAngle(float a) {
    while (a > eng::kPi) a -= 2.0f * eng::kPi;
    while (a < -eng::kPi) a += 2.0f * eng::kPi;
    return a;
}

void Mover::move(Vec2 wish, float maxSpeed, float dt, const World& world, bool collide) {
    const Vec2 target = wish * maxSpeed;
    const float accel = (wish.length() > 0.01f ? 22.0f : 28.0f) * dt;
    const Vec2 dv = target - velocity;
    const float dvLen = dv.length();
    velocity = dvLen <= accel ? target : velocity + dv / dvLen * accel;

    const Vec2 old{position.x, position.z};
    Vec2 next = old + velocity * dt;
    if (collide) {
        next = world.resolveCollision(next, kPlayerRadius);
    } else if (next.length() > World::kBoundary) {
        next = next * (World::kBoundary / next.length());
    }
    const Vec2 actual = dt > 0.0f ? (next - old) / dt : Vec2{0, 0};
    velocity = actual;
    position = world.ground(next.x, next.y);

    const float v = actual.length();
    if (v > 0.4f) {
        const float targetYaw = std::atan2(actual.x, actual.y);
        yaw += wrapAngle(targetYaw - yaw) * std::min(1.0f, dt * 12.0f);
    }
    walkPhase += v * dt * 4.2f;
    speed = v <= kWalkSpeed ? v / kWalkSpeed : 1.0f + (v - kWalkSpeed) / (kSprintSpeed - kWalkSpeed);
}

void Mover::animateTo(Vec3 newPosition, float newYaw, float dt) {
    const Vec2 delta{newPosition.x - position.x, newPosition.z - position.z};
    const float v = dt > 0.0f ? delta.length() / dt : 0.0f;
    velocity = dt > 0.0f ? delta / dt : Vec2{0, 0};
    position = newPosition;
    yaw += wrapAngle(newYaw - yaw) * std::min(1.0f, dt * 14.0f);
    walkPhase += v * dt * 4.2f;
    const float target = v <= kWalkSpeed ? v / kWalkSpeed : 1.0f + (v - kWalkSpeed) / (kSprintSpeed - kWalkSpeed);
    speed = eng::approach(speed, std::min(target, 2.2f), dt * 6.0f);
}

} // namespace ww
