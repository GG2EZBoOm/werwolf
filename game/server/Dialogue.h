#pragma once

#include "logic/Types.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace ww::dialogue {

enum class Intent { Unknown, Statement, AskSuspicion, AskInnocence, AskReason, AskAlibi, AskRole, AskVote, RoleClaim, Alibi, Greeting,
                    AskBelief };
enum class Place { Unknown, Home, Chapel, Fire, Well, Forest };

struct Person { uint8_t id; std::string name; };
struct Claim {
    uint8_t target;
    float weight;      // positive = suspicion, negative = defence
    bool fact = false; // "Berta ist ein Werwolf" / "Clara ist gut", not "ich traue Berta nicht"
};
struct RoleReport { uint8_t target; Role role; }; // "Clara ist die Hexe"
struct Message {
    Intent intent = Intent::Unknown;
    std::vector<Claim> claims;
    std::vector<RoleReport> roleReports;
    std::vector<uint8_t> mentions;
    uint8_t addressed = kNoPlayer;
    uint8_t topic = kNoPlayer;
    Role claimedRole = Role::Unknown;
    Role askedRole = Role::Unknown; // "Wer ist der Jäger?", "Bist du die Hexe?"
    Place place = Place::Unknown;
    bool question = false;
    bool wolfWord = false;   // mentions werewolves at all ("Anton, bist du ein Wolf?")
    bool seerReport = false; // presents seer results ("hab Berta gecheckt, sie ist ein Werwolf")
    // The results are framed as a check ("gecheckt", or in the same message as "ich bin Seher"). Only these
    // count towards "one check per night": a claimed seer's later "Berta ist unschuldig" may be an opinion.
    bool explicitCheck = false;
    bool toAll = false;        // to the whole round: "möchte jemand …", "welche Rollen habt ihr?"
    bool protectOffer = false; // "ich passe auf dich auf", "Seherin, ich beschütze dich"
    bool seerWord = false;     // mentions the seer at all
    bool sighting = false;     // "ich habe Anna heute Nacht bei der Leiche gesehen"
    bool correction = false;
    bool exclusiveAlibi = false;
};

// Weight of a seer's result: as strong as a bot's own check.
constexpr float kSeerWolf = 2.4f;
constexpr float kSeerGood = -1.8f;

// Deliberately bounded DE/EN game dialogue, with no access to hidden roles.
Message parse(const std::string& text, const std::vector<Person>& people, uint8_t speaker,
              uint8_t listener = kNoPlayer, uint8_t previousTopic = kNoPlayer);
// For a speaker who claimed the seer earlier: "Berta ist böse" is a result, not a hunch.
bool presentSeerResults(Message& message);

class Memory {
public:
    // Replaces a speaker's previous claim instead of accumulating repetitions.
    bool remember(uint8_t speaker, const Message& message, int day);
    float suspicion(uint8_t target, int day) const;
    uint8_t source(uint8_t target, int day) const;
    float credibility(uint8_t speaker) const;
    // Checks seer results and role reports against roles revealed by deaths.
    void observeRevealed(const GameView& view);

    Role claimedRole(uint8_t speaker) const;
    std::vector<uint8_t> defenders(uint8_t target) const; // who spoke up for target
    bool otherSeer(uint8_t speaker) const;                // someone else claims the seer as well
    uint8_t otherClaimant(Role role, uint8_t speaker) const; // someone else who claimed this role
    bool liar(uint8_t speaker) const { return m_liars.contains(speaker); }
    int confirmed(uint8_t speaker) const;
    int seerChecks(uint8_t speaker) const; // how many people the speaker claims to have checked as the seer
    // First-hand verdict on a speaker's report, e.g. one about the listener itself.
    void judge(uint8_t speaker, bool truthful);
    void support(uint8_t speaker); // someone else vouched for the speaker's report

private:
    struct Evidence { float weight = 0; int day = 0; bool checked = false; bool report = false; };
    std::map<std::pair<uint8_t, uint8_t>, Evidence> m_claims;
    std::map<uint8_t, float> m_credibility;
    std::map<uint8_t, Role> m_roles;
    struct Alibi { Place place; int day; bool exclusive; };
    std::map<uint8_t, Alibi> m_alibis;
    struct Report { Role role = Role::Unknown; float weight = 0; bool checked = false; bool seer = false; bool counted = false; }; // role or wolf/good result
    std::map<std::pair<uint8_t, uint8_t>, Report> m_reports;
    std::set<uint8_t> m_liars;
    std::map<uint8_t, int> m_confirmed;
};

} // namespace ww::dialogue
