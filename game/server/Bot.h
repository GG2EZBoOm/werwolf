#pragma once

#include "logic/Match.h"
#include "world/Movement.h"

#include "engine/core/Random.h"

#include <functional>
#include <map>
#include <string>

namespace ww {

// KI-Mitspieler für das Echtzeit-Spiel. Bewegt sich wie ein Mensch durchs Dorf und
// nutzt nur das, was seine persönliche Spielansicht verrät (plus sichtbare Positionen).
class Bot {
public:
    using SayFn = std::function<void(uint8_t botId, const std::string& text, bool wolves)>;

    Bot(uint8_t id, uint64_t seed);
    uint8_t id() const { return m_id; }
    void update(float dt, Match& match, const SayFn& say);
    // Der Bot hört eine Chatnachricht und passt seinen Verdacht an.
    void hear(const Match& match, uint8_t speaker, const std::string& text);

private:
    void onPhaseChange(const Match& match, const GameView& view);
    void chooseWanderTarget(const Match& match);
    void act(float dt, Match& match, const GameView& view, const SayFn& say, Vec2& wish, float& speed);
    void nightRole(float dt, Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish,
                   float& speed);
    uint8_t nearestPlayer(const Match& match, Vec3 from, bool (*filter)(const Match::Player&), float maxDist) const;
    uint8_t chooseVote(const Match& match, const GameView& view);
    Vec2 steerTo(Vec2 target) const;

    eng::Rng m_rng;
    uint8_t m_id;
    Mover m_mover;
    bool m_initialized = false;
    Phase m_phase = Phase::Lobby;
    int m_day = -1;
    Vec2 m_target{0, 0};
    float m_idle = 0.0f;
    float m_stuckTimer = 0.0f;
    Vec2 m_stuckPos{0, 0};
    float m_think = 0.0f;
    float m_chatTimer = -1.0f;
    float m_transformDelay = 0.0f;
    uint8_t m_quarry = kNoPlayer;
    bool m_voted = false;
    bool m_shot = false;
    float m_shotDelay = 0.0f;
    bool m_staysOut = false; // traut sich nachts aus dem Licht
    std::map<uint8_t, float> m_suspicion; // wen der Bot für einen Werwolf hält
    std::map<uint8_t, int> m_seenAsWolf;  // als Bestie beobachtet (Nacht-Nummer)
};

} // namespace ww
