#pragma once

#include "engine/audio/Audio.h"
#include "engine/core/Random.h"
#include <array>

namespace ww {

enum class Sound {
    Click, StepGrass, StepStone, StepWood, DoorOpen, DoorClose, DoorHit, DoorBreak,
    Transform, Attack, Bell, Night, Dawn, Vote, Death, Victory, Reveal, Magic, Warning, Chat,
    Wind, Crickets, Birds, Fire, Count
};

constexpr bool isFootstep(Sound sound) {
    return sound == Sound::StepGrass || sound == Sound::StepStone || sound == Sound::StepWood;
}

// Original procedural sounds are embedded in the executable: no downloads or asset paths.
class SoundBank {
public:
    static constexpr size_t kFootstepCount = 20;
    SoundBank();
    const eng::audio::ClipPtr& operator[](Sound sound) const { return m_clips[static_cast<size_t>(sound)]; }
    bool setFootsteps(std::vector<eng::audio::ClipPtr> clips);
    eng::audio::ClipPtr choose(Sound sound, eng::Rng& rng);
private:
    std::array<eng::audio::ClipPtr, static_cast<size_t>(Sound::Count)> m_clips;
    std::vector<eng::audio::ClipPtr> m_footsteps;
    std::vector<size_t> m_stepOrder;
    size_t m_nextStep = kFootstepCount;
    size_t m_lastStep = kFootstepCount;
};

} // namespace ww
