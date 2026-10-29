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
        enum class Kind { Seer, Witness, Trail, KnownRole };
        Kind kind = Kind::Seer;
        uint8_t target = kNoPlayer;
        float weight = 0.0f; // > 0 belastet, < 0 entlastet
    };
    struct Speech {
        std::optional<Evidence> evidence;
        bool reply = false; // auf Antworten antworten Bots nicht, sonst reden sie endlos weiter
    };

    Bot(uint8_t id, uint64_t seed);
    uint8_t id() const { return m_id; }
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
    Text react(const Match& match, const Evidence& heard, std::optional<Evidence>* evidence) const;
    Text explain(const Match& match, uint8_t target, std::optional<Evidence>* evidence = nullptr) const;
    bool knowsFirstHand(const Match& match, const Evidence& heard) const;
    bool repeats(uint8_t speaker, const dialogue::Message& message) const;
    void speak(const SayFn& say, const Text& text, bool secret, Speech speech = {});
    void updateConversation(float dt, const Match& match, const SayFn& say);
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
    std::array<float, 6> m_nightPlaces{};
    int m_alibiNight = -1;
};

// Verteilt eine Chatnachricht an alle Bots, die sie hören; höchstens einer antwortet.
void deliverChat(std::vector<Bot>& bots, const Match& match, uint8_t speaker, const Text& text, ChatChannel channel,
                 eng::Rng& rng);

} // namespace ww
