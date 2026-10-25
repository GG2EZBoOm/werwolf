#pragma once

#include "logic/Types.h"

#include "engine/core/Random.h"

#include <string>
#include <vector>

namespace ww {

// Die komplette Spielregel-Logik als Zustandsmaschine – ohne Netzwerk und Grafik.
// Der Server füttert sie mit Aktionen und Zeit, sie liefert Ansichten und Meldungen.
class Game {
public:
    struct Seat {
        uint8_t id = kNoPlayer;
        std::string name;
        bool bot = false;
    };

    struct Player {
        uint8_t id = kNoPlayer;
        std::string name;
        bool bot = false;
        bool connected = true;
        Role role = Role::Villager;
        bool alive = true;
        uint8_t lover = kNoPlayer;
        // Zustand der aktuellen Phase
        std::vector<uint8_t> selection;
        bool confirmed = false;
        bool skip = false;
    };

    bool start(const std::vector<Seat>& seats, const Settings& settings, uint64_t seed, std::string& error);
    // Nur für Tests: feste Rollenverteilung (Reihenfolge wie die Plätze).
    bool startWithRoles(const std::vector<Seat>& seats, const std::vector<Role>& roles, const Settings& settings,
                        uint64_t seed, std::string& error);
    void update(float dt);

    // Aktionen – geben bei Erfolg einen leeren String zurück, sonst eine Fehlermeldung.
    std::string select(uint8_t player, uint8_t target);
    std::string confirm(uint8_t player);
    std::string setWitchHeal(uint8_t player, bool use);
    std::string setSkip(uint8_t player, bool skip);
    void playerLeft(uint8_t player);
    void setConnected(uint8_t player, bool connected);
    // Überspringt die Restzeit der aktuellen Phase (Tests, Admin).
    void skipPhase() { m_timer = 0.0f; }

    Phase phase() const { return m_phase; }
    float timeLeft() const { return m_timer; }
    int day() const { return m_day; }
    Team winner() const { return m_winner; }
    const Settings& settings() const { return m_settings; }
    const std::vector<Player>& players() const { return m_players; }
    const Player* find(uint8_t id) const;

    ActionKind actionFor(uint8_t player) const;
    bool canTarget(uint8_t player, uint8_t target) const;
    std::optional<ChatChannel> speakChannel(uint8_t player) const;
    bool canRead(uint8_t player, ChatChannel channel) const;
    Team teamOf(uint8_t player) const;
    bool isWinner(uint8_t player) const;
    GameView viewFor(uint8_t viewer) const;

    std::vector<Notice> takeNotices();
    bool takeDirty();

private:
    struct Death {
        uint8_t player;
        DeathCause cause;
    };

    Player* findMutable(uint8_t id);
    void markDirty() { m_dirty = true; }
    void setTimer(float seconds);
    void resetPhaseState();
    void notify(Notice::Audience audience, uint8_t player, NoticeKind kind, std::string text);
    void narrate(std::string text) { notify(Notice::Audience::All, kNoPlayer, NoticeKind::Narration, std::move(text)); }
    void tell(uint8_t player, std::string text) {
        notify(Notice::Audience::Player, player, NoticeKind::Private, std::move(text));
    }
    std::string nameOf(uint8_t id) const;
    std::vector<uint8_t> alivePlayers() const;
    bool roleInGame(Role role) const;
    const Player* aliveWithRole(Role role) const;
    bool isNightActor(const Player& p) const;
    bool mixedCouple() const;
    void revealSeer(Player& seer);

    void advance();
    void enterNight();
    void endNight();
    void enterWitch();
    void resolveNight();
    void enterMorning();
    void enterHunter(Phase returnTo);
    void finishHunter();
    void enterDay();
    void enterVote();
    void resolveVote();
    void afterDeaths(Phase next);
    void kill(uint8_t id, DeathCause cause);
    void announceDeaths();
    void checkNightComplete();
    void checkVotesComplete();
    Team evaluateWinner() const;
    bool checkWin();

    Settings m_settings;
    eng::Rng m_rng;
    std::vector<Player> m_players;
    Phase m_phase = Phase::Lobby;
    float m_timer = 0.0f;
    float m_phaseDuration = 0.0f;
    int m_day = 0;
    Team m_winner = Team::None;

    uint8_t m_victim = kNoPlayer;     // Wahl der Werwölfe in dieser Nacht
    uint8_t m_protected = kNoPlayer;  // heute Nacht beschützt
    uint8_t m_lastProtected = kNoPlayer;
    bool m_healAvailable = false;
    bool m_poisonAvailable = false;
    bool m_healUsed = false;
    uint8_t m_poisonTarget = kNoPlayer;
    std::vector<SeerResult> m_seerResults;
    std::vector<Death> m_recentDeaths;
    std::vector<uint8_t> m_hunterQueue;
    uint8_t m_hunter = kNoPlayer;
    Phase m_hunterReturn = Phase::Day;
    bool m_voteGraceApplied = false;
    bool m_nightGraceApplied = false;

    std::vector<Notice> m_notices;
    bool m_dirty = true;
};

} // namespace ww
