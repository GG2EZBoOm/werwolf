#include "client/App.h"
#include "client/MusicResources.h"
#include "client/FootstepResources.h"
#include "engine/core/Log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace ww {
namespace {
#if defined(WERWOLF_HAS_MUSIC) || defined(WERWOLF_HAS_FOOTSTEPS)
eng::audio::ClipPtr readAudioResource(int id) {
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return {};
    const HGLOBAL loaded = LoadResource(module, resource);
    const auto* bytes = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    return bytes ? eng::audio::readWav({bytes, SizeofResource(module, resource)}) : eng::audio::ClipPtr{};
}
#endif
} // namespace

void App::initAudio() {
    m_sounds = std::make_unique<SoundBank>();
    m_audioReady = m_audioDevice.start(m_audio);
    if (!m_audioReady) return;
#ifdef WERWOLF_HAS_MUSIC
    const int resourceIds[] = {WW_MUSIC_DAY1, WW_MUSIC_DAY2, WW_MUSIC_NIGHT1, WW_MUSIC_NIGHT2, WW_MUSIC_NIGHT3};
    for (size_t i = 0; i < m_music.size(); ++i) {
        m_music[i] = readAudioResource(resourceIds[i]);
        if (m_music[i]) eng::log::info("Musik {} geladen: {:.1f} s, Stereo", i + 1, m_music[i]->seconds());
        else eng::log::warn("Musik-Ressource {} konnte nicht gelesen werden", resourceIds[i]);
    }
#endif
#ifdef WERWOLF_HAS_FOOTSTEPS
    std::vector<eng::audio::ClipPtr> footsteps;
    for (size_t i = 0; i < SoundBank::kFootstepCount; ++i)
        footsteps.push_back(readAudioResource(WW_FOOTSTEP_FIRST + static_cast<int>(i)));
    if (m_sounds->setFootsteps(std::move(footsteps)))
        eng::log::info("Schrittaufnahmen geladen: {} Varianten", SoundBank::kFootstepCount);
    else eng::log::warn("Schrittaufnahmen konnten nicht vollständig gelesen werden");
#endif
    const Sound loops[] = {Sound::Wind, Sound::Crickets, Sound::Birds, Sound::Fire};
    for (size_t i = 0; i < m_ambienceVoices.size(); ++i)
        m_ambienceVoices[i] = m_audio.play((*m_sounds)[loops[i]], eng::audio::Bus::Ambience, 0.0f, 0.0f, 1.0f, true);
    m_ui.onActivate = [this] { playSound(Sound::Click, 0.65f); };
}

void App::applyAudioSettings() {
    const bool background = m_config.muteUnfocused && (!m_window.hasFocus() || m_window.isMinimized());
    m_audio.setVolume(static_cast<float>(m_config.masterVolume) / 100.0f,
                      static_cast<float>(m_config.effectsVolume) / 100.0f,
                      static_cast<float>(m_config.ambienceVolume) / 100.0f, m_config.muted || background,
                      static_cast<float>(m_config.musicVolume) / 100.0f);
}

void App::playSound(Sound sound, float gain, float pan, float pitch) {
    if (!m_audioReady || m_config.muted || m_config.masterVolume == 0 || m_config.effectsVolume == 0 ||
        (m_config.muteUnfocused && (!m_window.hasFocus() || m_window.isMinimized()))) return;
    m_audio.play(m_sounds->choose(sound, m_audioRng), eng::audio::Bus::Effects, gain, pan, pitch);
    if (sound == Sound::Bell || sound == Sound::Death || sound == Sound::Reveal || sound == Sound::Magic || sound == Sound::Victory)
        m_musicDuckUntil = m_time + 2.0;
}

void App::updateAudio() {
    if (!m_audioReady || !m_worldReady) return;
    const bool match = m_screen == Screen::Session && m_inMatch;
    const bool sandbox = m_screen == Screen::Sandbox;
    const bool world = match || sandbox;
    const int scene = static_cast<int>(m_screen) * 2 + (match ? 1 : 0);
    if (scene != m_audioScene) {
        m_soundEvents.reset();
        m_audioScene = scene;
    }
    const Vec2 listener = world ? Vec2{m_player.position.x, m_player.position.z} : Vec2{m_menuCamPos.x, m_menuCamPos.z};
    const float yaw = world ? m_camera.yaw : std::atan2(m_menuCamTarget.x - m_menuCamPos.x, m_menuCamTarget.z - m_menuCamPos.z);
    auto panAt = [&](Vec2 position) {
        const Vec2 offset = position - listener;
        return soundPan(offset, yaw);
    };
    const float tod = match ? m_timeOfDay : (sandbox || m_screen == Screen::Preview) ? m_previewTimeOfDay : 0.93f;
    const float elevation = std::sin((tod - 0.25f) * 2.0f * eng::kPi);
    const float night = 1.0f - eng::smoothstep(-0.22f, 0.02f, elevation);
    updateMusic(world, match ? m_net.view().phase == Phase::Night : night > 0.6f);
    const int house = world ? m_world.houseAt(listener) : -1;
    const float outdoors = house >= 0 ? 0.2f : 1.0f;
    const float backdrop = world ? 1.0f : 0.6f;
    m_audio.setVoice(m_ambienceVoices[0], 0.45f * outdoors * backdrop, -0.2f);
    m_audio.setVoice(m_ambienceVoices[1], night * 0.6f * outdoors * backdrop, 0.35f);
    m_audio.setVoice(m_ambienceVoices[2], (1.0f - night) * 0.7f * outdoors * backdrop, -0.4f);

    const std::vector<DoorState>& doors = match ? m_net.doors() : m_sandboxDoors;
    float fire = 0.0f, firePan = 0.0f;
    auto fireAt = [&](Vec3 position, float radius, float level) {
        const Vec2 p{position.x, position.z};
        const float proximity = std::max(0.0f, 1.0f - (p - listener).length() / radius);
        const float gain = proximity * proximity * level;
        if (gain > fire) { fire = gain; firePan = panAt(p); }
    };
    fireAt(m_world.bonfire(), 25.0f, house >= 0 ? 0.15f : 1.0f);
    for (size_t i = 0; i < m_world.houses().size(); ++i) {
        const House& h = m_world.houses()[i];
        if (i < doors.size() && (doors[i].flags & DoorBurning)) fireAt(h.position, 30.0f, 1.0f);
        else if (house == static_cast<int>(i) && h.chimney &&
                 (i >= doors.size() || !(doors[i].flags & DoorBurnt))) fireAt(h.hearth, 12.0f, 0.85f);
    }
    const float follow = 1.0f - std::exp(-m_dt * 4.0f);
    m_fireGain += (fire - m_fireGain) * follow;
    m_firePan += (firePan - m_firePan) * follow;
    m_audio.setVoice(m_ambienceVoices[3], m_fireGain, m_firePan);

    if (!world) return;
    std::vector<EntityState> entities;
    if (match) {
        entities = m_entities;
        for (auto& e : entities) {
            if (e.id == m_net.myId() && e.kind == EntityLiving) {
                e.x = m_player.position.x;
                e.z = m_player.position.z;
            }
        }
    } else {
        auto add = [&](uint8_t id, const Mover& mover, float transform, uint8_t flags) {
            EntityState e;
            e.id = id;
            e.x = mover.position.x;
            e.z = mover.position.z;
            e.transform = transform;
            e.flags = flags;
            entities.push_back(e);
        };
        const uint8_t flags = static_cast<uint8_t>((m_playerAttack > 0.0f ? EntityAttack : 0) |
                                                  (m_player.velocity.length() > kWalkSpeed + 0.5f ? EntitySprint : 0));
        add(0, m_player, m_playerWolf, flags);
        for (const SandboxNpc& npc : m_npcs) add(npc.id, npc.mover, 0.0f, 0);
    }
    const auto cues = m_soundEvents.update(m_world, entities, doors);
    for (const SoundCue& cue : cues) {
        const float distance = (cue.position - listener).length();
        if (distance >= cue.range) continue;
        const float falloff = 1.0f - distance / cue.range;
        const float occlusion = m_world.lineBlocked(listener, cue.position, closedDoorMask(doors)) ? 0.4f : 1.0f;
        const bool footstep = isFootstep(cue.sound);
        const float pitch = footstep ? m_audioRng.uniform(0.98f, 1.02f) : m_audioRng.uniform(0.92f, 1.08f);
        const float variation = footstep ? m_audioRng.uniform(0.92f, 1.0f) : 1.0f;
        const float surface = cue.sound == Sound::StepGrass ? 0.85f : cue.sound == Sound::StepWood ? 0.95f : 1.0f;
        playSound(cue.sound, cue.gain * falloff * falloff * occlusion * variation * surface, panAt(cue.position), pitch);
    }
}

void App::updateMusic(bool inWorld, bool night) {
    const bool match = m_screen == Screen::Session && m_inMatch;
    const Phase phase = match ? m_net.view().phase : Phase::Day;
    const bool over = match && phase == Phase::GameOver;
    const bool nightActive = inWorld && night && !over;
    const bool dayActive = inWorld && !night && !over && phase != Phase::Execution;
    const float duck = m_time < m_musicDuckUntil ? 0.4f : 1.0f;
    const float gathering = match && (isGatheringPhase(phase) || m_pauseOpen || m_journalOpen) ? 0.65f : 1.0f;
    const float follow = 1.0f - std::exp(-m_dt * 1.3f);
    m_dayMusicGain += ((dayActive ? 0.5f * duck * gathering : 0.0f) - m_dayMusicGain) * follow;
    m_nightMusicGain += ((nightActive ? 0.6f * duck : 0.0f) - m_nightMusicGain) * follow;

    // Alternate the two day tracks with a 2.5 second overlap, preserving stereo.
    if (m_music[0] && m_music[1]) {
        const bool next = m_dayMusicVoice && m_time - m_dayTrackStarted >= m_music[m_dayTrack]->seconds() - 2.5;
        if (dayActive && (!m_dayMusicVoice || next)) {
            m_audio.stopVoice(m_previousDayVoice);
            m_previousDayVoice = m_dayMusicVoice;
            if (next) m_dayTrack = (m_dayTrack + 1) % 2;
            m_dayMusicVoice = m_audio.play(m_music[m_dayTrack], eng::audio::Bus::Music, 0.0f);
            m_dayTrackStarted = m_time;
            eng::log::info("Tagesmusik: Track {}", m_dayTrack + 1);
        }
        const float blend = std::clamp(static_cast<float>((m_time - m_dayTrackStarted) / 2.5), 0.0f, 1.0f);
        m_audio.setVoice(m_dayMusicVoice, m_dayMusicGain * blend);
        m_audio.setVoice(m_previousDayVoice, m_dayMusicGain * (1.0f - blend));
        if (blend >= 1.0f || (!dayActive && m_dayMusicGain < 0.001f)) {
            m_audio.stopVoice(m_previousDayVoice);
            m_previousDayVoice = 0;
        }
        if (!dayActive && m_dayMusicGain < 0.001f) {
            m_audio.stopVoice(m_dayMusicVoice);
            m_dayMusicVoice = 0;
        }
    }

    // Stingers are intentionally separated by quiet intervals, not looped back to back.
    if (nightActive && !m_musicWasNight) m_nextNightMusicAt = m_time;
    if (nightActive && m_time >= m_nextNightMusicAt) {
        const auto& track = m_music[2 + m_nightTrack];
        if (track) {
            m_audio.stopVoice(m_nightMusicVoice);
            m_nightMusicVoice = m_audio.play(track, eng::audio::Bus::Music, m_nightMusicGain);
            m_nextNightMusicAt = m_time + track->seconds() + m_audioRng.uniform(22.0f, 38.0f);
            eng::log::info("Nachtmusik: Stinger {}", m_nightTrack + 1);
        } else m_nextNightMusicAt = m_time + 30.0;
        m_nightTrack = (m_nightTrack + 1) % 3;
    }
    m_audio.setVoice(m_nightMusicVoice, m_nightMusicGain);
    if (!nightActive && m_nightMusicGain < 0.001f) {
        m_audio.stopVoice(m_nightMusicVoice);
        m_nightMusicVoice = 0;
    }
    m_musicWasNight = nightActive;
}
} // namespace ww
