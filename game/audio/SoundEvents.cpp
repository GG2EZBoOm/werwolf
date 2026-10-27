#include "audio/SoundEvents.h"
#include <algorithm>
#include <cmath>

namespace ww {
float soundPan(Vec2 offset, float cameraYaw) {
    // Same right vector as the third-person camera (looking +Z means right is -X).
    return std::clamp((-offset.x * std::cos(cameraYaw) + offset.y * std::sin(cameraYaw)) /
                      std::max(1.0f, offset.length()), -0.9f, 0.9f);
}

void SoundEvents::reset() {
    m_actors.clear();
    m_doors.clear();
}

std::vector<SoundCue> SoundEvents::update(const World& world, const std::vector<EntityState>& entities,
                                          const std::vector<DoorState>& doors) {
    std::vector<SoundCue> cues;
    std::erase_if(m_actors, [&](const auto& entry) {
        return std::none_of(entities.begin(), entities.end(), [&](const EntityState& e) {
            return e.id == entry.first && e.kind == EntityLiving;
        });
    });
    for (const EntityState& e : entities) {
        if (e.kind != EntityLiving) continue;
        const auto [it, inserted] = m_actors.try_emplace(e.id, Actor{e});
        if (inserted) continue;
        Actor& actor = it->second;
        const EntityState& previous = actor.last;
        const Vec2 position{e.x, e.z};
        const float distance = (position - Vec2{previous.x, previous.z}).length();
        if (distance < 3.0f) {
            actor.distance += distance;
            const float stride = e.transform > 0.5f ? 1.75f : (e.flags & EntitySprint) ? 1.65f : 1.35f;
            if (actor.distance >= stride) {
                actor.distance = std::fmod(actor.distance, stride);
                Sound sound = Sound::StepGrass;
                if (const int house = world.houseAt(position); house >= 0)
                    sound = house == world.chapelIndex() ? Sound::StepStone : Sound::StepWood;
                else if (world.squareFactor(e.x, e.z) > 0.5f || world.pathFactor(e.x, e.z) > 0.5f) sound = Sound::StepStone;
                cues.push_back({sound, position, (e.flags & EntitySprint) ? 0.8f : 0.58f, 14.0f});
            }
        } else actor.distance = 0.0f; // Teleport / server correction, not a footstep.
        if ((previous.transform <= 0.01f && e.transform > 0.01f) ||
            (previous.transform >= 0.99f && e.transform < 0.99f))
            cues.push_back({Sound::Transform, position, 0.8f, 24.0f});
        if ((e.flags & EntityAttack) && !(previous.flags & EntityAttack))
            cues.push_back({Sound::Attack, position, 0.8f, 18.0f});
        actor.last = e;
    }
    if (m_doors.size() == doors.size()) {
        for (size_t i = 0; i < doors.size() && i < world.houses().size(); ++i) {
            const auto& door = doors[i];
            const auto& previous = m_doors[i];
            const Vec2 position = world.doorCenter(static_cast<int>(i));
            if ((door.flags & DoorBroken) && !(previous.flags & DoorBroken))
                cues.push_back({Sound::DoorBreak, position, 1.0f, 32.0f});
            else if (door.health < previous.health)
                cues.push_back({Sound::DoorHit, position, 0.9f, 28.0f});
            else if ((door.flags ^ previous.flags) & DoorOpen)
                cues.push_back({(door.flags & DoorOpen) ? Sound::DoorOpen : Sound::DoorClose, position, 0.75f, 18.0f});
        }
    }
    m_doors = doors;
    return cues;
}
} // namespace ww
