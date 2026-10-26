#pragma once

#include "logic/Types.h"
#include "world/World.h"

#include "engine/core/Random.h"

#include <optional>
#include <string>
#include <vector>

namespace ww {

// Echtzeit-Regelwerk: Spieler bewegen sich frei durchs Dorf, Werwölfe jagen nachts,
// Rollen wirken auf Spieler in ihrer Nähe. Läuft nur auf dem Server.
class Match {
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

        Vec3 position;
        float yaw = 0.0f;
        uint8_t moveFlags = 0;
        double lastMoveTime = 0.0;
        bool needsCorrection = false;
        Vec3 ghostPosition;
        float ghostYaw = 0.0f;

        float transform = 0.0f;        // 0 = Mensch, 1 = Werwolf
        float transformTarget = 0.0f;
        float attackCooldown = 0.0f;
        float attackAnim = 0.0f;

        bool nightAbilityUsed = false;
        std::vector<uint8_t> cupidPicks;
        float hunterTimer = 0.0f;
        uint8_t vote = kNoPlayer;

        Vec3 deathPosition;
        float deathYaw = 0.0f;
        int deathDay = 0;
        bool diedAtNight = false;
        DeathCause deathCause = DeathCause::Wolves;
        bool buried = false;

        bool wolfForm() const { return transform >= 0.999f; }
    };

    bool start(const std::vector<Seat>& seats, const Settings& settings, const World& world, uint64_t seed,
               std::string& error);
    // Für Tests: feste Rollen (Reihenfolge wie die Plätze)
    bool startWithRoles(const std::vector<Seat>& seats, const std::vector<Role>& roles, const Settings& settings,
                        const World& world, uint64_t seed, std::string& error);
    void update(float dt);

    void movePlayer(uint8_t id, float x, float z, float yaw, uint8_t flags);
    std::string interact(uint8_t id, Interaction kind, uint8_t target);
    void playerLeft(uint8_t id);
    void setConnected(uint8_t id, bool connected);
    void skipPhase() { m_timer = 0.0f; }
    // Für Tests und Bots: Position direkt setzen (ohne Prüfung)
    void teleport(uint8_t id, Vec3 position);

    Phase phase() const { return m_phase; }
    float timeLeft() const { return m_timer; }
    float phaseDuration() const { return m_phaseDuration; }
    int day() const { return m_day; }
    Team winner() const { return m_winner; }
    double clock() const { return m_clock; }
    const Settings& settings() const { return m_settings; }
    const World& world() const { return *m_world; }
    const std::vector<Player>& players() const { return m_players; }
    const Player* find(uint8_t id) const;
    uint8_t packKillsLeft() const { return m_packKills; }
    uint8_t protectedTarget() const { return m_protected; }

    uint16_t abilitiesFor(uint8_t id) const;
    // Prüft eine Interaktion ohne sie auszuführen; liefert den Grund, falls nicht möglich.
    std::string checkInteraction(uint8_t id, Interaction kind, uint8_t target) const;
    std::vector<EntityState> entitiesFor(uint8_t viewer) const;
    GameView viewFor(uint8_t viewer) const;

    std::optional<ChatChannel> chatChannel(uint8_t speaker, bool wantWolves) const;
    bool canHear(uint8_t listener, uint8_t speaker, ChatChannel channel) const;
    bool canReadChannel(uint8_t listener, ChatChannel channel) const;

    Team teamOf(uint8_t id) const;
    bool isWinner(uint8_t id) const;

    std::vector<uint8_t> takeCorrections();
    std::vector<Notice> takeNotices();
    bool takeDirty();

    // Sitzplätze am Feuer für die Versammlung
    Vec3 seatPosition(int index, int count) const;

private:
    Player* findMutable(uint8_t id);
    void markDirty() { m_dirty = true; }
    void setTimer(float seconds);
    void notify(Notice::Audience audience, uint8_t player, NoticeKind kind, std::string text);
    void narrate(std::string text) { notify(Notice::Audience::All, kNoPlayer, NoticeKind::Narration, std::move(text)); }
    void tell(uint8_t player, std::string text) { notify(Notice::Audience::Player, player, NoticeKind::Private, std::move(text)); }
    std::string nameOf(uint8_t id) const;
    bool hasLineOfSight(Vec3 a, Vec3 b) const;
    bool mixedCouple() const;

    void enterDay();
    void enterAssembly(const std::string& reason);
    void enterVote();
    void resolveVote();
    void enterNight();
    void enterMorning();
    void advance();
    void kill(uint8_t id, DeathCause cause, bool announceNow);
    void announce(uint8_t id, DeathCause cause);
    void makeLovers(uint8_t a, uint8_t b);
    void checkVotesComplete();
    Team evaluateWinner() const;
    bool checkWin();

    const World* m_world = nullptr;
    Settings m_settings;
    eng::Rng m_rng;
    std::vector<Player> m_players;
    Phase m_phase = Phase::Lobby;
    float m_timer = 0.0f;
    float m_phaseDuration = 0.0f;
    double m_clock = 0.0;
    int m_day = 0;
    Team m_winner = Team::None;

    uint8_t m_packKills = 0;
    uint8_t m_protected = kNoPlayer;
    uint8_t m_lastProtected = kNoPlayer;
    uint8_t m_protector = kNoPlayer;
    bool m_healAvailable = false;
    bool m_poisonAvailable = false;
    bool m_bellRung = false;
    bool m_voteGraceApplied = false;
    float m_phaseElapsed = 0.0f;
    std::vector<SeerResult> m_seerResults;
    struct PendingDeath {
        uint8_t id;
        DeathCause cause;
    };
    std::vector<PendingDeath> m_nightDeaths;

    std::vector<Notice> m_notices;
    bool m_dirty = true;
};

} // namespace ww
