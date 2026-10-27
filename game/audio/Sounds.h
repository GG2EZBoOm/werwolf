#pragma once

#include "engine/audio/Audio.h"
#include <array>

namespace ww {

enum class Sound {
    Click, StepGrass, StepStone, StepWood, DoorOpen, DoorClose, DoorHit, DoorBreak,
    Transform, Attack, Bell, Night, Dawn, Vote, Death, Victory, Reveal, Magic, Warning, Chat,
    Wind, Crickets, Birds, Fire, Count
};

// Original procedural sounds are embedded in the executable: no downloads or asset paths.
class SoundBank {
public:
    SoundBank();
    const eng::audio::ClipPtr& operator[](Sound sound) const { return m_clips[static_cast<size_t>(sound)]; }
private:
    std::array<eng::audio::ClipPtr, static_cast<size_t>(Sound::Count)> m_clips;
};

} // namespace ww
