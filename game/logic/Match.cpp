#include "logic/Match.h"

#include "world/Movement.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace ww {

namespace {

constexpr float kExecutionSeconds = 7.0f;
constexpr float kMorningSeconds = 8.0f;
constexpr float kVoteGraceSeconds = 4.0f;
constexpr float kRangeTolerance = 0.8f; // Spielraum für Netzwerkverzögerung

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += (i + 1 == names.size()) ? " und " : ", ";
        out += names[i];
    }
    return out;
}

const char* roleWithArticle(Role role) {
    switch (role) {
    case Role::Villager: return "ein Dorfbewohner";
    case Role::Werewolf: return "ein Werwolf";
    case Role::Seer: return "die Seherin";
    case Role::Witch: return "die Hexe";
    case Role::Hunter: return "der Jäger";
    case Role::Cupid: return "Amor";
    case Role::Protector: return "der Beschützer";
    default: return "unbekannt";
    }
}

} // namespace

// --- Start -------------------------------------------------------------------

bool Match::start(const std::vector<Seat>& seats, const Settings& settings, const World& world, uint64_t seed,
                  std::string& error) {
    Settings s = settings;
    s.sanitize();
    std::vector<Role> roles = buildRoleList(s, static_cast<int>(seats.size()), &error);
    if (roles.empty()) return false;
    m_rng.seed(seed);
    m_rng.shuffle(roles);
    return startWithRoles(seats, roles, s, world, m_rng.next64(), error);
}

bool Match::startWithRoles(const std::vector<Seat>& seats, const std::vector<Role>& roles, const Settings& settings,
                           const World& world, uint64_t seed, std::string& error) {
    if (seats.size() != roles.size()) {
        error = "Rollenzahl passt nicht zur Spielerzahl.";
        return false;
    }
    if (static_cast<int>(seats.size()) < kMinPlayers) {
        error = "Es werden mindestens " + std::to_string(kMinPlayers) + " Spieler benötigt.";
        return false;
    }
    m_world = &world;
    m_settings = settings;
    m_settings.sanitize();
    m_rng.seed(seed);
    m_players.clear();
    for (size_t i = 0; i < seats.size(); ++i) {
        Player p;
        p.id = seats[i].id;
        p.name = seats[i].name;
        p.bot = seats[i].bot;
        p.role = roles[i];
        p.position = world.spawnPoint(static_cast<int>(i), static_cast<int>(seats.size()));
        p.yaw = std::atan2(-p.position.x, -p.position.z);
        p.needsCorrection = true;
        m_players.push_back(std::move(p));
    }
    m_phase = Phase::Lobby;
    m_day = 0;
    m_clock = 0.0;
    m_winner = Team::None;
    m_packKills = 0;
    m_protected = m_lastProtected = kNoPlayer;
    m_protector = kNoPlayer;
    m_healAvailable = m_poisonAvailable = false;
    for (const Player& p : m_players)
        if (p.role == Role::Witch) m_healAvailable = m_poisonAvailable = true;
    m_seerResults.clear();
    m_nightDeaths.clear();
    m_notices.clear();

    std::vector<std::string> wolves;
    for (const Player& p : m_players)
        if (p.role == Role::Werewolf) wolves.push_back(p.name);
    narrate(std::format("Willkommen im Dorf! {} Seelen leben hier – {} davon {}.", m_players.size(), wolves.size(),
                        wolves.size() == 1 ? "ist ein Werwolf" : "sind Werwölfe"));
    for (const Player& p : m_players) {
        tell(p.id, std::format("Deine Rolle: {}. {}", roleName(p.role), roleDescription(p.role)));
        if (p.role == Role::Werewolf) {
            std::vector<std::string> others;
            for (const Player& o : m_players)
                if (o.role == Role::Werewolf && o.id != p.id) others.push_back(o.name);
            tell(p.id, others.empty() ? std::string("Du jagst allein – kein anderer Werwolf ist im Dorf.")
                                      : "Dein Rudel: " + joinNames(others) + ". Euer Rudel-Chat ist geheim.");
        }
    }
    enterDay();
    return true;
}

// --- Hilfen ------------------------------------------------------------------

const Match::Player* Match::find(uint8_t id) const {
    for (const Player& p : m_players)
        if (p.id == id) return &p;
    return nullptr;
}

Match::Player* Match::findMutable(uint8_t id) {
    for (Player& p : m_players)
        if (p.id == id) return &p;
    return nullptr;
}

std::string Match::nameOf(uint8_t id) const {
    const Player* p = find(id);
    return p ? p->name : std::string("?");
}

void Match::setTimer(float seconds) {
    m_timer = seconds;
    m_phaseDuration = seconds;
    m_phaseElapsed = 0.0f;
    markDirty();
}

void Match::notify(Notice::Audience audience, uint8_t player, NoticeKind kind, std::string text) {
    m_notices.push_back(Notice{audience, player, kind, std::move(text)});
}

std::vector<Notice> Match::takeNotices() {
    std::vector<Notice> out;
    out.swap(m_notices);
    return out;
}

bool Match::takeDirty() {
    const bool d = m_dirty;
    m_dirty = false;
    return d;
}

std::vector<uint8_t> Match::takeCorrections() {
    std::vector<uint8_t> out;
    for (Player& p : m_players) {
        if (p.needsCorrection) {
            out.push_back(p.id);
            p.needsCorrection = false;
        }
    }
    return out;
}

bool Match::hasLineOfSight(Vec3 a, Vec3 b) const {
    return !m_world->lineBlockedByBuildings({a.x, a.z}, {b.x, b.z});
}

bool Match::mixedCouple() const {
    for (const Player& p : m_players) {
        if (p.lover == kNoPlayer) continue;
        const Player* partner = find(p.lover);
        if (!partner) return false;
        return (p.role == Role::Werewolf) != (partner->role == Role::Werewolf);
    }
    return false;
}

Vec3 Match::seatPosition(int index, int count) const {
    const float a = 2.0f * eng::kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count)) + 0.2f;
    const float r = count > 10 ? 8.2f : 7.0f;
    return m_world->ground(std::cos(a) * r, std::sin(a) * r);
}

void Match::teleport(uint8_t id, Vec3 position) {
    if (Player* p = findMutable(id)) {
        p->position = m_world->ground(position.x, position.z);
        p->lastMoveTime = m_clock;
        p->needsCorrection = true;
    }
}

// --- Bewegung ----------------------------------------------------------------

void Match::movePlayer(uint8_t id, float x, float z, float yaw, uint8_t flags) {
    Player* p = findMutable(id);
    if (!p || m_phase == Phase::GameOver) return;
    Vec2 target{x, z};
    if (target.length() > World::kBoundary) target = target * (World::kBoundary / target.length());
    if (!p->alive) {
        p->ghostPosition = m_world->ground(target.x, target.y) + Vec3{0, 0.4f, 0};
        p->ghostYaw = yaw;
        return;
    }
    const double dt = std::clamp(m_clock - p->lastMoveTime, 0.02, 1.0);
    p->lastMoveTime = m_clock;
    const float maxSpeed = (p->wolfForm() ? kWolfSpeed : kSprintSpeed) * 1.4f;
    const float dist = (target - Vec2{p->position.x, p->position.z}).length();
    if (dist > maxSpeed * static_cast<float>(dt) + 1.5f) {
        p->needsCorrection = true; // zu schnell: zurücksetzen
        return;
    }
    p->position = m_world->ground(target.x, target.y);
    p->yaw = yaw;
    p->moveFlags = flags & EntitySprint;
}

// --- Abfragen ----------------------------------------------------------------

uint16_t Match::abilitiesFor(uint8_t id) const {
    const Player* p = find(id);
    if (!p) return 0;
    uint16_t a = 0;
    if (p->hunterTimer > 0.0f) a |= AbilityHunterShot;
    if (!p->alive) return a;
    const bool night = m_phase == Phase::Night;
    switch (p->role) {
    case Role::Werewolf:
        if (night) a |= AbilityTransform;
        if (night && p->wolfForm() && m_packKills > 0) a |= AbilityAttack;
        break;
    case Role::Seer:
        if (night && !p->nightAbilityUsed) a |= AbilityInspect;
        break;
    case Role::Protector:
        if (night && !p->nightAbilityUsed) a |= AbilityProtect;
        break;
    case Role::Witch:
        if (night && m_healAvailable) a |= AbilityHeal;
        if (night && m_poisonAvailable) a |= AbilityPoison;
        break;
    case Role::Cupid:
        if (night && m_day == 1 && p->cupidPicks.size() < 2) a |= AbilityCupid;
        break;
    default:
        break;
    }
    if (m_phase == Phase::Vote) a |= AbilityVote;
    if (m_phase == Phase::Day && m_day >= 2 && !m_bellRung) a |= AbilityRingBell;
    return a;
}

std::string Match::checkInteraction(uint8_t id, Interaction kind, uint8_t targetId) const {
    const Player* p = find(id);
    if (!p) return "Du nimmst nicht an diesem Spiel teil.";
    const Player* t = find(targetId);
    const uint16_t abilities = abilitiesFor(id);
    auto distTo = [&](const Player& other) { return distanceXZ(p->position, other.position); };

    switch (kind) {
    case Interaction::Transform:
        if (!(abilities & AbilityTransform)) return "Verwandeln kannst du dich nur nachts.";
        return {};
    case Interaction::Attack:
        if (!(abilities & AbilityAttack)) {
            if (p->role != Role::Werewolf || m_phase != Phase::Night) return "Du kannst niemanden angreifen.";
            if (!p->wolfForm()) return "Verwandle dich zuerst (R).";
            return "Das Rudel hat heute Nacht schon gefressen.";
        }
        if (p->attackCooldown > 0.0f) return "Du holst noch aus …";
        if (!t || !t->alive) return "Hier ist niemand.";
        if (t->role == Role::Werewolf) return "Du greifst kein Mitglied deines Rudels an.";
        if (distTo(*t) > kAttackRange + kRangeTolerance) return "Zu weit entfernt.";
        return {};
    case Interaction::Inspect:
        if (!(abilities & AbilityInspect)) return "Deine Vision ist für heute Nacht verbraucht.";
        if (!t || !t->alive || t->id == id) return "Wähle einen lebenden Mitspieler.";
        if (distTo(*t) > kInspectRange + kRangeTolerance) return "Zu weit entfernt für eine Vision.";
        if (!hasLineOfSight(p->position, t->position)) return "Du kannst die Person nicht sehen.";
        return {};
    case Interaction::Protect:
        if (!(abilities & AbilityProtect)) return "Du hast heute Nacht schon jemanden beschützt.";
        if (!t || !t->alive) return "Wähle einen lebenden Mitspieler.";
        if (t->id == m_lastProtected) return "Diesen Spieler hast du letzte Nacht schon beschützt.";
        if (t->id != id && distTo(*t) > kProtectRange + kRangeTolerance) return "Geh näher heran.";
        return {};
    case Interaction::Heal:
        if (!(abilities & AbilityHeal)) return "Dein Heiltrank ist verbraucht.";
        if (!t || t->alive || t->buried) return "Hier liegt niemand, den du retten könntest.";
        if (!t->diedAtNight || t->deathDay != m_day || t->deathCause != DeathCause::Wolves)
            return "Für diese Seele kommt jede Hilfe zu spät.";
        if (distanceXZ(p->position, t->deathPosition) > kHealRange + kRangeTolerance) return "Geh näher heran.";
        return {};
    case Interaction::Poison:
        if (!(abilities & AbilityPoison)) return "Dein Gifttrank ist verbraucht.";
        if (!t || !t->alive || t->id == id) return "Wähle einen lebenden Mitspieler.";
        if (distTo(*t) > kPoisonRange + kRangeTolerance) return "Geh näher heran.";
        return {};
    case Interaction::CupidPick:
        if (!(abilities & AbilityCupid)) return "Amors Pfeile sind verschossen.";
        if (!t || !t->alive) return "Wähle einen lebenden Mitspieler.";
        if (std::find(p->cupidPicks.begin(), p->cupidPicks.end(), t->id) != p->cupidPicks.end())
            return "Diesen Spieler hast du schon gewählt.";
        if (t->id != id && distTo(*t) > kCupidRange + kRangeTolerance) return "Geh näher heran.";
        return {};
    case Interaction::HunterShot:
        if (!(abilities & AbilityHunterShot)) return "Du hast keinen Schuss mehr.";
        if (!t || !t->alive || t->id == id) return "Ziele auf einen lebenden Spieler.";
        if (distanceXZ(p->deathPosition, t->position) > kHunterRange) return "Zu weit entfernt.";
        if (!hasLineOfSight(p->deathPosition, t->position)) return "Kein freies Schussfeld.";
        return {};
    case Interaction::Vote:
        if (!(abilities & AbilityVote)) return "Gerade wird nicht abgestimmt.";
        if (targetId == kNoPlayer) return {};
        if (!t || !t->alive || t->id == id) return "Für diesen Spieler kannst du nicht stimmen.";
        return {};
    case Interaction::RingBell:
        if (!(abilities & AbilityRingBell)) {
            if (m_phase == Phase::Day && m_day < 2) return "Am ersten Tag wird keine Versammlung einberufen.";
            return "Die Glocke kann gerade nicht geläutet werden.";
        }
        if (distanceXZ(p->position, m_world->bell()) > kBellRange + kRangeTolerance) return "Geh zur Glocke.";
        if (m_phaseElapsed < 15.0f) return "Lasst dem Dorf noch etwas Zeit.";
        return {};
    }
    return "Unbekannte Aktion.";
}

std::vector<EntityState> Match::entitiesFor(uint8_t viewer) const {
    std::vector<EntityState> out;
    const Player* me = find(viewer);
    const bool viewerDead = me && !me->alive;
    const bool night = m_phase == Phase::Night;
    for (const Player& p : m_players) {
        if (p.alive) {
            EntityState e;
            e.id = p.id;
            e.kind = EntityLiving;
            e.x = p.position.x;
            e.z = p.position.z;
            e.yaw = p.yaw;
            e.transform = p.transform;
            e.flags = p.moveFlags;
            if (p.wolfForm()) e.flags |= EntityWolf;
            if (p.attackAnim > 0.0f) e.flags |= EntityAttack;
            if (night && p.transform < 0.01f) e.flags |= EntityLantern;
            out.push_back(e);
        } else {
            if (!p.buried) {
                EntityState c;
                c.id = p.id;
                c.kind = EntityCorpse;
                c.x = p.deathPosition.x;
                c.z = p.deathPosition.z;
                c.yaw = p.deathYaw;
                out.push_back(c);
            }
            if (viewerDead || viewer == p.id) {
                EntityState g;
                g.id = p.id;
                g.kind = EntityGhost;
                g.x = p.ghostPosition.x;
                g.z = p.ghostPosition.z;
                g.yaw = p.ghostYaw;
                out.push_back(g);
            }
        }
    }
    return out;
}

GameView Match::viewFor(uint8_t viewer) const {
    GameView v;
    v.phase = m_phase;
    v.day = static_cast<uint16_t>(m_day);
    v.timeLeft = std::max(0.0f, m_timer);
    v.phaseDuration = m_phaseDuration;
    v.you = viewer;
    v.worldSeed = m_world ? m_world->seed() : 0;
    v.settings = m_settings;
    v.winner = m_winner;

    const Player* me = find(viewer);
    const bool over = m_phase == Phase::GameOver;
    const bool spectator = !me || !me->alive;
    const bool meWolf = me && me->role == Role::Werewolf;
    const bool showVotes = m_phase == Phase::Vote || m_phase == Phase::Execution;

    for (const Player& p : m_players) {
        PlayerView pv;
        pv.id = p.id;
        pv.name = p.name;
        pv.alive = p.alive;
        pv.connected = p.connected;
        pv.bot = p.bot;
        bool knowRole = over || spectator || p.id == viewer || (!p.alive && m_settings.revealRoles) ||
                        (meWolf && p.role == Role::Werewolf) || (me && me->lover == p.id);
        if (!knowRole && me && me->role == Role::Seer)
            knowRole = std::any_of(m_seerResults.begin(), m_seerResults.end(),
                                   [&](const SeerResult& r) { return r.player == p.id; });
        pv.role = knowRole ? p.role : Role::Unknown;
        pv.lover = p.lover != kNoPlayer &&
                   (over || spectator || p.id == viewer || (me && (me->lover == p.id || me->role == Role::Cupid)));
        if (showVotes && p.alive) pv.vote = p.vote;
        pv.winner = over && isWinner(p.id);
        v.players.push_back(std::move(pv));
    }

    if (me) {
        v.yourRole = me->role;
        v.abilities = abilitiesFor(viewer);
        v.vote = me->vote;
        v.loverPartner = me->lover;
        v.cupidPicks = me->cupidPicks;
        v.hunterTimeLeft = me->hunterTimer;
        if (me->role == Role::Seer) v.seerResults = m_seerResults;
        if (me->role == Role::Protector) {
            v.lastProtected = m_lastProtected;
            v.protectedTarget = m_phase == Phase::Night ? m_protected : kNoPlayer;
        }
        if (me->role == Role::Witch) {
            v.healAvailable = m_healAvailable;
            v.poisonAvailable = m_poisonAvailable;
        }
        if (me->role == Role::Werewolf) v.packKillsLeft = m_packKills;
    }
    return v;
}

std::optional<ChatChannel> Match::chatChannel(uint8_t speaker, bool wantWolves) const {
    const Player* p = find(speaker);
    if (!p) return std::nullopt;
    if (m_phase == Phase::GameOver) return ChatChannel::Global;
    if (!p->alive) return ChatChannel::Dead;
    if (wantWolves && p->role == Role::Werewolf) return ChatChannel::Wolves;
    if (isGatheringPhase(m_phase)) return ChatChannel::Global;
    return ChatChannel::Nearby;
}

bool Match::canReadChannel(uint8_t listener, ChatChannel channel) const {
    const Player* l = find(listener);
    if (!l) return channel == ChatChannel::Global;
    const bool over = m_phase == Phase::GameOver;
    switch (channel) {
    case ChatChannel::Global:
    case ChatChannel::Nearby: return true;
    case ChatChannel::Wolves: return l->role == Role::Werewolf || !l->alive || over;
    case ChatChannel::Dead: return !l->alive || over;
    }
    return false;
}

bool Match::canHear(uint8_t listener, uint8_t speaker, ChatChannel channel) const {
    if (!canReadChannel(listener, channel)) return false;
    if (channel != ChatChannel::Nearby) return true;
    const Player* l = find(listener);
    const Player* s = find(speaker);
    if (!l || !s) return false;
    if (!l->alive || l->id == s->id) return true; // Geister hören alles
    return distanceXZ(l->position, s->position) <= kNearbyChatRange;
}

Team Match::teamOf(uint8_t id) const {
    const Player* p = find(id);
    if (!p) return Team::None;
    if (p->lover != kNoPlayer && mixedCouple()) return Team::Lovers;
    return roleTeam(p->role);
}

bool Match::isWinner(uint8_t id) const {
    if (m_winner == Team::None || m_winner == Team::Nobody) return false;
    return teamOf(id) == m_winner;
}

// --- Aktionen ----------------------------------------------------------------

std::string Match::interact(uint8_t id, Interaction kind, uint8_t targetId) {
    std::string why = checkInteraction(id, kind, targetId);
    if (!why.empty()) return why;
    Player* p = findMutable(id);
    Player* t = findMutable(targetId);

    switch (kind) {
    case Interaction::Transform:
        p->transformTarget = targetId ? 1.0f : 0.0f;
        break;
    case Interaction::Attack:
        p->attackCooldown = 1.2f;
        p->attackAnim = 0.4f;
        if (t->id == m_protected) {
            p->attackCooldown = 3.0f;
            tell(p->id, "Eine unsichtbare Macht schützt dieses Opfer – deine Klauen gleiten ab!");
            if (m_protector != kNoPlayer) tell(m_protector, "Dein Schutz hat heute Nacht ein Leben gerettet!");
            break;
        }
        --m_packKills;
        narrate("Ein grauenvoller Schrei zerreißt die Stille der Nacht …");
        kill(t->id, DeathCause::Wolves, false);
        if (m_packKills == 0)
            notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private, "Das Rudel ist für heute Nacht gesättigt.");
        break;
    case Interaction::Inspect:
        p->nightAbilityUsed = true;
        if (std::none_of(m_seerResults.begin(), m_seerResults.end(), [&](const SeerResult& r) { return r.player == t->id; }))
            m_seerResults.push_back({t->id, t->role});
        tell(p->id, std::format("Deine Vision zeigt dir: {} ist {}.", t->name, roleWithArticle(t->role)));
        break;
    case Interaction::Protect:
        p->nightAbilityUsed = true;
        m_protected = t->id;
        m_lastProtected = t->id;
        m_protector = p->id;
        tell(p->id, std::format("Du wachst heute Nacht über {}.", t->id == p->id ? std::string("dich selbst") : t->name));
        break;
    case Interaction::Heal: {
        m_healAvailable = false;
        auto revive = [&](Player& r) {
            r.alive = true;
            r.position = r.deathPosition;
            r.needsCorrection = true;
            r.hunterTimer = 0.0f;
            std::erase_if(m_nightDeaths, [&](const PendingDeath& d) { return d.id == r.id; });
            tell(r.id, "Ein bitterer Trank benetzt deine Lippen … du lebst!");
        };
        revive(*t);
        if (t->lover != kNoPlayer) {
            Player* l = findMutable(t->lover);
            if (l && !l->alive && l->deathCause == DeathCause::Grief && l->deathDay == m_day) revive(*l);
        }
        tell(p->id, std::format("Dein Heiltrank hat {} ins Leben zurückgeholt.", t->name));
        break;
    }
    case Interaction::Poison:
        m_poisonAvailable = false;
        tell(p->id, std::format("Dein Gift wirkt – {} sinkt leblos zu Boden.", t->name));
        kill(t->id, DeathCause::Poison, false);
        break;
    case Interaction::CupidPick:
        p->cupidPicks.push_back(t->id);
        if (p->cupidPicks.size() == 2) makeLovers(p->cupidPicks[0], p->cupidPicks[1]);
        else tell(p->id, std::format("{} ist getroffen. Wähle die zweite Person.", t->name));
        break;
    case Interaction::HunterShot:
        p->hunterTimer = 0.0f;
        narrate(std::format("Ein Schuss hallt durch das Dorf! {} hat mit dem letzten Atemzug abgedrückt.", p->name));
        kill(t->id, DeathCause::Hunter, true);
        break;
    case Interaction::Vote:
        p->vote = targetId;
        checkVotesComplete();
        break;
    case Interaction::RingBell:
        m_bellRung = true;
        enterAssembly(std::format("{} läutet die Glocke! Alle Bewohner versammeln sich am Feuer.", p->name));
        break;
    }
    markDirty();
    return {};
}

void Match::makeLovers(uint8_t a, uint8_t b) {
    Player* pa = findMutable(a);
    Player* pb = findMutable(b);
    if (!pa || !pb || a == b) return;
    pa->lover = b;
    pb->lover = a;
    for (const Player& p : m_players)
        if (p.role == Role::Cupid) tell(p.id, std::format("{} und {} sind nun unsterblich ineinander verliebt.", pa->name, pb->name));
    const bool mixed = mixedCouple();
    for (auto [x, y] : {std::pair{pa, pb}, std::pair{pb, pa}}) {
        tell(x->id, std::format("Amors Pfeil hat dich getroffen! Du bist in {} verliebt (Rolle: {}). Stirbt einer von "
                                "euch, stirbt auch der andere.",
                                y->name, roleName(y->role)));
        if (mixed) tell(x->id, "Ihr gehört verschiedenen Lagern an. Gemeinsam gewinnt ihr nur, wenn ihr als Letzte überlebt.");
    }
    markDirty();
}

void Match::setConnected(uint8_t id, bool connected) {
    if (Player* p = findMutable(id)) {
        p->connected = connected;
        markDirty();
    }
}

void Match::playerLeft(uint8_t id) {
    Player* p = findMutable(id);
    if (!p) return;
    p->connected = false;
    p->hunterTimer = 0.0f;
    markDirty();
    if (!p->alive || m_phase == Phase::GameOver) return;
    kill(id, DeathCause::Left, true);
    checkWin();
}

void Match::kill(uint8_t id, DeathCause cause, bool announceNow) {
    Player* p = findMutable(id);
    if (!p || !p->alive) return;
    p->alive = false;
    p->deathPosition = p->position;
    p->deathYaw = p->yaw;
    p->deathDay = m_day;
    p->diedAtNight = m_phase == Phase::Night;
    p->deathCause = cause;
    p->buried = false;
    p->ghostPosition = p->position + Vec3{0, 0.4f, 0};
    p->ghostYaw = p->yaw;
    p->transform = p->transformTarget = 0.0f;
    p->vote = kNoPlayer;
    if (p->role == Role::Hunter && cause != DeathCause::Left) {
        p->hunterTimer = kHunterShotSeconds;
        tell(p->id, "Du stirbst – doch dein Gewehr ist geladen! Ziele auf jemanden und drücke E.");
    }
    if (cause == DeathCause::Wolves) tell(p->id, "Klauen aus der Dunkelheit – du wurdest von einem Werwolf zerrissen.");
    if (cause == DeathCause::Poison) tell(p->id, "Ein brennender Schmerz … du wurdest vergiftet.");

    if (announceNow) announce(id, cause);
    else m_nightDeaths.push_back({id, cause});
    markDirty();

    if (p->lover != kNoPlayer) kill(p->lover, DeathCause::Grief, announceNow);
    if (m_phase != Phase::GameOver) checkWin();
}

void Match::announce(uint8_t id, DeathCause cause) {
    const Player* p = find(id);
    if (!p) return;
    std::string text;
    switch (cause) {
    case DeathCause::Wolves:
    case DeathCause::Poison: text = p->name + " wurde tot aufgefunden."; break;
    case DeathCause::Grief: text = std::format("{} stirbt vor Kummer um {}.", p->name, nameOf(p->lover)); break;
    case DeathCause::Vote: text = p->name + " wurde vom Dorf hingerichtet."; break;
    case DeathCause::Hunter: text = p->name + " wurde vom Jäger erschossen."; break;
    case DeathCause::Left: text = p->name + " hat das Dorf für immer verlassen."; break;
    }
    if (m_settings.revealRoles) text += std::format(" (Rolle: {})", roleName(p->role));
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Death, std::move(text));
}

void Match::checkVotesComplete() {
    if (m_phase != Phase::Vote) return;
    for (const Player& p : m_players)
        if (p.alive && p.vote == kNoPlayer) return;
    if (!m_voteGraceApplied && m_timer > kVoteGraceSeconds) {
        m_timer = kVoteGraceSeconds;
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, "Alle Stimmen sind abgegeben – noch wenige Sekunden zum Umentscheiden.");
    }
    m_voteGraceApplied = true;
}

// --- Phasen ------------------------------------------------------------------

void Match::update(float dt) {
    m_clock += dt;
    if (m_phase == Phase::Lobby) return;

    for (Player& p : m_players) {
        if (p.hunterTimer > 0.0f) {
            p.hunterTimer -= dt;
            if (p.hunterTimer <= 0.0f) {
                p.hunterTimer = 0.0f;
                narrate(std::format("{} war der Jäger – doch der letzte Schuss blieb aus.", p.name));
                markDirty();
                if (m_phase != Phase::GameOver && checkWin()) return;
            }
        }
        p.attackCooldown = std::max(0.0f, p.attackCooldown - dt);
        p.attackAnim = std::max(0.0f, p.attackAnim - dt);
        if (p.alive && p.transform != p.transformTarget) {
            p.transform = eng::approach(p.transform, p.transformTarget, dt / kTransformSeconds);
            if (p.transform == p.transformTarget) markDirty();
        }
    }

    if (m_phase == Phase::GameOver) return;
    m_phaseElapsed += dt;
    m_timer -= dt;
    if (m_timer <= 0.0f) advance();
}

void Match::advance() {
    switch (m_phase) {
    case Phase::Day:
        if (m_day <= 1) enterNight();
        else enterAssembly("Die Sonne sinkt – das Dorf versammelt sich am Feuer.");
        break;
    case Phase::Assembly: enterVote(); break;
    case Phase::Vote: resolveVote(); break;
    case Phase::Execution:
        if (!checkWin()) enterNight();
        break;
    case Phase::Night: enterMorning(); break;
    case Phase::Morning:
        if (!checkWin()) enterDay();
        break;
    default: break;
    }
}

void Match::enterDay() {
    ++m_day;
    m_phase = Phase::Day;
    m_bellRung = false;
    for (Player& p : m_players) p.vote = kNoPlayer;
    if (m_day == 1) {
        setTimer(std::max(30.0f, static_cast<float>(m_settings.daySeconds) * 0.5f));
        narrate("Tag 1: Erkundet das Dorf und lernt euch kennen. Bei Einbruch der Nacht erwachen die Werwölfe …");
    } else {
        setTimer(m_settings.daySeconds);
        narrate(std::format("Tag {}: Sucht nach Spuren und sprecht miteinander. Die Glocke ruft zur Versammlung.", m_day));
    }
}

void Match::enterAssembly(const std::string& reason) {
    m_phase = Phase::Assembly;
    setTimer(m_settings.discussionSeconds);
    narrate(reason);
    // Alle Lebenden setzen sich ans Feuer
    std::vector<Player*> alive;
    for (Player& p : m_players)
        if (p.alive) alive.push_back(&p);
    for (size_t i = 0; i < alive.size(); ++i) {
        const Vec3 seat = seatPosition(static_cast<int>(i), static_cast<int>(alive.size()));
        alive[i]->position = seat;
        alive[i]->yaw = std::atan2(-seat.x, -seat.z);
        alive[i]->lastMoveTime = m_clock;
        alive[i]->needsCorrection = true;
        alive[i]->transform = alive[i]->transformTarget = 0.0f;
    }
}

void Match::enterVote() {
    m_phase = Phase::Vote;
    m_voteGraceApplied = false;
    for (Player& p : m_players) p.vote = kNoPlayer;
    setTimer(m_settings.voteSeconds);
    narrate("Die Abstimmung beginnt! Schaut die Person an, die hingerichtet werden soll, und drückt E.");
}

void Match::resolveVote() {
    std::map<uint8_t, int> tally;
    for (const Player& p : m_players) {
        if (!p.alive || p.vote == kNoPlayer) continue;
        const Player* t = find(p.vote);
        if (t && t->alive) ++tally[t->id];
    }
    m_phase = Phase::Execution;
    setTimer(kExecutionSeconds);
    if (tally.empty()) {
        narrate("Niemand hat abgestimmt. Das Dorf geht ratlos auseinander.");
        return;
    }
    std::vector<std::pair<uint8_t, int>> ranking(tally.begin(), tally.end());
    std::stable_sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string summary = "Stimmen: ";
    for (size_t i = 0; i < ranking.size(); ++i) {
        if (i > 0) summary += ", ";
        summary += std::format("{} ({})", nameOf(ranking[i].first), ranking[i].second);
    }
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, summary);
    if (ranking.size() > 1 && ranking[0].second == ranking[1].second) {
        narrate("Gleichstand! Das Dorf kann sich nicht einigen – niemand wird hingerichtet.");
        return;
    }
    kill(ranking[0].first, DeathCause::Vote, true);
}

void Match::enterNight() {
    m_phase = Phase::Night;
    setTimer(m_settings.nightSeconds);
    m_packKills = 1;
    m_protected = kNoPlayer;
    m_protector = kNoPlayer;
    m_nightDeaths.clear();
    for (Player& p : m_players) {
        p.nightAbilityUsed = false;
        p.vote = kNoPlayer;
        if (!p.alive) p.buried = true; // die Toten der vergangenen Tage werden begraben
    }
    narrate(std::format("Nacht {} bricht herein. Bleibt im Licht – in der Dunkelheit lauern die Werwölfe …", m_day));
    notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private,
           "Die Jagd beginnt. Verwandelt euch mit R und reißt ein Opfer mit Linksklick – aber lasst euch nicht sehen!");
}

void Match::enterMorning() {
    // Amor hat sich nicht entschieden: Zufall entscheidet
    if (m_day == 1) {
        for (Player& c : m_players) {
            if (c.role != Role::Cupid || !c.alive || c.cupidPicks.size() >= 2) continue;
            std::vector<uint8_t> pool;
            for (const Player& p : m_players)
                if (p.alive && std::find(c.cupidPicks.begin(), c.cupidPicks.end(), p.id) == c.cupidPicks.end()) pool.push_back(p.id);
            m_rng.shuffle(pool);
            while (c.cupidPicks.size() < 2 && !pool.empty()) {
                c.cupidPicks.push_back(pool.back());
                pool.pop_back();
            }
            if (c.cupidPicks.size() == 2) {
                tell(c.id, "Amors Pfeile flogen ziellos durch die Nacht …");
                makeLovers(c.cupidPicks[0], c.cupidPicks[1]);
            }
        }
    }
    for (Player& p : m_players) p.transform = p.transformTarget = 0.0f;

    m_phase = Phase::Morning;
    setTimer(kMorningSeconds);
    narrate("Der Morgen graut über dem Dorf …");
    if (m_nightDeaths.empty()) narrate("Wie durch ein Wunder hat in dieser Nacht niemand das Leben verloren.");
    for (const PendingDeath& d : m_nightDeaths) announce(d.id, d.cause);
    m_nightDeaths.clear();
}

Team Match::evaluateWinner() const {
    int alive = 0, aliveWolves = 0, wolvesTeam = 0;
    for (const Player& p : m_players) {
        if (!p.alive) continue;
        ++alive;
        if (p.role == Role::Werewolf) ++aliveWolves;
        if (teamOf(p.id) == Team::Wolves) ++wolvesTeam;
    }
    if (alive == 0) return Team::Nobody;
    if (mixedCouple() && alive == 2) {
        bool loversAlive = true;
        for (const Player& p : m_players)
            if (p.lover != kNoPlayer && !p.alive) loversAlive = false;
        if (loversAlive) return Team::Lovers;
    }
    if (aliveWolves == 0) return Team::Village;
    if (wolvesTeam > 0 && wolvesTeam >= alive - wolvesTeam) return Team::Wolves;
    return Team::None;
}

bool Match::checkWin() {
    if (m_phase == Phase::GameOver) return true;
    // Ein Jäger darf seinen letzten Schuss noch abgeben
    for (const Player& p : m_players)
        if (p.hunterTimer > 0.0f) return false;
    const Team w = evaluateWinner();
    if (w == Team::None) return false;

    // Nächtliche Tote vor dem Spielende verkünden
    for (const PendingDeath& d : m_nightDeaths) announce(d.id, d.cause);
    m_nightDeaths.clear();

    m_winner = w;
    m_phase = Phase::GameOver;
    m_timer = 0.0f;
    m_phaseDuration = 0.0f;
    for (Player& p : m_players) p.transform = p.transformTarget = 0.0f;
    markDirty();
    switch (w) {
    case Team::Village:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, "Das Dorf hat gesiegt! Kein Werwolf treibt mehr sein Unwesen.");
        break;
    case Team::Wolves:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, "Die Werwölfe haben gesiegt! Das Dorf ist ihrem Hunger erlegen.");
        break;
    case Team::Lovers:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, "Die Verliebten haben gesiegt! Ihre Liebe hat alles überdauert.");
        break;
    default:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, "Niemand hat überlebt. Stille legt sich über das Dorf.");
        break;
    }
    std::string roles = "Die Rollen: ";
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (i > 0) roles += ", ";
        roles += std::format("{} ({})", m_players[i].name, roleName(m_players[i].role));
    }
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, roles);
    return true;
}

} // namespace ww
