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
    Night,     // Werwölfe, Seherin, Beschützer, Amor handeln gleichzeitig
    Witch,     // Hexe sieht das Opfer und entscheidet über ihre Tränke
    Morning,   // Verkündung der nächtlichen Todesfälle
    Hunter,    // Der gestorbene Jäger schießt
    Day,       // Diskussion
    Vote,      // Abstimmung
    Execution, // Ergebnis der Abstimmung
    GameOver
};

enum class ChatChannel : uint8_t { Public = 0, Wolves, Dead };

// Was ein Spieler im Moment tun darf.
enum class ActionKind : uint8_t {
    None = 0,
    CupidPick,
    Protect,
    Inspect,
    WolfVote,
    WitchPotions,
    HunterShot,
    SkipDiscussion,
    DayVote
};

enum class NoticeKind : uint8_t { Info = 0, Narration, Death, Private, Warning, Victory };

enum class DeathCause : uint8_t { Wolves, Poison, Grief, Vote, Hunter, Left };

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
    bool revealRoles = true; // Rolle Verstorbener wird aufgedeckt
    uint16_t nightSeconds = 45;
    uint16_t discussionSeconds = 120;
    uint16_t voteSeconds = 45;

    void sanitize();
    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

int autoWolfCount(int players);
// Ermittelt die Rollenverteilung. Bei ungültiger Konfiguration leer + Fehlertext.
std::vector<Role> buildRoleList(const Settings& settings, int players, std::string* error = nullptr);

struct SeerResult {
    uint8_t player = kNoPlayer;
    Role role = Role::Unknown;
};

// Ein Spieler aus Sicht eines bestimmten Betrachters.
struct PlayerView {
    uint8_t id = kNoPlayer;
    std::string name;
    bool alive = true;
    bool connected = true;
    bool bot = false;
    Role role = Role::Unknown; // soweit dem Betrachter bekannt
    bool lover = false;        // als verliebt markiert (falls bekannt)
    uint8_t vote = kNoPlayer;  // sichtbare Stimme / Wolfswahl
    bool skip = false;         // möchte die Diskussion überspringen
    bool winner = false;
};

// Personalisierte Momentaufnahme des Spiels, die der Server jedem Client schickt.
struct GameView {
    Phase phase = Phase::Lobby;
    uint16_t day = 0;
    float timeLeft = 0.0f;
    float phaseDuration = 0.0f;
    uint8_t you = kNoPlayer;
    uint8_t host = kNoPlayer;
    Role yourRole = Role::Unknown;
    Settings settings;
    std::vector<PlayerView> players;

    ActionKind action = ActionKind::None;
    std::vector<uint8_t> selection;
    bool confirmed = false;
    uint8_t loverPartner = kNoPlayer;
    std::vector<SeerResult> seerResults;
    uint8_t lastProtected = kNoPlayer;
    uint8_t witchVictim = kNoPlayer;
    bool healAvailable = false;
    bool poisonAvailable = false;
    bool healUsed = false;
    uint8_t poisonTarget = kNoPlayer;
    Team winner = Team::None;

    const PlayerView* player(uint8_t id) const;
    const PlayerView* me() const { return player(you); }
    bool inGame() const { return phase != Phase::Lobby; }

    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

// Wer darf gerade wo sprechen? (gemeinsame Regel für Server und Client)
std::optional<ChatChannel> speakChannel(Phase phase, bool alive, Role role);
bool canReadChannel(ChatChannel channel, Phase phase, bool alive, Role role);

struct Notice {
    enum class Audience : uint8_t { All, Player, Wolves };
    Audience audience = Audience::All;
    uint8_t player = kNoPlayer;
    NoticeKind kind = NoticeKind::Info;
    std::string text;
};

} // namespace ww
