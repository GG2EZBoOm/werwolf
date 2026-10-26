#include "server/Bot.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ww {

namespace {

const std::vector<std::string> kDayLines = {
    "Guten Morgen … oder auch nicht.",
    "Ich habe heute Nacht seltsame Geräusche am Brunnen gehört.",
    "{name}, wo warst du gestern Nacht?",
    "Ich bin nur ein einfacher Dorfbewohner, ehrlich!",
    "Ich traue {name} nicht über den Weg.",
    "Wir müssen zusammenhalten, sonst holen sie uns alle.",
    "Hat jemand {name} heute Nacht gesehen?",
    "Irgendetwas stimmt mit {name} nicht.",
    "Ich habe Augen im Wald leuchten sehen …",
    "Lasst uns nicht vorschnell urteilen.",
};

const std::vector<std::string> kWolfLines = {
    "Ich nehme mir {name} vor.",
    "{name} ist allein unterwegs … perfekt.",
    "Vorsicht, beim Feuer sind zu viele.",
    "Wer übernimmt heute Nacht?",
};

const std::vector<std::string> kEndLines = {"Gut gespielt!", "GG!", "Noch eine Runde?", "Das war knapp!"};

std::string fill(std::string line, const std::string& name) {
    const std::string key = "{name}";
    if (auto pos = line.find(key); pos != std::string::npos) line.replace(pos, key.size(), name);
    return line;
}

bool isAliveNonWolf(const Match::Player& p) { return p.alive && p.role != Role::Werewolf; }
bool isAlive(const Match::Player& p) { return p.alive; }

} // namespace

Bot::Bot(uint8_t id, uint64_t seed) : m_rng(seed), m_id(id) {}

Vec2 Bot::steerTo(Vec2 target) const {
    const Vec2 d = target - Vec2{m_mover.position.x, m_mover.position.z};
    const float len = d.length();
    return len > 0.3f ? d / len : Vec2{0, 0};
}

uint8_t Bot::nearestPlayer(const Match& match, Vec3 from, bool (*filter)(const Match::Player&), float maxDist) const {
    uint8_t best = kNoPlayer;
    float bestDist = maxDist;
    for (const Match::Player& p : match.players()) {
        if (p.id == m_id || !filter(p)) continue;
        const float d = distanceXZ(from, p.position);
        if (d < bestDist) {
            bestDist = d;
            best = p.id;
        }
    }
    return best;
}

void Bot::chooseWanderTarget(const Match& match) {
    const World& w = match.world();
    std::vector<Vec2> spots = {{0, 9},
                               {w.well().x, w.well().z + 2.0f},
                               {w.bell().x + 2.0f, w.bell().z},
                               {44.0f, 3.0f},
                               {-44.0f, 6.0f},
                               {0.0f, -40.0f},
                               {14.0f, -50.0f},
                               {-3.0f, 30.0f}};
    for (const House& h : w.houses()) spots.push_back({h.door.x, h.door.z});
    Vec2 t = m_rng.pick(spots);
    t = t + Vec2{m_rng.uniform(-3.0f, 3.0f), m_rng.uniform(-3.0f, 3.0f)};
    m_target = w.resolveCollision(t, 0.6f);
}

void Bot::onPhaseChange(const Match& match, const GameView& view) {
    m_voted = false;
    m_quarry = kNoPlayer;
    m_idle = m_rng.uniform(0.0f, 3.0f);
    m_think = m_rng.uniform(2.0f, 6.0f);
    m_chatTimer = -1.0f;
    m_transformDelay = m_rng.uniform(4.0f, 16.0f);
    const PlayerView* me = view.me();
    const bool alive = me && me->alive;
    if (view.phase == Phase::Day) {
        chooseWanderTarget(match);
        if (alive && m_rng.chance(0.35f)) m_chatTimer = m_rng.uniform(8.0f, 40.0f);
    } else if (view.phase == Phase::Assembly) {
        if (alive && m_rng.chance(0.6f)) m_chatTimer = m_rng.uniform(3.0f, 20.0f);
    } else if (view.phase == Phase::Night) {
        m_staysOut = m_rng.chance(0.35f);
        const float a = m_rng.uniform(0.0f, 2.0f * eng::kPi);
        m_target = {std::cos(a) * m_rng.uniform(3.0f, 6.5f), std::sin(a) * m_rng.uniform(3.0f, 6.5f)};
        if (m_staysOut) chooseWanderTarget(match);
    } else if (view.phase == Phase::GameOver) {
        if (m_rng.chance(0.3f)) m_chatTimer = m_rng.uniform(2.0f, 8.0f);
    }
}

void Bot::hear(const Match& match, uint8_t speaker, const std::string& text) {
    if (speaker == m_id) return;
    const Match::Player* me = match.find(m_id);
    const Match::Player* from = match.find(speaker);
    if (!me || !from) return;
    auto contains = [&](const char* word) { return text.find(word) != std::string::npos; };
    float weight = 0.4f;
    if (contains("Werwolf") || contains("werwolf") || contains("Wolf")) weight = 2.0f;
    else if (contains("traue") || contains("verdächtig") || contains("stimmt") || contains("Stimme geht") ||
             contains("gesehen") || contains("Leiche"))
        weight = 1.0f;
    if (contains("unschuldig") || contains("vertraue")) weight = -1.0f;

    for (const Match::Player& p : match.players()) {
        if (!p.alive || p.id == speaker || p.name.size() < 2 || text.find(p.name) == std::string::npos) continue;
        if (p.id == m_id) {
            // selbst beschuldigt: Misstrauen gegenüber dem Ankläger
            if (weight > 0.0f) m_suspicion[speaker] += weight * 0.6f;
            continue;
        }
        if (me->role == Role::Werewolf && p.role == Role::Werewolf) {
            // Wölfe lenken den Verdacht auf den Ankläger um
            if (weight > 0.0f) m_suspicion[speaker] += weight;
            continue;
        }
        m_suspicion[p.id] += weight * m_rng.uniform(0.5f, 1.0f);
    }
}

uint8_t Bot::chooseVote(const Match& match, const GameView& view) {
    std::vector<uint8_t> candidates;
    for (const Match::Player& p : match.players())
        if (p.alive && p.id != m_id) candidates.push_back(p.id);
    if (candidates.empty()) return kNoPlayer;
    // bekannte Werwölfe (Seherin) zuerst
    for (const SeerResult& r : view.seerResults) {
        const Match::Player* p = match.find(r.player);
        if (p && p->alive && r.role == Role::Werewolf && view.yourRole != Role::Werewolf) return r.player;
    }
    // stärkster Verdacht aus Beobachtungen und Gesprächen
    uint8_t suspect = kNoPlayer;
    float strongest = 1.0f;
    for (const auto& [id, value] : m_suspicion) {
        const Match::Player* p = match.find(id);
        if (!p || !p->alive || id == m_id) continue;
        if (view.yourRole == Role::Werewolf && p->role == Role::Werewolf) continue;
        if (value > strongest) {
            strongest = value;
            suspect = id;
        }
    }
    if (suspect != kNoPlayer && m_rng.chance(0.75f)) return suspect;
    std::map<uint8_t, int> tally;
    for (const PlayerView& p : view.players)
        if (p.vote != kNoPlayer && p.id != m_id) ++tally[p.vote];
    uint8_t leader = kNoPlayer;
    int best = 0;
    for (const auto& [id, count] : tally) {
        const PlayerView* t = view.player(id);
        if (!t || !t->alive || id == m_id) continue;
        if (view.yourRole == Role::Werewolf && t->role == Role::Werewolf) continue;
        if (count > best) {
            best = count;
            leader = id;
        }
    }
    if (leader != kNoPlayer && m_rng.chance(0.55f)) return leader;
    if (view.yourRole == Role::Werewolf) {
        std::vector<uint8_t> innocents;
        for (uint8_t id : candidates) {
            const PlayerView* t = view.player(id);
            if (t && t->role != Role::Werewolf) innocents.push_back(id);
        }
        if (!innocents.empty()) return m_rng.pick(innocents);
    }
    return m_rng.pick(candidates);
}

void Bot::update(float dt, Match& match, const SayFn& say) {
    const Match::Player* me = match.find(m_id);
    if (!me) return;
    const GameView view = match.viewFor(m_id);

    // Position mit dem Server abgleichen (z. B. nach einem Teleport zur Versammlung)
    const Vec3 serverPos = me->alive ? me->position : me->ghostPosition;
    if (!m_initialized || distanceXZ(m_mover.position, serverPos) > 1.0f) {
        m_mover.position = match.world().ground(serverPos.x, serverPos.z);
        m_mover.yaw = me->alive ? me->yaw : me->ghostYaw;
        m_mover.velocity = {0, 0};
        m_initialized = true;
    }
    if (view.phase != m_phase || view.day != m_day) {
        m_phase = view.phase;
        m_day = view.day;
        onPhaseChange(match, view);
    }

    // Augenzeuge: wer nachts in Werwolf-Gestalt gesehen wird, ist verdächtig
    if (me->alive && view.phase == Phase::Night) {
        for (const Match::Player& p : match.players()) {
            if (p.id == m_id || !p.alive || !p.wolfForm() || m_seenAsWolf[p.id] == view.day) continue;
            if (distanceXZ(p.position, me->position) > 9.0f ||
                match.world().lineBlockedByBuildings({p.position.x, p.position.z}, {me->position.x, me->position.z}))
                continue;
            m_seenAsWolf[p.id] = view.day;
            // in der Dunkelheit erkennt man nicht jedes Gesicht
            if (me->role != Role::Werewolf && m_rng.chance(0.55f)) m_suspicion[p.id] += 5.0f;
        }
    }

    if (m_chatTimer > 0.0f) {
        m_chatTimer -= dt;
        if (m_chatTimer <= 0.0f) {
            // stärksten Verdacht aussprechen
            uint8_t suspect = kNoPlayer;
            float strongest = 1.5f;
            for (const auto& [id, value] : m_suspicion) {
                const Match::Player* p = match.find(id);
                if (p && p->alive && value > strongest) {
                    strongest = value;
                    suspect = id;
                }
            }
            if (view.phase == Phase::GameOver) {
                say(m_id, m_rng.pick(kEndLines), false);
            } else if (me->alive && suspect != kNoPlayer && m_rng.chance(0.7f)) {
                const std::string& name = match.find(suspect)->name;
                say(m_id, strongest >= 4.0f ? "Ich habe " + name + " heute Nacht als Werwolf gesehen!"
                                            : "Ich bin mir sicher: " + name + " ist ein Werwolf!",
                    false);
            } else if (me->alive) {
                std::vector<std::string> names;
                for (const Match::Player& p : match.players())
                    if (p.alive && p.id != m_id) names.push_back(p.name);
                const std::string name = names.empty() ? "irgendwer" : m_rng.pick(names);
                bool spoke = false;
                if (view.yourRole == Role::Seer) {
                    for (const SeerResult& r : view.seerResults) {
                        const PlayerView* p = view.player(r.player);
                        if (p && p->alive && r.role == Role::Werewolf && m_rng.chance(0.7f)) {
                            say(m_id, "Ich bin die Seherin! " + p->name + " ist ein Werwolf!", false);
                            spoke = true;
                            break;
                        }
                    }
                }
                if (!spoke) say(m_id, fill(m_rng.pick(kDayLines), name), false);
            }
        }
    }

    Vec2 wish{0, 0};
    float speed = kWalkSpeed * 0.8f;
    act(dt, match, view, say, wish, speed);

    // Hängen geblieben? Neues Ziel wählen
    m_stuckTimer += dt;
    if (m_stuckTimer > 2.5f) {
        const Vec2 pos{m_mover.position.x, m_mover.position.z};
        if (wish.length() > 0.1f && (pos - m_stuckPos).length() < 0.7f) {
            chooseWanderTarget(match);
            m_quarry = kNoPlayer;
        }
        m_stuckPos = pos;
        m_stuckTimer = 0.0f;
    }

    const bool ghost = !me->alive;
    m_mover.move(wish, ghost ? kGhostSpeed * 0.5f : speed, dt, match.world(), !ghost);
    match.movePlayer(m_id, m_mover.position.x, m_mover.position.z, m_mover.yaw, speed > kWalkSpeed ? EntitySprint : 0);
}

void Bot::act(float dt, Match& match, const GameView& view, const SayFn& say, Vec2& wish, float& speed) {
    const Match::Player& me = *match.find(m_id);
    const Vec2 pos{m_mover.position.x, m_mover.position.z};

    // Letzter Schuss des Jägers
    if (view.can(AbilityHunterShot) && !m_shot) {
        m_shotDelay += dt;
        if (m_shotDelay > 3.0f) {
            uint8_t target = kNoPlayer;
            for (const SeerResult& r : view.seerResults)
                if (r.role == Role::Werewolf && match.checkInteraction(m_id, Interaction::HunterShot, r.player).empty()) target = r.player;
            if (target == kNoPlayer) {
                std::vector<uint8_t> options;
                for (const Match::Player& p : match.players())
                    if (match.checkInteraction(m_id, Interaction::HunterShot, p.id).empty()) options.push_back(p.id);
                if (!options.empty()) target = m_rng.pick(options);
            }
            if (target != kNoPlayer) match.interact(m_id, Interaction::HunterShot, target);
            m_shot = true;
        }
    }
    if (!me.alive) return; // Geister schweben einfach umher

    switch (view.phase) {
    case Phase::Day: {
        if (m_idle > 0.0f) {
            m_idle -= dt;
            break;
        }
        if ((m_target - pos).length() < 1.2f) {
            m_idle = m_rng.uniform(3.0f, 12.0f);
            chooseWanderTarget(match);
            break;
        }
        wish = steerTo(m_target);
        break;
    }
    case Phase::Assembly:
    case Phase::Execution:
    case Phase::Morning:
        break;
    case Phase::Vote:
        m_think -= dt;
        if (!m_voted && m_think <= 0.0f) {
            const uint8_t target = chooseVote(match, view);
            if (target != kNoPlayer) {
                match.interact(m_id, Interaction::Vote, target);
                if (m_rng.chance(0.3f)) say(m_id, "Meine Stimme geht an " + view.player(target)->name + ".", false);
            }
            m_voted = true;
        }
        break;
    case Phase::Night:
        nightRole(dt, match, view, me, say, wish, speed);
        break;
    default:
        break;
    }
}

void Bot::nightRole(float dt, Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish,
                    float& speed) {
    const Vec2 pos{m_mover.position.x, m_mover.position.z};
    auto goTowards = [&](Vec2 target, float stopDist) {
        if ((target - pos).length() > stopDist) wish = steerTo(target);
    };

    if (me.role == Role::Werewolf) {
        m_transformDelay -= dt;
        if (view.can(AbilityTransform) && me.transformTarget < 0.5f && view.packKillsLeft > 0 && m_transformDelay <= 0.0f) {
            // nur verwandeln, wenn niemand Fremdes zusieht
            bool watched = false;
            for (const Match::Player& p : match.players())
                if (p.alive && p.id != m_id && p.role != Role::Werewolf && distanceXZ(p.position, me.position) < 9.0f) watched = true;
            if (!watched || m_transformDelay < -25.0f) {
                match.interact(m_id, Interaction::Transform, 1);
                if (m_rng.chance(0.5f)) {
                    const uint8_t prey = nearestPlayer(match, me.position, isAliveNonWolf, 200.0f);
                    if (prey != kNoPlayer) say(m_id, fill(m_rng.pick(kWolfLines), match.find(prey)->name), true);
                }
            } else {
                // vom Feuer weg in den Schatten
                goTowards(pos.length() < 1.0f ? Vec2{20, 0} : pos * (22.0f / std::max(pos.length(), 1.0f)), 1.0f);
            }
        }
        if (me.wolfForm()) {
            if (view.packKillsLeft == 0) {
                match.interact(m_id, Interaction::Transform, 0);
                return;
            }
            // Beute: möglichst allein stehend
            if (m_quarry == kNoPlayer || !match.find(m_quarry)->alive) {
                float bestScore = 1e9f;
                for (const Match::Player& p : match.players()) {
                    if (!isAliveNonWolf(p)) continue;
                    int company = 0;
                    for (const Match::Player& o : match.players())
                        if (o.alive && o.id != p.id && distanceXZ(o.position, p.position) < 8.0f) ++company;
                    const float score = distanceXZ(p.position, me.position) + static_cast<float>(company) * 25.0f;
                    if (score < bestScore) {
                        bestScore = score;
                        m_quarry = p.id;
                    }
                }
            }
            if (const Match::Player* q = match.find(m_quarry)) {
                const Vec2 qp{q->position.x, q->position.z};
                speed = kWolfSpeed;
                wish = steerTo(qp);
                if ((qp - pos).length() < kAttackRange) match.interact(m_id, Interaction::Attack, m_quarry);
            }
            return;
        }
    }

    // Rollen mit nächtlichen Fähigkeiten
    m_think -= dt;
    if (m_think <= 0.0f) {
        m_think = m_rng.uniform(1.0f, 3.0f);
        if (view.can(AbilityInspect)) {
            for (const Match::Player& p : match.players()) {
                const bool known = std::any_of(view.seerResults.begin(), view.seerResults.end(), [&](const SeerResult& r) { return r.player == p.id; });
                if (!known && match.checkInteraction(m_id, Interaction::Inspect, p.id).empty()) {
                    match.interact(m_id, Interaction::Inspect, p.id);
                    break;
                }
            }
        }
        if (view.can(AbilityProtect)) {
            const uint8_t near = nearestPlayer(match, me.position, isAlive, kProtectRange);
            const uint8_t target = (near != kNoPlayer && m_rng.chance(0.7f)) ? near : m_id;
            match.interact(m_id, Interaction::Protect, target);
        }
        if (view.can(AbilityCupid)) {
            const uint8_t near = nearestPlayer(match, me.position, isAlive, kCupidRange);
            match.interact(m_id, Interaction::CupidPick, near != kNoPlayer ? near : m_id);
        }
        if (view.can(AbilityHeal)) {
            for (const Match::Player& p : match.players()) {
                if (p.alive || p.buried || !p.diedAtNight || p.deathDay != view.day) continue;
                if (match.checkInteraction(m_id, Interaction::Heal, p.id).empty()) {
                    if (m_rng.chance(0.75f)) match.interact(m_id, Interaction::Heal, p.id);
                } else if (distanceXZ(p.deathPosition, me.position) < 25.0f) {
                    m_target = {p.deathPosition.x, p.deathPosition.z};
                    m_staysOut = true;
                }
            }
        }
        if (view.can(AbilityPoison) && m_rng.chance(0.03f)) {
            const uint8_t near = nearestPlayer(match, me.position, isAlive, kPoisonRange);
            if (near != kNoPlayer) match.interact(m_id, Interaction::Poison, near);
        }
    }

    // Bewegung: die meisten bleiben im Licht am Feuer, manche streifen umher
    if (m_staysOut) {
        if ((m_target - pos).length() < 1.2f) {
            m_idle = m_rng.uniform(2.0f, 8.0f);
            chooseWanderTarget(match);
        }
        if (m_idle > 0.0f) m_idle -= dt;
        else goTowards(m_target, 1.0f);
    } else {
        goTowards(m_target, 0.8f);
    }
}

} // namespace ww
