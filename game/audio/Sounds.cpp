#include "audio/Sounds.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ww {
namespace {
constexpr float tau = 2.0f * std::numbers::pi_v<float>;
float tone(float cycles) { return std::sin(tau * cycles); }
float decay(float t, float rate) { return std::exp(-t * rate); }
float pulse(float t, float start, float length) {
    const float x = (t - start) / length;
    if (x <= 0.0f || x >= 1.0f) return 0.0f;
    return std::sin(std::numbers::pi_v<float> * x);
}

eng::audio::ClipPtr synthesize(Sound sound) {
    const bool loop = sound >= Sound::Wind;
    float duration = 0.5f;
    switch (sound) {
    case Sound::Click: duration = 0.09f; break;
    case Sound::StepGrass: case Sound::StepStone: case Sound::StepWood: duration = 0.23f; break;
    case Sound::DoorOpen: duration = 0.75f; break;
    case Sound::DoorClose: case Sound::DoorHit: case Sound::Attack: duration = 0.38f; break;
    case Sound::DoorBreak: duration = 1.0f; break;
    case Sound::Transform: duration = 1.8f; break;
    case Sound::Bell: duration = 3.8f; break;
    case Sound::Night: case Sound::Victory: duration = 3.0f; break;
    case Sound::Dawn: case Sound::Reveal: case Sound::Magic: duration = 1.5f; break;
    case Sound::Death: duration = 1.6f; break;
    case Sound::Warning: case Sound::Chat: duration = 0.24f; break;
    default: break;
    }
    if (loop) duration = 8.0f;
    const size_t count = static_cast<size_t>(duration * eng::audio::kSampleRate);
    const size_t blend = loop ? static_cast<size_t>(0.3f * eng::audio::kSampleRate) : 0;
    auto clip = std::make_shared<eng::audio::Clip>();
    clip->samples.resize(count + blend);
    uint32_t rng = 0x71b9u + static_cast<uint32_t>(sound) * 7919u;
    float low = 0.0f, body = 0.0f, crackle = 0.0f;
    for (size_t i = 0; i < clip->samples.size(); ++i) {
        const float t = static_cast<float>(i) / eng::audio::kSampleRate;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float noise = static_cast<float>(rng & 0xffffu) / 32767.5f - 1.0f;
        low += (noise - low) * 0.012f;
        body += (noise - body) * 0.18f;
        const float high = noise - body;
        float value = 0.0f;
        switch (sound) {
        case Sound::Click:
            value = (0.38f * tone(780.0f * t) + 0.3f * noise) * decay(t, 65.0f);
            break;
        case Sound::StepGrass:
            value = 1.1f * body * decay(t, 26.0f) + 0.18f * high * pulse(t, 0.02f, 0.16f) +
                    0.22f * tone(76.0f * t) * decay(t, 35.0f);
            break;
        case Sound::StepStone:
            value = (0.52f * body + 0.35f * tone(145.0f * t) + 0.15f * tone(670.0f * t)) * decay(t, 35.0f);
            break;
        case Sound::StepWood:
            value = (0.36f * tone(112.0f * t) + 0.22f * tone(241.0f * t) + 0.25f * body) * decay(t, 23.0f);
            break;
        case Sound::DoorOpen:
            value = (0.14f * tone(175.0f * t + 18.0f * t * t + 0.3f * tone(16.0f * t)) + 0.25f * body) *
                    pulse(t, 0.02f, 0.68f) + 0.35f * body * decay(t, 40.0f);
            break;
        case Sound::DoorClose: case Sound::DoorHit:
            value = (0.55f * tone(83.0f * t) + 0.24f * tone(191.0f * t) + 0.5f * body) * decay(t, 18.0f);
            if (sound == Sound::DoorHit) value += 0.25f * high * decay(t, 40.0f);
            break;
        case Sound::DoorBreak:
            value = 0.6f * body * decay(t, 5.0f) + 0.3f * tone(65.0f * t) * decay(t, 8.0f);
            for (float at : {0.02f, 0.09f, 0.16f, 0.31f, 0.48f}) value += 0.4f * high * pulse(t, at, 0.045f);
            break;
        case Sound::Transform:
            value = (0.29f * tone(92.0f * t - 14.0f * t * t + 0.22f * tone(8.0f * t)) +
                     0.16f * tone(188.0f * t - 22.0f * t * t) + 1.4f * low) * pulse(t, 0.0f, duration);
            break;
        case Sound::Attack:
            value = 0.9f * body * pulse(t, 0.0f, 0.19f) +
                    0.35f * tone(94.0f * t - 65.0f * t * t) * pulse(t, 0.08f, 0.28f);
            break;
        case Sound::Bell:
            for (float at : {0.0f, 0.85f}) {
                const float s = t - at;
                if (s >= 0.0f) value += (0.27f * tone(440.0f * s) + 0.17f * tone(923.0f * s) +
                                        0.09f * tone(1211.0f * s) + 0.05f * tone(1763.0f * s)) *
                                       decay(s, 1.7f) * std::min(1.0f, s * 250.0f);
            }
            break;
        case Sound::Night:
            value = (0.23f * tone(110.0f * t) + 0.12f * tone(164.81f * t) +
                     0.09f * tone(233.08f * t) + 0.5f * low) * pulse(t, 0.0f, duration);
            break;
        case Sound::Dawn: case Sound::Reveal: case Sound::Magic: case Sound::Victory: {
            const float root = sound == Sound::Magic ? 440.0f : sound == Sound::Reveal ? 220.0f : 261.63f;
            const float ratios[] = {1.0f, 1.25f, 1.5f, 2.0f};
            for (int note = 0; note < 4; ++note) {
                const float s = t - static_cast<float>(note) * 0.16f;
                if (s >= 0.0f) value += 0.22f * tone(root * ratios[note] * s) * decay(s, 3.2f) * std::min(1.0f, s * 80.0f);
            }
            break;
        }
        case Sound::Vote:
            value = (0.3f * tone(330.0f * t) + 0.16f * tone(495.0f * t)) * decay(t, 10.0f);
            break;
        case Sound::Death:
            value = (0.32f * tone(120.0f * t - 30.0f * t * t) + 0.9f * low) * pulse(t, 0.0f, duration) * decay(t, 1.5f);
            break;
        case Sound::Warning:
            value = 0.26f * tone(220.0f * t) * (pulse(t, 0.0f, 0.09f) + pulse(t, 0.12f, 0.09f));
            break;
        case Sound::Chat:
            value = (0.2f * tone(660.0f * t) + 0.1f * tone(990.0f * t)) * decay(t, 22.0f);
            break;
        case Sound::Wind:
            value = 1.8f * low * (0.55f + 0.15f * tone(t * 0.25f) + 0.12f * tone(t * 0.63f));
            break;
        case Sound::Crickets: {
            const float chirp = std::max(0.0f, tone(1.5f * t)) * std::pow(std::max(0.0f, tone(24.0f * t)), 3.0f);
            value = 0.12f * tone(4100.0f * t + 0.5f * tone(3.0f * t)) * chirp + 0.15f * low;
            break;
        }
        case Sound::Birds:
            for (float at : {0.5f, 0.73f, 3.6f, 4.0f, 6.3f}) {
                const float s = t - at;
                value += 0.16f * tone(1950.0f * s + 900.0f * s * s + 0.25f * tone(28.0f * s)) * pulse(t, at, 0.18f);
            }
            break;
        case Sound::Fire:
            crackle *= 0.985f;
            if ((rng & 8191u) < 9u) crackle = 0.25f + 0.4f * std::fabs(noise);
            value = 1.1f * low + 0.12f * body + high * crackle;
            break;
        default: break;
        }
        if (!loop) value *= std::clamp(t / 0.004f, 0.0f, 1.0f) * std::clamp((duration - t) / 0.045f, 0.0f, 1.0f);
        clip->samples[i] = std::clamp(value, -0.95f, 0.95f);
    }
    // Wrap into the tail smoothly, including nonperiodic noise and fire crackles.
    for (size_t i = 0; i < blend; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(blend);
        clip->samples[i] = clip->samples[count + i] * (1.0f - f) + clip->samples[i] * f;
    }
    clip->samples.resize(count);
    return clip;
}
} // namespace

SoundBank::SoundBank() {
    for (size_t i = 0; i < m_clips.size(); ++i) m_clips[i] = synthesize(static_cast<Sound>(i));
}
} // namespace ww
