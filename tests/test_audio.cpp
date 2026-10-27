#include "TestFramework.h"
#include "audio/SoundEvents.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

using namespace ww;
using namespace eng::audio;

namespace {
const SoundBank& bank() { static const SoundBank sounds; return sounds; }
ClipPtr constantClip(float value, size_t size = 441) {
    auto clip = std::make_shared<Clip>();
    clip->samples.assign(size, value);
    return clip;
}
double energy(const std::vector<int16_t>& pcm, size_t channel = 0) {
    double result = 0;
    for (size_t i = channel; i < pcm.size(); i += 2) result += std::fabs(pcm[i]);
    return result;
}
bool contains(const std::vector<SoundCue>& cues, Sound sound) {
    return std::any_of(cues.begin(), cues.end(), [&](const SoundCue& cue) { return cue.sound == sound; });
}
} // namespace

TEST(all_sounds_are_audible_finite_and_have_safe_edges) {
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        const auto& samples = bank()[static_cast<Sound>(i)]->samples;
        CHECK(!samples.empty());
        CHECK(std::all_of(samples.begin(), samples.end(), [](float s) { return std::isfinite(s) && std::fabs(s) <= 0.95f; }));
        double energySum = 0.0;
        for (float s : samples) energySum += static_cast<double>(s) * s;
        CHECK(energySum / static_cast<double>(samples.size()) > 0.00001);
        if (i < static_cast<int>(Sound::Wind)) {
            CHECK_EQ(samples.front(), 0.0f);
            CHECK(std::fabs(samples.back()) < 0.001f);
        } else {
            // Loop boundary must not introduce a large discontinuity.
            CHECK(std::fabs(samples.front() - samples.back()) < 0.15f);
        }
    }
}

TEST(mixer_panning_mute_and_bus_volume) {
    Mixer mixer;
    mixer.setVolume(1, 1, 0, false);
    mixer.play(constantClip(0.25f), Bus::Effects, 1, -1, 1, true);
    std::vector<int16_t> pcm(kSampleRate / 5 * 2);
    mixer.render(pcm);
    CHECK(energy(pcm) > 10000);
    CHECK_EQ(energy(pcm, 1), 0.0);
    mixer.setVolume(1, 0, 1, false);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm), 0.0);
    mixer.play(constantClip(0.25f), Bus::Ambience, 1, 1, 1, true);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm), 0.0);
    CHECK(energy(pcm, 1) > 10000);
    mixer.setVolume(1, 1, 1, true);
    mixer.render(pcm); // let the short fade finish
    mixer.render(pcm);
    CHECK_EQ(energy(pcm) + energy(pcm, 1), 0.0);
    mixer.setVolume(1, 1, 1, false);
    mixer.render(pcm);
    CHECK(energy(pcm) > 10000);
    mixer.stopAll();
    mixer.render(pcm);
    CHECK_EQ(energy(pcm) + energy(pcm, 1), 0.0);
}

TEST(mixer_finishes_effects_and_keeps_loops_under_load) {
    Mixer mixer;
    mixer.setVolume(1, 1, 1, false);
    CHECK_EQ(mixer.play(nullptr), 0u);
    const auto loop = mixer.play(constantClip(0.1f, 1), Bus::Ambience, 1, 0, 2, true);
    for (size_t i = 0; i < Mixer::kMaxVoices * 3; ++i)
        CHECK(mixer.play(bank()[Sound::Click], Bus::Effects, 1, 0, 2) != 0);
    std::vector<int16_t> pcm(kSampleRate * 2);
    mixer.render(pcm);
    CHECK(std::all_of(pcm.begin(), pcm.end(), [](int16_t s) { return s > -32767 && s < 32767; }));
    mixer.render(pcm);
    CHECK(energy(pcm) > 10000); // loop survives eviction of short effects
    mixer.setVoice(loop, 0);
    mixer.render(pcm);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm) + energy(pcm, 1), 0.0);
    mixer.setVolume(std::numeric_limits<float>::quiet_NaN(), 1, 1, false);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm), 0.0);
}

TEST(world_audio_uses_transitions_and_ignores_ghosts_and_teleports) {
    World world;
    world.generate(1337);
    SoundEvents events;
    EntityState actor;
    actor.id = 1;
    actor.z = 4;
    std::vector<EntityState> entities{actor};
    std::vector<DoorState> doors(world.houses().size());
    CHECK(events.update(world, entities, doors).empty());
    CHECK(events.update(world, entities, doors).empty());
    entities[0].x += 1.5f;
    auto cues = events.update(world, entities, doors);
    CHECK_EQ(cues.size(), size_t{1});
    CHECK(contains(cues, Sound::StepStone));
    CHECK(events.update(world, entities, doors).empty()); // standing still
    entities[0].x += 30.0f;
    CHECK(events.update(world, entities, doors).empty());
    entities[0].transform = 0.2f;
    entities[0].flags = EntityAttack;
    cues = events.update(world, entities, doors);
    CHECK(contains(cues, Sound::Transform));
    CHECK(contains(cues, Sound::Attack));
    entities[0].transform = 0.6f;
    CHECK(events.update(world, entities, doors).empty()); // no repeated sound during animation
    entities[0].kind = EntityGhost;
    entities[0].x += 1.5f;
    CHECK(events.update(world, entities, doors).empty());
    entities[0].kind = EntityLiving;
    CHECK(events.update(world, entities, doors).empty()); // new actor gets a baseline
    doors[0].flags = DoorOpen;
    CHECK(contains(events.update(world, entities, doors), Sound::DoorOpen));
    CHECK(events.update(world, entities, doors).empty());
    doors[0].flags = DoorLocked;
    CHECK(contains(events.update(world, entities, doors), Sound::DoorClose));
    doors[0].health = 50;
    CHECK(contains(events.update(world, entities, doors), Sound::DoorHit));
    doors[0].flags = DoorBroken | DoorOpen;
    doors[0].health = 0;
    cues = events.update(world, entities, doors);
    CHECK_EQ(cues.size(), size_t{1});
    CHECK(contains(cues, Sound::DoorBreak));
    events.reset();
    CHECK(events.update(world, entities, doors).empty()); // reconnect doesn't replay old events
}

TEST(footsteps_follow_floor_surface) {
    World world;
    world.generate(1337);
    SoundEvents events;
    const Vec2 indoors = world.houseToWorld(0, {-1.0f, 0.0f});
    EntityState actor;
    actor.id = 1;
    actor.x = indoors.x;
    actor.z = indoors.y;
    events.update(world, {actor}, {});
    const Vec2 next = world.houseToWorld(0, {0.5f, 0.0f});
    actor.x = next.x;
    actor.z = next.y;
    CHECK(contains(events.update(world, {actor}, {}), Sound::StepWood));
}

TEST(stereo_music_preserves_channels_and_has_its_own_volume) {
    auto clip = std::make_shared<Clip>();
    clip->channels = 2;
    clip->samples = {0.2f, -0.4f, 0.2f, -0.4f};
    Mixer mixer;
    mixer.setVolume(1, 0, 0, false, 1);
    const auto voice = mixer.play(clip, Bus::Music, 1, 0, 1, true);
    std::vector<int16_t> pcm(kSampleRate / 5 * 2);
    mixer.render(pcm);
    CHECK(pcm[pcm.size() - 2] > 0);
    CHECK(pcm.back() < 0);
    CHECK(energy(pcm, 1) > energy(pcm));
    mixer.setVolume(1, 1, 1, false, 0);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm) + energy(pcm, 1), 0.0);
    mixer.setVolume(1, 1, 1, false, 1);
    mixer.stopVoice(voice);
    mixer.render(pcm);
    CHECK_EQ(energy(pcm) + energy(pcm, 1), 0.0);
}

TEST(spatial_audio_matches_the_camera_right_vector) {
    CHECK(soundPan({-5, 0}, 0) > 0.8f);
    CHECK(soundPan({5, 0}, 0) < -0.8f);
    CHECK(std::fabs(soundPan({0, 5}, 0)) < 0.001f);
    CHECK(soundPan({0, 5}, eng::kPi / 2) > 0.8f);
    CHECK_EQ(soundPan({0, 0}, 0), 0.0f);
}

TEST(wav_reader_rejects_corruption_and_preserves_stereo) {
    std::vector<uint8_t> bytes;
    auto text = [&](const char* s) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(s[i])); };
    auto u16 = [&](unsigned n) { bytes.push_back(static_cast<uint8_t>(n)); bytes.push_back(static_cast<uint8_t>(n >> 8)); };
    auto u32 = [&](unsigned n) { u16(n); u16(n >> 16); };
    text("RIFF"); u32(40); text("WAVE"); text("fmt "); u32(16);
    u16(1); u16(2); u32(kSampleRate); u32(kSampleRate * 4); u16(4); u16(16);
    text("data"); u32(4); u16(16384); u16(49152);
    const auto clip = readWav(bytes);
    CHECK(clip != nullptr);
    if (clip) {
        CHECK_EQ(clip->channels, 2u);
        CHECK_EQ(clip->frames(), size_t{1});
        CHECK_EQ(clip->samples[0], 0.5f);
        CHECK_EQ(clip->samples[1], -0.5f);
    }
    for (size_t i = 0; i < bytes.size(); ++i) CHECK(!readWav(std::span(bytes).first(i)));
    auto corrupt = bytes;
    corrupt[40] = 255; // data chunk points beyond the file
    CHECK(!readWav(corrupt));
    corrupt = bytes;
    corrupt[22] = 3; // unsupported channel count
    CHECK(!readWav(corrupt));
    corrupt = bytes;
    corrupt[34] = 24; // packaged files must be converted to PCM16
    CHECK(!readWav(corrupt));
}

#ifdef WERWOLF_TEST_MUSIC_DIR
TEST(imported_music_files_decode_at_runtime) {
    const char* names[] = {"day_tension_1.wav", "day_tension_2.wav", "night_foreshadow.wav",
                           "night_eyes_in_the_dark.wav", "night_creeping_death.wav"};
    for (const char* name : names) {
        std::ifstream file(std::string(WERWOLF_TEST_MUSIC_DIR) + "/" + name, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto clip = readWav(bytes);
        CHECK(clip != nullptr);
        if (!clip) continue;
        CHECK_EQ(clip->channels, 2u);
        CHECK(clip->seconds() > 10 && clip->seconds() < 60);
        CHECK(std::all_of(clip->samples.begin(), clip->samples.end(), [](float x) { return std::isfinite(x) && std::fabs(x) <= 0.86f; }));
    }
}
#endif

// Optional listening sample containing every effect and an ambience bed.
// Run: test_audio --demo <file.wav>. Does not require an output device.
int main(int argc, char** argv) {
    const int result = test::runAll();
    if (result != 0 || argc != 3 || std::string(argv[1]) != "--demo") return result;
    Mixer mixer;
    mixer.setVolume(0.75f, 0.85f, 0.6f, false);
    mixer.play(bank()[Sound::Wind], Bus::Ambience, 0.45f, -0.2f, 1, true);
    mixer.play(bank()[Sound::Crickets], Bus::Ambience, 0.6f, 0.35f, 1, true);
    mixer.play(bank()[Sound::Fire], Bus::Ambience, 0.7f, -0.45f, 1, true);
    std::vector<int16_t> all;
    std::vector<int16_t> part(kSampleRate * 2 * 2);
    mixer.render(part);
    all.insert(all.end(), part.begin(), part.end());
    for (int i = 0; i < static_cast<int>(Sound::Wind); ++i) {
        mixer.play(bank()[static_cast<Sound>(i)], Bus::Effects, 0.8f);
        mixer.render(part);
        all.insert(all.end(), part.begin(), part.end());
    }
    const uint32_t bytes = static_cast<uint32_t>(all.size() * sizeof(int16_t));
    std::ofstream out(argv[2], std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16);
    u16(1); u16(2); u32(kSampleRate); u32(kSampleRate * 4); u16(4); u16(16);
    out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(all.data()), bytes);
    return out ? 0 : 1;
}
