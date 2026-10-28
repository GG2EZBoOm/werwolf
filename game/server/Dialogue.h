#pragma once

#include "logic/Types.h"

#include <map>
#include <string>
#include <vector>

namespace ww::dialogue {

enum class Intent { Unknown, Statement, AskSuspicion, AskInnocence, AskReason, AskAlibi, AskRole, AskVote, RoleClaim, Alibi, Greeting };
enum class Place { Unknown, Home, Chapel, Fire, Well, Forest };

struct Person { uint8_t id; std::string name; };
struct Claim { uint8_t target; float weight; }; // positive = suspicion, negative = defence
struct Message {
    Intent intent = Intent::Unknown;
    std::vector<Claim> claims;
    std::vector<uint8_t> mentions;
    uint8_t addressed = kNoPlayer;
    uint8_t topic = kNoPlayer;
    Role claimedRole = Role::Unknown;
    Place place = Place::Unknown;
    bool question = false;
    bool correction = false;
    bool exclusiveAlibi = false;
};

// Deliberately bounded DE/EN game dialogue, with no access to hidden roles.
Message parse(const std::string& text, const std::vector<Person>& people, uint8_t speaker,
              uint8_t listener = kNoPlayer, uint8_t previousTopic = kNoPlayer);

class Memory {
public:
    // Replaces a speaker's previous claim instead of accumulating repetitions.
    bool remember(uint8_t speaker, const Message& message, int day);
    float suspicion(uint8_t target, int day) const;
    uint8_t source(uint8_t target, int day) const;
    float credibility(uint8_t speaker) const;
    void observeRevealed(const GameView& view);

private:
    struct Evidence { float weight = 0; int day = 0; bool checked = false; };
    std::map<std::pair<uint8_t, uint8_t>, Evidence> m_claims;
    std::map<uint8_t, float> m_credibility;
    std::map<uint8_t, Role> m_roles;
    struct Alibi { Place place; int day; bool exclusive; };
    std::map<uint8_t, Alibi> m_alibis;
};

} // namespace ww::dialogue
