#include "server/Bot.h"

#include <algorithm>
#include <map>

namespace ww {

namespace {

const std::vector<std::string> kDayLines = {
    "Guten Morgen … oder auch nicht.",
    "Ich habe heute Nacht seltsame Geräusche am Brunnen gehört.",
    "{name}, du warst gestern auffällig still.",
    "Ich bin nur ein einfacher Dorfbewohner, ehrlich!",
    "Ich traue {name} nicht über den Weg.",
    "Wir müssen zusammenhalten, sonst holen sie uns alle.",
    "Hat jemand {name} heute Nacht gesehen?",
    "Irgendetwas stimmt mit {name} nicht.",
    "Ich habe kein gutes Gefühl bei {name}.",
    "Lasst uns nicht vorschnell urteilen.",
};

const std::vector<std::string> kWolfLines = {
    "Wie wär's mit {name}?",
    "Ich bin für {name}.",
    "{name} kommt mir verdächtig nach Seherin vor.",
    "Leise, Rudel … nehmen wir {name}.",
};

const std::vector<std::string> kEndLines = {"Gut gespielt!", "GG!", "Noch eine Runde?", "Das war knapp!"};

std::string fill(std::string line, const std::string& name) {
    const std::string key = "{name}";
    if (auto pos = line.find(key); pos != std::string::npos) line.replace(pos, key.size(), name);
    return line;
}

} // namespace

Bot::Bot(uint8_t id, uint64_t seed) : m_rng(seed), m_id(id) {}

float Bot::thinkDelay(ActionKind action) {
    switch (action) {
    case ActionKind::WolfVote: return m_rng.uniform(3.0f, 9.0f);
    case ActionKind::Inspect:
    case ActionKind::Protect:
    case ActionKind::CupidPick: return m_rng.uniform(2.0f, 7.0f);
    case ActionKind::WitchPotions: return m_rng.uniform(3.0f, 8.0f);
    case ActionKind::HunterShot: return m_rng.uniform(3.0f, 7.0f);
    case ActionKind::SkipDiscussion: return m_rng.uniform(25.0f, 70.0f);
    case ActionKind::DayVote: return m_rng.uniform(5.0f, 18.0f);
    default: return 1.0f;
    }
}

std::vector<uint8_t> Bot::validTargets(const Game& game, const GameView& view) const {
    std::vector<uint8_t> out;
    for (const PlayerView& p : view.players)
        if (game.canTarget(m_id, p.id)) out.push_back(p.id);
    return out;
}

uint8_t Bot::mostVoted(const GameView& view, const std::vector<uint8_t>& allowed) const {
    std::map<uint8_t, int> tally;
    for (const PlayerView& p : view.players)
        if (p.vote != kNoPlayer && p.id != m_id) ++tally[p.vote];
    uint8_t best = kNoPlayer;
    int bestCount = 0;
    for (const auto& [id, count] : tally) {
        if (count > bestCount && std::find(allowed.begin(), allowed.end(), id) != allowed.end()) {
            best = id;
            bestCount = count;
        }
    }
    return best;
}

std::string Bot::randomOtherName(const GameView& view) {
    std::vector<std::string> names;
    for (const PlayerView& p : view.players)
        if (p.alive && p.id != m_id) names.push_back(p.name);
    return names.empty() ? std::string("irgendwer") : m_rng.pick(names);
}

void Bot::update(float dt, Game& game, const SayFn& say) {
    const GameView view = game.viewFor(m_id);
    if (view.phase != m_phase || view.day != m_day) {
        m_phase = view.phase;
        m_day = view.day;
        m_acted = false;
        m_think = thinkDelay(view.action);
        m_chatTimer = -1.0f;
        if ((view.phase == Phase::Day && view.me() && view.me()->alive && m_rng.chance(0.45f)) ||
            (view.phase == Phase::GameOver && m_rng.chance(0.3f)))
            m_chatTimer = m_rng.uniform(3.0f, 14.0f);
    }

    if (m_chatTimer > 0.0f) {
        m_chatTimer -= dt;
        if (m_chatTimer <= 0.0f) chatter(view, say);
    }

    if (m_acted || view.action == ActionKind::None) return;
    m_think -= dt;
    if (m_think > 0.0f) return;
    m_acted = true;
    act(game, view, say);
}

void Bot::chatter(const GameView& view, const SayFn& say) {
    if (view.phase == Phase::GameOver) {
        say(m_id, m_rng.pick(kEndLines));
        return;
    }
    // Eine Seherin, die einen lebenden Werwolf kennt, packt gerne aus
    if (view.yourRole == Role::Seer) {
        for (const SeerResult& r : view.seerResults) {
            const PlayerView* p = view.player(r.player);
            if (p && p->alive && r.role == Role::Werewolf && m_rng.chance(0.6f)) {
                say(m_id, "Ich bin die Seherin! " + p->name + " ist ein Werwolf!");
                return;
            }
        }
    }
    say(m_id, fill(m_rng.pick(kDayLines), randomOtherName(view)));
}

void Bot::act(Game& game, const GameView& view, const SayFn& say) {
    std::vector<uint8_t> targets = validTargets(game, view);
    auto randomTarget = [&]() { return targets.empty() ? kNoPlayer : m_rng.pick(targets); };
    auto knownWolfTarget = [&]() -> uint8_t {
        for (uint8_t id : targets) {
            const PlayerView* p = view.player(id);
            if (p && p->role == Role::Werewolf) return id;
        }
        return kNoPlayer;
    };

    switch (view.action) {
    case ActionKind::WolfVote: {
        uint8_t target = kNoPlayer;
        // Dem Rudel folgen, wenn schon jemand gewählt hat
        for (const PlayerView& p : view.players) {
            if (p.id != m_id && p.role == Role::Werewolf && p.vote != kNoPlayer && game.canTarget(m_id, p.vote) &&
                m_rng.chance(0.75f)) {
                target = p.vote;
                break;
            }
        }
        if (target == kNoPlayer) target = randomTarget();
        if (target == kNoPlayer) return;
        game.select(m_id, target);
        game.confirm(m_id);
        if (m_rng.chance(0.4f)) {
            const PlayerView* t = view.player(target);
            say(m_id, fill(m_rng.pick(kWolfLines), t ? t->name : "irgendwen"));
        }
        break;
    }
    case ActionKind::Inspect:
    case ActionKind::Protect: {
        uint8_t target = randomTarget();
        if (view.action == ActionKind::Protect && game.canTarget(m_id, m_id) && m_rng.chance(0.25f)) target = m_id;
        if (target == kNoPlayer) return;
        game.select(m_id, target);
        game.confirm(m_id);
        break;
    }
    case ActionKind::CupidPick: {
        if (targets.size() < 2) return;
        m_rng.shuffle(targets);
        game.select(m_id, targets[0]);
        game.select(m_id, targets[1]);
        game.confirm(m_id);
        break;
    }
    case ActionKind::WitchPotions: {
        if (view.witchVictim != kNoPlayer && view.healAvailable &&
            (view.witchVictim == m_id || m_rng.chance(0.55f)))
            game.setWitchHeal(m_id, true);
        if (view.poisonAvailable) {
            const uint8_t wolf = knownWolfTarget();
            if (wolf != kNoPlayer) game.select(m_id, wolf);
            else if (m_rng.chance(0.12f)) game.select(m_id, randomTarget());
        }
        game.confirm(m_id);
        break;
    }
    case ActionKind::HunterShot: {
        uint8_t target = knownWolfTarget();
        if (target == kNoPlayer) target = randomTarget();
        if (target == kNoPlayer) return;
        game.select(m_id, target);
        game.confirm(m_id);
        break;
    }
    case ActionKind::SkipDiscussion:
        game.setSkip(m_id, true);
        break;
    case ActionKind::DayVote: {
        if (targets.empty()) return;
        uint8_t target = kNoPlayer;
        if (view.yourRole == Role::Werewolf) {
            std::vector<uint8_t> innocents;
            for (uint8_t id : targets) {
                const PlayerView* p = view.player(id);
                if (p && p->role != Role::Werewolf) innocents.push_back(id);
            }
            if (innocents.empty()) innocents = targets;
            target = m_rng.chance(0.6f) ? mostVoted(view, innocents) : kNoPlayer;
            if (target == kNoPlayer) target = m_rng.pick(innocents);
        } else {
            target = knownWolfTarget();
            if (target == kNoPlayer && m_rng.chance(0.5f)) target = mostVoted(view, targets);
            if (target == kNoPlayer) target = randomTarget();
        }
        game.select(m_id, target);
        if (m_rng.chance(0.3f)) {
            const PlayerView* t = view.player(target);
            if (t) say(m_id, "Meine Stimme geht an " + t->name + ".");
        }
        break;
    }
    default:
        break;
    }
}

} // namespace ww
