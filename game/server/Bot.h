#pragma once

#include "logic/Match.h"
#include "server/Dialogue.h"
#include "world/Movement.h"

#include "engine/core/Random.h"

#include <functional>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace ww {

// KI-Mitspieler für das Echtzeit-Spiel. Bewegt sich wie ein Mensch durchs Dorf und
// nutzt nur das, was seine persönliche Spielansicht verrät (plus sichtbare Positionen).
class Bot {
public:
    using SayFn = std::function<void(uint8_t botId, const Text& text, bool wolves)>;

    // Ein Hinweis aus erster Hand, den ein Bot gerade ausspricht. Andere Bots übernehmen nur
    // diesen, nie den Wortlaut – so schaukeln sich Gerüchte zwischen Bots nicht auf.
    struct Evidence {
        // Vouch: "Stimmt, ich bin die Hexe" bestätigt den Sprecher; Sighting: nachts beim Haus des Opfers gesehen
        enum class Kind { Seer, Witness, Trail, KnownRole, Vouch, Sighting, Opinion };
        Kind kind = Kind::Seer;
        uint8_t target = kNoPlayer;
        float weight = 0.0f; // > 0 belastet, < 0 entlastet
    };
    struct Speech {
        std::optional<Evidence> evidence;
        bool reply = false;          // auf Antworten antworten Bots nicht, sonst reden sie endlos weiter
        uint8_t topic = kNoPlayer;   // worum es geht: wer es gerade gehört hat, muss es nicht wiederholen
        bool alarm = false;          // "Hilfe, ein Werwolf!": wer es hört, flieht vor topic
    };

    // Wie ein Bot spielt und schreibt. Jeder Bot im Spiel bekommt eigene Züge; neutral ist der Standard.
    struct Personality {
        enum class Style { Formal, Casual, Terse };
        float talk = 1.0f;     // 0.5 still … 1.5 gesprächig
        float trust = 1.0f;    // wie stark Aussagen anderer zählen: 0.7 skeptisch … 1.2 gutgläubig
        float boldness = 1.0f; // 0.7 vorsichtig … 1.3 vorschnell (Verdachtsschwelle, früh abstimmen)
        float follow = 0.55f;  // Neigung, sich der Mehrheit anzuschließen
        float caution = 0.5f;  // nachts eher zu Hause, meidet Verdächtige
        Style style = Style::Formal;
        static Personality random(uint64_t seed);
    };
    // Schreibstil: förmlich unverändert, locker ("hab Anna geprüft"), knapp (höchstens zwei Sätze)
    static Text styled(const Text& line, Personality::Style style);
    // Nächte, in denen eine Seherin bisher prüfen konnte: tagsüber Tag − 1, nachts auch die laufende.
    // Vor der ersten Nacht kann niemand ein Prüfergebnis haben.
    static int nightsSoFar(const Match& match);

    Bot(uint8_t id, uint64_t seed, const Personality& personality = {});
    uint8_t id() const { return m_id; }
    const Personality& personality() const { return m_personality; }
    struct Feelings { float fear = 0; float anger = 0; float stress = 0; };
    const Feelings& feelings() const { return m_feelings; }
    float relationship(uint8_t other) const;
    void update(float dt, Match& match, const SayFn& say);
    // Der Bot hört eine Chatnachricht und passt seinen Verdacht an.
    void hear(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel = ChatChannel::Global,
              bool reply = true, const Speech* speech = nullptr);
    int replyPriority(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel,
                      const Speech* speech = nullptr) const;
    // Was der Bot gerade sagt; nur während seines say()-Aufrufs gesetzt.
    const Speech* speaking() const { return m_speaking ? &*m_speaking : nullptr; }

private:
    enum class Nav { Moving, Arrived, Blocked };

    void onPhaseChange(const Match& match, const GameView& view);
    void chooseWanderTarget(const Match& match);
    void act(float dt, Match& match, const GameView& view, const SayFn& say, Vec2& wish, float& speed);
    bool dayRole(Match& match, const GameView& view, const Match::Player& me, Vec2& wish);
    void nightRole(float dt, Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish,
                   float& speed);
    // Jagd der Werwölfe und des Serienmörders; true, solange gejagt wird
    bool hunt(Match& match, const GameView& view, const Match::Player& me, Vec2& wish, float& speed);
    void goHome(Match& match, const GameView& view, const Match::Player& me, Vec2& wish);
    // Flucht vor einer Bestie: ins nächste Haus, das man vor ihr erreicht, und verriegeln – sonst weg von ihr
    bool flee(Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish, float& speed);
    // Läuft zum Ziel und kümmert sich um Türen auf dem Weg
    Nav navigate(Match& match, const GameView& view, Vec2 target, float stopDist, Vec2& wish, bool breakDoors);
    uint8_t nearestPlayer(const Match& match, Vec3 from, bool (*filter)(const Match::Player&), float maxDist) const;
    uint8_t strongestSuspect(const Match& match, float threshold) const;
    uint8_t chooseVote(const Match& match, const GameView& view);
    uint8_t chooseCaptain(const Match& match, const GameView& view);
    float suspicion(const Match& match, uint8_t target) const;
    dialogue::Message understand(const Match& match, uint8_t speaker, const Text& text) const;
    Text answer(const Match& match, uint8_t speaker, const dialogue::Message& message, bool contradiction,
                std::optional<Evidence>* evidence) const;
    Text react(const Match& match, uint8_t speaker, const Evidence& heard, std::optional<Evidence>* evidence) const;
    Text explain(const Match& match, uint8_t target, std::optional<Evidence>* evidence = nullptr) const;
    bool knowsFirstHand(const Match& match, const Evidence& heard) const;
    // Gehört other zum eigenen Rudel oder Clan? (nur für Wölfe und Vampire wahr)
    bool ally(const GameView& view, Role other) const;
    // Kann der Bot eine Rollen- oder Seherangabe über target selbst prüfen? +1 wahr, -1 gelogen, 0 unbekannt
    int verdict(const Match& match, const dialogue::Message& message, uint8_t target) const;
    bool repeats(uint8_t speaker, const dialogue::Message& message) const;
    void speak(const SayFn& say, const Text& text, bool secret, Speech speech = {});
    void updateConversation(float dt, const Match& match, const SayFn& say);
    // Täuschung der Wölfe
    bool packSpeaker(const Match& match) const;        // dieser Wolf-Bot spricht fürs Rudel (niedrigste Nummer)
    bool packClaimedSeer(const Match& match) const;    // jemand aus dem Rudel gibt sich schon als Seherin aus
    bool wouldCounterclaim(const Match& match, const dialogue::Message& message, uint8_t speaker) const;
    uint8_t scapegoat(const Match& match, uint8_t avoid) const; // Unschuldiger, den das Rudel belasten will
    Text counterclaim(const Match& match, uint8_t speaker, std::optional<Evidence>* evidence) const;
    // Schlüsse aus dem Spielverlauf und Reaktionen auf Ereignisse
    void observe(float dt, const Match& match, const GameView& view);
    void review(const Match& match, const GameView& view);
    // Sagt line in der Phase phase ab Tag day (frühestens nach due), z. B. einen Schluss in der nächsten Versammlung
    void plan(Phase phase, int day, double due, Text line, std::optional<Evidence> evidence = {}, uint8_t about = kNoPlayer,
              uint8_t event = kNoPlayer);
    void speakPlanned(const Match& match, const SayFn& say);
    void lookAt(uint8_t player, double until);
    void faceSomeone(float dt, const Match& match);
    void updateFeelings(float dt, const Match& match, const GameView& view);
    void feelChat(const Match& match, uint8_t speaker, const dialogue::Message& message);
    void changeRelationship(uint8_t other, float amount);
    float socialBias(uint8_t other) const;
    Text emotionalStyle(Text text) const;
    Text roleHint(const Match& match) const;
    bool shouldReveal(const Match& match, uint8_t target = kNoPlayer) const;
    uint8_t sacrificeTarget(const Match& match) const;
    void roleplay(const Match& match, const SayFn& say);
    Vec2 steerTo(Vec2 target) const;
    Vec2 position() const { return {m_mover.position.x, m_mover.position.z}; }

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
    float m_phaseTime = 0.0f;
    uint8_t m_quarry = kNoPlayer;
    std::set<uint8_t> m_spared;           // heute Nacht nicht angreifbar (beschützt, wehrhaft)
    int m_douseHouse = -1;
    bool m_voted = false;
    bool m_shot = false;
    float m_shotDelay = 0.0f;
    bool m_staysOut = false;              // traut sich nachts aus dem Licht
    bool m_goHome = false;                // schließt sich nachts im eigenen Haus ein
    bool m_reported = false;              // Fährtenleser hat seine Spur verraten
    bool m_wantsSteal = false;            // Dieb will in dieser Nacht stehlen
    std::map<uint8_t, float> m_suspicion; // wen der Bot für einen Werwolf hält
    std::map<uint8_t, int> m_seenAsWolf;  // als Bestie beobachtet (Nacht-Nummer)
    std::map<uint8_t, int> m_wolfWitness; // tatsächlich erkannt, nicht nur im Sichtfeld
    std::map<uint8_t, int> m_vampireWitness; // nachts mit rot glühenden Augen erkannt
    dialogue::Memory m_dialogue;
    struct Topic { uint8_t player = kNoPlayer; double time = 0; };
    std::map<uint8_t, Topic> m_topics;
    struct Reply {
        uint8_t speaker = kNoPlayer;
        ChatChannel channel = ChatChannel::Global;
        Phase phase = Phase::Lobby;
        int day = 0;
        double due = 0;
        Text text;
        dialogue::Intent intent = dialogue::Intent::Unknown;
        uint8_t topic = kNoPlayer;
        Speech speech;
    };
    std::deque<Reply> m_replies;          // angesprochene Fragen warten, statt verloren zu gehen
    std::optional<Reply> m_lastReply;     // zuletzt gegebene Antwort (gegen Wiederholungen)
    std::optional<Speech> m_speaking;
    double m_replyAfter = 0;              // so lange überlässt der Bot allgemeines Geplauder anderen
    double m_nextReply = 0;               // frühester Zeitpunkt der nächsten Antwort
    double m_reconsiderAfter = 0;
    double m_lastDiscussion = -100;       // zuletzt fiel eine Beschuldigung oder ein Seherergebnis
    std::map<uint8_t, double> m_heardTopic; // wann ein anderer Bot zuletzt über jemanden gesprochen hat
    std::array<float, 6> m_nightPlaces{};
    int m_alibiNight = -1;

    Personality m_personality;
    uint64_t m_salt = 0;                  // für Entscheidungen in const-Methoden, die je Bot verschieden ausfallen
    bool m_claimedSeer = false;           // Wolf: gibt sich als Seherin aus
    int m_fakeReportDay = -1;
    // Verlauf: Stimmen je Tag, nachts gesehene Personen, schon ausgewertete Tote
    std::map<int, std::map<uint8_t, uint8_t>> m_votes;
    std::map<uint8_t, std::vector<Vec2>> m_sightings; // diese Nacht: wo jemand (in Menschengestalt) zu sehen war
    float m_sightTimer = 0.0f;
    std::set<uint8_t> m_aliveAtNight;
    std::set<uint8_t> m_reviewed;
    // Geplante eigene Sätze (Reaktionen, Schlüsse), die gesagt werden, sobald es passt
    struct Planned {
        Text text;
        Phase phase = Phase::Lobby;
        int day = 0;
        double due = 0;
        std::optional<Evidence> evidence;
        uint8_t about = kNoPlayer;
        uint8_t event = kNoPlayer;
    };
    std::deque<Planned> m_planned;
    double m_nextPlanned = 0;
    // Körpersprache
    double m_now = 0;                     // Spielzeit des letzten update()
    uint8_t m_lookAt = kNoPlayer;
    double m_lookUntil = 0;
    uint8_t m_companion = kNoPlayer;      // läuft tagsüber mit jemandem mit
    Vec2 m_companionOffset{0, 0};
    float m_companionTime = 0.0f;
    float m_hesitate = 0.0f;              // zögert nachts, bevor er losgeht
    Feelings m_feelings;
    std::map<uint8_t, float> m_relationships; // -1 Feindschaft ... +1 Freundschaft, kein Rollenwissen
    std::map<uint8_t, float> m_resentment;    // Erinnerung an ein öffentlich belegtes Fehlurteil
    struct SocialClaim { int day = -1; unsigned signs = 0; };
    std::map<std::pair<uint8_t, uint8_t>, SocialClaim> m_socialClaims;
    std::set<uint8_t> m_feltDeaths;
    std::set<uint8_t> m_feltVerdicts;
    bool m_socialInitialized = false;
    bool m_seerRevealed = false;
    std::set<uint8_t> m_sharedResults;
    uint8_t m_sacrificed = kNoPlayer;
    double m_roleSpeakAfter = 0;
    int m_roleHintDay = -1;
    uint8_t m_fleeFrom = kNoPlayer;       // vor wem der Bot gerade flieht
    double m_fleeUntil = 0;
    bool m_shouted = false;               // diese Nacht schon um Hilfe gerufen
};

// Verteilt eine Chatnachricht an alle Bots, die sie hören; höchstens einer antwortet.
void deliverChat(std::vector<Bot>& bots, const Match& match, uint8_t speaker, const Text& text, ChatChannel channel,
                 eng::Rng& rng);

} // namespace ww
