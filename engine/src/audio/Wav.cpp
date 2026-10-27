#include "engine/audio/Audio.h"

#include <cstring>

namespace eng::audio {
ClipPtr readWav(std::span<const uint8_t> bytes) {
    auto tag = [&](size_t at, const char* value) { return at + 4 <= bytes.size() && std::memcmp(bytes.data() + at, value, 4) == 0; };
    auto u16 = [&](size_t at) { return static_cast<unsigned>(bytes[at]) | (static_cast<unsigned>(bytes[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (static_cast<uint32_t>(u16(at + 2)) << 16); };
    if (bytes.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE")) return {};
    const uint64_t end = static_cast<uint64_t>(u32(4)) + 8;
    if (end > bytes.size() || end < 12) return {};
    unsigned channels = 0;
    std::span<const uint8_t> data;
    for (size_t at = 12; at + 8 <= end;) {
        const size_t size = u32(at + 4);
        const size_t start = at + 8;
        if (size > end - start) return {};
        if (tag(at, "fmt ")) {
            if (size < 16 || u16(start) != 1 || u32(start + 4) != kSampleRate || u16(start + 14) != 16) return {};
            channels = u16(start + 2);
            if ((channels != 1 && channels != 2) || u16(start + 12) != channels * 2 ||
                u32(start + 8) != kSampleRate * channels * 2) return {};
        } else if (tag(at, "data")) data = bytes.subspan(start, size);
        at = start + size + (size & 1);
    }
    if (channels == 0 || data.empty() || data.size() % (channels * 2) != 0) return {};
    auto clip = std::make_shared<Clip>();
    clip->channels = channels;
    clip->samples.resize(data.size() / 2);
    for (size_t i = 0; i < clip->samples.size(); ++i) {
        const unsigned bits = static_cast<unsigned>(data[i * 2]) | (static_cast<unsigned>(data[i * 2 + 1]) << 8);
        const int sample = bits >= 32768 ? static_cast<int>(bits) - 65536 : static_cast<int>(bits);
        clip->samples[i] = static_cast<float>(sample) / 32768.0f;
    }
    return clip;
}
} // namespace eng::audio
