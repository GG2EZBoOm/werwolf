#include "engine/audio/Audio.h"

#include <algorithm>
#include <cmath>

namespace eng::audio {
namespace {
float finiteClamp(float value, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : low;
}
void gains(float gain, float pan, float& left, float& right) {
    gain = finiteClamp(gain, 0.0f, 1.0f);
    pan = finiteClamp(pan, -1.0f, 1.0f);
    left = gain * std::sqrt(0.5f * (1.0f - pan));
    right = gain * std::sqrt(0.5f * (1.0f + pan));
}
} // namespace

VoiceId Mixer::play(ClipPtr clip, Bus bus, float gain, float pan, float pitch, bool loop) {
    if (!clip || (clip->channels != 1 && clip->channels != 2) || clip->samples.empty() ||
        clip->samples.size() % clip->channels != 0) return 0;
    std::lock_guard lock(m_mutex);
    Voice* slot = nullptr;
    for (Voice& voice : m_voices) {
        if (!voice.clip) { slot = &voice; break; }
        // Preserve ambience loops; evict the oldest effect when saturated.
        if (!voice.loop && voice.bus != Bus::Music && (!slot || voice.id < slot->id)) slot = &voice;
    }
    if (!slot) return 0;
    *slot = Voice{};
    slot->clip = std::move(clip);
    slot->id = m_nextId++;
    slot->pitch = finiteClamp(pitch, 0.5f, 2.0f);
    slot->bus = bus;
    slot->loop = loop;
    gains(gain, pan, slot->targetLeft, slot->targetRight);
    return slot->id;
}

void Mixer::setVoice(VoiceId id, float gain, float pan) {
    std::lock_guard lock(m_mutex);
    for (Voice& voice : m_voices)
        if (voice.clip && voice.id == id) gains(gain, pan, voice.targetLeft, voice.targetRight);
}

void Mixer::setVolume(float master, float effects, float ambience, bool muted, float music) {
    std::lock_guard lock(m_mutex);
    m_targetMaster = muted ? 0.0f : finiteClamp(master, 0.0f, 1.0f);
    m_effects = finiteClamp(effects, 0.0f, 1.0f);
    m_ambience = finiteClamp(ambience, 0.0f, 1.0f);
    m_music = finiteClamp(music, 0.0f, 1.0f);
}

void Mixer::stopVoice(VoiceId id) {
    std::lock_guard lock(m_mutex);
    for (Voice& voice : m_voices)
        if (voice.id == id) voice = Voice{};
}

void Mixer::stopAll() {
    std::lock_guard lock(m_mutex);
    for (Voice& voice : m_voices) voice = Voice{};
}

void Mixer::render(std::span<int16_t> stereo) {
    std::lock_guard lock(m_mutex);
    std::fill(stereo.begin(), stereo.end(), int16_t{0});
    constexpr float smooth = 1.0f / (0.008f * kSampleRate); // 8 ms, click-free changes
    for (size_t i = 0; i + 1 < stereo.size(); i += 2) {
        m_master += (m_targetMaster - m_master) * smooth;
        float left = 0.0f, right = 0.0f;
        for (Voice& voice : m_voices) {
            if (!voice.clip) continue;
            const auto& samples = voice.clip->samples;
            const size_t channels = voice.clip->channels;
            const size_t frames = voice.clip->frames();
            const size_t index = static_cast<size_t>(voice.cursor);
            const size_t next = index + 1 < frames ? index + 1 : (voice.loop ? 0 : index);
            const float fraction = static_cast<float>(voice.cursor - static_cast<double>(index));
            auto sample = [&](size_t channel) {
                const float a = samples[index * channels + channel];
                return a + (samples[next * channels + channel] - a) * fraction;
            };
            voice.left += (voice.targetLeft - voice.left) * smooth;
            voice.right += (voice.targetRight - voice.right) * smooth;
            const float bus = voice.bus == Bus::Ambience ? m_ambience : voice.bus == Bus::Music ? m_music : m_effects;
            left += sample(0) * voice.left * bus;
            right += sample(channels == 2 ? 1 : 0) * voice.right * bus;
            voice.cursor += voice.pitch;
            if (voice.cursor >= static_cast<double>(frames)) {
                if (voice.loop) voice.cursor = std::fmod(voice.cursor, static_cast<double>(frames));
                else voice.clip.reset();
            }
        }
        // A soft limiter keeps overlapping footsteps, fire and cues from clipping.
        auto pcm = [this](float sample) {
            sample *= m_master;
            return static_cast<int16_t>(32767.0f * sample / (1.0f + std::fabs(sample)));
        };
        stereo[i] = pcm(left);
        stereo[i + 1] = pcm(right);
    }
}
} // namespace eng::audio
