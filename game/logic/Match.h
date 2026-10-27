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
        Role startRole = Role::Villager; // Rolle bei Spielbeginn (Dieb, Verfluchte, Infizierte … wechseln)
        Role acting = Role::Unknown;     // Schauspieler: Rolle dieser Nacht
        bool alive = true;
        uint8_t lover = kNoPlayer;
        uint8_t house = kNoPlayer;       // eigenes Haus

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
        bool dayAbilityUsed = false;
        bool shield = false;           // Dorfältester
        bool holyWaterUsed = false;    // Pfarrer
        bool infectUsed = false;       // Urwolf
        bool idiotRevealed = false;    // Dorfdepp wurde begnadigt
        bool foxActive = true;         // Fuchs hat seinen Spürsinn noch
        bool judgeUsed = false;        // Stotternder Richter
        bool sideChosen = false;       // Wolfshund
        bool enchanted = false;        // vom Flötenspieler verzaubert
        bool newlyEnchanted = false;
        int enchantsTonight = 0;       // Flötenspieler
        uint8_t model = kNoPlayer;     // Wildes Kind
        std::vector<Role> actorCards;  // Schauspieler
        std::vector<SeerResult> seerResults;
        uint8_t protecting = kNoPlayer;    // heute Nacht beschützt
        uint8_t lastProtected = kNoPlayer; // letzte Nacht beschützt
        std::vector<uint8_t> cupidPicks;
        float hunterTimer = 0.0f;
        float successorTimer = 0.0f;   // sterbender Hauptmann
        uint8_t vote = kNoPlayer;
        float burn = 0.0f;             // Sekunden in einem brennenden Haus

        // Fährtenleser
        uint8_t trackTarget = kNoPlayer;
        std::vector<int> trackHouses;
        std::vector<uint8_t> trackVictims;

        Vec3 deathPosition;
        float deathYaw = 0.0f;
        int deathDay = 0;
        bool diedAtNight = false;
        DeathCause deathCause = DeathCause::Wolves;
        bool buried = false;

        bool wolfForm() const { return transform >= 0.999f; }
        // Rolle, nach der der Spieler gerade handelt (Schauspieler nachts)
        Role effectiveRole() const { return acting != Role::Unknown ? acting : role; }
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
    uint8_t captain() const { return m_captain; }
    bool election() const { return m_election; }
    bool isProtected(uint8_t id) const;
    const std::vector<DoorState>& doors() const { return m_doors; }
    DoorMask closedDoors() const { return closedDoorMask(m_doors); }
    uint32_t dousedHouses() const { return m_doused; }
    bool hasLineOfSight(Vec3 a, Vec3 b) const;
    // Name für Meldungen: "das Haus von Anna", "die Kapelle" (im Dativ: "dem Haus von Anna")
    std::string houseLabel(int house, bool dative = false) const;
    // Dorfhälfte für den Sektierer: 0 = Osten, 1 = Westen
    int sectOf(const Player& p) const;

    uint32_t abilitiesFor(uint8_t id) const;
    // Prüft eine Interaktion ohne sie auszuführen; liefert den Grund, falls nicht möglich.
    std::string checkInteraction(uint8_t id, Interaction kind, uint8_t target) const;
    std::vector<EntityState> entitiesFor(uint8_t viewer) const;
    GameView viewFor(uint8_t viewer) const;

    // wantSecret: Rudel-Chat (Werwölfe), Geisterchat (Medium) oder Geschwister-Chat
    std::optional<ChatChannel> chatChannel(uint8_t speaker, bool wantSecret) const;
    bool canHear(uint8_t listener, uint8_t speaker, ChatChannel channel) const;
    bool canReadChannel(uint8_t listener, ChatChannel channel) const;
    // Das kleine Mädchen liest den Rudel-Chat ohne Namen
    bool hidesSpeaker(uint8_t listener, ChatChannel channel) const;

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
    void notifyArea(Vec2 at, float radius, NoticeKind kind, std::string text);
    void narrate(std::string text) { notify(Notice::Audience::All, kNoPlayer, NoticeKind::Narration, std::move(text)); }
    void tell(uint8_t player, std::string text) { notify(Notice::Audience::Player, player, NoticeKind::Private, std::move(text)); }
    void tellWolves(std::string text) { notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private, std::move(text)); }
    std::string nameOf(uint8_t id) const;
    bool mixedCouple() const;
    bool sameFamily(const Player& a, const Player& b) const;
    void notifyProtectors(uint8_t victim);
    std::string checkDoor(const Player& p, uint8_t house, float range) const;
    bool doorwayOccupied(int house) const;
    void setCaptain(uint8_t id, std::string text);

    void enterDay();
    void enterAssembly(const std::string& reason, float seconds);
    void enterVote();
    void resolveVote();
    void resolveElection(const std::vector<std::pair<uint8_t, int>>& ranking);
    void finishServant();
    void enterNight();
    void enterMorning();
    void advance();
    void updateFires(float dt);
    void updateTracking();
    void updateBear(float dt);
    // Angriff eines Werwolfs (Attack) oder Infektion durch den Urwolf
    void wolfStrike(Player& wolf, Player& victim, bool infect);
    void becomeWolf(Player& p, const std::string& reason);
    void kill(uint8_t id, DeathCause cause, bool announceNow, uint8_t killer = kNoPlayer);
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
    bool m_wolfRage = false;  // Wolfsjunges ist gestorben: nächste Nacht ein Opfer mehr
    bool m_wolfDied = false;  // für den großen bösen Wolf
    bool m_healAvailable = false;
    bool m_poisonAvailable = false;
    bool m_bellRung = false;
    bool m_voteGraceApplied = false;
    uint8_t m_jesterWinner = kNoPlayer;
    uint8_t m_angelWinner = kNoPlayer;
    bool m_firstExecution = false;    // die erste Hinrichtung hat stattgefunden
    uint8_t m_captain = kNoPlayer;
    bool m_election = false;          // laufende Hauptmannwahl
    bool m_electionHeld = false;
    uint8_t m_marked = kNoPlayer;     // vom Raben belastet
    uint8_t m_rustyWolf = kNoPlayer;  // stirbt in der nächsten Nacht an der Klinge des Ritters
    uint8_t m_servantPending = kNoPlayer; // Hingerichteter, dessen Rolle die Magd übernehmen kann
    bool m_servantTook = false;
    bool m_secondVote = false;        // Stotternder Richter
    float m_bearCooldown = 0.0f;
    float m_phaseElapsed = 0.0f;
    struct PendingDeath {
        uint8_t id;
        DeathCause cause;
    };
    std::vector<PendingDeath> m_nightDeaths;

    // Türen und Feuer (Index = Haus)
    std::vector<DoorState> m_doors;
    std::vector<float> m_doorNoise; // Abklingzeit für Lärm-Meldungen
    std::vector<float> m_fire;      // verbleibende Brenndauer
    uint32_t m_doused = 0;

    std::vector<Notice> m_notices;
    bool m_dirty = true;
};

} // namespace ww
