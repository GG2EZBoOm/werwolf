#pragma once

#include "logic/Game.h"

#include "engine/core/Random.h"

#include <functional>
#include <string>

namespace ww {

// Einfache KI-Mitspieler, damit man auch mit wenigen Menschen spielen (und testen) kann.
// Bots sehen nur, was ihnen ihre persönliche Spielansicht verrät.
class Bot {
public:
    using SayFn = std::function<void(uint8_t botId, const std::string& text)>;

    Bot(uint8_t id, uint64_t seed);
    uint8_t id() const { return m_id; }
    void update(float dt, Game& game, const SayFn& say);

private:
    void act(Game& game, const GameView& view, const SayFn& say);
    void chatter(const GameView& view, const SayFn& say);
    std::vector<uint8_t> validTargets(const Game& game, const GameView& view) const;
    uint8_t mostVoted(const GameView& view, const std::vector<uint8_t>& allowed) const;
    std::string randomOtherName(const GameView& view);
    float thinkDelay(ActionKind action);

    eng::Rng m_rng;
    uint8_t m_id;
    Phase m_phase = Phase::Lobby;
    int m_day = -1;
    float m_think = 0.0f;
    float m_chatTimer = -1.0f;
    bool m_acted = false;
};

} // namespace ww
