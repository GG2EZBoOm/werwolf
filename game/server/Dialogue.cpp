#include "server/Dialogue.h"

#include "engine/core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ww::dialogue {
namespace {

struct Token { std::string word; uint8_t person = kNoPlayer; };
using Tokens = std::vector<Token>;

Tokens tokenize(std::string_view input) {
    Tokens out;
    std::string word;
    auto flush = [&] { if (!word.empty()) { out.push_back({word}); word.clear(); } };
    bool quoted = false;
    for (size_t i = 0; i < input.size();) {
        char32_t cp = eng::utf8::next(input, i);
        if (cp == '"' || cp == 0x201e || cp == 0x201c || cp == 0x201d || cp == '`') {
            flush(); quoted = !quoted; continue;
        }
        if (quoted) continue; // quoted accusations are not the author's own verdict
        if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
        if (cp == U'Ä' || cp == U'ä') { word += "ae"; continue; }
        if (cp == U'Ö' || cp == U'ö') { word += "oe"; continue; }
        if (cp == U'Ü' || cp == U'ü') { word += "ue"; continue; }
        if (cp == U'ß' || cp == U'ẞ') { word += "ss"; continue; }
        if (cp == '\'' || cp == 0x2019) continue; // don't -> dont
        if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == '_' || cp >= 0x80) {
            eng::utf8::append(word, cp);
        } else {
            flush();
            if (cp == '.' || cp == '?' || cp == '!' || cp == ',' || cp == ':' || cp == ';')
                out.push_back({std::string(1, static_cast<char>(cp))});
        }
    }
    flush();
    return out;
}

bool has(const Tokens& tokens, std::initializer_list<std::string_view> words) {
    return std::any_of(tokens.begin(), tokens.end(), [&](const Token& t) {
        return t.person == kNoPlayer && std::find(words.begin(), words.end(), t.word) != words.end();
    });
}

bool phrase(const Tokens& tokens, std::string_view value) {
    std::string sentence = " ";
    for (const auto& t : tokens) sentence += (t.person == kNoPlayer ? t.word : "@") + " ";
    return sentence.find(" " + std::string(value) + " ") != std::string::npos;
}

void identify(Tokens& tokens, const std::vector<Person>& people) {
    // Longest name wins (Ann / Anna / Anna Maria); duplicate names stay ambiguous.
    struct Name { uint8_t id; Tokens tokens; };
    std::vector<Name> names;
    for (const auto& p : people) {
        auto name = tokenize(p.name);
        if (!name.empty()) names.push_back({p.id, std::move(name)});
    }
    std::stable_sort(names.begin(), names.end(), [](const Name& a, const Name& b) { return a.tokens.size() > b.tokens.size(); });
    for (size_t i = 0; i < tokens.size(); ++i) {
        size_t length = 0;
        uint8_t id = kNoPlayer;
        for (const auto& n : names) {
            if (i + n.tokens.size() > tokens.size() || (length && n.tokens.size() < length)) continue;
            bool match = true;
            for (size_t j = 0; j < n.tokens.size(); ++j)
                if (tokens[i + j].word != n.tokens[j].word) match = false;
            if (!match) continue;
            if (length) id = kNoPlayer;
            else { length = n.tokens.size(); id = n.id; }
        }
        if (length) {
            tokens[i] = {"@", id};
            tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(i + 1), tokens.begin() + static_cast<ptrdiff_t>(i + length));
        }
    }
}

Role roleIn(const Tokens& t) {
    if (has(t, {"seherin", "seher", "seer"})) return Role::Seer;
    if (has(t, {"hexe", "witch"})) return Role::Witch;
    if (has(t, {"beschuetzer", "protector"})) return Role::Protector;
    if (has(t, {"faehrtenleser", "tracker"})) return Role::Tracker;
    if (has(t, {"jaeger", "hunter"})) return Role::Hunter;
    if (has(t, {"dorfbewohner", "villager"})) return Role::Villager;
    return Role::Unknown;
}

} // namespace

Message parse(const std::string& text, const std::vector<Person>& people, uint8_t speaker, uint8_t listener,
              uint8_t previousTopic) {
    Message result;
    Tokens t = tokenize(text);
    identify(t, people);
    for (const auto& token : t)
        if (token.person != kNoPlayer && std::find(result.mentions.begin(), result.mentions.end(), token.person) == result.mentions.end())
            result.mentions.push_back(token.person);
    result.question = has(t, {"?"}) || has(t, {"wer", "wen", "warum", "wieso", "weshalb", "wo", "welche", "welchen", "who", "why", "where"}) ||
        (!t.empty() && has(Tokens{t.front()}, {"ist", "sind", "bist", "hast", "habt", "is", "are", "did", "do"}));
    // Vocatives: "Anton, ...", "Anton bist du ...", "Hallo Anton".
    if (t.size() >= 2 && t[0].person != kNoPlayer &&
        has(Tokens{t[1]}, {",", ":", "bist", "hast", "wer", "wen", "warum", "wo", "was", "are", "do", "why", "who", "where", "what"})) {
        result.addressed = t[0].person;
        t.erase(t.begin());
        if (!t.empty() && has(Tokens{t[0]}, {",", ":"})) t.erase(t.begin());
    } else if (t.size() >= 2 && has(Tokens{t[0]}, {"hallo", "hi", "hey", "hello"}) && t[1].person != kNoPlayer) {
        result.addressed = t[1].person;
    }
    if (result.addressed != kNoPlayer) listener = result.addressed;
    if (!t.empty() && has(Tokens{t.front()}, {"ist", "sind", "bist", "hast", "habt", "warst", "was", "what", "is", "are", "did", "do", "were", "beweise", "evidence"}))
        result.question = true;
    result.correction = phrase(t, "lag falsch") || phrase(t, "nehme zurueck") || phrase(t, "was wrong") ||
        phrase(t, "take it back") || has(t, {"korrigiere", "korrektur", "correction"});
    result.exclusiveAlibi = phrase(t, "ganze nacht") || phrase(t, "all night") || phrase(t, "entire night") || has(t, {"durchgehend", "ausschliesslich"});

    const bool self = has(t, {"ich", "i", "im"});
    const bool negated = has(t, {"nicht", "kein", "keine", "keinen", "not", "no", "dont", "isnt", "never", "nie"});
    if (result.question) {
        if (has(t, {"warum", "wieso", "weshalb", "why", "beweis", "beweise", "evidence"})) result.intent = Intent::AskReason;
        else if (has(t, {"wo", "where"})) result.intent = Intent::AskAlibi;
        else if (has(t, {"unschuldig", "innocent", "safe", "vertrauen", "trust"})) result.intent = Intent::AskInnocence;
        else if (has(t, {"rolle", "role", "bist"}) || phrase(t, "are you")) result.intent = Intent::AskRole;
        else if (has(t, {"waehlen", "waehlst", "waehlt", "waehlen", "abstimmen", "vote", "voting"})) result.intent = Intent::AskVote;
        else result.intent = Intent::AskSuspicion;
    } else if (self && !negated && roleIn(t) != Role::Unknown && (has(t, {"bin", "im", "am"}))) {
        result.intent = Intent::RoleClaim;
        result.claimedRole = roleIn(t);
    } else if (self && has(t, {"war", "was"}) && !negated) {
        if (has(t, {"zuhause", "haus", "hause", "home", "house"})) result.place = Place::Home;
        else if (has(t, {"kapelle", "kirche", "chapel", "church"})) result.place = Place::Chapel;
        else if (has(t, {"feuer", "lagerfeuer", "fire", "campfire"})) result.place = Place::Fire;
        else if (has(t, {"brunnen", "well"})) result.place = Place::Well;
        else if (has(t, {"wald", "forest", "woods"})) result.place = Place::Forest;
        if (result.place != Place::Unknown) result.intent = Intent::Alibi;
    } else if (has(t, {"hallo", "hi", "hey", "hello", "moin"})) result.intent = Intent::Greeting;

    // Separate clauses so "Anna ist unschuldig, aber Berta ist ein Wolf" is not flattened.
    std::vector<Tokens> clauses(1);
    for (size_t i = 0; i < t.size(); ++i) {
        const auto& token = t[i];
        bool split = has(Tokens{token}, {".", "!", "?", ";", ",", "aber", "but"});
        if (has(Tokens{token}, {"und", "and"}) && i + 1 < t.size() && t[i + 1].person != kNoPlayer &&
            has(clauses.back(), {"ist", "sind", "is", "are", "verdaechtig", "suspicious", "wolf", "werwolf", "unschuldig", "innocent"})) split = true;
        if (split) { if (!clauses.back().empty()) clauses.emplace_back(); }
        else clauses.back().push_back(token);
    }
    bool negatedBelief = false;
    for (const auto& clause : clauses) {
        if (clause.empty()) continue;
        std::vector<uint8_t> targets;
        for (const auto& token : clause)
            if (token.person != kNoPlayer) targets.push_back(token.person);
        const bool you = has(clause, {"du", "dich", "dir", "you"});
        const bool me = has(clause, {"ich", "i", "im"});
        const bool asksWho = result.question && has(t, {"wer", "wen", "who"});
        if (targets.empty() && you && !asksWho && listener != kNoPlayer) targets.push_back(listener);
        if (targets.empty() && me && has(clause, {"bin", "am", "im"})) targets.push_back(speaker);
        if (targets.empty() && has(clause, {"er", "sie", "ihn", "he", "she", "him", "her"}) && previousTopic != kNoPlayer)
            targets.push_back(previousTopic);
        if (!targets.empty()) result.topic = targets.back();
        if (result.question) continue; // asking is never evidence
        if (targets.empty() && (phrase(clause, "glaube nicht") || phrase(clause, "denke nicht") || phrase(clause, "dont think"))) {
            negatedBelief = true;
            continue;
        }
        if (has(clause, {"wenn", "falls", "if", "ob", "whether"})) continue;
        const bool wolfWord = has(clause, {"wolf", "werwolf", "werewolf", "woelfe", "werwoelfe", "wolves", "werewolves"});
        const bool wolf = wolfWord && (has(clause, {"ist", "sind", "bin", "bist", "is", "isnt", "are", "arent", "am", "im", "als", "as", "verwandelt", "transformed"}) || clause.size() <= 3) &&
            !has(clause, {"beim", "mit", "with", "near"});
        const bool suspect = has(clause, {"verdaechtig", "verdaechtige", "suspicious", "misstraue", "suspect", "sus", "luegt", "lying"});
        const bool defend = has(clause, {"unschuldig", "innocent", "vertraue", "traue", "trust", "safe"});
        const bool negative = has(clause, {"nicht", "kein", "keine", "keinen", "not", "no", "dont", "isnt", "arent", "never", "nie"}) ||
            (negatedBelief && has(clause, {"dass", "that"}));
        negatedBelief = false;
        const bool vote = has(clause, {"waehle", "vote", "voting"}) || phrase(clause, "stimme geht") || phrase(clause, "stimme fuer");
        float weight = wolf ? 1.8f : suspect ? 1.1f : defend ? -1.0f : vote ? 0.8f : 0.0f;
        if (negative) weight = -weight;
        if (has(clause, {"vielleicht", "maybe", "eventuell", "glaube", "think", "vermute"})) weight *= 0.55f;
        if (has(clause, {"sagt", "sagte", "says", "said", "laut", "angeblich", "supposedly"}) ||
            phrase(clause, "hat gesagt")) continue; // ambiguous reported speech: ask, do not pin it on every named person
        if (wolf && weight > 0 && has(clause, {"gesehen", "saw", "verwandelt", "transformed", "geprueft", "checked"})) weight += 0.6f;
        if (weight != 0.0f) {
            for (uint8_t id : targets) {
                auto old = std::find_if(result.claims.begin(), result.claims.end(), [id](const Claim& c) { return c.target == id; });
                if (old == result.claims.end()) result.claims.push_back({id, weight});
                else old->weight = weight; // the last explicit correction in this message wins
            }
            if (!result.claims.empty() && result.intent == Intent::Unknown) result.intent = Intent::Statement;
        }
    }
    if (result.topic == kNoPlayer && (result.intent == Intent::AskReason || has(t, {"er", "sie", "he", "she"})))
        result.topic = previousTopic;
    return result;
}

float Memory::credibility(uint8_t speaker) const {
    const auto it = m_credibility.find(speaker);
    return it == m_credibility.end() ? 0.8f : it->second;
}

bool Memory::remember(uint8_t speaker, const Message& message, int day) {
    bool contradiction = false;
    for (const auto& c : message.claims) {
        auto& old = m_claims[{speaker, c.target}];
        if (old.weight * c.weight < 0 && day - old.day <= 1 && !message.correction) contradiction = true;
        old = {c.weight, day, false};
    }
    if (message.claimedRole != Role::Unknown) {
        auto [it, fresh] = m_roles.emplace(speaker, message.claimedRole);
        if (!fresh && it->second != message.claimedRole) { contradiction = !message.correction; it->second = message.claimedRole; }
    }
    if (message.place != Place::Unknown) {
        auto [it, fresh] = m_alibis.emplace(speaker, Alibi{message.place, day, message.exclusiveAlibi});
        if (!fresh) {
            if (it->second.day == day && it->second.place != message.place &&
                (it->second.exclusive || message.exclusiveAlibi) && !message.correction) contradiction = true;
            it->second = {message.place, day, message.exclusiveAlibi};
        }
    }
    if (contradiction) m_credibility[speaker] = std::max(0.3f, credibility(speaker) - 0.2f);
    return contradiction;
}

float Memory::suspicion(uint8_t target, int day) const {
    float total = 0.0f;
    for (const auto& [key, e] : m_claims) {
        if (key.second != target) continue;
        const float selfClaim = key.first == target ? 0.25f : 1.0f;
        total += e.weight * credibility(key.first) * selfClaim * std::pow(0.65f, static_cast<float>(std::max(0, day - e.day)));
    }
    return std::clamp(total, -2.0f, 4.0f);
}

uint8_t Memory::source(uint8_t target, int day) const {
    float best = 0;
    uint8_t source = kNoPlayer;
    for (const auto& [key, e] : m_claims) {
        const float weight = e.weight * credibility(key.first) * std::pow(0.65f, static_cast<float>(std::max(0, day - e.day)));
        if (key.second == target && weight > best) { best = weight; source = key.first; }
    }
    return source;
}

void Memory::observeRevealed(const GameView& view) {
    for (auto& [key, e] : m_claims) {
        const PlayerView* target = view.player(key.second);
        if (e.checked || !target || target->alive || target->role == Role::Unknown) continue;
        e.checked = true;
        if (std::abs(e.weight) < 1.5f) continue;
        const bool correct = (e.weight > 0) == isWolfRole(target->role);
        m_credibility[key.first] = std::clamp(credibility(key.first) + (correct ? 0.1f : -0.25f), 0.3f, 1.2f);
    }
}

} // namespace ww::dialogue
