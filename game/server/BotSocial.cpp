// Was Bots menschlicher macht: Persönlichkeit und Schreibstil, Täuschung der Wölfe,
// Schlüsse aus Stimmen und nächtlichen Beobachtungen, Reaktionen auf Tote.
#include "server/Bot.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ww {
namespace {

void replaceAll(std::string& s, std::string_view from, std::string_view to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

void dropFinalPeriod(std::string& s) {
    if (s.size() > 1 && s.back() == '.' && s[s.size() - 2] != '.') s.pop_back();
}

std::string casual(std::string s, bool german) {
    if (german) {
        if (s.starts_with("Ich habe ")) s.replace(0, 9, "Hab ");
        if (s.starts_with("Ich glaube ")) s.replace(0, 11, "Glaub ");
        replaceAll(s, ". Ich habe ", ". Hab ");
        replaceAll(s, " ich habe ", " hab ");
        replaceAll(s, " etwas ", " was ");
        replaceAll(s, "Okay.", "Ok.");
    } else {
        if (s.starts_with("I have ")) s.replace(0, 7, "I've ");
        replaceAll(s, " I have ", " I've ");
        replaceAll(s, "I am ", "I'm ");
        replaceAll(s, "do not ", "don't ");
        replaceAll(s, "cannot ", "can't ");
    }
    // klein anfangen wie im Chat – aber nur bei gewöhnlichen Wörtern, Namen bleiben groß
    static const std::array<std::string_view, 14> plain = {"Ich", "Hab", "Glaub", "Das", "Die", "Wir", "Meine", "Mein", "Nicht",
                                                           "Lasst", "Wer", "Hat", "Hast", "Gut"};
    const std::string_view first = std::string_view(s).substr(0, s.find_first_of(" ,.!?:;"));
    if (german && std::find(plain.begin(), plain.end(), first) != plain.end()) s[0] = static_cast<char>(s[0] - 'A' + 'a');
    dropFinalPeriod(s);
    return s;
}

std::string terse(std::string s) {
    int sentences = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if ((s[i] == '.' || s[i] == '!' || s[i] == '?') && s[i + 1] == ' ' && ++sentences == 2) {
            s.resize(i + 1);
            break;
        }
    }
    dropFinalPeriod(s);
    return s;
}

// Wählt je Bot und Anlass eine von mehreren Formulierungen, ohne den Zufallsgenerator zu verbrauchen.
template <size_t N>
const Text& variant(const std::array<Text, N>& lines, uint64_t salt, uint64_t occasion) {
    return lines[static_cast<size_t>((salt ^ (occasion * 0x9E3779B97F4A7C15ull)) % N)];
}

} // namespace

Bot::Personality Bot::Personality::random(uint64_t seed) {
    eng::Rng rng(seed ^ 0x5bd1e9955bd1e995ull);
    Personality p;
    p.talk = rng.uniform(0.5f, 1.5f);
    p.trust = rng.uniform(0.7f, 1.2f);
    p.boldness = rng.uniform(0.7f, 1.3f);
    p.follow = rng.uniform(0.25f, 0.8f);
    p.caution = rng.uniform(0.2f, 0.9f);
    const float style = rng.uniform();
    p.style = style < 0.3f ? Style::Formal : style < 0.75f ? Style::Casual : Style::Terse;
    return p;
}

Text Bot::styled(const Text& line, Personality::Style style) {
    switch (style) {
    case Personality::Style::Casual: return {casual(line.de, true), casual(line.en, false)};
    case Personality::Style::Terse: return {terse(line.de), terse(line.en)};
    default: return line;
    }
}

// --- Rollen nennen -----------------------------------------------------------

namespace {
bool uniqueRole(Role role) { return role != Role::Unknown && role != Role::Villager && role != Role::Sister && role != Role::Brother; }
}

Text Bot::roleAnswer(const Match& match, Role asked, std::optional<Evidence>* evidence) const {
    const auto view = match.viewFor(m_id);
    const Role mine = view.yourRole;
    auto claim = [&](Role role, Text line) {
        if (evidence) *evidence = Evidence{Evidence::Kind::RoleClaim, m_id, role == Role::Villager ? -0.2f : -0.5f, role};
        return line;
    };
    auto plainly = [&](Role role) {
        return claim(role, tx("Ich bin {}.", "I'm {}.", roleWithArticle(role)));
    };
    const Text secret{"Meine Rolle behalte ich vorerst für mich. Über konkrete Beobachtungen können wir sprechen.",
                      "I'm keeping my role to myself for now. We can discuss specific observations."};
    // Wer einmal etwas gesagt hat, bleibt dabei – sonst fliegt die Lüge auf
    if (m_publicRole != Role::Unknown) {
        if (asked != Role::Unknown && asked != m_publicRole)
            return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
        return claim(m_publicRole, tx("Wie gesagt: Ich bin {}.", "Like I said: I'm {}.", roleWithArticle(m_publicRole)));
    }
    // je Bot und Tag eine feste Entscheidung, damit dieselbe Frage nicht mal so, mal so beantwortet wird
    const float roll = static_cast<float>((m_salt ^ (static_cast<uint64_t>(match.day()) * 0x9E3779B97F4A7C15ull)) % 1000) / 1000.0f;
    int votesAgainstMe = 0;
    for (const auto& p : view.players) votesAgainstMe += p.alive && p.vote == m_id;
    const float pressure = std::min(1.0f, m_feelings.stress + 0.3f * static_cast<float>(votesAgainstMe));
    if (isEvilRole(mine)) {
        // Wölfe und Vampire lügen: ein harmloser Dorfbewohner – oder, wer sich traut, ein falscher Jäger
        const bool hunterTaken = m_dialogue.otherClaimant(Role::Hunter, m_id) != kNoPlayer;
        if ((asked == Role::Unknown || asked == Role::Hunter) && !hunterTaken && roll < 0.2f * m_personality.boldness + 0.2f * pressure)
            return claim(Role::Hunter, {"Ich bin der Jäger. Hängt mich, und ich nehme jemanden mit.",
                                        "I'm the hunter. Hang me and I'll take someone with me."});
        if (asked != Role::Unknown) return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
        if (roll < 0.65f) return claim(Role::Villager, {"Nur ein einfacher Dorfbewohner – leider ohne besondere Fähigkeiten.",
                                                         "Just a simple villager – no special abilities, sadly."});
        return secret;
    }
    if (asked != Role::Unknown && asked != mine) return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
    if (isPublicRole(mine)) return plainly(mine);
    // Wie gern man die eigene Rolle verrät: Der Jäger schreckt ab, ein Dorfbewohner verliert nichts,
    // Hexe und Beschützer wären sofort das nächste Opfer und schweigen lieber – außer unter Druck
    float reveal = 0.3f;
    switch (mine) {
    case Role::Hunter: reveal = 0.6f; break;
    case Role::Villager: reveal = 0.55f; break;
    case Role::Elder: reveal = 0.4f; break;
    case Role::Witch:
    case Role::Protector:
    case Role::Seer: reveal = 0.08f; break;
    default: break;
    }
    if (asked == mine) reveal += 0.15f; // direkt gefragt
    if (roll >= std::min(0.95f, reveal + 0.5f * pressure)) return secret;
    if (mine == Role::Hunter)
        return claim(mine, {"Ich bin der Jäger. Hängt mich, und ich nehme jemanden mit.", "I'm the hunter. Hang me and I'll take someone with me."});
    if (mine == Role::Villager)
        return claim(mine, {"Nur ein einfacher Dorfbewohner – leider ohne besondere Fähigkeiten.", "Just a simple villager – no special abilities, sadly."});
    if (const Text hint = roleHint(match); !hint.empty())
        return claim(mine, tx("Ich bin {}. {}", "I'm {}. {}", roleWithArticle(mine), hint));
    return plainly(mine);
}

// --- Täuschung der Wölfe -----------------------------------------------------

bool Bot::ally(const GameView& view, Role other) const {
    if (isWolfRole(view.yourRole)) return isWolfRole(other);
    if (isVampireRole(view.yourRole)) return isVampireRole(other);
    return false;
}

bool Bot::packSpeaker(const Match& match) const {
    const auto view = match.viewFor(m_id);
    if (!isEvilRole(view.yourRole)) return false;
    for (const auto& p : match.players()) {
        const auto* known = view.player(p.id);
        if (p.alive && p.bot && p.id < m_id && known && ally(view, known->role)) return false;
    }
    return true;
}

bool Bot::packClaimedSeer(const Match& match) const {
    if (m_claimedSeer) return true;
    const auto view = match.viewFor(m_id);
    for (const auto& p : view.players)
        if (p.alive && p.id != m_id && ally(view, p.role) && m_dialogue.claimedRole(p.id) == Role::Seer) return true;
    return false;
}

bool Bot::wouldCounterclaim(const Match& match, const dialogue::Message& message, uint8_t speaker) const {
    // only with a "result" a seer could actually have by now
    if (!message.seerReport || !packSpeaker(match) || packClaimedSeer(match) || sacrificeTarget(match) != kNoPlayer ||
        nightsSoFar(match) < 1) return false;
    const auto view = match.viewFor(m_id);
    if (const auto* from = view.player(speaker); from && ally(view, from->role)) return false;
    const bool exposesPack = std::any_of(message.claims.begin(), message.claims.end(), [&](const dialogue::Claim& c) {
        const auto* p = view.player(c.target);
        return c.fact && c.weight > 0 && (c.target == m_id || (p && ally(view, p->role)));
    });
    if (!exposesPack) return false;
    // Nicht jeder Wolf traut sich: je nach Mut, für jeden Bot und Tag anders
    const uint64_t roll = (m_salt ^ (static_cast<uint64_t>(match.day()) * 0x9E3779B97F4A7C15ull)) % 100;
    return static_cast<float>(roll) < 25.0f + 25.0f * m_personality.boldness;
}

uint8_t Bot::scapegoat(const Match& match, uint8_t avoid) const {
    const auto view = match.viewFor(m_id);
    uint8_t best = kNoPlayer;
    float bestScore = -1.0f;
    for (const auto& p : view.players) {
        // keine bekannten Rollen: wer bestätigt ist, lässt sich schlecht belasten
        if (!p.alive || p.id == m_id || p.id == avoid || p.role != Role::Unknown) continue;
        const auto own = m_suspicion.find(p.id); // bei Wölfen: wer dem Rudel gefährlich wird
        const float score = (own == m_suspicion.end() ? 0.0f : own->second) + static_cast<float>((m_salt >> (p.id % 48)) & 7) * 0.05f;
        if (score > bestScore) { bestScore = score; best = p.id; }
    }
    return best;
}

Text Bot::counterclaim(const Match& match, uint8_t speaker, std::optional<Evidence>* evidence) const {
    if (evidence) *evidence = Evidence{Evidence::Kind::Seer, speaker, dialogue::kSeerWolf};
    static const std::array<Text, 2> lines = {{
        {"Moment – ich bin die Seherin, nicht du. Und ich habe dich geprüft: Werwolf.",
         "Hold on – I'm the seer, not you. And I checked you: werewolf."},
        {"Das ist gelogen! Ich bin die Seherin, und du bist der Werwolf – ich habe dich geprüft.",
         "That's a lie! I'm the seer, and you're the werewolf – I checked you."}}};
    return variant(lines, m_salt, static_cast<uint64_t>(match.day()));
}

// --- Verlauf beobachten und auswerten ----------------------------------------

void Bot::observe(float dt, const Match& match, const GameView& view) {
    // öffentliche Stimmen merken, solange abgestimmt wird
    if (view.phase == Phase::Vote && !view.election)
        for (const auto& p : view.players)
            if (p.alive) m_votes[view.day][p.id] = p.vote; // also remember withdrawn votes
    const auto* me = match.find(m_id);
    if (!me || !me->alive || view.phase != Phase::Night) return;
    m_sightTimer -= dt;
    if (m_sightTimer > 0.0f) return;
    m_sightTimer = 1.0f;
    for (const EntityState& e : match.entitiesFor(m_id)) {
        if (e.id == m_id || e.kind != EntityLiving || e.transform >= 0.3f) continue; // die Bestie ist der Augenzeugenfall
        const Vec3 pos{e.x, 0, e.z};
        if (distanceXZ(pos, me->position) > 14.0f || !match.hasLineOfSight(pos, me->position)) continue;
        auto& seen = m_sightings[e.id];
        if (seen.size() < 200) seen.push_back({e.x, e.z});
    }
}

void Bot::review(const Match& match, const GameView& view) {
    const auto* me = match.find(m_id);
    if (!me) return;
    const bool wolf = isEvilRole(view.yourRole);
    const double now = match.clock();
    auto knownWolf = [&](uint8_t id) {
        const auto* p = view.player(id);
        return p && ally(view, p->role);
    };
    if (view.phase == Phase::Night) {
        m_aliveAtNight.clear();
        for (const auto& p : match.players())
            if (p.alive) m_aliveAtNight.insert(p.id);
        m_sightings.clear();
    }

    // Wer ist seit Einbruch der Nacht gestorben? Entsetzen und, falls gesehen, ein Verdacht
    if (view.phase == Phase::Day && me->alive) {
        bool reported = false;
        for (uint8_t id : m_aliveAtNight) {
            const auto* victim = match.find(id);
            if (!victim || victim->alive || !victim->diedAtNight || m_reviewed.contains(id)) continue;
            m_reviewed.insert(id);
            const bool housemate = victim->house != kNoPlayer && victim->house == me->house;
            if (m_rng.chance(std::min(0.9f, (housemate ? 0.7f : 0.25f) * m_personality.talk))) {
                static const std::array<Text, 4> lines = {{
                    {"Oh nein, {}!", "Oh no, {}!"},
                    {"Nicht {} … das darf nicht wahr sein.", "Not {} … this can't be true."},
                    {"{} ist tot? Wer war das?", "{} is dead? Who did this?"},
                    {"Das hat {} nicht verdient. Heute finden wir den Wolf.", "{} didn't deserve that. Today we find the wolf."}}};
                const Text& line = variant(lines, m_salt, id);
                plan(Phase::Day, view.day, now + m_rng.uniform(2.0f, 9.0f), tx(line.de, line.en, victim->name), {}, id);
            }
            // Nachts beim Haus des Opfers gesehen – nur verdächtig, wenn es dort auch gestorben ist.
            // Am Lagerfeuer stehen alle; wer dort war, beweist nichts.
            if (victim->house == kNoPlayer || victim->house >= match.world().houses().size()) continue;
            const auto& doorPos = match.world().houses()[victim->house].door;
            const Vec2 door{doorPos.x, doorPos.z};
            if ((Vec2{victim->deathPosition.x, victim->deathPosition.z} - door).length() > 10.0f) continue;
            for (const auto& [seen, positions] : m_sightings) {
                const auto* p = match.find(seen);
                if (reported || !p || !p->alive || seen == id || seen == m_id || (wolf && knownWolf(seen))) continue;
                const bool near = std::any_of(positions.begin(), positions.end(), [&](Vec2 at) { return (at - door).length() < 7.0f; });
                if (!near) continue;
                reported = true;
                if (!wolf) m_suspicion[seen] += 1.2f;
                plan(Phase::Assembly, view.day, 0,
                     tx("Ich habe {} heute Nacht beim Haus von {} gesehen.", "I saw {} near {}'s house last night.", p->name, victim->name),
                     Evidence{Evidence::Kind::Sighting, seen, 1.2f}, seen, id);
            }
        }
    }

    // Hingerichtete: Reaktion und Schlüsse aus den Stimmen
    if (view.phase != Phase::Execution && view.phase != Phase::Night && view.phase != Phase::Day) return;
    for (const auto& p : match.players()) {
        const auto* known = view.player(p.id);
        if (p.alive || p.deathCause != DeathCause::Vote || m_reviewed.contains(p.id) || !known || known->role == Role::Unknown) continue;
        m_reviewed.insert(p.id);
        const bool wasWolf = isEvilRole(known->role);
        const auto votes = m_votes[p.deathDay];
        const bool votedFor = votes.contains(m_id) && votes.at(m_id) == p.id;
        std::vector<uint8_t> voters;
        for (const auto& [voter, target] : votes)
            if (target == p.id && voter != m_id) voters.push_back(voter);
        if (me->alive && view.phase == Phase::Execution && m_rng.chance(std::min(0.8f, (wolf ? 0.15f : 0.35f) * m_personality.talk))) {
            static const std::array<Text, 2> caught = {{{"Ich wusste es!", "I knew it!"}, {"Endlich einer!", "Finally, one of them!"}}};
            static const std::array<Text, 2> innocent = {{
                {"Oh nein … {} war unschuldig.", "Oh no … {} was innocent."}, {"Das war ein Fehler. {} war keiner von ihnen.",
                                                                              "That was a mistake. {} wasn't one of them."}}};
            const Text line = wasWolf ? (votedFor ? variant(caught, m_salt, p.id)
                                                  : Text{"Gut, dass wir {} erwischt haben.", "Good thing we caught {}."})
                            : votedFor ? Text{"Ich habe für {} gestimmt. Das tut mir leid.", "I voted for {}. I'm sorry."}
                                       : variant(innocent, m_salt, p.id);
            plan(Phase::Execution, view.day, now + m_rng.uniform(1.0f, 4.0f), tx(line.de, line.en, p.name), {}, p.id);
        }
        if (!me->alive) continue;
        uint8_t mention = kNoPlayer;
        if (wasWolf) {
            // wer den Wolf noch verteidigt hat, macht sich verdächtig
            for (uint8_t defender : m_dialogue.defenders(p.id)) {
                const auto* d = match.find(defender);
                if (!d || !d->alive || defender == m_id || wolf) continue;
                m_suspicion[defender] += 1.0f;
                mention = defender;
            }
            for (uint8_t voter : voters)
                if (!wolf) {
                    m_suspicion[voter] -= 0.4f;
                    changeRelationship(voter, 0.18f);
                    m_dialogue.support(voter); // public success builds trust, even if it was a wolf's sacrifice
                }
            if (const auto* d = match.find(mention))
                plan(Phase::Assembly, p.deathDay + 1, 0,
                     tx("{} hat {} noch verteidigt – und {} war ein Werwolf.", "{} still defended {} – and {} was a werewolf.", d->name, p.name, p.name),
                     {}, mention, p.id);
        } else {
            for (uint8_t voter : voters) {
                const auto* v = match.find(voter);
                if (!v || !v->alive || (wolf && knownWolf(voter))) continue;
                if (!wolf) m_suspicion[voter] += 0.5f;
                if (mention == kNoPlayer || m_rng.chance(0.4f)) mention = voter;
            }
            if (const auto* v = match.find(mention); v && m_rng.chance(0.5f + 0.3f * m_personality.talk))
                plan(Phase::Assembly, p.deathDay + 1, 0,
                     tx("{} hat für {} gestimmt, und {} war unschuldig.", "{} voted for {}, and {} was innocent.", v->name, p.name, p.name),
                     {}, mention, p.id);
        }
    }
}

void Bot::plan(Phase phase, int day, double due, Text line, std::optional<Evidence> evidence, uint8_t about, uint8_t event) {
    if (m_planned.size() >= 4) return;
    m_planned.push_back({std::move(line), phase, day, due, evidence, about, event});
}

void Bot::speakPlanned(const Match& match, const SayFn& say) {
    const auto* me = match.find(m_id);
    std::erase_if(m_planned, [&](const Planned& p) { return match.day() > p.day + 1; });
    if (!me || !me->alive || m_planned.empty() || !m_replies.empty() || match.clock() < m_nextPlanned ||
        match.clock() < m_replyAfter || me->wolfForm()) return;
    for (auto it = m_planned.begin(); it != m_planned.end(); ++it) {
        if (it->phase != match.phase() || match.day() < it->day || match.clock() < it->due) continue;
        if (const auto* about = match.find(it->about); about && about->alive) lookAt(it->about, match.clock() + 3.0);
        const Planned line = std::move(*it);
        m_planned.erase(it);
        m_nextPlanned = match.clock() + m_rng.uniform(5.0f, 9.0f);
        m_lastDiscussion = match.clock();
        speak(say, line.text, false, {line.evidence, false, line.event});
        return;
    }
}

// --- Körpersprache -----------------------------------------------------------

void Bot::lookAt(uint8_t player, double until) {
    if (player == m_id) return;
    m_lookAt = player;
    m_lookUntil = until;
}

void Bot::faceSomeone(float dt, const Match& match) {
    const auto* other = match.find(m_lookAt);
    if (!other || match.clock() > m_lookUntil || m_mover.velocity.length() > 0.3f) return;
    const Vec3 at = other->alive ? other->position : other->ghostPosition;
    const Vec2 d{at.x - m_mover.position.x, at.z - m_mover.position.z};
    if (d.length() < 0.5f || d.length() > 25.0f) return;
    m_mover.yaw += wrapAngle(std::atan2(d.x, d.y) - m_mover.yaw) * std::min(1.0f, dt * 5.0f);
}

} // namespace ww
