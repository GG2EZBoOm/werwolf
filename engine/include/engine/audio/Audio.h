#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace eng::audio {

constexpr int kSampleRate = 44100;
enum class Bus { Effects, Ambience, Music };
struct Clip {
    std::vector<float> samples; // Interleaved, 44.1 kHz, [-1, 1]
    unsigned channels = 1;
    size_t frames() const { return samples.size() / channels; }
    float seconds() const { return static_cast<float>(frames()) / kSampleRate; }
};
using ClipPtr = std::shared_ptr<const Clip>;
using VoiceId = uint64_t;

// Strict PCM16 mono/stereo WAV reader for packaged music; rejects malformed input.
ClipPtr readWav(std::span<const uint8_t> bytes);

// Thread-safe control; the output thread renders interleaved stereo PCM.
// Also usable without an audio device for deterministic tests.
class Mixer {
public:
    static constexpr size_t kMaxVoices = 48;
    VoiceId play(ClipPtr clip, Bus bus = Bus::Effects, float gain = 1.0f,
                 float pan = 0.0f, float pitch = 1.0f, bool loop = false);
    void setVoice(VoiceId id, float gain, float pan = 0.0f);
    void setVolume(float master, float effects, float ambience, bool muted, float music = 1.0f);
    void stopVoice(VoiceId id);
    void stopAll();
    void render(std::span<int16_t> stereo);

private:
    struct Voice {
        ClipPtr clip;
        VoiceId id = 0;
        double cursor = 0.0;
        float pitch = 1.0f;
        float left = 0.0f, right = 0.0f;
        float targetLeft = 0.0f, targetRight = 0.0f;
        Bus bus = Bus::Effects;
        bool loop = false;
    };
    std::mutex m_mutex;
    std::array<Voice, kMaxVoices> m_voices{};
    VoiceId m_nextId = 1;
    float m_master = 0.0f, m_targetMaster = 0.0f;
    float m_effects = 1.0f, m_ambience = 1.0f, m_music = 1.0f;
};

// Native Windows output. No audio device is a recoverable failure.
class Device {
public:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    bool start(Mixer& mixer);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace eng::audio
