#include "server/Bot.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>

namespace ww {

namespace {

const std::vector<Text> kDayLines = {
    {"Guten Morgen … oder auch nicht.", "Good morning … or not."},
    {"Hat jemand etwas Verdächtiges beobachtet?", "Has anyone observed anything suspicious?"},
    {"{name}, wo warst du gestern Nacht?", "{name}, where were you last night?"},
    {"Nicht jede Behauptung ist schon ein Beweis.", "Not every claim is proof."},
    {"{name}, hast du einen konkreten Hinweis?", "{name}, do you have a specific lead?"},
    {"Wir müssen zusammenhalten, sonst holen sie uns alle.", "We have to stick together, or they'll get us all."},
    {"Hat jemand {name} heute Nacht gesehen?", "Did anyone see {name} last night?"},
    {"Ich möchte Beobachtungen und Vermutungen auseinanderhalten.", "I'd like to distinguish observations from guesses."},
    {"Wer kann ein Alibi bestätigen?", "Who can confirm an alibi?"},
    {"Lasst uns nicht vorschnell urteilen.", "Let's not judge too quickly."},
    {"Verriegelt heute Nacht eure Türen!", "Bolt your doors tonight!"},
    {"{name}, was spricht für deinen Verdacht?", "{name}, what supports your suspicion?"},
};

const std::vector<Text> kWolfLines = {
    {"Ich nehme mir {name} vor.", "I'll take {name}."},
    {"{name} ist allein unterwegs … perfekt.", "{name} is out alone … perfect."},
    {"Vorsicht, beim Feuer sind zu viele.", "Careful, there are too many at the fire."},
    {"Wer übernimmt heute Nacht?", "Who's taking tonight?"},
    {"{name} hat sich eingeschlossen – die Tür hält nicht lange.", "{name} has locked themselves in – the door won't hold for long."},
};

const std::vector<Text> kJesterLines = {
    {"Vielleicht war ich es ja … wer weiß?", "Maybe it was me … who knows?"},
    {"Ich würde mich an eurer Stelle hängen.", "If I were you, I'd hang me."},
    {"Hehe. Ich sage nichts mehr ohne meinen Anwalt.", "Hehe. I'm not saying anything without my lawyer."},
    {"Ich habe ein ganz schlechtes Gewissen, aber das hat nichts zu bedeuten.", "I have a really guilty conscience, but that means nothing."},
};

const std::vector<Text> kEndLines = {
    {"Gut gespielt!", "Well played!"}, {"GG!", "GG!"}, {"Noch eine Runde?", "One more round?"}, {"Das war knapp!", "That was close!"}};

Text fill(Text line, const std::string& name) {
    const std::string key = "{name}";
    for (std::string* s : {&line.de, &line.en})
        if (auto pos = s->find(key); pos != std::string::npos) s->replace(pos, key.size(), name);
    return line;
}

bool isAliveNonWolf(const Match::Player& p) { return p.alive && !isWolfRole(p.role); }
bool isAlive(const Match::Player& p) { return p.alive; }

// Welt -> lokale Hauskoordinaten (Umkehrung von World::houseToWorld)
Vec2 toLocal(const House& h, Vec2 p) {
    const Vec2 d = p - Vec2{h.position.x, h.position.z};
    const float c = std::cos(h.yaw), s = std::sin(h.yaw);
    return {c * d.x - s * d.y, s * d.x + c * d.y};
}

bool doorPassable(const DoorState& d) { return d.flags & (DoorOpen | DoorBroken | DoorBurnt); }
bool isNeutralKillerRole(Role r) { return r == Role::SerialKiller || r == Role::Arsonist; }

} // namespace

Bot::Bot(uint8_t id, uint64_t seed) : m_rng(seed), m_id(id) {}

Vec2 Bot::steerTo(Vec2 target) const {
    const Vec2 d = target - position();
    const float len = d.length();
    return len > 0.3f ? d / len : Vec2{0, 0};
}

uint8_t Bot::nearestPlayer(const Match& match, Vec3 from, bool (*filter)(const Match::Player&), float maxDist) const {
    uint8_t best = kNoPlayer;
    float bestDist = maxDist;
    for (const Match::Player& p : match.players()) {
        if (p.id == m_id || !filter(p)) continue;
        const float d = distanceXZ(from, p.position);
        if (d < bestDist && match.hasLineOfSight(from, p.position)) {
            bestDist = d;
            best = p.id;
        }
    }
    return best;
}

uint8_t Bot::strongestSuspect(const Match& match, float threshold) const {
    uint8_t suspect = kNoPlayer;
    float strongest = threshold;
    const auto view = match.viewFor(m_id);
    for (const auto& player : view.players) {
        const uint8_t id = player.id;
        const float value = !isWolfRole(view.yourRole) && isWolfRole(player.role)
            ? std::max(8.0f, suspicion(match, id)) : suspicion(match, id);
        if (isWolfRole(view.yourRole) && isWolfRole(player.role)) continue;
        if (player.role != Role::Unknown && !isWolfRole(player.role)) continue;
        if (player.alive && id != m_id && value > strongest) {
            strongest = value;
            suspect = id;
        }
    }
    return suspect;
}

void Bot::chooseWanderTarget(const Match& match) {
    const World& w = match.world();
    // Manche setzen sich eine Weile in eine Kirchenbank
    if (const int chapelHouse = w.chapelIndex(); chapelHouse >= 0 && m_rng.chance(0.12f)) {
        const int gap = std::min(static_cast<int>(m_rng.uniform(0.0f, static_cast<float>(chapel::kPewRows - 1))), chapel::kPewRows - 2);
        const float side = m_rng.chance(0.5f) ? -1.0f : 1.0f;
        m_target = w.houseToWorld(chapelHouse, {side * m_rng.uniform(1.3f, 2.6f), chapel::pewZ(gap) + chapel::kPewSpacing * 0.5f});
        return;
    }
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
    t = w.resolveCollision(t, 0.6f);
    if (w.houseAt(t, 0.5f) >= 0) t = spots.front(); // nicht versehentlich in ein Haus
    m_target = t;
}

void Bot::onPhaseChange(const Match& match, const GameView& view) {
    m_voted = false;
    m_quarry = kNoPlayer;
    m_spared.clear();
    m_douseHouse = -1;
    m_phaseTime = 0.0f;
    m_idle = m_rng.uniform(0.0f, 3.0f);
    m_think = m_rng.uniform(2.0f, 6.0f);
    m_chatTimer = -1.0f;
    m_transformDelay = m_rng.uniform(4.0f, 16.0f);
    m_goHome = false;
    m_reply.reset();
    m_dialogue.observeRevealed(view);
    m_wantsSteal = m_rng.chance(0.7f);
    const PlayerView* me = view.me();
    const bool alive = me && me->alive;
    if (view.phase == Phase::Day) {
        chooseWanderTarget(match);
        m_reported = false;
        if (alive && m_rng.chance(0.35f)) m_chatTimer = m_rng.uniform(8.0f, 40.0f);
    } else if (view.phase == Phase::Assembly) {
        if (alive && m_rng.chance(0.6f)) m_chatTimer = m_rng.uniform(3.0f, 20.0f);
        // Der Bär brummt öffentlich, wenn ein Sitznachbar des Bärenführers ein Werwolf ist
        std::vector<const Match::Player*> seated;
        for (const Match::Player& p : match.players())
            if (p.alive) seated.push_back(&p);
        const size_t n = seated.size();
        for (size_t i = 0; i < n && n > 2 && alive; ++i) {
            if (seated[i]->role != Role::BearTamer) continue;
            const Match::Player* left = seated[(i + n - 1) % n];
            const Match::Player* right = seated[(i + 1) % n];
            if (!isWolfRole(left->role) && !isWolfRole(right->role)) continue;
            for (const Match::Player* s : {left, right})
                if (s->id != m_id && !isWolfRole(view.yourRole)) m_suspicion[s->id] += 1.5f;
        }
    } else if (view.phase == Phase::Night) {
        const float roll = m_rng.uniform(0.0f, 1.0f);
        m_goHome = roll < 0.45f;
        m_staysOut = roll >= 0.85f;
        const float a = m_rng.uniform(0.0f, 2.0f * eng::kPi);
        m_target = {std::cos(a) * m_rng.uniform(3.0f, 6.5f), std::sin(a) * m_rng.uniform(3.0f, 6.5f)};
        if (m_staysOut) chooseWanderTarget(match);
    } else if (view.phase == Phase::GameOver) {
        if (m_rng.chance(0.3f)) m_chatTimer = m_rng.uniform(2.0f, 8.0f);
    }
}

uint8_t Bot::chooseCaptain(const Match& match, const GameView& view) {
    const bool wolf = isWolfRole(view.yourRole);
    std::vector<uint8_t> trusted, others;
    for (const Match::Player& p : match.players()) {
        if (!p.alive || p.id == m_id) continue;
        const PlayerView* pv = view.player(p.id);
        if (wolf && pv && isWolfRole(pv->role)) trusted.push_back(p.id); // das Rudel hält zusammen
        else if (!wolf && suspicion(match, p.id) <= 0.5f) trusted.push_back(p.id);
        else others.push_back(p.id);
    }
    if (!trusted.empty()) return m_rng.pick(trusted);
    return others.empty() ? kNoPlayer : m_rng.pick(others);
}

uint8_t Bot::chooseVote(const Match& match, const GameView& view) {
    const bool wolf = isWolfRole(view.yourRole);
    std::vector<uint8_t> candidates;
    for (const Match::Player& p : match.players())
        if (p.alive && p.id != m_id) candidates.push_back(p.id);
    if (candidates.empty()) return kNoPlayer;
    // bekannte Werwölfe (Seherin) zuerst
    for (const SeerResult& r : view.seerResults) {
        const Match::Player* p = match.find(r.player);
        if (p && p->alive && isWolfRole(r.role) && !wolf) return r.player;
    }
    // stärkster Verdacht aus Beobachtungen und Gesprächen
    const uint8_t suspect = strongestSuspect(match, 1.0f);
    if (suspect != kNoPlayer && m_rng.chance(0.75f)) return suspect;
    if (const Match::Player* me = match.find(m_id); me && me->role == Role::Sectarian) {
        std::vector<uint8_t> enemies;
        for (const Match::Player& p : match.players())
            if (p.alive && p.id != m_id && match.sectOf(p) != match.sectOf(*me)) enemies.push_back(p.id);
        if (!enemies.empty()) return m_rng.pick(enemies);
    }
    std::map<uint8_t, int> tally;
    for (const PlayerView& p : view.players)
        if (p.vote != kNoPlayer && p.id != m_id) ++tally[p.vote];
    uint8_t leader = kNoPlayer;
    int best = 0;
    for (const auto& [id, count] : tally) {
        const PlayerView* t = view.player(id);
        if (!t || !t->alive || id == m_id) continue;
        if (wolf && isWolfRole(t->role)) continue;
        if (count > best) {
            best = count;
            leader = id;
        }
    }
    if (leader != kNoPlayer && m_rng.chance(0.55f)) return leader;
    std::vector<uint8_t> options;
    for (uint8_t id : candidates) {
        const PlayerView* t = view.player(id);
        if (!t) continue;
        if (wolf && isWolfRole(t->role)) continue;
        if (!wolf && t->role != Role::Unknown && !isWolfRole(t->role)) continue;
        options.push_back(id);
    }
    // Prefer a neutral candidate over somebody credibly defended, without treating an alibi as proof.
    std::vector<uint8_t> unexonerated;
    for (uint8_t id : options)
        if (suspicion(match, id) >= -0.25f) unexonerated.push_back(id);
    if (!unexonerated.empty()) return m_rng.pick(unexonerated);
    return options.empty() ? m_rng.pick(candidates) : m_rng.pick(options);
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
    m_phaseTime += dt;
    updateConversation(dt, match, say);

    // Augenzeuge: wer nachts als (halbe) Bestie gesehen wird, ist verdächtig
    if (me->alive && view.phase == Phase::Night) {
        for (const EntityState& p : match.entitiesFor(m_id)) {
            if (p.id == m_id || p.kind != EntityLiving || p.transform < 0.3f || m_seenAsWolf[p.id] == view.day) continue;
            const Vec3 pos{p.x, 0, p.z};
            if (distanceXZ(pos, me->position) > 12.0f || !match.hasLineOfSight(pos, me->position)) continue;
            m_seenAsWolf[p.id] = view.day;
            // in der Dunkelheit erkennt man nicht jedes Gesicht
            if (!isWolfRole(me->role) && m_rng.chance(0.6f)) {
                m_suspicion[p.id] += 5.0f;
                m_wolfWitness[p.id] = view.day;
            }
        }
    }

    // Fährtenleser: Führte die Spur zu einer Leiche?
    if (me->alive && me->role == Role::Tracker && !m_reported && view.phase == Phase::Day && me->trackTarget != kNoPlayer &&
        !me->trackVictims.empty()) {
        m_reported = true;
        m_suspicion[me->trackTarget] += 6.0f;
        if (const Match::Player* t = match.find(me->trackTarget); t && t->alive) {
            say(m_id,
                tx("Ich bin der Fährtenleser! Die Spur von {} führte heute Nacht direkt zur Leiche von {}!",
                   "I'm the tracker! The trail of {} led straight to the body of {} last night!", t->name,
                   match.find(me->trackVictims.front())->name),
                false);
        }
    }

    if (m_chatTimer > 0.0f && !m_reply && match.clock() >= m_replyAfter) {
        m_chatTimer -= dt;
        if (m_chatTimer <= 0.0f) {
            const uint8_t suspect = strongestSuspect(match, 1.5f);
            if (view.phase == Phase::GameOver) {
                say(m_id, m_rng.pick(kEndLines), false);
            } else if (me->alive && me->role == Role::Jester && m_rng.chance(0.6f)) {
                say(m_id, m_rng.pick(kJesterLines), false);
            } else if (me->alive && suspect != kNoPlayer && m_rng.chance(0.7f)) {
                say(m_id, explain(match, suspect), false);
            } else if (me->alive) {
                std::vector<std::string> names;
                for (const Match::Player& p : match.players())
                    if (p.alive && p.id != m_id) names.push_back(p.name);
                const std::string name = names.empty() ? "?" : m_rng.pick(names);
                bool spoke = false;
                if (view.yourRole == Role::Seer) {
                    for (const SeerResult& r : view.seerResults) {
                        const PlayerView* p = view.player(r.player);
                        if (p && p->alive && isWolfRole(r.role) && m_rng.chance(0.7f)) {
                            say(m_id, tx("Ich bin die Seherin! {} ist ein Werwolf!", "I'm the seer! {} is a werewolf!", p->name), false);
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
        const Vec2 pos = position();
        if (wish.length() > 0.1f && (pos - m_stuckPos).length() < 0.7f) {
            if (match.world().houseAt(pos) < 0) chooseWanderTarget(match);
            m_quarry = kNoPlayer;
            // seitlich ausweichen
            m_mover.velocity = Vec2{-wish.y, wish.x} * kWalkSpeed;
        }
        m_stuckPos = pos;
        m_stuckTimer = 0.0f;
    }

    const bool ghost = !me->alive;
    m_mover.move(wish, ghost ? kGhostSpeed * 0.5f : speed, dt, match.world(), !ghost, match.closedDoors());
    match.movePlayer(m_id, m_mover.position.x, m_mover.position.z, m_mover.yaw, speed > kWalkSpeed ? EntitySprint : 0);
}

Bot::Nav Bot::navigate(Match& match, const GameView& view, Vec2 target, float stopDist, Vec2& wish, bool breakDoors) {
    const World& w = match.world();
    const Vec2 pos = position();
    const int here = w.houseAt(pos, 0.15f);
    const int there = w.houseAt(target, 0.15f);
    if (here == there) {
        if ((target - pos).length() <= stopDist) return Nav::Arrived;
        wish = steerTo(here >= 0 ? w.routeInside(here, pos, target) : target);
        return Nav::Moving;
    }
    // Erst durch die Tür des Hauses, das wir verlassen bzw. betreten
    const int house = here >= 0 ? here : there;
    const House& h = w.houses()[static_cast<size_t>(house)];
    const Vec2 local = toLocal(h, pos);
    const float hd = h.depth * 0.5f;
    const bool aligned = std::fabs(local.x) < h.doorWidth * 0.5f - 0.15f;
    const Vec2 outside{h.door.x, h.door.z};
    const Vec2 inside{h.doorInside.x, h.doorInside.z};
    const float doorStep = toLocal(h, outside).y; // vor der Kapelle liegt der Türpunkt hinter der Treppe
    Vec2 waypoint;
    if (here >= 0) waypoint = (aligned && local.y > hd - 1.15f) ? outside : w.routeInside(here, pos, inside);
    else waypoint = (aligned && local.y < doorStep + 0.15f && local.y > hd - 0.6f) ? inside : outside;

    if (static_cast<size_t>(house) < match.doors().size() && !doorPassable(match.doors()[static_cast<size_t>(house)]) &&
        (w.doorCenter(house) - pos).length() < kDoorRange) {
        const uint8_t door = static_cast<uint8_t>(house);
        if (match.checkInteraction(m_id, Interaction::DoorToggle, door).empty()) {
            match.interact(m_id, Interaction::DoorToggle, door);
        } else if (breakDoors && view.can(AbilityBreakDoor)) {
            if (match.checkInteraction(m_id, Interaction::DoorBreak, door).empty())
                match.interact(m_id, Interaction::DoorBreak, door);
        } else {
            return Nav::Blocked;
        }
        wish = steerTo(here >= 0 ? inside : outside) * 0.3f;
        return Nav::Moving;
    }
    wish = steerTo(waypoint);
    return Nav::Moving;
}

void Bot::goHome(Match& match, const GameView& view, const Match::Player& me, Vec2& wish) {
    const World& w = match.world();
    const int house = me.house;
    if (house == kNoPlayer || static_cast<size_t>(house) >= match.doors().size() ||
        (match.doors()[static_cast<size_t>(house)].flags & (DoorBurning | DoorBurnt))) {
        m_goHome = false;
        return;
    }
    const House& h = w.houses()[static_cast<size_t>(house)];
    const Vec2 rest = w.houseToWorld(house, {-h.width * 0.5f * 0.33f, h.depth * 0.5f * 0.3f});
    if (navigate(match, view, rest, 0.5f, wish, false) == Nav::Blocked) {
        m_goHome = false; // ausgesperrt: dann eben ans Feuer
        return;
    }
    // Tür hinter sich schließen, sobald alle Mitbewohner da sind (oder es zu spät wird)
    const DoorState& d = match.doors()[static_cast<size_t>(house)];
    if (w.houseAt(position()) != house || !(d.flags & DoorOpen) || (d.flags & DoorBroken)) return;
    bool everyoneHome = true;
    for (const Match::Player& p : match.players())
        if (p.alive && p.id != m_id && p.house == me.house && w.houseAt({p.position.x, p.position.z}) != house) everyoneHome = false;
    if (!everyoneHome && m_phaseTime < 25.0f) return;
    const uint8_t door = static_cast<uint8_t>(house);
    if (match.checkInteraction(m_id, Interaction::DoorToggle, door).empty()) match.interact(m_id, Interaction::DoorToggle, door);
}

void Bot::act(float dt, Match& match, const GameView& view, const SayFn& say, Vec2& wish, float& speed) {
    const Match::Player& me = *match.find(m_id);
    const World& w = match.world();
    const Vec2 pos = position();

    // Letzter Schuss des Jägers
    if (view.can(AbilityHunterShot) && !m_shot) {
        m_shotDelay += dt;
        if (m_shotDelay > 3.0f) {
            uint8_t target = kNoPlayer;
            for (const SeerResult& r : view.seerResults)
                if (isWolfRole(r.role) && match.checkInteraction(m_id, Interaction::HunterShot, r.player).empty()) target = r.player;
            if (target == kNoPlayer) {
                const uint8_t suspect = strongestSuspect(match, 1.0f);
                if (suspect != kNoPlayer && match.checkInteraction(m_id, Interaction::HunterShot, suspect).empty()) target = suspect;
            }
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
    if (view.can(AbilityNameSuccessor)) {
        m_shotDelay += dt;
        if (m_shotDelay > 3.0f) {
            const uint8_t next = chooseCaptain(match, view);
            if (next != kNoPlayer) match.interact(m_id, Interaction::NameSuccessor, next);
        }
    }
    if (!me.alive) return; // Geister schweben einfach umher

    // Feuer! Raus aus dem Haus
    const int inHouse = w.houseAt(pos, 0.1f);
    if (inHouse >= 0 && static_cast<size_t>(inHouse) < match.doors().size() &&
        (match.doors()[static_cast<size_t>(inHouse)].flags & DoorBurning)) {
        const House& h = w.houses()[static_cast<size_t>(inHouse)];
        const Vec2 out = Vec2{h.door.x, h.door.z} + (Vec2{h.door.x, h.door.z} - Vec2{h.position.x, h.position.z}).normalized() * 2.5f;
        speed = kSprintSpeed;
        navigate(match, view, out, 0.5f, wish, false);
        return;
    }

    switch (view.phase) {
    case Phase::Day: {
        if (dayRole(match, view, me, wish)) break;
        if (m_idle > 0.0f) {
            m_idle -= dt;
            break;
        }
        const Nav nav = navigate(match, view, m_target, 1.2f, wish, false);
        if (nav != Nav::Moving) {
            m_idle = nav == Nav::Arrived ? m_rng.uniform(3.0f, 12.0f) : 0.0f;
            chooseWanderTarget(match);
        }
        break;
    }
    case Phase::Assembly:
    case Phase::Morning:
        break;
    case Phase::Execution:
        // Die ergebene Magd übernimmt manchmal die Rolle des Hingerichteten
        if (view.can(AbilityTakeRole) && m_phaseTime > 2.0f && !m_voted) {
            m_voted = true;
            if (m_rng.chance(0.35f)) match.interact(m_id, Interaction::TakeRole, view.condemned);
        }
        break;
    case Phase::Vote:
        m_think -= dt;
        if (!m_voted && m_think <= 0.0f) {
            m_voted = true;
            if (view.can(AbilitySecondVote) && m_rng.chance(0.2f)) match.interact(m_id, Interaction::SecondVote, 0);
            if (!view.can(AbilityVote)) break;
            const uint8_t target = view.election ? chooseCaptain(match, view) : chooseVote(match, view);
            if (target != kNoPlayer) {
                match.interact(m_id, Interaction::Vote, target);
                if (m_rng.chance(0.3f))
                    say(m_id,
                        view.election ? tx("Ich wähle {}.", "I choose {}.", view.player(target)->name)
                                      : tx("Meine Stimme geht an {}.", "My vote goes to {}.", view.player(target)->name),
                        false);
            }
        }
        break;
    case Phase::Night:
        nightRole(dt, match, view, me, say, wish, speed);
        break;
    default:
        break;
    }
}

bool Bot::dayRole(Match& match, const GameView& view, const Match::Player& me, Vec2& wish) {
    const World& w = match.world();
    // Schmied: eigene Tür beschlagen
    if (view.can(AbilityReinforce) && me.house != kNoPlayer && m_phaseTime > 6.0f) {
        const uint8_t door = me.house;
        if (match.checkInteraction(m_id, Interaction::Reinforce, door).empty()) {
            match.interact(m_id, Interaction::Reinforce, door);
            return false;
        }
        if (static_cast<size_t>(door) < match.doors().size() && !(match.doors()[door].flags & DoorBurnt)) {
            const House& h = w.houses()[door];
            navigate(match, view, {h.door.x, h.door.z}, 0.8f, wish, false);
            return true;
        }
    }
    // Pfarrer: den Hauptverdächtigen mit Weihwasser prüfen
    if (view.can(AbilityHolyWater) && m_phaseTime > 10.0f) {
        const uint8_t suspect = strongestSuspect(match, 4.0f);
        if (const Match::Player* t = match.find(suspect)) {
            if (match.checkInteraction(m_id, Interaction::HolyWater, suspect).empty()) {
                match.interact(m_id, Interaction::HolyWater, suspect);
                if (!isWolfRole(t->role)) m_suspicion[suspect] = 0.0f;
                return false;
            }
            navigate(match, view, {t->position.x, t->position.z}, 1.5f, wish, false);
            return true;
        }
    }
    return false;
}

bool Bot::hunt(Match& match, const GameView& view, const Match::Player& me, Vec2& wish, float& speed) {
    const bool killer = me.role == Role::SerialKiller;
    const Interaction strike = killer ? Interaction::Stab : Interaction::Attack;
    const bool betray = view.can(AbilityBetray);
    const bool feed = killer || view.can(AbilityAttack);
    auto prey = [&](const Match::Player& p) { return killer || (isWolfRole(p.role) ? betray : feed); };
    // Beute: möglichst allein und nicht hinter verriegelten Türen
    if (m_quarry == kNoPlayer || !match.find(m_quarry)->alive || m_spared.count(m_quarry)) {
        m_quarry = kNoPlayer;
        float bestScore = 1e9f;
        for (const Match::Player& p : match.players()) {
            if (!p.alive || p.id == m_id || m_spared.count(p.id) || !prey(p)) continue;
            int company = 0;
            for (const Match::Player& o : match.players())
                if (o.alive && o.id != p.id && o.id != m_id && distanceXZ(o.position, p.position) < 8.0f) ++company;
            float score = distanceXZ(p.position, me.position) + static_cast<float>(company) * 25.0f;
            const int h = match.world().houseAt({p.position.x, p.position.z});
            if (h >= 0 && static_cast<size_t>(h) < match.doors().size()) {
                const DoorState& d = match.doors()[static_cast<size_t>(h)];
                if (!doorPassable(d)) score += (d.flags & DoorReinforced) ? 45.0f : 20.0f;
            }
            if (score < bestScore) {
                bestScore = score;
                m_quarry = p.id;
            }
        }
    }
    const Match::Player* q = match.find(m_quarry);
    if (!q) return false;
    const Vec2 qp{q->position.x, q->position.z};
    speed = killer ? kSprintSpeed : kWolfSpeed;
    const Nav nav = navigate(match, view, qp, 0.0f, wish, true);
    if (nav == Nav::Blocked) {
        m_spared.insert(m_quarry);
        m_quarry = kNoPlayer;
        return true;
    }
    if ((qp - position()).length() < kAttackRange && match.checkInteraction(m_id, strike, m_quarry).empty()) {
        const uint8_t killsBefore = match.packKillsLeft();
        // Der Urwolf infiziert lieber, solange er kann
        const Interaction kind = view.can(AbilityInfect) && m_rng.chance(0.35f) ? Interaction::Infect : strike;
        match.interact(m_id, kind, m_quarry);
        const Match::Player* after = match.find(m_quarry);
        if (!killer && after && after->alive && match.packKillsLeft() == killsBefore) m_spared.insert(m_quarry);
        if (killer && after && after->alive) m_spared.insert(m_quarry);
        m_quarry = kNoPlayer;
    }
    return true;
}

void Bot::nightRole(float dt, Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish,
                    float& speed) {
    const World& w = match.world();
    const Vec2 pos = position();
    auto goTowards = [&](Vec2 target, float stopDist) { navigate(match, view, target, stopDist, wish, false); };

    if (isWolfRole(me.role)) {
        m_transformDelay -= dt;
        if (view.can(AbilityTransform) && me.transformTarget < 0.5f && view.packKillsLeft > 0 && m_transformDelay <= 0.0f) {
            // nur verwandeln, wenn niemand Fremdes zusieht
            bool watched = false;
            for (const Match::Player& p : match.players())
                if (p.alive && p.id != m_id && !isWolfRole(p.role) && distanceXZ(p.position, me.position) < 9.0f &&
                    match.hasLineOfSight(p.position, me.position))
                    watched = true;
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
            return;
        }
        if (me.wolfForm()) {
            if (view.packKillsLeft == 0 && !view.can(AbilityBetray)) {
                match.interact(m_id, Interaction::Transform, 0);
                m_goHome = true;
                return;
            }
            hunt(match, view, me, wish, speed);
            return;
        }
    }

    if (me.role == Role::SerialKiller && view.can(AbilityStab) && m_phaseTime > 12.0f) {
        if (hunt(match, view, me, wish, speed)) return;
    }

    // Dieb und Flötenspieler müssen nah an ihr Ziel heran
    if ((view.can(AbilitySteal) && m_wantsSteal) || view.can(AbilityEnchant)) {
        const Interaction kind = view.can(AbilitySteal) ? Interaction::Steal : Interaction::Enchant;
        const Match::Player* q = match.find(m_quarry);
        if (!q || !q->alive || (kind == Interaction::Enchant && q->enchanted) || m_spared.count(m_quarry)) {
            m_quarry = kNoPlayer;
            float best = 1e9f;
            for (const Match::Player& p : match.players()) {
                if (!p.alive || p.id == m_id || m_spared.count(p.id) || (kind == Interaction::Enchant && p.enchanted)) continue;
                const float d = distanceXZ(p.position, me.position);
                if (d < best) {
                    best = d;
                    m_quarry = p.id;
                }
            }
            q = match.find(m_quarry);
        }
        if (q && m_phaseTime > 6.0f) {
            if (match.checkInteraction(m_id, kind, q->id).empty()) {
                match.interact(m_id, kind, q->id);
                m_quarry = kNoPlayer;
            } else if (navigate(match, view, {q->position.x, q->position.z}, 1.5f, wish, false) == Nav::Blocked) {
                m_spared.insert(q->id);
            }
            return;
        }
    }

    if (me.role == Role::Arsonist) {
        const int doused = std::popcount(view.doused);
        if (view.can(AbilityIgnite) && doused >= 2 && view.timeLeft < view.phaseDuration * 0.55f) {
            match.interact(m_id, Interaction::Ignite, 0);
        } else if (view.can(AbilityDouse) && !(view.can(AbilityIgnite) && doused >= 2) && m_phaseTime > 8.0f) {
            if (m_douseHouse < 0) {
                std::vector<int> options;
                for (size_t i = 0; i < match.doors().size(); ++i)
                    if (static_cast<int>(i) != me.house && !w.houses()[i].chapel && !(view.doused & (1u << i)) &&
                        !(match.doors()[i].flags & (DoorBurning | DoorBurnt)))
                        options.push_back(static_cast<int>(i));
                if (!options.empty()) m_douseHouse = m_rng.pick(options);
            }
            if (m_douseHouse >= 0) {
                const uint8_t house = static_cast<uint8_t>(m_douseHouse);
                if (match.checkInteraction(m_id, Interaction::Douse, house).empty()) {
                    match.interact(m_id, Interaction::Douse, house);
                    m_douseHouse = -1;
                    m_goHome = true;
                } else {
                    const House& h = w.houses()[house];
                    goTowards({h.door.x, h.door.z}, 0.8f);
                    return;
                }
            }
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
        if (view.can(AbilitySniff)) {
            uint8_t target = strongestSuspect(match, 0.5f);
            if (target == kNoPlayer || !match.checkInteraction(m_id, Interaction::Sniff, target).empty())
                target = nearestPlayer(match, me.position, isAlive, kSniffRange);
            if (target != kNoPlayer && match.checkInteraction(m_id, Interaction::Sniff, target).empty()) {
                match.interact(m_id, Interaction::Sniff, target);
                if (match.find(m_id)->foxActive) m_suspicion[target] += 2.0f;
            }
        }
        if (view.can(AbilityMark)) {
            uint8_t target = strongestSuspect(match, 0.5f);
            if (target == kNoPlayer || !match.checkInteraction(m_id, Interaction::Mark, target).empty())
                target = nearestPlayer(match, me.position, isAlive, kMarkRange);
            if (target != kNoPlayer && match.checkInteraction(m_id, Interaction::Mark, target).empty())
                match.interact(m_id, Interaction::Mark, target);
        }
        if (view.can(AbilityChooseModel)) {
            const uint8_t near = nearestPlayer(match, me.position, isAlive, kModelRange);
            if (near != kNoPlayer) match.interact(m_id, Interaction::ChooseModel, near);
        }
        if (view.can(AbilityChooseSide)) match.interact(m_id, Interaction::ChooseSide, m_rng.chance(0.5f) ? 1 : 0);
        if (view.can(AbilityTrack)) {
            uint8_t target = strongestSuspect(match, 0.5f);
            if (target == kNoPlayer || !match.checkInteraction(m_id, Interaction::Track, target).empty())
                target = nearestPlayer(match, me.position, isAlive, kTrackRange);
            if (target != kNoPlayer && match.checkInteraction(m_id, Interaction::Track, target).empty())
                match.interact(m_id, Interaction::Track, target);
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
                    m_goHome = false;
                }
            }
        }
        if (view.can(AbilityPoison) && m_rng.chance(0.03f)) {
            const uint8_t near = nearestPlayer(match, me.position, isAlive, kPoisonRange);
            if (near != kNoPlayer) match.interact(m_id, Interaction::Poison, near);
        }
    }

    // Krach an einer Tür: wer draußen ist oder gleich nebenan wohnt, läuft hin und sieht nach
    const int inside = w.houseAt(pos);
    if (!isWolfRole(me.role) && !isNeutralKillerRole(me.role)) {
        for (size_t i = 0; i < match.doors().size(); ++i) {
            const DoorState& d = match.doors()[i];
            if (doorPassable(d) || d.health >= 100 || static_cast<int>(i) == inside) continue;
            const House& h = w.houses()[i];
            const Vec2 door{h.door.x, h.door.z};
            if ((door - pos).length() > (inside >= 0 ? 20.0f : 45.0f)) continue;
            m_goHome = false;
            m_staysOut = true;
            m_idle = 0.0f;
            m_target = door + Vec2{m_rng.uniform(-2.5f, 2.5f), m_rng.uniform(1.0f, 3.0f)};
            m_target = w.resolveCollision(m_target, 0.6f);
            speed = kSprintSpeed;
            break;
        }
    }

    // Bewegung: viele schließen sich zu Hause ein, andere bleiben am Feuer oder streifen umher
    if (m_goHome) {
        goHome(match, view, me, wish);
        if (m_goHome) return;
    }
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
