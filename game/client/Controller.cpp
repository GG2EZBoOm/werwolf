#include "client/Controller.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::kPi;

void ThirdPersonCamera::update(const eng::Input& input, bool mouseCaptured) {
    if (mouseCaptured) {
        yaw -= input.mouseDelta.x * sensitivity;
        pitch += input.mouseDelta.y * sensitivity * (invertY ? -1.0f : 1.0f);
    }
    pitch = std::clamp(pitch, -0.45f, 1.25f);
    distance = std::clamp(distance - input.wheel * 0.6f, 2.2f, 10.0f);
}

Vec3 ThirdPersonCamera::forward() const {
    return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

eng::Camera ThirdPersonCamera::camera(Vec3 focus, const World& world) const {
    const Vec3 right{-std::cos(yaw), 0.0f, std::sin(yaw)};
    const Vec3 target = focus + Vec3{0, 1.55f, 0} + right * 0.35f;

    // Drinnen: das Dach wird ausgeblendet, die Kamera bleibt über dem Raum und schaut eher von oben
    if (const int house = world.houseAt({focus.x, focus.z}, 0.25f); house >= 0) {
        const House& h = world.houses()[static_cast<size_t>(house)];
        const float p = std::max(pitch, 0.6f);
        const Vec3 dir{std::sin(yaw) * std::cos(p), -std::sin(p), std::cos(yaw) * std::cos(p)};
        Vec3 pos = target - dir * std::min(distance + 1.0f, 6.0f);
        const float mx = h.width * 0.5f - House::kWall - 0.2f, mz = h.depth * 0.5f - House::kWall - 0.2f;
        Vec2 local = world.worldToHouse(house, {pos.x, pos.z});
        local = {std::clamp(local.x, -mx, mx), std::clamp(local.y, -mz, mz)};
        const Vec2 clamped = world.houseToWorld(house, local);
        pos = {clamped.x, std::clamp(pos.y, h.floorY + 1.2f, h.floorY + h.wallHeight + 3.2f), clamped.y};
        eng::Camera cam;
        cam.target = target;
        cam.position = pos;
        cam.fovY = (fieldOfView + 4.0f) * kPi / 180.0f;
        return cam;
    }

    const Vec3 dir = forward();
    float dist = distance;
    // Kamera nicht in Häuser schieben
    for (int i = 1; i <= 12; ++i) {
        const float d = distance * static_cast<float>(i) / 12.0f;
        const Vec3 p = target - dir * d;
        if (p.y < world.height(p.x, p.z) + 6.0f && world.insideBuilding({p.x, p.z}, 0.3f)) {
            dist = std::max(0.8f, d - distance / 12.0f);
            break;
        }
    }
    eng::Camera cam;
    cam.target = target;
    cam.position = target - dir * dist;
    const float ground = world.height(cam.position.x, cam.position.z) + 0.35f;
    if (cam.position.y < ground) cam.position.y = ground;
    cam.fovY = fieldOfView * kPi / 180.0f;
    return cam;
}

Vec2 inputDirection(const eng::Input& input, float cameraYaw) {
    float f = 0.0f, s = 0.0f;
    if (input.down(eng::Key::W) || input.down(eng::Key::Up)) f += 1.0f;
    if (input.down(eng::Key::S) || input.down(eng::Key::Down)) f -= 1.0f;
    if (input.down(eng::Key::D) || input.down(eng::Key::Right)) s += 1.0f;
    if (input.down(eng::Key::A) || input.down(eng::Key::Left)) s -= 1.0f;
    const Vec2 forward{std::sin(cameraYaw), std::cos(cameraYaw)};
    const Vec2 right{-std::cos(cameraYaw), std::sin(cameraYaw)};
    Vec2 dir = forward * f + right * s;
    const float len = dir.length();
    return len > 1e-3f ? dir / len : Vec2{0, 0};
}

} // namespace ww
