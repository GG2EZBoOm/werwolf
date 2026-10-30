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

Bot::Bot(uint8_t id, uint64_t seed, const Personality& personality)
    : m_rng(seed), m_id(id), m_personality(personality), m_salt(seed * 0x9E3779B97F4A7C15ull + id) {}

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
        const float value = !isEvilRole(view.yourRole) && isEvilRole(player.role)
            ? std::max(8.0f, suspicion(match, id)) : suspicion(match, id);
        if (ally(view, player.role)) continue;
        if (player.role != Role::Unknown && !isEvilRole(player.role)) continue;
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
    m_think = m_rng.uniform(2.0f, 6.0f) / m_personality.boldness; // Vorschnelle stimmen früh ab
    m_chatTimer = -1.0f;
    m_transformDelay = m_rng.uniform(4.0f, 16.0f);
    m_goHome = false;
    m_replies.clear();
    m_companion = kNoPlayer;
    m_hesitate = 0.0f;
    m_dialogue.observeRevealed(view);
    review(match, view);
    m_wantsSteal = m_rng.chance(0.7f);
    const PlayerView* me = view.me();
    const bool alive = me && me->alive;
    const float talk = m_personality.talk;
    const bool planned = std::any_of(m_planned.begin(), m_planned.end(), [&](const Planned& p) { return p.phase == view.phase; });
    if (view.phase == Phase::Day) {
        chooseWanderTarget(match);
        m_reported = false;
        if (alive && m_rng.chance(std::min(0.9f, 0.35f * talk))) m_chatTimer = m_rng.uniform(8.0f, 40.0f) / talk;
        // Neugier: manche sehen sich zuerst an, wo heute Nacht jemand gestorben ist
        for (const Match::Player& p : match.players()) {
            if (!alive || p.alive || !p.diedAtNight || !m_aliveAtNight.contains(p.id) || !m_rng.chance(0.6f - 0.4f * m_personality.caution))
                continue;
            m_target = match.world().resolveCollision(Vec2{p.deathPosition.x, p.deathPosition.z} +
                                                          Vec2{m_rng.uniform(-2.5f, 2.5f), m_rng.uniform(-2.5f, 2.5f)}, 0.6f);
            if (match.world().houseAt(m_target, 0.5f) >= 0) m_target = {p.deathPosition.x, p.deathPosition.z};
            m_idle = m_rng.uniform(0.0f, 1.5f);
            break;
        }
    } else if (view.phase == Phase::Assembly) {
        if (alive && (planned || m_rng.chance(std::min(0.95f, 0.6f * talk)))) m_chatTimer = m_rng.uniform(3.0f, 20.0f) / talk;
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
                if (s->id != m_id && !isEvilRole(view.yourRole)) m_suspicion[s->id] += 1.5f;
        }
    } else if (view.phase == Phase::Night) {
        // Vorsichtige schließen sich eher ein; erst ein Moment des Zögerns, dann los
        const float roll = m_rng.uniform(0.0f, 1.0f);
        m_goHome = roll < 0.25f + 0.4f * m_personality.caution + 0.35f * m_feelings.fear;
        m_staysOut = roll >= std::min(0.99f, 0.775f + 0.15f * m_personality.caution + 0.2f * m_feelings.fear);
        if (!isEvilRole(view.yourRole)) m_hesitate = m_rng.uniform(0.5f, 2.5f) * (0.6f + m_personality.caution);
        m_shouted = false;
        m_fleeFrom = kNoPlayer;
        const float a = m_rng.uniform(0.0f, 2.0f * eng::kPi);
        m_target = {std::cos(a) * m_rng.uniform(3.0f, 6.5f), std::sin(a) * m_rng.uniform(3.0f, 6.5f)};
        if (m_staysOut) chooseWanderTarget(match);
    } else if (view.phase == Phase::GameOver) {
        if (m_rng.chance(0.3f)) m_chatTimer = m_rng.uniform(2.0f, 8.0f);
    }
}

uint8_t Bot::chooseCaptain(const Match& match, const GameView& view) {
    const bool wolf = isEvilRole(view.yourRole);
    std::vector<uint8_t> trusted, others;
    for (const Match::Player& p : match.players()) {
        if (!p.alive || p.id == m_id) continue;
        const PlayerView* pv = view.player(p.id);
        if (wolf && pv && ally(view, pv->role)) trusted.push_back(p.id); // das Rudel hält zusammen
        else if (!wolf && suspicion(match, p.id) <= 0.5f) trusted.push_back(p.id);
        else others.push_back(p.id);
    }
    if (!trusted.empty()) {
        const auto best = std::max_element(trusted.begin(), trusted.end(), [&](uint8_t a, uint8_t b) { return relationship(a) < relationship(b); });
        if (relationship(*best) >= 0.4f && m_rng.chance(0.75f)) return *best;
        return m_rng.pick(trusted);
    }
    return others.empty() ? kNoPlayer : m_rng.pick(others);
}

uint8_t Bot::chooseVote(const Match& match, const GameView& view) {
    const bool wolf = isEvilRole(view.yourRole);
    std::vector<uint8_t> candidates;
    for (const Match::Player& p : match.players())
        if (p.alive && p.id != m_id) candidates.push_back(p.id);
    if (candidates.empty()) return kNoPlayer;
    if (wolf) if (const uint8_t sacrifice = sacrificeTarget(match); sacrifice != kNoPlayer) return sacrifice;
    // bekannte Werwölfe (Seherin) zuerst
    for (const SeerResult& r : view.seerResults) {
        const Match::Player* p = match.find(r.player);
        if (p && p->alive && isEvilRole(r.role) && !wolf) return r.player;
    }
    // stärkster Verdacht aus Beobachtungen und Gesprächen
    const uint8_t suspect = strongestSuspect(match, 1.0f / m_personality.boldness);
    // einem Seherergebnis oder Augenzeugen folgt man eher als einer vagen Ahnung; wer einen klaren
    // Verdacht hat und trotzdem schwankt, läuft dann aber nicht einfach der Mehrheit hinterher
    if (suspect != kNoPlayer && m_rng.chance(suspicion(match, suspect) >= 1.8f ? 0.9f : 0.75f)) return suspect;
    const bool ownMind = suspect != kNoPlayer;
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
        if (wolf && ally(view, t->role)) continue;
        if (!wolf && suspicion(match, id) < -0.25f) continue; // nicht hinter den Wölfen her gegen eine glaubwürdige Seherin
        if (count > best) {
            best = count;
            leader = id;
        }
    }
    if (leader != kNoPlayer && !ownMind && m_rng.chance(m_personality.follow)) return leader;
    std::vector<uint8_t> options;
    for (uint8_t id : candidates) {
        const PlayerView* t = view.player(id);
        if (!t) continue;
        if (wolf && ally(view, t->role)) continue;
        if (!wolf && t->role != Role::Unknown && !isEvilRole(t->role)) continue;
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
    updateFeelings(dt, match, view);
    if (view.phase != m_phase || view.day != m_day) {
        m_phase = view.phase;
        m_day = view.day;
        onPhaseChange(match, view);
    }
    m_phaseTime += dt;
    m_now = match.clock();
    updateConversation(dt, match, say);
    observe(dt, match, view);
    roleplay(match, say);
    speakPlanned(match, say);
    whisper(match, say);

    // Augenzeuge: wer nachts als (halbe) Bestie gesehen wird, ist verdächtig
    if (me->alive && view.phase == Phase::Night) {
        for (const EntityState& p : match.entitiesFor(m_id)) {
            if (p.id == m_id || p.kind != EntityLiving || p.transform < 0.3f || m_seenAsWolf[p.id] == view.day) continue;
            const Vec3 pos{p.x, 0, p.z};
            if (distanceXZ(pos, me->position) > 12.0f || !match.hasLineOfSight(pos, me->position)) continue;
            m_seenAsWolf[p.id] = view.day;
            // in der Dunkelheit erkennt man nicht jedes Gesicht – und manchmal hält man die Bestie für jemanden,
            // den man kurz vorher in der Nähe gesehen hat
            if (!isWolfRole(me->role) && m_rng.chance(0.6f)) {
                uint8_t seen = p.id;
                if (m_rng.chance(0.25f)) {
                    float nearest = 10.0f;
                    for (const auto& [other, positions] : m_sightings) {
                        const Match::Player* o = match.find(other);
                        if (other == p.id || !o || !o->alive) continue;
                        for (const Sight& s : positions)
                            if (const float d = (s.at - Vec2{p.x, p.z}).length(); d < nearest) { nearest = d; seen = other; }
                    }
                }
                m_suspicion[seen] += 5.0f;
                m_wolfWitness[seen] = view.day;
            }
        }
        // Rot glühende Augen in der Dunkelheit: aus der Nähe erkennt man, wer der Vampir ist
        for (const EntityState& p : match.entitiesFor(m_id)) {
            if (p.id == m_id || p.kind != EntityLiving || !(p.flags & EntityRedEyes) || isVampireRole(me->role) ||
                m_vampireWitness.contains(p.id)) continue;
            const Vec3 pos{p.x, 0, p.z};
            if (distanceXZ(pos, me->position) > 6.0f || !match.hasLineOfSight(pos, me->position) || !m_rng.chance(0.006f)) continue;
            m_suspicion[p.id] += 4.0f;
            m_vampireWitness[p.id] = view.day;
        }
        // Ein Wolf in Gestalt der Bestie merkt, wer ihn gesehen hat – und holt sich diesen Zeugen zuerst
        if (me->wolfForm())
            for (const Match::Player& p : match.players()) {
                if (!p.alive || p.id == m_id || isWolfRole(p.role) || m_seenAsWolf[p.id] == -view.day - 1) continue;
                if (distanceXZ(p.position, me->position) > 12.0f || !match.hasLineOfSight(p.position, me->position)) continue;
                m_seenAsWolf[p.id] = -view.day - 1;
                m_suspicion[p.id] += 4.0f;
                m_quarry = kNoPlayer;
            }
    }

    // Fährtenleser: Führte die Spur zu einer Leiche?
    if (me->alive && me->role == Role::Tracker && !m_reported && view.phase == Phase::Day && me->trackTarget != kNoPlayer &&
        !me->trackVictims.empty()) {
        m_reported = true;
        m_suspicion[me->trackTarget] += 6.0f;
        if (const Match::Player* t = match.find(me->trackTarget); t && t->alive) {
            speak(say,
                  tx("Ich bin der Fährtenleser! Die Spur von {} führte heute Nacht direkt zur Leiche von {}!",
                     "I'm the tracker! The trail of {} led straight to the body of {} last night!", t->name,
                     match.find(me->trackVictims.front())->name),
                  false, {Evidence{Evidence::Kind::Trail, t->id, 2.0f}});
        }
    }

    if (m_chatTimer > 0.0f && m_replies.empty() && match.clock() >= m_replyAfter) {
        m_chatTimer -= dt;
        if (m_chatTimer <= 0.0f) {
            const uint8_t suspect = strongestSuspect(match, 1.5f);
            if (view.phase == Phase::GameOver) {
                speak(say, m_rng.pick(kEndLines), false);
            } else if (me->alive && me->role == Role::Jester && m_rng.chance(0.6f)) {
                speak(say, m_rng.pick(kJesterLines), false);
            } else if (me->alive && view.phase == Phase::Assembly && view.day >= 2 && !m_offered && m_publicRole == Role::Unknown &&
                       (view.yourRole == Role::Protector || packSpeaker(match)) && m_dialogue.otherClaimant(Role::Seer, m_id) == kNoPlayer &&
                       dailyRoll(view.day, 3) < (view.yourRole == Role::Protector ? 0.25f : 0.2f * m_personality.boldness)) {
                // Der Beschützer bietet der Seherin Schutz an – oder ein Wolf stellt ihr genau damit eine Falle
                m_offered = true;
                m_publicRole = Role::Protector;
                Speech offer;
                offer.evidence = Evidence{Evidence::Kind::RoleClaim, m_id, -0.5f, Role::Protector};
                offer.protectOffer = true;
                speak(say, {"Ich bin der Beschützer. Seherin, sag mir heimlich, wer du bist – dann passe ich heute Nacht auf dich auf.",
                            "I'm the protector. Seer, tell me in secret who you are – then I'll watch over you tonight."},
                      false, offer);
            } else if (me->alive && view.phase == Phase::Assembly && m_publicRole == Role::Unknown && !volunteerRole(match, nullptr).empty()) {
                // Von sich aus die Rolle nennen, wo es Sinn ergibt (Jäger, Dorfälteste …) – Wölfe und Vampire lügen dabei
                std::optional<Evidence> claim;
                const Text line = volunteerRole(match, &claim);
                m_publicRole = claim->role;
                speak(say, line, false, {claim});
            } else if (me->alive && view.phase == Phase::Assembly && view.day >= 2 && packSpeaker(match) && m_fakeReportDay != view.day &&
                       (m_claimedSeer || (!packClaimedSeer(match) && m_rng.chance(0.55f * m_personality.boldness)))) {
                // Ein Wolf gibt sich als Seherin aus: belastet einen Unschuldigen oder entlastet einen bedrängten Gefährten
                m_fakeReportDay = view.day;
                const bool first = !m_claimedSeer;
                m_claimedSeer = true;
                uint8_t mate = kNoPlayer;
                for (const auto& p : view.players)
                    if (p.alive && p.id != m_id && ally(view, p.role) && m_dialogue.suspicion(p.id, view.day) > 1.0f &&
                        p.id != sacrificeTarget(match)) mate = p.id;
                if (const Match::Player* m = match.find(mate); m && !first) {
                    speak(say, tx("Ich habe {} geprüft: kein Werwolf.", "I checked {}: not a werewolf.", m->name), false,
                          {Evidence{Evidence::Kind::Seer, mate, dialogue::kSeerGood}});
                } else if (const Match::Player* v = match.find(scapegoat(match, kNoPlayer))) {
                    speak(say,
                          first ? tx("Ich bin die Seherin! {} ist ein Werwolf!", "I'm the seer! {} is a werewolf!", v->name)
                                : tx("Ich habe {} geprüft: Werwolf.", "I checked {}: werewolf.", v->name),
                          false, {Evidence{Evidence::Kind::Seer, v->id, dialogue::kSeerWolf}});
                }
            } else if (me->alive && suspect != kNoPlayer && match.clock() - m_heardTopic[suspect] > 25.0 && m_rng.chance(0.7f)) {
                // nicht wiederholen, was ein anderer gerade über dieselbe Person gesagt hat
                std::optional<Evidence> evidence;
                const Text line = explain(match, suspect, &evidence);
                speak(say, line, false, {evidence, false, suspect});
            } else if (me->alive) {
                std::vector<std::string> names;
                for (const Match::Player& p : match.players())
                    if (p.alive && p.id != m_id) names.push_back(p.name);
                const std::string name = names.empty() ? "?" : m_rng.pick(names);
                bool spoke = false;
                if (view.yourRole == Role::Seer) {
                    for (const SeerResult& r : view.seerResults) {
                        const PlayerView* p = view.player(r.player);
                        if (p && p->alive && isEvilRole(r.role) && shouldReveal(match, r.player) && m_rng.chance(0.7f)) {
                            speak(say,
                                  isVampireRole(r.role) ? tx("Ich bin die Seherin! {} ist ein Vampir!", "I'm the seer! {} is a vampire!", p->name)
                                                        : tx("Ich bin die Seherin! {} ist ein Werwolf!", "I'm the seer! {} is a werewolf!", p->name),
                                  false, {Evidence{Evidence::Kind::Seer, r.player, 2.4f}});
                            spoke = true;
                            break;
                        }
                    }
                }
                // Plaudern stört eine laufende Diskussion; "Guten Morgen" passt nur tagsüber
                const bool discussing = match.clock() - m_lastDiscussion < 30.0 ||
                    std::any_of(m_planned.begin(), m_planned.end(), [&](const Planned& p) { return p.phase == view.phase; });
                if (!spoke && !discussing) {
                    const int first = view.phase == Phase::Day ? 0 : 1;
                    speak(say, fill(kDayLines[static_cast<size_t>(m_rng.range(first, static_cast<int>(kDayLines.size()) - 1))], name), false);
                }
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
    if (!ghost) faceSomeone(dt, match); // stehend dreht man sich zu dem, der gerade spricht
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

bool Bot::flee(Match& match, const GameView& view, const Match::Player& me, const SayFn& say, Vec2& wish, float& speed) {
    const World& w = match.world();
    const Vec2 pos = position();
    // die nächste Bestie in Sichtweite
    const Match::Player* seen = nullptr;
    float nearest = 16.0f;
    for (const EntityState& e : match.entitiesFor(m_id)) {
        const bool vampireNear = (e.flags & EntityRedEyes) && !isVampireRole(me.role);
        if (e.id == m_id || e.kind != EntityLiving || !((e.flags & EntityWolf) || e.transform >= 0.5f || vampireNear)) continue;
        const Vec3 at{e.x, 0, e.z};
        const float d = distanceXZ(at, me.position);
        if (vampireNear && !(e.flags & EntityWolf) && (d > 7.0f || !m_vampireWitness.contains(e.id))) continue; // nur wer die Augen bemerkt hat
        if (d < nearest && match.hasLineOfSight(at, me.position)) {
            nearest = d;
            seen = match.find(e.id);
        }
    }
    if (seen) {
        m_fleeFrom = seen->id;
        m_fleeUntil = match.clock() + 6.0;
        m_feelings.fear = std::min(1.0f, m_feelings.fear + 0.002f);
        if (!m_shouted) {
            m_shouted = true;
            static const std::array<Text, 3> shouts = {{{"Hilfe! Ein Werwolf!", "Help! A werewolf!"},
                {"Lauft! Da ist ein Werwolf!", "Run! There's a werewolf!"},
                {"Ein Werwolf! Alle in die Häuser!", "A werewolf! Everyone inside!"}}};
            if (m_rng.chance(std::min(0.9f, 0.6f * m_personality.talk + 0.3f * m_feelings.fear))) {
                Speech alarm;
                alarm.topic = seen->id;
                alarm.alarm = true;
                speak(say, shouts[static_cast<size_t>(m_rng.range(0, static_cast<int>(shouts.size()) - 1))], false, alarm);
            }
        }
    }
    const Match::Player* beast = match.find(m_fleeFrom);
    if (!beast || !beast->alive || match.clock() > m_fleeUntil) {
        m_fleeFrom = kNoPlayer;
        return false;
    }
    const Vec2 from{beast->position.x, beast->position.z};
    speed = kSprintSpeed;
    m_hesitate = 0.0f;
    m_idle = 0.0f;
    // Schon drinnen? Tür zu – von innen verriegelt sie sich – und nach hinten
    if (const int inside = w.houseAt(pos, 0.15f); inside >= 0 && static_cast<size_t>(inside) < match.doors().size()) {
        const uint8_t door = static_cast<uint8_t>(inside);
        const DoorState& d = match.doors()[door];
        if ((d.flags & DoorOpen) && !(d.flags & DoorBroken) && match.checkInteraction(m_id, Interaction::DoorToggle, door).empty())
            match.interact(m_id, Interaction::DoorToggle, door);
        const House& h = w.houses()[door];
        m_target = w.houseToWorld(inside, {0.0f, -h.depth * 0.5f * 0.4f});
        m_goHome = inside == me.house;
        navigate(match, view, m_target, 0.6f, wish, false);
        return true;
    }
    // Ein Haus, das ich vor der Bestie erreiche? Das eigene ist am vertrautesten
    int refuge = -1;
    float best = 1e9f;
    for (size_t i = 0; i < w.houses().size() && i < match.doors().size(); ++i) {
        const DoorState& d = match.doors()[i];
        if ((d.flags & (DoorBroken | DoorBurnt | DoorBurning)) || (!(d.flags & DoorOpen) && (d.flags & DoorLocked))) continue;
        const Vec2 door{w.houses()[i].door.x, w.houses()[i].door.z};
        const float mine = (door - pos).length();
        const float theirs = (door - from).length();
        // nur eine Tür, die man klar vor der Bestie erreicht – und nicht in ihre Richtung
        if (mine > 30.0f || mine / kSprintSpeed + 0.5f > theirs / kWolfSpeed) continue;
        const Vec2 toDoor = (door - pos).normalized(), toBeast = (from - pos).normalized();
        if (mine > 1.0f && toDoor.x * toBeast.x + toDoor.y * toBeast.y > 0.3f) continue;
        const float score = static_cast<int>(i) == me.house ? mine * 0.75f : mine;
        if (score < best) {
            best = score;
            refuge = static_cast<int>(i);
        }
    }
    if (refuge >= 0) {
        const House& h = w.houses()[static_cast<size_t>(refuge)];
        m_target = w.houseToWorld(refuge, {0.0f, -h.depth * 0.5f * 0.4f});
        navigate(match, view, m_target, 0.6f, wish, false);
        return true;
    }
    // Kein Haus erreichbar: weg von der Bestie
    Vec2 away = pos - from;
    away = away.length() > 0.01f ? away.normalized() : Vec2{1.0f, 0.0f};
    m_target = w.resolveCollision(pos + away * 12.0f, 0.6f);
    navigate(match, view, m_target, 0.5f, wish, false);
    return true;
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
                if (isEvilRole(r.role) && match.checkInteraction(m_id, Interaction::HunterShot, r.player).empty()) target = r.player;
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
        // ein Stück mit jemandem gehen, dem man traut
        const Match::Player* companion = match.find(m_companion);
        if (companion && companion->alive && m_companionTime > 0.0f && w.houseAt({companion->position.x, companion->position.z}) < 0) {
            m_companionTime -= dt;
            m_target = w.resolveCollision(Vec2{companion->position.x, companion->position.z} + m_companionOffset, 0.6f);
            if ((m_target - pos).length() > 4.0f) m_idle = 0.0f;
        } else {
            m_companion = kNoPlayer;
        }
        if (m_idle > 0.0f) {
            m_idle -= dt;
        } else if (const Nav nav = navigate(match, view, m_target, m_companion != kNoPlayer ? 1.8f : 1.2f, wish, false); nav != Nav::Moving) {
            m_idle = nav == Nav::Arrived ? m_rng.uniform(3.0f, 12.0f) : 0.0f;
            if (m_companion == kNoPlayer && m_rng.chance(0.15f + 0.3f * m_personality.follow)) {
                float best = 30.0f;
                for (const Match::Player& p : match.players()) {
                    const float d = distanceXZ(p.position, me.position) - 8.0f * relationship(p.id);
                    if (!p.alive || p.id == m_id || d > best || relationship(p.id) < -0.25f || suspicion(match, p.id) > 0.5f) continue;
                    best = d;
                    m_companion = p.id;
                }
                const float a = m_rng.uniform(0.0f, 2.0f * eng::kPi);
                m_companionOffset = Vec2{std::cos(a), std::sin(a)} * m_rng.uniform(1.5f, 2.5f);
                m_companionTime = m_rng.uniform(15.0f, 35.0f);
            }
            if (m_companion == kNoPlayer) chooseWanderTarget(match);
        }
        // Vorsichtige halten Abstand zu dem, den sie für einen Wolf halten
        if (m_personality.caution > 0.4f) {
            for (const Match::Player& p : match.players()) {
                const Vec2 away = pos - Vec2{p.position.x, p.position.z};
                if (!p.alive || p.id == m_id || away.length() > 3.5f || away.length() < 0.01f || suspicion(match, p.id) < 2.0f) continue;
                const Vec2 sum = wish + away.normalized() * m_personality.caution;
                wish = sum.length() > 1.0f ? sum.normalized() : sum;
            }
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
                if (m_rng.chance(std::min(0.8f, 0.3f * m_personality.talk)))
                    speak(say,
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
                if (!isEvilRole(t->role)) m_suspicion[suspect] = 0.0f;
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
    const bool vampire = isVampireRole(me.role);
    const Interaction strike = killer ? Interaction::Stab : vampire ? Interaction::Bite : Interaction::Attack;
    const bool betray = view.can(AbilityBetray);
    const bool feed = killer || view.can(AbilityAttack);
    auto prey = [&](const Match::Player& p) {
        if (vampire) return !isVampireRole(p.role);
        return killer || (isWolfRole(p.role) ? betray : feed);
    };
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
            if (!vampire && isVampireRole(p.role)) score += 30.0f; // kaltes Blut reizt einen Werwolf kaum
            // Wölfe holen sich gezielt, wer ihnen gefährlich wird: eine Seherin oder lautstarke Ankläger
            if (!killer && !ally(view, p.role)) {
                const auto threat = m_suspicion.find(p.id);
                score -= std::clamp(threat == m_suspicion.end() ? 0.0f : threat->second, 0.0f, 6.0f) * 12.0f;
                if (m_dialogue.claimedRole(p.id) == Role::Seer) score -= 40.0f;
            }
            const int h = match.world().houseAt({p.position.x, p.position.z});
            if (h >= 0 && static_cast<size_t>(h) < match.doors().size()) {
                const DoorState& d = match.doors()[static_cast<size_t>(h)];
                if (!doorPassable(d) && vampire && h != me.house) continue; // ohne Einladung kein Weg hinein
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
    speed = killer || vampire ? kSprintSpeed : kWolfSpeed;
    const Nav nav = navigate(match, view, qp, 0.0f, wish, !vampire);
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
        if ((killer || vampire) && after && after->alive) m_spared.insert(m_quarry);
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
                    if (prey != kNoPlayer) speak(say, fill(m_rng.pick(kWolfLines), match.find(prey)->name), true);
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
    if (isVampireRole(me.role) && m_phaseTime > 8.0f) {
        if (!me.vampireForm && view.packKillsLeft > 0 && view.can(AbilityTransform)) {
            bool watched = false;
            for (const Match::Player& p : match.players())
                if (p.alive && p.id != m_id && !isVampireRole(p.role) && distanceXZ(p.position, me.position) < 9.0f &&
                    match.hasLineOfSight(p.position, me.position))
                    watched = true;
            if (!watched || m_phaseTime > 40.0f) match.interact(m_id, Interaction::Transform, 1);
        } else if (me.vampireForm && view.packKillsLeft == 0) {
            match.interact(m_id, Interaction::Transform, 0); // satt: wieder unauffällig
        }
    }
    if (isVampireRole(me.role) && view.can(AbilityBite) && m_phaseTime > 8.0f) {
        const uint8_t before = m_quarry;
        if (hunt(match, view, me, wish, speed)) {
            if (const Match::Player* q = match.find(m_quarry); q && m_quarry != before && m_rng.chance(0.35f * m_personality.talk)) {
                static const std::array<Text, 3> lines = {{{"Ich hole mir {name}.", "I'll take {name}."},
                    {"{name} hat die Tür offen gelassen …", "{name} left the door open …"},
                    {"Leise jetzt. {name} ist allein.", "Quiet now. {name} is alone."}}};
                speak(say, fill(lines[static_cast<size_t>(m_rng.range(0, 2))], q->name), true);
            }
            return;
        }
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
            // Wer sich als Seherin zu erkennen gegeben hat (und kein Lügner ist), bekommt den Schutz – dafür geht man zu ihr
            uint8_t ward = kNoPlayer;
            for (const Match::Player& p : match.players())
                if (p.alive && p.id != m_id && m_dialogue.claimedRole(p.id) == Role::Seer && !m_dialogue.liar(p.id) && suspicion(match, p.id) < 1.0f)
                    ward = p.id;
            const Match::Player* seer = match.find(ward);
            if (seer && match.checkInteraction(m_id, Interaction::Protect, ward).empty()) {
                match.interact(m_id, Interaction::Protect, ward);
            } else if (seer && m_phaseTime < 45.0f && ward != view.lastProtected) {
                m_target = {seer->position.x, seer->position.z};
                m_goHome = false;
                m_staysOut = true;
            } else {
                const uint8_t near = nearestPlayer(match, me.position, isAlive, kProtectRange);
                const uint8_t target = (near != kNoPlayer && m_rng.chance(0.7f)) ? near : m_id;
                match.interact(m_id, Interaction::Protect, target);
            }
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
        if (view.can(AbilityPoison)) {
            const uint8_t suspect = strongestSuspect(match, 3.0f);
            if (suspect != kNoPlayer && match.checkInteraction(m_id, Interaction::Poison, suspect).empty())
                match.interact(m_id, Interaction::Poison, suspect);
        }
    }

    // Wer eine Bestie sieht (oder einen Hilferuf hört), rennt um sein Leben
    if (!isWolfRole(me.role) && flee(match, view, me, say, wish, speed)) return;

    // Krach an einer Tür: wer draußen ist oder gleich nebenan wohnt, läuft hin und sieht nach
    const int inside = w.houseAt(pos);
    if (!isEvilRole(me.role) && !isNeutralKillerRole(me.role) && m_feelings.fear < 0.55f) {
        for (size_t i = 0; i < match.doors().size(); ++i) {
            const DoorState& d = match.doors()[i];
            if (doorPassable(d) || d.health >= 100 || static_cast<int>(i) == inside) continue;
            const House& h = w.houses()[i];
            const Vec2 door{h.door.x, h.door.z};
            if ((door - pos).length() > (inside >= 0 ? 20.0f : 45.0f)) continue;
            m_goHome = false;
            m_staysOut = true;
            m_idle = 0.0f;
            m_hesitate = 0.0f;
            m_target = door + Vec2{m_rng.uniform(-2.5f, 2.5f), m_rng.uniform(1.0f, 3.0f)};
            m_target = w.resolveCollision(m_target, 0.6f);
            speed = kSprintSpeed;
            break;
        }
    }

    // Erst einmal unschlüssig umsehen, dann los
    if (m_hesitate > 0.0f) {
        m_hesitate -= dt;
        m_mover.yaw += dt * ((m_id & 1) ? 1.1f : -1.1f);
        return;
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
