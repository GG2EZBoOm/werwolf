#pragma once

#include "i18n/Text.h"

#include "engine/core/ByteBuffer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ww {

constexpr uint8_t kNoPlayer = 255;
constexpr int kMinPlayers = 4;
constexpr int kMaxPlayers = 32;

enum class Role : uint8_t {
    Unknown = 0,
    Villager,
    Werewolf,
    // Dorf (Reihenfolge = Vorrang, wenn mehr Sonderrollen gewählt sind als Plätze frei)
    Seer,
    Witch,
    Hunter,
    Cupid,
    LittleGirl,     // Kleines Mädchen: belauscht nachts den Rudel-Chat
    Thief,          // Dieb: stiehlt in der ersten Nacht eine Rolle
    Protector,
    Elder,          // Dorfältester: übersteht den ersten Wolfsangriff
    Scapegoat,      // Sündenbock: stirbt bei Gleichstand
    Idiot,          // Dorfdepp: überlebt die erste Hinrichtung
    Fox,            // Fuchs: wittert Werwölfe in einer Gruppe
    BearTamer,      // Bärenführer: sein Bär brummt neben Werwölfen
    Knight,         // Ritter mit der rostigen Klinge: vergiftet seinen Mörder
    Servant,        // Ergebene Magd: übernimmt die Rolle eines Hingerichteten
    Judge,          // Stotternder Richter: verlangt einmal eine zweite Abstimmung
    Actor,          // Schauspieler: spielt jede Nacht eine andere Rolle
    WildChild,      // Wildes Kind: wird zum Werwolf, wenn sein Vorbild stirbt
    WolfHound,      // Wolfshund: entscheidet sich für Dorf oder Rudel
    Sister,         // Zwei Schwestern: kennen sich
    Brother,        // Drei Brüder: kennen sich
    HonestVillager, // Dorfbewohner-Dorfbewohner: allen als ehrlich bekannt
    Raven,          // Rabe: belastet nachts einen Spieler mit zwei Stimmen
    Medium,         // spricht nachts mit den Toten
    Tracker,        // Fährtenleser: verfolgt nachts einen Spieler
    Blacksmith,     // Schmied: verstärkt und repariert Türen
    Priest,         // Pfarrer: Weihwasser vernichtet einen Werwolf
    Mayor,          // Bürgermeister: öffentlich bekannt, doppelte Stimme
    Cursed,         // Verfluchter: wird zum Werwolf, wenn er angegriffen wird
    // Werwölfe
    AlphaWolf,      // Urwolf: kann einmal ein Opfer infizieren statt töten
    WolfCub,        // Wolfsjunges: sein Tod macht das Rudel rasend
    BigBadWolf,     // Großer böser Wolf: zusätzliches Opfer, solange kein Wolf starb
    WhiteWolf,      // Weißer Werwolf: im Rudel, gewinnt aber allein
    // Neutral
    Piper,          // Flötenspieler: verzaubert alle
    Angel,          // Engel: gewinnt, wenn er in der ersten Runde stirbt
    Sectarian,      // Sektierer: seine Dorfhälfte soll übrig bleiben
    Jester,         // Narr: gewinnt, wenn er hingerichtet wird
    SerialKiller,   // Serienmörder: tötet nachts mit dem Messer, gewinnt allein
    Arsonist,       // Brandstifter: tränkt Häuser mit Öl und zündet sie an
    // Vampire
    Vampire,        // Vampir: eigener Clan, beißt nachts lautlos, kommt durch keine geschlossene Tür
    Count
};

enum class Faction : uint8_t { Village, Wolves, Neutral };

enum class Team : uint8_t { None = 0, Village, Wolves, Lovers, Nobody, Jester, Killer, Arsonist, Piper, Angel, WhiteWolf, Sectarian,
                            Vampires };

enum class Phase : uint8_t {
    Lobby = 0,
    Day,       // freies Erkunden, Nähe-Chat
    Assembly,  // Versammlung am Feuer: Diskussion
    Vote,      // Abstimmung
    Execution, // Urteil
    Night,     // Jagd, nächtliche Fähigkeiten
    Morning,   // Verkündung der Toten
    GameOver
};

enum class ChatChannel : uint8_t { Global = 0, Nearby, Wolves, Dead, Family, Vampires };

enum class NoticeKind : uint8_t { Info = 0, Narration, Death, Private, Warning, Victory };

enum class DeathCause : uint8_t { Wolves, Poison, Grief, Vote, Hunter, Left, Fire, Knife, HolyWater, Rust, Scapegoat, Vampire };

// Was ein Spieler tun möchte (E, F, Q, Linksklick, R ...)
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
    DoorToggle, // Tür öffnen/schließen (Ziel = Haus); von innen geschlossen = verriegelt
    DoorBreak,  // Tür einschlagen (Ziel = Haus)
    Reinforce,  // Schmied verstärkt/repariert eine Tür (Ziel = Haus)
    Douse,      // Brandstifter tränkt ein Haus mit Öl (Ziel = Haus)
    Ignite,     // Brandstifter zündet alle getränkten Häuser an
    Track,      // Fährtenleser verfolgt einen Spieler
    HolyWater,  // Pfarrer
    Infect,     // Urwolf
    Stab,       // Serienmörder
    Steal,      // Dieb
    Enchant,    // Flötenspieler
    Sniff,      // Fuchs
    TakeRole,   // Ergebene Magd (Ziel = Hingerichteter)
    SecondVote, // Stotternder Richter
    ChooseModel,// Wildes Kind
    ChooseSide, // Wolfshund (Ziel 0 = Dorf, 1 = Rudel)
    Mark,       // Rabe
    NameSuccessor, // sterbender Hauptmann
    Bite,       // Vampir saugt ein Opfer aus
};

enum AbilityBits : uint32_t {
    AbilityTransform = 1u << 0,
    AbilityAttack = 1u << 1,
    AbilityInspect = 1u << 2,
    AbilityProtect = 1u << 3,
    AbilityHeal = 1u << 4,
    AbilityPoison = 1u << 5,
    AbilityCupid = 1u << 6,
    AbilityHunterShot = 1u << 7,
    AbilityVote = 1u << 8,
    AbilityRingBell = 1u << 9,
    AbilityBreakDoor = 1u << 10,
    AbilityReinforce = 1u << 11,
    AbilityDouse = 1u << 12,
    AbilityIgnite = 1u << 13,
    AbilityTrack = 1u << 14,
    AbilityHolyWater = 1u << 15,
    AbilityInfect = 1u << 16,
    AbilityStab = 1u << 17,
    AbilitySteal = 1u << 18,
    AbilityEnchant = 1u << 19,
    AbilitySniff = 1u << 20,
    AbilityTakeRole = 1u << 21,
    AbilitySecondVote = 1u << 22,
    AbilityChooseModel = 1u << 23,
    AbilityChooseSide = 1u << 24,
    AbilityMark = 1u << 25,
    AbilityNameSuccessor = 1u << 26,
    AbilityBetray = 1u << 27, // Weißer Werwolf darf einen Werwolf reißen
    AbilityBite = 1u << 28,   // Vampir beißt (einmal je Nacht für den ganzen Clan)
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
constexpr float kDoorRange = 2.4f;
constexpr float kTrackRange = 14.0f;
constexpr float kHolyWaterRange = 3.0f;
constexpr float kStealRange = 3.0f;
constexpr float kEnchantRange = 4.0f;
constexpr float kSniffRange = 10.0f;
constexpr float kModelRange = 6.0f;
constexpr float kMarkRange = 14.0f;
constexpr float kNearbyChatRange = 24.0f;
constexpr float kTransformSeconds = 1.6f;
constexpr float kHunterShotSeconds = 15.0f;
constexpr float kSuccessorSeconds = 15.0f;

// Namen und Beschreibungen in einer Sprache; ohne Sprache in der Sprache der Oberfläche
const char* roleName(Role role, Lang lang);
inline const char* roleName(Role role) { return roleName(role, language()); }
Text roleText(Role role);
// "die Hexe", "ein Dorfbewohner" – für Sätze wie "Anna ist die Hexe."
Text roleWithArticle(Role role);
const char* roleDescription(Role role, Lang lang);
inline const char* roleDescription(Role role) { return roleDescription(role, language()); }
Text roleDescriptionText(Role role);
const char* roleIcon(Role role);
Faction roleFaction(Role role);
Team roleTeam(Role role);
bool isWolfRole(Role role);
bool isVampireRole(Role role);
// Werwolf oder Vampir: eine Gefahr fürs Dorf (die Seherin nennt beide "böse")
bool isEvilRole(Role role);
// Rollen, die in der Lobby einzeln zugeschaltet werden können
bool isOptionalRole(Role role);
// Rollen, die allen Mitspielern bekannt sind
bool isPublicRole(Role role);
// Zwei Schwestern bzw. drei Brüder: Anzahl Karten der Rolle
int roleCardCount(Role role);
const char* teamName(Team team, Lang lang);
inline const char* teamName(Team team) { return teamName(team, language()); }
inline Text teamText(Team team) { return {teamName(team, Lang::German), teamName(team, Lang::English)}; }
const char* phaseTitle(Phase phase, Lang lang);
inline const char* phaseTitle(Phase phase) { return phaseTitle(phase, language()); }
const char* channelName(ChatChannel channel, Lang lang);
inline const char* channelName(ChatChannel channel) { return channelName(channel, language()); }

struct Settings {
    uint8_t wolves = 0;       // 0 = automatisch nach Spielerzahl
    uint64_t roles = 0;       // Bitmaske der zugeschalteten Sonderrollen (Bit = Role)
    bool revealRoles = true;
    bool captain = true;      // Hauptmannwahl am ersten Abend
    uint16_t daySeconds = 150;
    uint16_t discussionSeconds = 90;
    uint16_t voteSeconds = 40;
    uint16_t nightSeconds = 100;

    Settings();
    bool has(Role role) const { return (roles >> static_cast<uint64_t>(role)) & 1u; }
    void set(Role role, bool enabled);
    void sanitize();
    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

int autoWolfCount(int players);
int autoVampireCount(int players); // wenn Vampire zugeschaltet sind
std::vector<Role> buildRoleList(const Settings& settings, int players, Text* error = nullptr);

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
    uint8_t house = kNoPlayer; // eigenes Haus
    bool captain = false;      // Hauptmann (öffentlich)
    bool enchanted = false;    // vom Flötenspieler verzaubert (sehen Flötenspieler und Verzauberte)
    bool marked = false;       // vom Raben belastet (öffentlich)
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

    uint32_t abilities = 0;
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
    uint8_t tracked = kNoPlayer;     // Fährtenleser: verfolgte Person
    uint32_t doused = 0;             // Brandstifter: getränkte Häuser
    bool shieldLeft = false;         // Dorfältester: Schutz noch vorhanden
    bool election = false;           // die Abstimmung ist die Hauptmannwahl
    Role acting = Role::Unknown;     // Schauspieler: Rolle dieser Nacht
    uint8_t model = kNoPlayer;       // Wildes Kind: Vorbild
    uint8_t condemned = kNoPlayer;   // Ergebene Magd: Rolle dieses Hingerichteten übernehmen
    float successorTimeLeft = 0.0f;  // sterbender Hauptmann
    Team winner = Team::None;

    const PlayerView* player(uint8_t id) const;
    const PlayerView* me() const { return player(you); }
    bool inGame() const { return phase != Phase::Lobby; }
    bool can(AbilityBits a) const { return (abilities & a) != 0; }

    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r);
};

// --- Positionen und Türen (hochfrequent) -------------------------------------------

enum EntityKind : uint8_t { EntityLiving = 0, EntityCorpse = 1, EntityGhost = 2 };
enum EntityFlags : uint8_t {
    EntityWolf = 1,      // in Werwolf-Gestalt
    EntitySprint = 2,
    EntityAttack = 4,    // holt gerade zum Hieb aus
    EntityLantern = 8,   // trägt eine Laterne
    EntityRedEyes = 16,  // Vampir: nachts glühen die Augen schwach rot
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

enum DoorFlags : uint8_t {
    DoorOpen = 1,
    DoorLocked = 2,
    DoorBroken = 4,     // eingeschlagen (steht offen)
    DoorReinforced = 8,
    DoorBurning = 16,
    DoorBurnt = 32,     // Haus ist abgebrannt
};

struct DoorState {
    uint8_t flags = 0;
    uint8_t health = 100; // Prozent der Haltbarkeit
};

void writeEntities(eng::ByteWriter& w, const std::vector<EntityState>& entities);
bool readEntities(eng::ByteReader& r, std::vector<EntityState>& entities);
void writeDoors(eng::ByteWriter& w, const std::vector<DoorState>& doors);
bool readDoors(eng::ByteReader& r, std::vector<DoorState>& doors);
// Bitmaske der Türen, die den Weg versperren
uint32_t closedDoorMask(const std::vector<DoorState>& doors);

struct Notice {
    enum class Audience : uint8_t { All, Player, Wolves, Dead, Area };
    Audience audience = Audience::All;
    uint8_t player = kNoPlayer;
    NoticeKind kind = NoticeKind::Info;
    Text text;
    float x = 0.0f, z = 0.0f, radius = 0.0f; // für Area
};

// Phasen, in denen alle sich gegenseitig hören (Versammlung)
bool isGatheringPhase(Phase phase);
// Tageszeit (0 = Mitternacht, 0.5 = Mittag) für Licht und Himmel, abgeleitet aus der Phase.
float timeOfDay(Phase phase, float timeLeft, float phaseDuration);

} // namespace ww
