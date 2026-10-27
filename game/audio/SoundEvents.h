#pragma once

#include "audio/Sounds.h"
#include "logic/Types.h"
#include "world/World.h"
#include <unordered_map>

namespace ww {
float soundPan(Vec2 offset, float cameraYaw);

struct SoundCue {
    Sound sound;
    Vec2 position;
    float gain = 1.0f;
    float range = 18.0f;
};

// Only observes state the server has already disclosed to this client.
// Baselines suppress old events on joining, respawning or changing scenes.
class SoundEvents {
public:
    void reset();
    std::vector<SoundCue> update(const World& world, const std::vector<EntityState>& entities,
                                 const std::vector<DoorState>& doors);
private:
    struct Actor {
        EntityState last;
        float distance = 0.0f;
    };
    std::unordered_map<uint8_t, Actor> m_actors;
    std::vector<DoorState> m_doors;
};
} // namespace ww
