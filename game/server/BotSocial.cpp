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

// Wie viel Sinn es ergibt, die eigene Rolle offen zu nennen: Der Jäger schreckt ab, Dorfälteste, Dorfdepp und Ritter
// überstehen oder bestrafen einen Angriff, Geschwister bürgen füreinander. Wer nachts etwas herausfindet, rettet oder
// heimlich wirkt (Seherin, Hexe, Beschützer, Fuchs, Medium …), wäre sofort das nächste Opfer und schweigt.
float revealSense(Role role) {
    switch (role) {
    case Role::Hunter: return 0.6f;
    case Role::Villager: return 0.4f;
    case Role::Elder:
    case Role::Idiot:
    case Role::Knight: return 0.35f;
    case Role::Sister:
    case Role::Brother: return 0.3f;
    case Role::Blacksmith:
    case Role::Scapegoat: return 0.2f;
    default: return 0.0f;
    }
}

const std::array<Text, 4> kVillagerLines = {
    Text{"Nur ein einfacher Dorfbewohner – leider ohne besondere Fähigkeiten.", "Just a simple villager – no special abilities, sadly."},
    Text{"Ich bin Dorfbewohner, ganz ohne Fähigkeiten. Ich kann nur aufpassen und abstimmen.",
         "I'm a villager, no abilities at all. All I can do is watch and vote."},
    Text{"Bei mir nichts Spannendes: ganz normaler Dorfbewohner.", "Nothing exciting here: a perfectly ordinary villager."},
    Text{"Dorfbewohner. Nichts, wofür die Wölfe mich nachts holen müssten.", "Villager. Nothing the wolves would need to come for me at night."},
};
const std::array<Text, 4> kSecretLines = {
    Text{"Meine Rolle behalte ich vorerst für mich. Über konkrete Beobachtungen können wir sprechen.",
         "I'm keeping my role to myself for now. We can discuss specific observations."},
    Text{"Das sage ich lieber noch nicht. Wer seine Rolle zu früh nennt, ist nachts dran.",
         "I'd rather not say yet. Whoever names their role too early is next at night."},
    Text{"Da halte ich mich erst mal bedeckt.", "I'll keep that to myself for now."},
    Text{"Noch nicht – erst will ich sehen, wer hier was behauptet.", "Not yet – first I want to see who claims what."},
};

// verteilt auch die unteren Bits, damit benachbarte Bots nicht dieselbe Formulierung wählen
uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

int votesOn(const GameView& view, uint8_t id) {
    int votes = 0;
    for (const auto& p : view.players) votes += p.alive && p.vote == id;
    return votes;
}

// Druck auf einen Bot: Angst und Stimmen gegen ihn bringen auch Vorsichtige zum Reden
float pressureOn(const GameView& view, uint8_t id, float stress) {
    return std::min(1.0f, stress + 0.3f * static_cast<float>(votesOn(view, id)));
}
}

float Bot::dailyRoll(int day, uint64_t purpose) const {
    return static_cast<float>(mix(mix(m_salt + purpose * 0xD1B54A32D192ED03ull) ^ static_cast<uint64_t>(day)) % 1000) / 1000.0f;
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
    const Text& secret = variant(kSecretLines, mix(m_salt), static_cast<uint64_t>(match.day()));
    const Text& villager = variant(kVillagerLines, mix(m_salt), 7);
    // Wer einmal etwas gesagt hat, bleibt dabei – sonst fliegt die Lüge auf
    if (m_publicRole != Role::Unknown) {
        if (asked != Role::Unknown && asked != m_publicRole)
            return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
        return claim(m_publicRole, tx("Wie gesagt: Ich bin {}.", "Like I said: I'm {}.", roleWithArticle(m_publicRole)));
    }
    // je Bot und Tag eine feste Entscheidung, damit dieselbe Frage nicht mal so, mal so beantwortet wird
    const float roll = dailyRoll(match.day(), 0);
    const float pressure = pressureOn(view, m_id, m_feelings.stress);
    if (isEvilRole(mine)) {
        // Wölfe und Vampire lügen: ein harmloser Dorfbewohner – oder, wer sich traut, ein falscher Jäger
        const bool hunterTaken = m_dialogue.otherClaimant(Role::Hunter, m_id) != kNoPlayer;
        if ((asked == Role::Unknown || asked == Role::Hunter) && !hunterTaken && roll < 0.2f * m_personality.boldness + 0.2f * pressure)
            return claim(Role::Hunter, {"Ich bin der Jäger. Hängt mich, und ich nehme jemanden mit.",
                                        "I'm the hunter. Hang me and I'll take someone with me."});
        if (asked != Role::Unknown) return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
        if (roll < 0.55f) return claim(Role::Villager, villager);
        return secret;
    }
    if (asked != Role::Unknown && asked != mine) return tx("Nein, {} bin ich nicht.", "No, I'm not {}.", roleWithArticle(asked));
    if (isPublicRole(mine)) return plainly(mine);
    // Nur wo es Sinn ergibt – Seherin, Hexe & Co. verraten sich höchstens, wenn ihnen der Strick droht
    const float sense = revealSense(mine);
    float reveal = sense > 0.0f ? sense + 0.5f * pressure : 0.03f + 0.25f * static_cast<float>(votesOn(view, m_id));
    if (asked == mine) reveal += 0.15f; // direkt gefragt
    if (roll >= std::min(0.95f, reveal)) return secret;
    if (mine == Role::Hunter)
        return claim(mine, {"Ich bin der Jäger. Hängt mich, und ich nehme jemanden mit.", "I'm the hunter. Hang me and I'll take someone with me."});
    if (mine == Role::Villager) return claim(mine, villager);
    if (const Text hint = roleHint(match); !hint.empty())
        return claim(mine, tx("Ich bin {}. {}", "I'm {}. {}", roleWithArticle(mine), hint));
    return plainly(mine);
}

Text Bot::volunteerRole(const Match& match, std::optional<Evidence>* evidence) const {
    const auto view = match.viewFor(m_id);
    Role shown = view.yourRole;
    const float pressure = pressureOn(view, m_id, m_feelings.stress);
    float sense = revealSense(shown);
    if (isEvilRole(shown)) {
        // gelogen: meistens ein harmloser Dorfbewohner, mutige oder bedrängte Wölfe geben den Jäger
        const bool hunterTaken = m_dialogue.otherClaimant(Role::Hunter, m_id) != kNoPlayer;
        const float bluff = dailyRoll(0, 2);
        shown = !hunterTaken && bluff < 0.15f * m_personality.boldness + 0.3f * pressure ? Role::Hunter : Role::Villager;
        sense = 0.3f;
    }
    if (sense <= 0.0f || isPublicRole(shown)) return {}; // die einen wären sofort das nächste Opfer, die anderen kennt man ohnehin
    // je Bot und Tag eine feste Entscheidung: die meisten schweigen, unter Druck reden mehr
    const float roll = dailyRoll(match.day(), 1);
    if (roll >= std::min(0.7f, sense * (0.4f + pressure) * m_personality.talk)) return {};
    if (evidence) *evidence = Evidence{Evidence::Kind::RoleClaim, m_id, shown == Role::Villager ? -0.2f : -0.5f, shown};
    if (shown == Role::Hunter)
        return {"Zur Info: Ich bin der Jäger. Wer mich hängt, nimmt jemanden mit.", "For the record: I'm the hunter. Whoever hangs me takes someone along."};
    if (shown == Role::Villager)
        return {"Damit ihr wisst, woran ihr seid: Ich bin nur ein einfacher Dorfbewohner.", "So you know where you stand: I'm just a simple villager."};
    return tx("Damit ihr Anhaltspunkte habt: Ich bin {}.", "To give you something to go on: I'm {}.", roleWithArticle(shown));
}

void Bot::considerOffer(const Match& match, uint8_t speaker, bool claimsProtector) {
    const auto view = match.viewFor(m_id);
    const auto* from = match.find(speaker);
    if (!from || !from->alive || speaker == m_id || m_whisperTo != kNoPlayer || m_publicRole != Role::Unknown) return;
    if (view.yourRole == Role::Seer) {
        // Ein Lügner, ein Verdächtiger oder ein zweiter "Beschützer" erfährt nichts – Vertrauen und Vorsicht entscheiden
        const bool doubtful = m_dialogue.liar(speaker) || suspicion(match, speaker) > 0.8f || !claimsProtector ||
                              m_dialogue.otherClaimant(Role::Protector, speaker) != kNoPlayer;
        const float chance = std::clamp(0.45f + 0.8f * (m_personality.trust - 0.95f) - 0.3f * (m_personality.caution - 0.5f), 0.15f, 0.8f);
        if (doubtful || !m_rng.chance(chance)) return;
    } else if (!(isEvilRole(view.yourRole) && packSpeaker(match) && !ally(view, view.player(speaker) ? view.player(speaker)->role : Role::Unknown) &&
                 m_rng.chance(0.3f * m_personality.boldness))) {
        return; // nur ein Wolf lügt hier: als "Seherin" lässt er den Beschützer seine Nacht verschwenden
    }
    m_whisperTo = speaker;
    m_whisperRole = Role::Seer;
    m_whisperDay = match.day();
}

void Bot::whisper(const Match& match, const SayFn& say) {
    if (m_whisperTo == kNoPlayer) return;
    const auto* me = match.find(m_id);
    const auto* to = match.find(m_whisperTo);
    if (!me || !me->alive || !to || !to->alive || match.day() > m_whisperDay + 1) {
        m_whisperTo = kNoPlayer;
        return;
    }
    if (match.viewFor(m_id).yourRole == Role::Seer &&
        (m_dialogue.liar(m_whisperTo) || suspicion(match, m_whisperTo) > 0.8f || m_dialogue.otherClaimant(Role::Protector, m_whisperTo) != kNoPlayer)) {
        m_whisperTo = kNoPlayer;
        return;
    }
    // nur im Nähe-Chat, nie vor allen in der Versammlung: tagsüber geht man hin, nachts und morgens nur, wer ohnehin nah ist
    const Phase phase = match.phase();
    if ((phase != Phase::Day && phase != Phase::Night && phase != Phase::Morning) || !m_replies.empty()) return;
    if (distanceXZ(me->position, to->position) > 4.0f || !match.hasLineOfSight(me->position, to->position)) {
        if (phase != Phase::Day) return;
        m_companion = m_whisperTo; // hingehen
        m_companionTime = 20.0f;
        m_companionOffset = Vec2{1.2f, 0.0f};
        return;
    }
    Speech speech;
    speech.evidence = Evidence{Evidence::Kind::RoleClaim, m_id, -0.5f, m_whisperRole};
    speech.topic = m_id;
    m_publicRole = m_whisperRole;
    lookAt(m_whisperTo, match.clock() + 4.0);
    const std::string name = to->name;
    m_whisperTo = kNoPlayer;
    speak(say, tx("Psst, {}: Ich bin die Seherin. Pass heute Nacht auf mich auf.", "Psst, {}: I'm the seer. Watch over me tonight.", name), false, speech);
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
