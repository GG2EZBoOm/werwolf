#pragma once

#include "engine/core/ByteBuffer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ww {

constexpr uint8_t kNoPlayer = 255;
constexpr int kMinPlayers = 4;
constexpr int kMaxPlayers = 16;

enum class Role : uint8_t { Unknown = 0, Villager, Werewolf, Seer, Witch, Hunter, Cupid, Protector, Count };

enum class Team : uint8_t { None = 0, Village, Wolves, Lovers, Nobody };

enum class Phase : uint8_t {
    Lobby = 0,
    Day,       // freies Erkunden, Nähe-Chat
    Assembly,  // Versammlung am Feuer: Diskussion
    Vote,      // Abstimmung
    Execution, // Urteil
    Night,     // Jagd der Werwölfe, nächtliche Fähigkeiten
    Morning,   // Verkündung der Toten
    GameOver
};

enum class ChatChannel : uint8_t { Global = 0, Nearby, Wolves, Dead };

enum class NoticeKind : uint8_t { Info = 0, Narration, Death, Private, Warning, Victory };

enum class DeathCause : uint8_t { Wolves, Poison, Grief, Vote, Hunter, Left };

// Was ein Spieler tun möchte (E, Linksklick, R ...)
enum class Interaction : uint8_t {
    Attack = 1, // Werwolf reißt ein Opfer
    Inspect,    // Seherin sieht die Rolle
    Protect,    // Beschützer
    Heal,       // Hexe belebt eine Leiche wieder (Ziel = Leiche)
    Poison,     // Hexe vergiftet
    CupidPick,  // Amor wählt einen Verliebten
    HunterShot, // letzter Schuss des Jägers
    Vote,       // Stimme in der Abstimmung (Ziel 255 = zurückziehen)
    RingBell,   // Versammlung einberufen
    Transform,  // Werwolf verwandelt sich (Ziel 1) / zurück (Ziel 0)
};

enum AbilityBits : uint16_t {
    AbilityTransform = 1 << 0,
    AbilityAttack = 1 << 1,
    AbilityInspect = 1 << 2,
    AbilityProtect = 1 << 3,
    AbilityHeal = 1 << 4,
    AbilityPoison = 1 << 5,
    AbilityCupid = 1 << 6,
    AbilityHunterShot = 1 << 7,
    AbilityVote = 1 << 8,
    AbilityRingBell = 1 << 9,
};

// Reichweiten in Metern
constexpr float kAttackRange = 2.4f;
constexpr float kInspectRange = 14.0f;
constexpr float kProtectRange = 4.5f;
constexpr float kHealRange = 2.8f;
constexpr float kPoisonRange = 2.8f;
constexpr float kCupidRange = 6.0f;
constexpr float kHunterRange = 35.0f;
constexpr float kBellRange = 3.4f;
constexpr float kNearbyChatRange = 24.0f;
constexpr float kTransformSeconds = 1.6f;
constexpr float kHunterShotSeconds = 15.0f;

const char* roleName(Role role);
const char* roleDescription(Role role);
const char* roleIcon(Role role);
Team roleTeam(Role role);
const char* teamName(Team team);
const char* phaseTitle(Phase phase);
const char* channelName(ChatChannel channel);

struct Settings {
    uint8_t wolves = 0; // 0 = automatisch nach Spielerzahl
    bool seer = true;
    bool witch = true;
    bool hunter = true;
    bool cupid = false;
    bool protector = false;
    bool revealRoles = true;
    uint16_t daySeconds = 150;
    uint16_t discussionSeconds = 90;
    uint16_t voteSeconds = 40;
    uint16_t nightSeconds = 100;

    void sanitize();
    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

int autoWolfCount(int players);
std::vector<Role> buildRoleList(const Settings& settings, int players, std::string* error = nullptr);

struct SeerResult {
    uint8_t player = kNoPlayer;
    Role role = Role::Unknown;
};

struct PlayerView {
    uint8_t id = kNoPlayer;
    std::string name;
    bool alive = true;
    bool connected = true;
    bool bot = false;
    Role role = Role::Unknown; // soweit dem Betrachter bekannt
    bool lover = false;
    uint8_t vote = kNoPlayer;  // öffentliche Stimme in der Abstimmung
    bool winner = false;
};

// Personalisierte Momentaufnahme (Regeln, Wissen). Positionen kommen getrennt per Snapshot.
struct GameView {
    Phase phase = Phase::Lobby;
    uint16_t day = 0;
    float timeLeft = 0.0f;
    float phaseDuration = 0.0f;
    uint8_t you = kNoPlayer;
    uint8_t host = kNoPlayer;
    uint32_t worldSeed = 0;
    Role yourRole = Role::Unknown;
    Settings settings;
    std::vector<PlayerView> players;

    uint16_t abilities = 0;
    uint8_t vote = kNoPlayer;
    uint8_t loverPartner = kNoPlayer;
    std::vector<SeerResult> seerResults;
    std::vector<uint8_t> cupidPicks;
    uint8_t protectedTarget = kNoPlayer; // heute Nacht beschützt
    uint8_t lastProtected = kNoPlayer;
    bool healAvailable = false;
    bool poisonAvailable = false;
    uint8_t packKillsLeft = 0;
    float hunterTimeLeft = 0.0f;
    Team winner = Team::None;

    const PlayerView* player(uint8_t id) const;
    const PlayerView* me() const { return player(you); }
    bool inGame() const { return phase != Phase::Lobby; }
    bool can(AbilityBits a) const { return (abilities & a) != 0; }

    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

// --- Positionen (hochfrequent) -------------------------------------------------

enum EntityKind : uint8_t { EntityLiving = 0, EntityCorpse = 1, EntityGhost = 2 };
enum EntityFlags : uint8_t {
    EntityWolf = 1,      // in Werwolf-Gestalt
    EntitySprint = 2,
    EntityAttack = 4,    // holt gerade zum Hieb aus
    EntityLantern = 8,   // trägt eine Laterne
};

struct EntityState {
    uint8_t id = kNoPlayer;
    uint8_t kind = EntityLiving;
    float x = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    uint8_t flags = 0;
    float transform = 0.0f; // 0 = Mensch, 1 = Werwolf
};

void writeEntities(eng::ByteWriter& w, const std::vector<EntityState>& entities);
bool readEntities(eng::ByteReader& r, std::vector<EntityState>& entities);

struct Notice {
    enum class Audience : uint8_t { All, Player, Wolves, Dead };
    Audience audience = Audience::All;
    uint8_t player = kNoPlayer;
    NoticeKind kind = NoticeKind::Info;
    std::string text;
};

// Phasen, in denen alle sich gegenseitig hören (Versammlung)
bool isGatheringPhase(Phase phase);
// Tageszeit (0 = Mitternacht, 0.5 = Mittag) für Licht und Himmel, abgeleitet aus der Phase.
float timeOfDay(Phase phase, float timeLeft, float phaseDuration);

} // namespace ww
