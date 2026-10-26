#pragma once

#include "world/Movement.h"
#include "world/World.h"

#include "engine/gfx/Renderer3D.h"
#include "engine/platform/Input.h"

namespace ww {

// Kamera hinter der Figur, mit der Maus gedreht, weicht Gebäuden aus.
struct ThirdPersonCamera {
    float yaw = 0.0f;     // Blickrichtung (forward = sin/cos)
    float pitch = 0.32f;  // positiv = von oben
    float distance = 5.0f;
    float sensitivity = 0.0024f;

    void update(const eng::Input& input, bool mouseCaptured);
    eng::Camera camera(Vec3 focus, const World& world) const;
    Vec3 forward() const;
};

// Wunschrichtung aus WASD relativ zur Kamera
Vec2 inputDirection(const eng::Input& input, float cameraYaw);

} // namespace ww
