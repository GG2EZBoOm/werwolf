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

// "Böse" is game slang for the wolf team, like "evil" in English lobbies.
const std::initializer_list<std::string_view> kWolfWords = {
    "wolf", "werwolf", "werewolf", "woelfe", "werwoelfe", "wolves", "werewolves", "boese", "evil",
    "vampir", "vampirin", "vampire", "vampiren", "vampires"};

Role roleIn(const Tokens& t) {
    if (has(t, {"seherin", "seher", "seer"})) return Role::Seer;
    if (has(t, {"hexe", "witch"})) return Role::Witch;
    if (has(t, {"beschuetzer", "protector"})) return Role::Protector;
    if (has(t, {"faehrtenleser", "tracker"})) return Role::Tracker;
    if (has(t, {"jaeger", "hunter"})) return Role::Hunter;
    if (has(t, {"dorfbewohner", "dorfbewohnerin", "villager"})) return Role::Villager;
    return Role::Unknown;
}

// The role in "ich bin (immer noch) Seher", not another role named later in the same message.
Role claimedRoleIn(const Tokens& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        if (!has(Tokens{t[i]}, {"bin", "am", "im"})) continue;
        Tokens rest;
        for (size_t j = i + 1; j < t.size() && !has(Tokens{t[j]}, {".", "!", "?", ";", ",", "und", "and", "aber", "but"}); ++j)
            rest.push_back(t[j]);
        if (has(rest, {"nicht", "kein", "keine", "not", "no"})) continue;
        if (const Role role = roleIn(rest); role != Role::Unknown) return role;
    }
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
    // Vocatives: "Anton, ...", "Anton bist du ...", "Anton du bist sus", "Hallo Anton".
    if (t.size() >= 2 && t[0].person != kNoPlayer &&
        has(Tokens{t[1]}, {",", ":", "bist", "hast", "wer", "wen", "warum", "wo", "was", "du", "dich", "dir", "denkst",
                           "meinst", "glaubst", "are", "do", "why", "who", "where", "what", "you"})) {
        result.addressed = t[0].person;
        t.erase(t.begin());
        if (!t.empty() && has(Tokens{t[0]}, {",", ":"})) t.erase(t.begin());
    } else if (t.size() >= 2 && has(Tokens{t[0]}, {"hallo", "hi", "hey", "hello", "servus", "moin"}) && t[1].person != kNoPlayer) {
        result.addressed = t[1].person;
    }
    result.wolfWord = has(t, kWolfWords);
    if (result.addressed != kNoPlayer) listener = result.addressed;
    if (!t.empty() && has(Tokens{t.front()}, {"ist", "sind", "bist", "hast", "habt", "warst", "was", "what", "is", "are", "did", "do", "were", "beweise", "evidence"}))
        result.question = true;
    result.correction = phrase(t, "lag falsch") || phrase(t, "nehme zurueck") || phrase(t, "was wrong") ||
        phrase(t, "take it back") || has(t, {"korrigiere", "korrektur", "correction"});
    result.exclusiveAlibi = phrase(t, "ganze nacht") || phrase(t, "all night") || phrase(t, "entire night") || has(t, {"durchgehend", "ausschliesslich"});

    const bool self = has(t, {"ich", "i", "im"});
    const bool negated = has(t, {"nicht", "kein", "keine", "keinen", "not", "no", "dont", "isnt", "never", "nie"});
    if (result.question) {
        if (has(t, {"glaubt", "glaubst", "believe", "believes"})) result.intent = Intent::AskBelief; // "warum glaubt mir keiner?"
        else if (has(t, {"warum", "wieso", "weshalb", "why", "beweis", "beweise", "evidence"})) result.intent = Intent::AskReason;
        else if (has(t, {"wo", "where"})) result.intent = Intent::AskAlibi;
        else if (has(t, {"unschuldig", "innocent", "safe", "vertrauen", "trust"})) result.intent = Intent::AskInnocence;
        else if (has(t, {"rolle", "role", "bist"}) || phrase(t, "are you")) result.intent = Intent::AskRole;
        else if (has(t, {"waehlen", "waehlst", "waehlt", "abstimmen", "stimmst", "voten", "votest", "votet", "vote", "voting"}))
            result.intent = Intent::AskVote;
        else result.intent = Intent::AskSuspicion;
    } else if (self && claimedRoleIn(t) != Role::Unknown) {
        result.intent = Intent::RoleClaim;
        result.claimedRole = claimedRoleIn(t);
    } else if (self && has(t, {"war", "was"}) && !negated) {
        if (has(t, {"zuhause", "haus", "hause", "home", "house"})) result.place = Place::Home;
        else if (has(t, {"kapelle", "kirche", "chapel", "church"})) result.place = Place::Chapel;
        else if (has(t, {"feuer", "lagerfeuer", "fire", "campfire"})) result.place = Place::Fire;
        else if (has(t, {"brunnen", "well"})) result.place = Place::Well;
        else if (has(t, {"wald", "forest", "woods"})) result.place = Place::Forest;
        if (result.place != Place::Unknown) result.intent = Intent::Alibi;
    } else if (has(t, {"hallo", "hi", "hey", "hello", "moin", "servus", "huhu", "guten"})) result.intent = Intent::Greeting;

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
    // A seer presenting results states facts: "ich hab Berta gecheckt, sie ist ein Werwolf".
    const bool seerContext = result.claimedRole == Role::Seer ||
        has(t, {"gecheckt", "gechecked", "geprueft", "checked", "angeschaut", "angesehen", "untersucht", "inspiziert", "inspected", "peeked"});
    bool negatedBelief = false;
    uint8_t lastNamed = kNoPlayer; // "er"/"sie" refers to the last person named in this message, never the speaker
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
        if (targets.empty() && has(clause, {"er", "sie", "ihn", "he", "she", "him", "her"})) {
            const uint8_t referent = lastNamed != kNoPlayer ? lastNamed : previousTopic;
            if (referent != kNoPlayer && referent != speaker) targets.push_back(referent);
        }
        for (uint8_t id : targets)
            if (id != speaker) lastNamed = id;
        if (!targets.empty()) result.topic = targets.back();
        if (result.question) continue; // asking is never evidence
        if (targets.empty() && (phrase(clause, "glaube nicht") || phrase(clause, "denke nicht") || phrase(clause, "dont think"))) {
            negatedBelief = true;
            continue;
        }
        if (has(clause, {"wenn", "falls", "if", "ob", "whether"})) continue;
        const bool wolfWord = has(clause, kWolfWords);
        const bool wolf = wolfWord && (has(clause, {"ist", "sind", "bin", "bist", "is", "isnt", "are", "arent", "am", "im", "als", "as", "verwandelt", "transformed"}) || clause.size() <= 3) &&
            !has(clause, {"beim", "mit", "with", "near"});
        const bool suspect = has(clause, {"verdaechtig", "verdaechtige", "suspicious", "misstraue", "suspect", "sus", "luegt", "lying",
                                          "komisch", "seltsam", "merkwuerdig", "suspekt", "schuldig", "luegner", "luegnerin",
                                          "weird", "strange", "sketchy", "shady", "fishy", "guilty", "liar"});
        const bool defend = has(clause, {"unschuldig", "innocent", "vertraue", "traue", "trust", "safe", "harmlos", "sauber",
                                         "vertrauenswuerdig", "harmless", "clean", "cleared", "trustworthy"});
        const bool negative = has(clause, {"nicht", "kein", "keine", "keinen", "not", "no", "dont", "isnt", "arent", "never", "nie"}) ||
            (negatedBelief && has(clause, {"dass", "that"}));
        negatedBelief = false;
        // "Berta raus!", "wir voten Berta", "hängt Berta" – lobby slang for an elimination vote
        const bool vote = has(clause, {"waehle", "waehlen", "voten", "vote", "voting", "raus", "haengen", "haengt", "lynchen",
                                       "rauswerfen", "hinrichten", "lynch", "hang", "eliminate"}) ||
            phrase(clause, "stimme geht") || phrase(clause, "stimme fuer") || phrase(clause, "stimmen fuer");
        // "Clara ist die Hexe": somebody else's role, which later deaths can confirm or refute
        const Role named = roleIn(clause);
        const bool reportsRole = named != Role::Unknown && targets.size() == 1 && targets.front() != speaker && !negative &&
            has(clause, {"ist", "war", "is", "was"}) && !has(clause, {"sagt", "sagte", "says", "said", "laut", "angeblich"});
        if (reportsRole) result.roleReports.push_back({targets.front(), named});
        const bool good = defend || reportsRole || has(clause, {"gut", "good", "dorfi"});
        float weight = wolf ? 1.8f : suspect ? 1.1f : good ? -1.0f : vote ? 0.8f : 0.0f;
        if (negative) weight = -weight;
        const bool hedged = has(clause, {"vielleicht", "maybe", "eventuell", "glaube", "think", "vermute"});
        if (hedged) weight *= 0.55f;
        if (has(clause, {"sagt", "sagte", "says", "said", "laut", "angeblich", "supposedly"}) ||
            phrase(clause, "hat gesagt")) continue; // ambiguous reported speech: ask, do not pin it on every named person
        const bool fact = !hedged && !suspect && !vote &&
            (wolf || reportsRole || (good && has(clause, {"ist", "sind", "war", "is", "are", "was"})));
        if (seerContext && fact && weight != 0.0f) weight = weight > 0 ? kSeerWolf : kSeerGood;
        else if (wolf && weight > 0 && has(clause, {"gesehen", "saw", "verwandelt", "transformed", "geprueft", "gecheckt", "checked"}))
            weight += 0.6f;
        if (weight != 0.0f) {
            for (uint8_t id : targets) {
                auto old = std::find_if(result.claims.begin(), result.claims.end(), [id](const Claim& c) { return c.target == id; });
                if (old == result.claims.end()) result.claims.push_back({id, weight, fact});
                else *old = {id, weight, fact}; // the last explicit correction in this message wins
            }
            if (!result.claims.empty() && result.intent == Intent::Unknown) result.intent = Intent::Statement;
        }
    }
    if (result.topic == kNoPlayer && (result.intent == Intent::AskReason || has(t, {"er", "sie", "he", "she"})))
        result.topic = previousTopic;
    result.seerReport = seerContext && !result.question &&
        std::any_of(result.claims.begin(), result.claims.end(), [](const Claim& c) { return c.fact; });
    return result;
}

bool presentSeerResults(Message& message) {
    for (auto& c : message.claims)
        if (c.fact) {
            c.weight = c.weight > 0 ? kSeerWolf : kSeerGood;
            message.seerReport = true;
        }
    return message.seerReport;
}

float Memory::credibility(uint8_t speaker) const {
    const auto it = m_credibility.find(speaker);
    return it == m_credibility.end() ? 0.8f : it->second;
}

Role Memory::claimedRole(uint8_t speaker) const {
    const auto it = m_roles.find(speaker);
    return it == m_roles.end() ? Role::Unknown : it->second;
}

std::vector<uint8_t> Memory::defenders(uint8_t target) const {
    std::vector<uint8_t> out;
    for (const auto& [key, e] : m_claims)
        if (key.second == target && key.first != target && e.weight <= -0.5f) out.push_back(key.first);
    return out;
}

bool Memory::otherSeer(uint8_t speaker) const {
    return std::any_of(m_roles.begin(), m_roles.end(), [&](const auto& r) { return r.first != speaker && r.second == Role::Seer; });
}

int Memory::seerChecks(uint8_t speaker) const {
    int checks = 0;
    for (const auto& [key, r] : m_reports) checks += key.first == speaker && r.seer && r.weight != 0.0f;
    return checks;
}

int Memory::confirmed(uint8_t speaker) const {
    const auto it = m_confirmed.find(speaker);
    return it == m_confirmed.end() ? 0 : it->second;
}

void Memory::judge(uint8_t speaker, bool truthful) {
    if (!truthful) {
        m_liars.insert(speaker);
        m_credibility[speaker] = 0.2f;
    } else if (!liar(speaker)) {
        ++m_confirmed[speaker];
        m_credibility[speaker] = std::max(credibility(speaker), 1.3f);
    }
}

void Memory::support(uint8_t speaker) {
    if (!liar(speaker)) m_credibility[speaker] = std::min(1.3f, credibility(speaker) + 0.25f);
}

bool Memory::remember(uint8_t speaker, const Message& message, int day) {
    bool contradiction = false;
    const bool seer = message.seerReport || message.claimedRole == Role::Seer || claimedRole(speaker) == Role::Seer;
    for (const auto& c : message.claims) {
        auto& old = m_claims[{speaker, c.target}];
        if (old.weight * c.weight < 0 && day - old.day <= 1 && !message.correction) contradiction = true;
        const bool report = message.seerReport && c.fact;
        if (old.weight * c.weight > 0 && std::abs(old.weight) > std::abs(c.weight))
            old.day = day; // "wir müssen Berta voten" must not weaken "Berta ist ein Werwolf" said before
        else
            old = {c.weight, day, false, report || (old.report && old.weight * c.weight > 0)};
        if (!report) continue;
        auto& r = m_reports[{speaker, c.target}];
        if (r.weight * c.weight <= 0) r.checked = false; // a new or changed result is checked again
        r.weight = c.weight;
        r.seer = true;
    }
    for (const auto& reported : message.roleReports) {
        auto& r = m_reports[{speaker, reported.target}];
        if (r.role != reported.role) r.checked = false;
        r.role = reported.role;
        r.seer = r.seer || seer;
    }
    if (message.claimedRole == Role::Seer && claimedRole(speaker) != Role::Seer) {
        // A second seer is suspicious, the more so when the "seer" was just accused by the first one.
        for (const auto& [other, role] : m_roles) {
            if (other == speaker || role != Role::Seer || liar(other)) continue;
            const auto accused = m_claims.find({other, speaker});
            const bool accusedFirst = accused != m_claims.end() && accused->second.weight > 0;
            m_credibility[speaker] = credibility(speaker) * (accusedFirst ? 0.45f : 0.6f);
            break;
        }
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
        const float age = e.report ? 0.0f : static_cast<float>(std::max(0, day - e.day)); // a seer's result does not fade
        total += e.weight * credibility(key.first) * selfClaim * std::pow(0.65f, age);
    }
    if (liar(target)) total += 3.0f; // whoever fakes seer results is most likely a wolf
    else if (claimedRole(target) == Role::Seer) total -= 0.6f * credibility(target); // keep a credible seer alive
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
        const bool correct = (e.weight > 0) == isEvilRole(target->role);
        const float old = credibility(key.first);
        m_credibility[key.first] = std::clamp(old + (correct ? 0.1f : -0.25f), std::min(0.3f, old), std::max(1.2f, old));
    }
    for (auto& [key, r] : m_reports) {
        const PlayerView* target = view.player(key.second);
        if (r.checked || !target || target->alive || target->role == Role::Unknown) continue;
        r.checked = true;
        // "Dorfbewohner" is often meant as "on the village's side"
        const bool match = r.role == Role::Villager ? !isEvilRole(target->role)
                         : r.role != Role::Unknown  ? r.role == target->role
                                                    : (r.weight > 0) == isEvilRole(target->role);
        if (match) {
            ++m_confirmed[key.first];
            m_credibility[key.first] = std::min(1.5f, credibility(key.first) + 0.3f);
        } else if (r.seer) {
            judge(key.first, false);
        } else {
            m_credibility[key.first] = std::max(0.3f, credibility(key.first) - 0.25f);
        }
    }
}

} // namespace ww::dialogue
