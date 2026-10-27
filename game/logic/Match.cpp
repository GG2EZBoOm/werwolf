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
constexpr float kDoorHitCooldown = 1.0f;
constexpr int kDoorDamage = 20;           // fünf Schläge
constexpr int kReinforcedDoorDamage = 10; // zehn Schläge
constexpr float kDoorNoiseRadius = 30.0f;
constexpr float kDoorNoiseInterval = 5.0f;
constexpr float kFireSeconds = 12.0f;
constexpr float kBurnSeconds = 3.5f;      // so lange überlebt man in einem brennenden Haus
constexpr float kSniffRadius = 5.0f;      // der Fuchs wittert die Person und ihre Nachbarn
constexpr float kBearRange = 3.5f;
constexpr float kBearCooldown = 30.0f;

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
    case Role::Elder: return "der Dorfälteste";
    case Role::LittleGirl: return "das kleine Mädchen";
    case Role::Medium: return "das Medium";
    case Role::Tracker: return "der Fährtenleser";
    case Role::Blacksmith: return "der Schmied";
    case Role::Priest: return "der Pfarrer";
    case Role::Mayor: return "der Bürgermeister";
    case Role::Idiot: return "der Dorfdepp";
    case Role::Cursed: return "der Verfluchte";
    case Role::AlphaWolf: return "der Urwolf";
    case Role::WolfCub: return "das Wolfsjunge";
    case Role::Jester: return "der Narr";
    case Role::SerialKiller: return "der Serienmörder";
    case Role::Arsonist: return "der Brandstifter";
    case Role::Thief: return "der Dieb";
    case Role::Scapegoat: return "der Sündenbock";
    case Role::Fox: return "der Fuchs";
    case Role::BearTamer: return "der Bärenführer";
    case Role::Knight: return "der Ritter mit der rostigen Klinge";
    case Role::Servant: return "die ergebene Magd";
    case Role::Judge: return "der stotternde Richter";
    case Role::Actor: return "der Schauspieler";
    case Role::WildChild: return "das wilde Kind";
    case Role::WolfHound: return "der Wolfshund";
    case Role::Sister: return "eine der zwei Schwestern";
    case Role::Brother: return "einer der drei Brüder";
    case Role::HonestVillager: return "ein ehrlicher Dorfbewohner";
    case Role::Raven: return "der Rabe";
    case Role::BigBadWolf: return "der große böse Wolf";
    case Role::WhiteWolf: return "der weiße Werwolf";
    case Role::Piper: return "der Flötenspieler";
    case Role::Angel: return "der Engel";
    case Role::Sectarian: return "der Sektierer";
    default: return "unbekannt";
    }
}

bool isNeutralKiller(Role role) { return role == Role::SerialKiller || role == Role::Arsonist || role == Role::WhiteWolf; }

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

    // Häuser verteilen: jeder bekommt eines, bei vielen Spielern wird geteilt
    const size_t houseCount = std::min<size_t>(world.houses().size(), 32);
    std::vector<uint8_t> homes;
    for (size_t i = 0; i < houseCount; ++i) homes.push_back(static_cast<uint8_t>(i));
    m_rng.shuffle(homes);

    m_players.clear();
    for (size_t i = 0; i < seats.size(); ++i) {
        Player p;
        p.id = seats[i].id;
        p.name = seats[i].name;
        p.bot = seats[i].bot;
        p.role = p.startRole = roles[i];
        p.shield = p.role == Role::Elder;
        p.house = homes.empty() ? kNoPlayer : homes[i % homes.size()];
        p.position = world.spawnPoint(static_cast<int>(i), static_cast<int>(seats.size()));
        p.yaw = std::atan2(-p.position.x, -p.position.z);
        p.needsCorrection = true;
        m_players.push_back(std::move(p));
    }

    // Der Schauspieler bekommt drei Rollen, die möglichst nicht im Spiel sind
    std::vector<Role> pool;
    for (Role r : {Role::Seer, Role::Protector, Role::Fox, Role::Tracker})
        if (std::none_of(roles.begin(), roles.end(), [&](Role x) { return x == r; })) pool.push_back(r);
    for (Role r : {Role::Seer, Role::Protector, Role::Fox, Role::Tracker})
        if (pool.size() < 3 && std::find(pool.begin(), pool.end(), r) == pool.end()) pool.push_back(r);
    for (Player& p : m_players) {
        if (p.role != Role::Actor) continue;
        p.actorCards = pool;
        m_rng.shuffle(p.actorCards);
        p.actorCards.resize(3);
    }

    m_phase = Phase::Lobby;
    m_day = 0;
    m_clock = 0.0;
    m_winner = Team::None;
    m_packKills = 0;
    m_wolfRage = m_wolfDied = false;
    m_jesterWinner = m_angelWinner = kNoPlayer;
    m_firstExecution = false;
    m_captain = kNoPlayer;
    m_election = m_electionHeld = false;
    m_marked = m_rustyWolf = m_servantPending = kNoPlayer;
    m_servantTook = m_secondVote = false;
    m_bearCooldown = 0.0f;
    m_healAvailable = m_poisonAvailable = false;
    for (const Player& p : m_players)
        if (p.role == Role::Witch) m_healAvailable = m_poisonAvailable = true;
    m_nightDeaths.clear();
    m_notices.clear();
    m_doors.assign(houseCount, DoorState{DoorOpen, 100});
    m_doorNoise.assign(houseCount, 0.0f);
    m_fire.assign(houseCount, 0.0f);
    m_doused = 0;

    std::vector<std::string> wolves;
    bool neutral = false, littleGirl = false;
    for (const Player& p : m_players) {
        if (isWolfRole(p.role)) wolves.push_back(p.name);
        neutral |= roleFaction(p.role) == Faction::Neutral;
        littleGirl |= p.role == Role::LittleGirl;
    }
    narrate(std::format("Willkommen im Dorf! {} Seelen leben hier – {} davon {}.", m_players.size(), wolves.size(),
                        wolves.size() == 1 ? "ist ein Werwolf" : "sind Werwölfe"));
    if (neutral) narrate("Doch nicht jeder im Dorf verfolgt dieselben Ziele …");
    for (const Player& p : m_players) {
        if (p.role == Role::Mayor) narrate(std::format("{} ist der Bürgermeister – seine Stimme zählt doppelt.", p.name));
        if (p.role == Role::BearTamer) narrate(std::format("{} ist der Bärenführer. Sein Bär brummt, wenn ein Werwolf in der Nähe ist.", p.name));
        if (p.role == Role::HonestVillager) narrate(std::format("{} ist bekanntermaßen ein ehrlicher, einfacher Dorfbewohner.", p.name));
    }

    for (const Player& p : m_players) {
        tell(p.id, std::format("Deine Rolle: {}. {}", roleName(p.role), roleDescription(p.role)));
        if (p.house != kNoPlayer)
            tell(p.id, "Dein Haus ist auf der Karte markiert. Schließt du die Tür von innen (Q), ist sie verriegelt.");
        if (isWolfRole(p.role)) {
            std::vector<std::string> others;
            for (const Player& o : m_players) {
                if (!isWolfRole(o.role) || o.id == p.id) continue;
                // Der weiße Werwolf gibt sich als gewöhnlicher Werwolf aus
                const Role shown = o.role == Role::WhiteWolf ? Role::Werewolf : o.role;
                others.push_back(std::format("{} ({})", o.name, roleName(shown)));
            }
            tell(p.id, others.empty() ? std::string("Du jagst allein – kein anderer Werwolf ist im Dorf.")
                                      : "Dein Rudel: " + joinNames(others) + ". Euer Rudel-Chat ist geheim.");
            if (littleGirl) tell(p.id, "Vorsicht: Ein kleines Mädchen spitzt nachts die Ohren und belauscht euren Rudel-Chat.");
        }
        if (p.role == Role::Sister || p.role == Role::Brother) {
            std::vector<std::string> family;
            for (const Player& o : m_players)
                if (o.id != p.id && o.role == p.role) family.push_back(o.name);
            tell(p.id, std::format("{}: {}. Mit Tab sprecht ihr heimlich miteinander.",
                                   p.role == Role::Sister ? "Deine Schwester" : "Deine Brüder", joinNames(family)));
        }
        if (p.role == Role::Sectarian) {
            std::vector<std::string> mine, other;
            for (const Player& o : m_players)
                if (o.id != p.id) (sectOf(o) == sectOf(p) ? mine : other).push_back(o.name);
            tell(p.id, std::format("Deine Hälfte des Dorfes ({}): {}. Die andere Hälfte muss verschwinden: {}.",
                                   sectOf(p) == 0 ? "Osten" : "Westen", mine.empty() ? std::string("niemand") : joinNames(mine),
                                   other.empty() ? std::string("niemand") : joinNames(other)));
        }
        if (p.role == Role::Actor) {
            std::vector<std::string> cards;
            for (Role r : p.actorCards) cards.push_back(roleName(r));
            tell(p.id, "Deine Rollen für die nächsten Nächte: " + joinNames(cards) + ".");
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

std::string Match::houseLabel(int house) const {
    std::vector<std::string> owners;
    for (const Player& p : m_players)
        if (p.house == house) owners.push_back(p.name);
    if (owners.empty()) return "ein leerstehendes Haus";
    return "das Haus von " + joinNames(owners);
}

int Match::sectOf(const Player& p) const {
    if (!m_world || p.house >= m_world->houses().size()) return 0;
    return m_world->houses()[p.house].position.x >= 0.0f ? 0 : 1;
}

bool Match::sameFamily(const Player& a, const Player& b) const {
    return (a.role == Role::Sister || a.role == Role::Brother) && a.role == b.role;
}

bool Match::isProtected(uint8_t id) const {
    return std::any_of(m_players.begin(), m_players.end(), [&](const Player& p) { return p.protecting == id; });
}

void Match::notifyProtectors(uint8_t victim) {
    for (const Player& p : m_players)
        if (p.protecting == victim) tell(p.id, "Dein Schutz hat heute Nacht ein Leben gerettet!");
}

void Match::setTimer(float seconds) {
    m_timer = seconds;
    m_phaseDuration = seconds;
    m_phaseElapsed = 0.0f;
    markDirty();
}

void Match::notify(Notice::Audience audience, uint8_t player, NoticeKind kind, std::string text) {
    Notice n;
    n.audience = audience;
    n.player = player;
    n.kind = kind;
    n.text = std::move(text);
    m_notices.push_back(std::move(n));
}

void Match::notifyArea(Vec2 at, float radius, NoticeKind kind, std::string text) {
    Notice n;
    n.audience = Notice::Audience::Area;
    n.kind = kind;
    n.text = std::move(text);
    n.x = at.x;
    n.z = at.y;
    n.radius = radius;
    m_notices.push_back(std::move(n));
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
    return !m_world->lineBlocked({a.x, a.z}, {b.x, b.z}, closedDoors());
}

bool Match::mixedCouple() const {
    for (const Player& p : m_players) {
        if (p.lover == kNoPlayer) continue;
        const Player* partner = find(p.lover);
        if (!partner) return false;
        return roleTeam(p.role) != roleTeam(partner->role);
    }
    return false;
}

bool Match::doorwayOccupied(int house) const {
    // Überschneidet sich jemand mit dem Türblatt (in Hauskoordinaten)?
    const House& h = m_world->houses()[static_cast<size_t>(house)];
    const float doorZ = h.depth * 0.5f - House::kWall * 0.5f;
    for (const Player& p : m_players) {
        if (!p.alive) continue;
        const Vec2 local = m_world->worldToHouse(house, {p.position.x, p.position.z});
        if (std::fabs(local.x) < House::kDoorWidth * 0.5f + kPlayerRadius &&
            std::fabs(local.y - doorZ) < House::kWall * 0.5f + kPlayerRadius)
            return true;
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

void Match::setCaptain(uint8_t id, std::string text) {
    m_captain = id;
    narrate(std::move(text));
    if (id != kNoPlayer) tell(id, "Als Hauptmann zählt deine Stimme doppelt. Stirbst du, bestimmst du deinen Nachfolger.");
    markDirty();
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
    const Vec2 from{p->position.x, p->position.z};
    const float dist = (target - from).length();
    if (dist > maxSpeed * static_cast<float>(dt) + 1.5f) {
        p->needsCorrection = true; // zu schnell: zurücksetzen
        return;
    }
    if (dist > 0.001f && m_world->lineBlocked(from, target, closedDoors())) {
        p->needsCorrection = true; // durch eine Wand oder geschlossene Tür
        return;
    }
    p->position = m_world->ground(target.x, target.y);
    p->yaw = yaw;
    p->moveFlags = flags & EntitySprint;
}

// --- Abfragen ----------------------------------------------------------------

uint32_t Match::abilitiesFor(uint8_t id) const {
    const Player* p = find(id);
    if (!p) return 0;
    uint32_t a = 0;
    if (p->hunterTimer > 0.0f) a |= AbilityHunterShot;
    if (p->successorTimer > 0.0f) a |= AbilityNameSuccessor;
    if (!p->alive) return a;
    const bool night = m_phase == Phase::Night;
    const bool day = m_phase == Phase::Day;
    const bool firstNight = night && m_day == 1;
    switch (p->effectiveRole()) {
    case Role::Werewolf:
    case Role::AlphaWolf:
    case Role::WolfCub:
    case Role::BigBadWolf:
    case Role::WhiteWolf:
        if (night) a |= AbilityTransform;
        if (night && p->wolfForm()) {
            a |= AbilityBreakDoor;
            if (m_packKills > 0) a |= AbilityAttack;
            if (m_packKills > 0 && p->role == Role::AlphaWolf && !p->infectUsed) a |= AbilityInfect;
            if (p->role == Role::WhiteWolf && m_day % 2 == 0 && !p->nightAbilityUsed) a |= AbilityBetray;
        }
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
        if (firstNight && p->cupidPicks.size() < 2) a |= AbilityCupid;
        break;
    case Role::Tracker:
        if (night && !p->nightAbilityUsed) a |= AbilityTrack;
        break;
    case Role::Blacksmith:
        if (day && !p->dayAbilityUsed) a |= AbilityReinforce;
        break;
    case Role::Priest:
        if ((day || night) && !p->holyWaterUsed) a |= AbilityHolyWater;
        break;
    case Role::SerialKiller:
        if (night) a |= AbilityBreakDoor;
        if (night && !p->nightAbilityUsed) a |= AbilityStab;
        break;
    case Role::Arsonist:
        if (night && !p->nightAbilityUsed) {
            a |= AbilityDouse;
            if (m_doused) a |= AbilityIgnite;
        }
        break;
    case Role::Thief:
        if (firstNight && !p->nightAbilityUsed) a |= AbilitySteal;
        break;
    case Role::Fox:
        if (night && p->foxActive && !p->nightAbilityUsed) a |= AbilitySniff;
        break;
    case Role::WildChild:
        if (m_day == 1 && (day || night) && p->model == kNoPlayer) a |= AbilityChooseModel;
        break;
    case Role::WolfHound:
        if (firstNight && !p->sideChosen) a |= AbilityChooseSide;
        break;
    case Role::Raven:
        if (night && !p->nightAbilityUsed) a |= AbilityMark;
        break;
    case Role::Piper:
        if (night && p->enchantsTonight < 2) a |= AbilityEnchant;
        break;
    case Role::Judge:
        if (m_phase == Phase::Vote && !m_election && !p->judgeUsed) a |= AbilitySecondVote;
        break;
    case Role::Servant:
        if (m_phase == Phase::Execution && m_servantPending != kNoPlayer && m_servantPending != id && !m_servantTook)
            a |= AbilityTakeRole;
        break;
    default:
        break;
    }
    if (m_phase == Phase::Vote && !p->idiotRevealed) a |= AbilityVote;
    if (day && m_day >= 2 && !m_bellRung) a |= AbilityRingBell;
    return a;
}

std::string Match::checkDoor(const Player& p, uint8_t house, float range) const {
    if (house >= m_doors.size()) return "Hier ist keine Tür.";
    if (m_doors[house].flags & DoorBurnt) return "Von diesem Haus ist nur Asche übrig.";
    const Vec2 door = m_world->doorCenter(house);
    if ((Vec2{p.position.x, p.position.z} - door).length() > range + kRangeTolerance) return "Geh näher an die Tür.";
    return {};
}

std::string Match::checkInteraction(uint8_t id, Interaction kind, uint8_t targetId) const {
    const Player* p = find(id);
    if (!p) return "Du nimmst nicht an diesem Spiel teil.";
    const Player* t = find(targetId);
    const uint32_t abilities = abilitiesFor(id);
    auto distTo = [&](const Player& other) { return distanceXZ(p->position, other.position); };
    auto visible = [&](const Player& other) { return hasLineOfSight(p->position, other.position); };
    // Gemeinsame Prüfung für Fähigkeiten, die auf einen anderen lebenden Spieler in Reichweite zielen
    auto nearOther = [&](float range, const char* tooFar) -> std::string {
        if (!t || !t->alive || t->id == id) return "Wähle einen lebenden Mitspieler.";
        if (distTo(*t) > range + kRangeTolerance) return tooFar;
        if (!visible(*t)) return "Du kannst die Person nicht sehen.";
        return {};
    };

    switch (kind) {
    case Interaction::Transform:
        if (!(abilities & AbilityTransform)) return "Verwandeln kannst du dich nur nachts.";
        return {};
    case Interaction::Attack:
    case Interaction::Infect: {
        if (kind == Interaction::Infect && !(abilities & AbilityInfect)) {
            if (p->role != Role::AlphaWolf) return "Du kannst niemanden infizieren.";
            if (p->infectUsed) return "Deine Infektion hast du schon weitergegeben.";
        }
        const bool betray = kind == Interaction::Attack && t && t->alive && t->id != id && isWolfRole(t->role) &&
                            (abilities & AbilityBetray);
        if (!betray && !(abilities & AbilityAttack)) {
            if (!isWolfRole(p->role) || m_phase != Phase::Night) return "Du kannst niemanden angreifen.";
            if (!p->wolfForm()) return "Verwandle dich zuerst (R).";
            return "Das Rudel hat heute Nacht schon gefressen.";
        }
        if (p->attackCooldown > 0.0f) return "Du holst noch aus …";
        if (!t || !t->alive || t->id == id) return "Hier ist niemand.";
        if (isWolfRole(t->role) && !betray) return "Du greifst kein Mitglied deines Rudels an.";
        if (distTo(*t) > kAttackRange + kRangeTolerance) return "Zu weit entfernt.";
        if (!visible(*t)) return "Etwas versperrt dir den Weg.";
        return {};
    }
    case Interaction::Stab:
        if (!(abilities & AbilityStab)) {
            if (p->role != Role::SerialKiller || m_phase != Phase::Night) return "Du kannst niemanden angreifen.";
            return "Dein Messer hat heute Nacht schon Blut geschmeckt.";
        }
        if (p->attackCooldown > 0.0f) return "Du holst noch aus …";
        if (!t || !t->alive || t->id == id) return "Hier ist niemand.";
        if (distTo(*t) > kAttackRange + kRangeTolerance) return "Zu weit entfernt.";
        if (!visible(*t)) return "Etwas versperrt dir den Weg.";
        return {};
    case Interaction::Inspect:
        if (!(abilities & AbilityInspect)) return "Deine Vision ist für heute Nacht verbraucht.";
        return nearOther(kInspectRange, "Zu weit entfernt für eine Vision.");
    case Interaction::Protect:
        if (!(abilities & AbilityProtect)) return "Du hast heute Nacht schon jemanden beschützt.";
        if (!t || !t->alive) return "Wähle einen lebenden Mitspieler.";
        if (t->id == p->lastProtected) return "Diesen Spieler hast du letzte Nacht schon beschützt.";
        if (t->id != id && distTo(*t) > kProtectRange + kRangeTolerance) return "Geh näher heran.";
        if (t->id != id && !visible(*t)) return "Du kannst die Person nicht sehen.";
        return {};
    case Interaction::Heal:
        if (!(abilities & AbilityHeal)) return "Dein Heiltrank ist verbraucht.";
        if (!t || t->alive || t->buried) return "Hier liegt niemand, den du retten könntest.";
        if (!t->diedAtNight || t->deathDay != m_day ||
            (t->deathCause != DeathCause::Wolves && t->deathCause != DeathCause::Knife))
            return "Für diese Seele kommt jede Hilfe zu spät.";
        if (distanceXZ(p->position, t->deathPosition) > kHealRange + kRangeTolerance) return "Geh näher heran.";
        if (!hasLineOfSight(p->position, t->deathPosition)) return "Du kommst nicht an die Person heran.";
        return {};
    case Interaction::Poison:
        if (!(abilities & AbilityPoison)) return "Dein Gifttrank ist verbraucht.";
        return nearOther(kPoisonRange, "Geh näher heran.");
    case Interaction::CupidPick:
        if (!(abilities & AbilityCupid)) return "Amors Pfeile sind verschossen.";
        if (!t || !t->alive) return "Wähle einen lebenden Mitspieler.";
        if (std::find(p->cupidPicks.begin(), p->cupidPicks.end(), t->id) != p->cupidPicks.end())
            return "Diesen Spieler hast du schon gewählt.";
        if (t->id != id && distTo(*t) > kCupidRange + kRangeTolerance) return "Geh näher heran.";
        if (t->id != id && !visible(*t)) return "Du kannst die Person nicht sehen.";
        return {};
    case Interaction::HunterShot:
        if (!(abilities & AbilityHunterShot)) return "Du hast keinen Schuss mehr.";
        if (!t || !t->alive || t->id == id) return "Ziele auf einen lebenden Spieler.";
        if (distanceXZ(p->deathPosition, t->position) > kHunterRange) return "Zu weit entfernt.";
        if (!hasLineOfSight(p->deathPosition, t->position)) return "Kein freies Schussfeld.";
        return {};
    case Interaction::Track:
        if (!(abilities & AbilityTrack)) return "Du verfolgst heute Nacht schon eine Fährte.";
        return nearOther(kTrackRange, "Zu weit entfernt.");
    case Interaction::HolyWater:
        if (!(abilities & AbilityHolyWater)) {
            if (p->role != Role::Priest) return "Du hast kein Weihwasser.";
            if (p->holyWaterUsed) return "Dein Weihwasser ist aufgebraucht.";
            return "Jetzt ist nicht die Zeit dafür.";
        }
        return nearOther(kHolyWaterRange, "Geh näher heran.");
    case Interaction::Steal:
        if (!(abilities & AbilitySteal)) return "Stehlen kannst du nur in der ersten Nacht.";
        return nearOther(kStealRange, "Schleich dich näher heran.");
    case Interaction::Enchant:
        if (!(abilities & AbilityEnchant)) return "Deine Flöte schweigt für heute Nacht.";
        if (t && t->enchanted) return "Diese Person ist schon verzaubert.";
        return nearOther(kEnchantRange, "Geh näher heran, damit sie deine Melodie hört.");
    case Interaction::Sniff:
        if (!(abilities & AbilitySniff)) return p->foxActive ? "Du hast heute Nacht schon gewittert." : "Deine Nase hat dich verlassen.";
        return nearOther(kSniffRange, "Zu weit entfernt für deine Nase.");
    case Interaction::Mark:
        if (!(abilities & AbilityMark)) return "Du hast heute Nacht schon jemanden belastet.";
        return nearOther(kMarkRange, "Zu weit entfernt.");
    case Interaction::ChooseModel:
        if (!(abilities & AbilityChooseModel)) return "Du hast dein Vorbild schon gewählt.";
        return nearOther(kModelRange, "Geh näher heran.");
    case Interaction::ChooseSide:
        if (!(abilities & AbilityChooseSide)) return "Du hast dich bereits entschieden.";
        if (targetId > 1) return "Unbekannte Entscheidung.";
        return {};
    case Interaction::SecondVote:
        if (!(abilities & AbilitySecondVote))
            return p->judgeUsed ? "Du hast deine zweite Abstimmung schon verlangt." : "Das geht nur während einer Abstimmung.";
        return {};
    case Interaction::TakeRole:
        if (!(abilities & AbilityTakeRole)) return "Hier gibt es keine Rolle zu übernehmen.";
        if (targetId != m_servantPending) return "Du kannst nur die Rolle des Hingerichteten übernehmen.";
        return {};
    case Interaction::NameSuccessor:
        if (!(abilities & AbilityNameSuccessor)) return "Du bist nicht der Hauptmann.";
        if (!t || !t->alive || t->id == id) return "Wähle einen lebenden Nachfolger.";
        return {};
    case Interaction::Vote:
        if (!(abilities & AbilityVote)) {
            if (p->idiotRevealed && m_phase == Phase::Vote) return "Als Dorfdepp darfst du nicht mehr abstimmen.";
            return "Gerade wird nicht abgestimmt.";
        }
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
    case Interaction::DoorToggle: {
        if (!p->alive) return "Geister gleiten durch Türen hindurch.";
        if (m_phase == Phase::GameOver) return "Das Spiel ist vorbei.";
        if (std::string why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if (d.flags & DoorBroken) return "Die Tür ist zerschmettert und lässt sich nicht mehr schließen.";
        const bool inside = m_world->houseAt({p->position.x, p->position.z}, 0.3f) == targetId;
        if (!(d.flags & DoorOpen) && (d.flags & DoorLocked) && !inside) return "Die Tür ist von innen verriegelt.";
        if ((d.flags & DoorOpen) && doorwayOccupied(targetId)) return "Jemand steht in der Tür.";
        return {};
    }
    case Interaction::DoorBreak: {
        if (!(abilities & AbilityBreakDoor)) {
            if (isWolfRole(p->role) && m_phase == Phase::Night) return "Verwandle dich zuerst (R).";
            return "Du bist nicht stark genug, um eine Tür einzuschlagen.";
        }
        if (std::string why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if (d.flags & (DoorOpen | DoorBroken)) return "Die Tür steht doch offen.";
        if (!(d.flags & DoorLocked)) return "Die Tür ist nicht verriegelt – öffne sie einfach (Q).";
        if (p->attackCooldown > 0.0f) return "Du holst noch aus …";
        return {};
    }
    case Interaction::Reinforce: {
        if (!(abilities & AbilityReinforce)) {
            if (p->role != Role::Blacksmith) return "Du bist kein Schmied.";
            if (m_phase != Phase::Day) return "Schmieden kannst du nur tagsüber.";
            return "Für heute ist deine Esse erkaltet.";
        }
        if (std::string why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if ((d.flags & DoorReinforced) && !(d.flags & DoorBroken) && d.health >= 100)
            return "Diese Tür ist bereits mit Eisen beschlagen.";
        return {};
    }
    case Interaction::Douse: {
        if (!(abilities & AbilityDouse)) {
            if (p->role != Role::Arsonist) return "Du hast kein Öl dabei.";
            if (m_phase != Phase::Night) return "Am helllichten Tag würde man dich sehen.";
            return "Für heute Nacht hast du genug getan.";
        }
        if (std::string why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        if (m_doused & (1u << targetId)) return "Dieses Haus ist schon mit Öl getränkt.";
        if (m_doors[targetId].flags & DoorBurning) return "Dieses Haus brennt bereits.";
        return {};
    }
    case Interaction::Ignite:
        if (!(abilities & AbilityIgnite)) {
            if (p->role != Role::Arsonist) return "Du hast nichts zum Anzünden.";
            if (!m_doused) return "Noch ist kein Haus mit Öl getränkt.";
            if (m_phase != Phase::Night) return "Warte auf die Dunkelheit.";
            return "Für heute Nacht hast du genug getan.";
        }
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
            if (night && p.transform < 0.01f && p.role != Role::SerialKiller && p.role != Role::Arsonist)
                e.flags |= EntityLantern;
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
    v.election = m_election;

    const Player* me = find(viewer);
    const bool over = m_phase == Phase::GameOver;
    const bool spectator = !me || !me->alive;
    const bool meWolf = me && isWolfRole(me->role);
    const bool showVotes = m_phase == Phase::Vote || m_phase == Phase::Execution;
    const bool seesEnchanted = spectator || over || (me && (me->role == Role::Piper || me->enchanted));

    for (const Player& p : m_players) {
        PlayerView pv;
        pv.id = p.id;
        pv.name = p.name;
        pv.alive = p.alive;
        pv.connected = p.connected;
        pv.bot = p.bot;
        pv.house = p.house;
        pv.captain = p.id == m_captain;
        pv.marked = p.id == m_marked && m_phase != Phase::Night;
        pv.enchanted = p.enchanted && seesEnchanted;
        const bool revealedDead = !p.alive && m_settings.revealRoles && p.id != m_servantPending;
        bool knowRole = over || spectator || p.id == viewer || revealedDead || (me && me->lover == p.id) ||
                        isPublicRole(p.role) || p.idiotRevealed || (me && sameFamily(*me, p));
        Role shown = p.role;
        if (!knowRole && meWolf && isWolfRole(p.role)) {
            knowRole = true;
            if (p.role == Role::WhiteWolf) shown = Role::Werewolf; // tarnt sich im Rudel
        }
        if (!knowRole && me)
            for (const SeerResult& r : me->seerResults)
                if (r.player == p.id) {
                    knowRole = true;
                    shown = r.role;
                }
        pv.role = knowRole ? shown : Role::Unknown;
        pv.lover = p.lover != kNoPlayer &&
                   (over || spectator || p.id == viewer || (me && (me->lover == p.id || me->role == Role::Cupid)));
        if (showVotes && p.alive) pv.vote = p.vote;
        pv.winner = over && isWinner(p.id);
        v.players.push_back(std::move(pv));
    }

    if (me) {
        v.yourRole = me->role;
        v.acting = me->acting;
        v.abilities = abilitiesFor(viewer);
        v.vote = me->vote;
        v.loverPartner = me->lover;
        v.cupidPicks = me->cupidPicks;
        v.hunterTimeLeft = me->hunterTimer;
        v.successorTimeLeft = me->successorTimer;
        v.seerResults = me->seerResults;
        v.lastProtected = me->lastProtected;
        v.protectedTarget = m_phase == Phase::Night ? me->protecting : kNoPlayer;
        v.model = me->model;
        if (v.abilities & AbilityTakeRole) v.condemned = m_servantPending;
        if (me->role == Role::Witch) {
            v.healAvailable = m_healAvailable;
            v.poisonAvailable = m_poisonAvailable;
        }
        if (meWolf) v.packKillsLeft = m_packKills;
        if (me->effectiveRole() == Role::Tracker && m_phase == Phase::Night) v.tracked = me->trackTarget;
        if (me->role == Role::Arsonist) v.doused = m_doused;
        if (me->role == Role::Elder) v.shieldLeft = me->shield;
    }
    return v;
}

std::optional<ChatChannel> Match::chatChannel(uint8_t speaker, bool wantSecret) const {
    const Player* p = find(speaker);
    if (!p) return std::nullopt;
    if (m_phase == Phase::GameOver) return ChatChannel::Global;
    if (!p->alive) return ChatChannel::Dead;
    if (wantSecret && isWolfRole(p->role)) return ChatChannel::Wolves;
    if (wantSecret && p->effectiveRole() == Role::Medium && m_phase == Phase::Night) return ChatChannel::Dead;
    if (wantSecret && (p->role == Role::Sister || p->role == Role::Brother)) return ChatChannel::Family;
    if (isGatheringPhase(m_phase)) return ChatChannel::Global;
    return ChatChannel::Nearby;
}

bool Match::canReadChannel(uint8_t listener, ChatChannel channel) const {
    const Player* l = find(listener);
    if (!l) return channel == ChatChannel::Global;
    const bool over = m_phase == Phase::GameOver;
    const bool night = m_phase == Phase::Night;
    switch (channel) {
    case ChatChannel::Global:
    case ChatChannel::Nearby: return true;
    case ChatChannel::Wolves:
        return isWolfRole(l->role) || !l->alive || over || (l->effectiveRole() == Role::LittleGirl && night);
    case ChatChannel::Dead: return !l->alive || over || (l->effectiveRole() == Role::Medium && night);
    case ChatChannel::Family: return !l->alive || over || l->role == Role::Sister || l->role == Role::Brother;
    }
    return false;
}

bool Match::hidesSpeaker(uint8_t listener, ChatChannel channel) const {
    const Player* l = find(listener);
    return l && channel == ChatChannel::Wolves && l->alive && !isWolfRole(l->role) &&
           l->effectiveRole() == Role::LittleGirl && m_phase != Phase::GameOver;
}

bool Match::canHear(uint8_t listener, uint8_t speaker, ChatChannel channel) const {
    if (!canReadChannel(listener, channel)) return false;
    const Player* l = find(listener);
    const Player* s = find(speaker);
    if (channel == ChatChannel::Family) {
        if (!l || !s) return false;
        return !l->alive || m_phase == Phase::GameOver || l->id == s->id || sameFamily(*l, *s);
    }
    if (channel != ChatChannel::Nearby) return true;
    if (!l || !s) return false;
    if (!l->alive || l->id == s->id) return true; // Geister hören alles
    const float d = distanceXZ(l->position, s->position);
    if (d > kNearbyChatRange) return false;
    // Hinter geschlossenen Türen und Wänden hört man nur aus der Nähe
    return d <= 6.0f || hasLineOfSight(l->position, s->position);
}

Team Match::teamOf(uint8_t id) const {
    const Player* p = find(id);
    if (!p) return Team::None;
    if (p->lover != kNoPlayer && mixedCouple()) return Team::Lovers;
    return roleTeam(p->role);
}

bool Match::isWinner(uint8_t id) const {
    if (m_winner == Team::None || m_winner == Team::Nobody) return false;
    if (m_winner == Team::Jester) return id == m_jesterWinner;
    if (m_winner == Team::Angel) return id == m_angelWinner;
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
        if (isWolfRole(t->role)) {
            // Der weiße Werwolf reißt einen aus dem Rudel
            p->attackCooldown = 1.2f;
            p->attackAnim = 0.4f;
            p->nightAbilityUsed = true;
            if (isProtected(t->id)) {
                notifyProtectors(t->id);
                tell(p->id, "Eine unsichtbare Macht schützt dieses Opfer!");
                break;
            }
            narrate("Ein grauenvoller Schrei zerreißt die Stille der Nacht …");
            kill(t->id, DeathCause::Wolves, false, p->id);
        } else {
            wolfStrike(*p, *t, false);
        }
        break;
    case Interaction::Infect:
        wolfStrike(*p, *t, true);
        break;
    case Interaction::Stab:
        p->attackCooldown = 1.2f;
        p->attackAnim = 0.4f;
        p->nightAbilityUsed = true;
        if (isProtected(t->id)) {
            tell(p->id, "Jemand wacht über dieses Opfer – du musst fliehen, bevor man dich erkennt!");
            notifyProtectors(t->id);
            break;
        }
        narrate("Ein erstickter Schrei hallt durch die Nacht …");
        kill(t->id, DeathCause::Knife, false, p->id);
        break;
    case Interaction::Inspect:
        p->nightAbilityUsed = true;
        if (std::none_of(p->seerResults.begin(), p->seerResults.end(), [&](const SeerResult& r) { return r.player == t->id; }))
            p->seerResults.push_back({t->id, t->role});
        tell(p->id, std::format("Deine Vision zeigt dir: {} ist {}.", t->name, roleWithArticle(t->role)));
        break;
    case Interaction::Protect:
        p->nightAbilityUsed = true;
        p->protecting = t->id;
        p->lastProtected = t->id;
        tell(p->id, std::format("Du wachst heute Nacht über {}.", t->id == p->id ? std::string("dich selbst") : t->name));
        break;
    case Interaction::Heal: {
        m_healAvailable = false;
        auto revive = [&](Player& r) {
            r.alive = true;
            r.position = r.deathPosition;
            r.needsCorrection = true;
            r.hunterTimer = 0.0f;
            r.successorTimer = 0.0f;
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
        kill(t->id, DeathCause::Poison, false, p->id);
        break;
    case Interaction::CupidPick:
        p->cupidPicks.push_back(t->id);
        if (p->cupidPicks.size() == 2) makeLovers(p->cupidPicks[0], p->cupidPicks[1]);
        else tell(p->id, std::format("{} ist getroffen. Wähle die zweite Person.", t->name));
        break;
    case Interaction::HunterShot:
        p->hunterTimer = 0.0f;
        narrate(std::format("Ein Schuss hallt durch das Dorf! {} hat mit dem letzten Atemzug abgedrückt.", p->name));
        kill(t->id, DeathCause::Hunter, true, p->id);
        break;
    case Interaction::Track:
        p->nightAbilityUsed = true;
        p->trackTarget = t->id;
        p->trackHouses.clear();
        p->trackVictims.clear();
        tell(p->id, std::format("Du heftest dich an die Fersen von {}. Bis zum Morgen siehst du, wohin die Fährte führt.", t->name));
        break;
    case Interaction::HolyWater: {
        p->holyWaterUsed = true;
        const bool night = m_phase == Phase::Night;
        if (isWolfRole(t->role)) {
            tell(p->id, std::format("Das Weihwasser zischt auf der Haut von {} – ein Werwolf!", t->name));
            if (!night) narrate(std::format("{} besprengt {} mit Weihwasser – die Haut zischt und brennt! Ein Werwolf!", p->name, t->name));
            kill(t->id, DeathCause::HolyWater, !night, p->id);
        } else {
            tell(p->id, std::format("Das Weihwasser perlt einfach ab – {} ist kein Werwolf.", t->name));
            tell(t->id, std::format("{} hat dich mit Weihwasser besprengt. Es perlt einfach ab.", p->name));
        }
        break;
    }
    case Interaction::Steal: {
        // Die gestohlene Rolle wirkt noch in derselben Nacht
        const Role stolen = t->role;
        p->role = stolen;
        p->shield = t->shield;
        p->actorCards = std::move(t->actorCards);
        p->model = t->model;
        p->sideChosen = t->sideChosen;
        t->role = Role::Villager;
        t->shield = false;
        t->actorCards.clear();
        t->acting = Role::Unknown;
        t->model = kNoPlayer;
        tell(t->id, "Im Schlaf hat dir jemand deine Rolle gestohlen – du bist jetzt ein einfacher Dorfbewohner.");
        tell(p->id, std::format("Du hast {} die Rolle gestohlen: {}. {}", t->name, roleName(stolen), roleDescription(stolen)));
        if (isWolfRole(stolen)) {
            tellWolves(std::format("{} hat die Rolle von {} gestohlen und jagt nun mit euch.", p->name, t->name));
            tell(t->id, "Dein Rudel hat dich verloren – du gehörst jetzt zum Dorf.");
        }
        break;
    }
    case Interaction::Enchant:
        ++p->enchantsTonight;
        t->enchanted = true;
        t->newlyEnchanted = true;
        tell(p->id, std::format("Deine Melodie umschmeichelt {} – verzaubert!", t->name));
        break;
    case Interaction::Sniff: {
        p->nightAbilityUsed = true;
        bool wolf = false;
        for (const Player& o : m_players)
            if (o.alive && isWolfRole(o.role) && distanceXZ(o.position, t->position) <= kSniffRadius) wolf = true;
        if (wolf) {
            tell(p->id, std::format("Deine Nase zuckt: Bei {} oder ganz in der Nähe wittert du einen Werwolf!", t->name));
        } else {
            p->foxActive = false;
            tell(p->id, std::format("Kein Werwolf bei {} – und deine Nase hat dich verlassen. Du bist jetzt ein einfacher Fuchs.", t->name));
        }
        break;
    }
    case Interaction::Mark:
        p->nightAbilityUsed = true;
        m_marked = t->id;
        tell(p->id, std::format("Du hast {} belastet. Morgen erfährt das ganze Dorf davon.", t->name));
        break;
    case Interaction::ChooseModel:
        p->model = t->id;
        tell(p->id, std::format("{} ist dein Vorbild. Stirbt {}, wirst du zum Werwolf.", t->name, t->name));
        break;
    case Interaction::ChooseSide:
        p->sideChosen = true;
        if (targetId == 1) becomeWolf(*p, "Du folgst dem Ruf des Rudels.");
        else tell(p->id, "Du bleibst dem Dorf treu – ein guter Hund.");
        break;
    case Interaction::SecondVote:
        p->judgeUsed = true;
        m_secondVote = true;
        tell(p->id, "Nach diesem Urteil folgt eine zweite Abstimmung.");
        break;
    case Interaction::TakeRole: {
        m_servantTook = true;
        const Role taken = t->role;
        narrate(std::format("{} tritt vor: Die ergebene Magd übernimmt die Rolle von {} – welche, bleibt ihr Geheimnis.", p->name, t->name));
        p->role = taken;
        p->shield = taken == Role::Elder;
        t->role = Role::Servant;
        tell(p->id, std::format("Deine neue Rolle: {}. {}", roleName(taken), roleDescription(taken)));
        if (isWolfRole(taken)) tellWolves(std::format("{} hat die Rolle des Hingerichteten übernommen und gehört jetzt zum Rudel.", p->name));
        break;
    }
    case Interaction::NameSuccessor:
        p->successorTimer = 0.0f;
        setCaptain(t->id, std::format("{} übergibt mit letzter Kraft das Amt des Hauptmanns an {}.", p->name, t->name));
        break;
    case Interaction::Vote:
        p->vote = targetId;
        checkVotesComplete();
        break;
    case Interaction::RingBell:
        m_bellRung = true;
        enterAssembly(std::format("{} läutet die Glocke! Alle Bewohner versammeln sich am Feuer.", p->name),
                      m_settings.discussionSeconds);
        break;
    case Interaction::DoorToggle: {
        DoorState& d = m_doors[targetId];
        const bool inside = m_world->houseAt({p->position.x, p->position.z}, 0.3f) == targetId;
        if (d.flags & DoorOpen) {
            d.flags &= static_cast<uint8_t>(~DoorOpen);
            if (inside) d.flags |= DoorLocked;
        } else {
            d.flags |= DoorOpen;
            d.flags &= static_cast<uint8_t>(~DoorLocked);
        }
        break;
    }
    case Interaction::DoorBreak: {
        DoorState& d = m_doors[targetId];
        p->attackCooldown = kDoorHitCooldown;
        p->attackAnim = 0.4f;
        const int damage = (d.flags & DoorReinforced) ? kReinforcedDoorDamage : kDoorDamage;
        d.health = static_cast<uint8_t>(std::max(0, d.health - damage));
        const Vec2 door = m_world->doorCenter(targetId);
        if (d.health == 0) {
            d.flags = static_cast<uint8_t>((d.flags & (DoorBurning)) | DoorBroken | DoorOpen);
            notifyArea(door, kDoorNoiseRadius, NoticeKind::Warning,
                       std::format("Mit einem Krachen birst die Tür – {} steht offen!", houseLabel(targetId)));
            m_doorNoise[targetId] = kDoorNoiseInterval;
        } else if (m_doorNoise[targetId] <= 0.0f) {
            notifyArea(door, kDoorNoiseRadius, NoticeKind::Warning,
                       std::format("Lautes Krachen von Holz! Etwas hämmert gegen die Tür – {}.", houseLabel(targetId)));
            m_doorNoise[targetId] = kDoorNoiseInterval;
        }
        break;
    }
    case Interaction::Reinforce: {
        DoorState& d = m_doors[targetId];
        p->dayAbilityUsed = true;
        const bool broken = d.flags & DoorBroken;
        d.flags = static_cast<uint8_t>((d.flags & ~(DoorBroken | DoorLocked)) | DoorReinforced);
        if (broken) d.flags |= DoorOpen;
        d.health = 100;
        tell(p->id, std::format("{} – du beschlägst die Tür mit Eisen. Sie wird Werwölfe doppelt so lange aufhalten.",
                                broken ? "Du setzt die zerschmetterte Tür wieder ein" : "Hammer, Amboss, Nieten"));
        break;
    }
    case Interaction::Douse:
        p->nightAbilityUsed = true;
        m_doused |= 1u << targetId;
        tell(p->id, std::format("Du tränkst {} mit Öl. Zünde es an, wann immer du willst (F).", houseLabel(targetId)));
        break;
    case Interaction::Ignite: {
        p->nightAbilityUsed = true;
        std::vector<std::string> names;
        for (size_t i = 0; i < m_doors.size(); ++i) {
            if (!(m_doused & (1u << i))) continue;
            m_doors[i].flags |= DoorBurning;
            m_fire[i] = kFireSeconds;
            names.push_back(houseLabel(static_cast<int>(i)));
        }
        m_doused = 0;
        narrate(std::format("Feuer! Flammen schlagen aus {}! Wer drinnen ist, muss sofort hinaus!", joinNames(names)));
        break;
    }
    }
    markDirty();
    return {};
}

void Match::wolfStrike(Player& wolf, Player& victim, bool infect) {
    wolf.attackCooldown = 1.2f;
    wolf.attackAnim = 0.4f;
    if (isProtected(victim.id)) {
        wolf.attackCooldown = 3.0f;
        tell(wolf.id, "Eine unsichtbare Macht schützt dieses Opfer – deine Klauen gleiten ab!");
        notifyProtectors(victim.id);
        return;
    }
    if (victim.role == Role::SerialKiller) {
        wolf.attackCooldown = 3.0f;
        tell(wolf.id, "Dein Opfer reißt eine Klinge hoch und wehrt dich ab – das ist kein gewöhnlicher Mensch!");
        tell(victim.id, std::format("Ein Werwolf hat dich angefallen – doch dein Messer war schneller. Es war {}.", wolf.name));
        return;
    }
    --m_packKills;
    if (infect) {
        wolf.infectUsed = true;
        becomeWolf(victim, "Ein Biss in der Dunkelheit … das Gift des Urwolfs fließt durch deine Adern.");
        tellWolves(std::format("Der Urwolf hat {} infiziert – {} gehört jetzt zum Rudel!", victim.name, victim.name));
    } else if (victim.role == Role::Cursed) {
        becomeWolf(victim, "Die Klauen reißen deine Haut auf … und der Fluch erwacht!");
        tellWolves(std::format("{} überlebt den Angriff – der Fluch hat {} zu einem von euch gemacht!", victim.name, victim.name));
    } else if (victim.shield) {
        victim.shield = false;
        tell(victim.id, "Du hast den Angriff mit letzter Kraft überstanden. Ein zweites Mal wirst du nicht so viel Glück haben.");
        tell(wolf.id, "Dein Opfer wehrt sich zäh und entkommt deinen Klauen – es ist der Dorfälteste!");
        narrate("Kampfgeräusche dringen durch die Nacht … dann ist es wieder still.");
    } else {
        narrate("Ein grauenvoller Schrei zerreißt die Stille der Nacht …");
        if (victim.role == Role::Knight) m_rustyWolf = wolf.id;
        kill(victim.id, DeathCause::Wolves, false, wolf.id);
    }
    if (m_packKills == 0 && m_phase == Phase::Night) tellWolves("Das Rudel ist für heute Nacht gesättigt.");
}

void Match::becomeWolf(Player& p, const std::string& reason) {
    p.role = Role::Werewolf;
    p.shield = false;
    std::vector<std::string> pack;
    for (const Player& o : m_players)
        if (o.alive && isWolfRole(o.role) && o.id != p.id) pack.push_back(o.name);
    tell(p.id, reason + " Du bist jetzt ein Werwolf! Dein Rudel: " + (pack.empty() ? std::string("niemand") : joinNames(pack)) +
                   ". Verwandle dich nachts mit R.");
    markDirty();
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

void Match::kill(uint8_t id, DeathCause cause, bool announceNow, uint8_t killer) {
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
    p->burn = 0.0f;
    if (p->role == Role::Hunter && cause != DeathCause::Left) {
        p->hunterTimer = kHunterShotSeconds;
        tell(p->id, "Du stirbst – doch dein Gewehr ist geladen! Ziele auf jemanden und drücke E.");
    }
    if (m_captain == id) {
        if (cause == DeathCause::Left) {
            std::vector<uint8_t> candidates;
            for (const Player& o : m_players)
                if (o.alive) candidates.push_back(o.id);
            if (!candidates.empty()) {
                const uint8_t next = m_rng.pick(candidates);
                setCaptain(next, std::format("{} wird neuer Hauptmann.", nameOf(next)));
            } else {
                m_captain = kNoPlayer;
            }
        } else {
            p->successorTimer = kSuccessorSeconds;
            tell(p->id, "Als Hauptmann bestimmst du jetzt deinen Nachfolger: Schau jemanden an und drücke E.");
        }
    }
    switch (cause) {
    case DeathCause::Wolves: tell(p->id, "Klauen aus der Dunkelheit – du wurdest von einem Werwolf zerrissen."); break;
    case DeathCause::Poison: tell(p->id, "Ein brennender Schmerz … du wurdest vergiftet."); break;
    case DeathCause::Knife: tell(p->id, "Eine Klinge blitzt im Dunkeln … du wurdest erstochen."); break;
    case DeathCause::Fire: tell(p->id, "Rauch, Hitze, Flammen … du bist im Feuer umgekommen."); break;
    case DeathCause::HolyWater: tell(p->id, "Das Weihwasser brennt wie Feuer – deine Wolfsgestalt zerfällt zu Asche."); break;
    case DeathCause::Rust: tell(p->id, "Die Wunde von der rostigen Klinge des Ritters hat sich entzündet … du stirbst."); break;
    default: break;
    }
    if (isWolfRole(p->role)) m_wolfDied = true;
    if (p->role == Role::WolfCub) {
        m_wolfRage = true;
        tellWolves("Das Wolfsjunge ist tot! Rasend vor Zorn dürft ihr in der nächsten Nacht ein Opfer mehr reißen.");
    }
    if (cause == DeathCause::Vote && p->role == Role::Jester && m_jesterWinner == kNoPlayer) m_jesterWinner = p->id;
    if (p->role == Role::Angel && m_angelWinner == kNoPlayer &&
        ((p->diedAtNight && m_day == 1) || (cause == DeathCause::Vote && !m_firstExecution)))
        m_angelWinner = p->id;

    // Wilde Kinder verlieren ihr Vorbild
    for (Player& c : m_players)
        if (c.alive && c.role == Role::WildChild && c.model == id)
            becomeWolf(c, std::format("{} ist tot – dein Vorbild hat dich verlassen und das Wilde in dir erwacht.", p->name));

    // Fährtenleser: Führt die Fährte zu einem Opfer?
    if (killer != kNoPlayer)
        for (Player& t : m_players)
            if (t.trackTarget == killer && m_phase == Phase::Night) t.trackVictims.push_back(id);

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
    case DeathCause::Scapegoat: text = p->name + " wurde als Sündenbock hingerichtet."; break;
    case DeathCause::Hunter: text = p->name + " wurde vom Jäger erschossen."; break;
    case DeathCause::Left: text = p->name + " hat das Dorf für immer verlassen."; break;
    case DeathCause::Fire: text = p->name + " ist in den Flammen umgekommen."; break;
    case DeathCause::Knife: text = p->name + " wurde erstochen aufgefunden – ein Messer, keine Klauen!"; break;
    case DeathCause::HolyWater: text = p->name + " wurde vom Weihwasser zu Asche verbrannt – ein Werwolf!"; break;
    case DeathCause::Rust: text = p->name + " ist an einer Blutvergiftung gestorben – die rostige Klinge des Ritters!"; break;
    }
    if (m_settings.revealRoles && id != m_servantPending) text += std::format(" (Rolle: {})", roleName(p->role));
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Death, std::move(text));
}

void Match::checkVotesComplete() {
    if (m_phase != Phase::Vote) return;
    for (const Player& p : m_players)
        if (p.alive && !p.idiotRevealed && p.vote == kNoPlayer) return;
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
        if (p.successorTimer > 0.0f) {
            p.successorTimer -= dt;
            if (p.successorTimer <= 0.0f) {
                p.successorTimer = 0.0f;
                std::vector<uint8_t> candidates;
                for (const Player& o : m_players)
                    if (o.alive) candidates.push_back(o.id);
                if (!candidates.empty() && m_captain == p.id) {
                    const uint8_t next = m_rng.pick(candidates);
                    setCaptain(next, std::format("{} hat keinen Nachfolger bestimmt – das Los fällt auf {}.", p.name, nameOf(next)));
                }
            }
        }
        p.attackCooldown = std::max(0.0f, p.attackCooldown - dt);
        p.attackAnim = std::max(0.0f, p.attackAnim - dt);
        if (p.alive && p.transform != p.transformTarget) {
            p.transform = eng::approach(p.transform, p.transformTarget, dt / kTransformSeconds);
            if (p.transform == p.transformTarget) markDirty();
        }
    }
    for (float& n : m_doorNoise) n = std::max(0.0f, n - dt);
    updateFires(dt);
    if (m_phase == Phase::GameOver) return;
    if (m_phase == Phase::Night) updateTracking();
    updateBear(dt);

    m_phaseElapsed += dt;
    m_timer -= dt;
    if (m_timer <= 0.0f) advance();
}

void Match::updateFires(float dt) {
    for (size_t i = 0; i < m_doors.size(); ++i) {
        if (!(m_doors[i].flags & DoorBurning)) continue;
        m_fire[i] -= dt;
        if (m_fire[i] <= 0.0f) {
            m_doors[i].flags = DoorBurnt | DoorOpen;
            m_doors[i].health = 0;
            m_fire[i] = 0.0f;
            markDirty();
        }
    }
    for (Player& p : m_players) {
        if (!p.alive) continue;
        const int h = m_world->houseAt({p.position.x, p.position.z}, 0.1f);
        const bool burning = h >= 0 && static_cast<size_t>(h) < m_doors.size() && (m_doors[h].flags & DoorBurning);
        if (!burning) {
            p.burn = 0.0f;
            continue;
        }
        if (p.burn == 0.0f) tell(p.id, "Das Haus brennt! Raus hier, sofort!");
        p.burn += dt;
        if (p.burn >= kBurnSeconds && m_phase != Phase::GameOver) {
            uint8_t arsonist = kNoPlayer;
            for (const Player& a : m_players)
                if (a.role == Role::Arsonist) arsonist = a.id;
            kill(p.id, DeathCause::Fire, m_phase != Phase::Night, arsonist);
        }
    }
}

void Match::updateTracking() {
    for (Player& t : m_players) {
        if (!t.alive || t.trackTarget == kNoPlayer) continue;
        const Player* target = find(t.trackTarget);
        if (!target || !target->alive) continue;
        const int h = m_world->houseAt({target->position.x, target->position.z});
        if (h >= 0 && h != target->house && std::find(t.trackHouses.begin(), t.trackHouses.end(), h) == t.trackHouses.end())
            t.trackHouses.push_back(h);
    }
}

void Match::updateBear(float dt) {
    m_bearCooldown = std::max(0.0f, m_bearCooldown - dt);
    if (m_phase != Phase::Day || m_bearCooldown > 0.0f) return;
    for (const Player& tamer : m_players) {
        if (!tamer.alive || tamer.role != Role::BearTamer) continue;
        for (const Player& w : m_players) {
            if (!w.alive || w.id == tamer.id || !isWolfRole(w.role)) continue;
            if (distanceXZ(w.position, tamer.position) > kBearRange) continue;
            notifyArea({tamer.position.x, tamer.position.z}, 25.0f, NoticeKind::Warning,
                       std::format("Der Bär von {} brummt plötzlich bedrohlich …", tamer.name));
            m_bearCooldown = kBearCooldown;
            return;
        }
    }
}

void Match::advance() {
    switch (m_phase) {
    case Phase::Day:
        if (m_day <= 1) {
            if (m_settings.captain && !m_electionHeld) {
                m_election = true;
                m_electionHeld = true;
                enterAssembly("Die Sonne sinkt – das Dorf versammelt sich, um einen Hauptmann zu wählen.",
                              std::max(20.0f, static_cast<float>(m_settings.discussionSeconds) * 0.5f));
            } else {
                enterNight();
            }
        } else {
            enterAssembly("Die Sonne sinkt – das Dorf versammelt sich am Feuer.", m_settings.discussionSeconds);
        }
        break;
    case Phase::Assembly: enterVote(); break;
    case Phase::Vote: resolveVote(); break;
    case Phase::Execution:
        finishServant();
        if (m_election) {
            m_election = false;
            markDirty();
            enterNight();
            break;
        }
        if (checkWin()) break;
        if (m_secondVote) {
            m_secondVote = false;
            narrate("Der Richter räuspert sich und verlangt stotternd eine zweite Abstimmung!");
            enterVote();
            break;
        }
        enterNight();
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
    for (Player& p : m_players) {
        p.vote = kNoPlayer;
        p.dayAbilityUsed = false;
    }
    // Zerschmetterte Türen werden notdürftig wieder eingesetzt
    for (DoorState& d : m_doors) {
        if (d.flags & DoorBroken) {
            d.flags = DoorOpen;
            d.health = 100;
        }
    }
    if (m_day == 1) {
        setTimer(std::max(30.0f, static_cast<float>(m_settings.daySeconds) * 0.5f));
        narrate(m_settings.captain
                    ? "Tag 1: Erkundet das Dorf und lernt euch kennen. Am Abend wählt ihr einen Hauptmann – danach "
                      "erwachen die Werwölfe."
                    : "Tag 1: Erkundet das Dorf und lernt euch kennen. Bei Einbruch der Nacht erwachen die Werwölfe – "
                      "verriegelt dann eure Türen!");
    } else {
        setTimer(m_settings.daySeconds);
        narrate(std::format("Tag {}: Sucht nach Spuren und sprecht miteinander. Die Glocke ruft zur Versammlung.", m_day));
    }
}

void Match::enterAssembly(const std::string& reason, float seconds) {
    m_phase = Phase::Assembly;
    setTimer(seconds);
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
        alive[i]->burn = 0.0f;
    }
    // Der Bär brummt, wenn ein Sitznachbar ein Werwolf ist
    const size_t n = alive.size();
    for (size_t i = 0; i < n && n > 2; ++i) {
        if (alive[i]->role != Role::BearTamer) continue;
        const Player* left = alive[(i + n - 1) % n];
        const Player* right = alive[(i + 1) % n];
        if (isWolfRole(left->role) || isWolfRole(right->role))
            narrate(std::format("Der Bär von {} brummt bedrohlich – neben ihm sitzt ein Werwolf!", alive[i]->name));
        else
            narrate(std::format("Der Bär von {} schnauft friedlich.", alive[i]->name));
    }
}

void Match::enterVote() {
    m_phase = Phase::Vote;
    m_voteGraceApplied = false;
    for (Player& p : m_players) p.vote = kNoPlayer;
    setTimer(m_settings.voteSeconds);
    if (m_election) narrate("Wählt euren Hauptmann! Schaut eure Wahl an und drückt E.");
    else narrate("Die Abstimmung beginnt! Schaut die Person an, die hingerichtet werden soll, und drückt E.");
}

void Match::resolveElection(const std::vector<std::pair<uint8_t, int>>& ranking) {
    std::vector<uint8_t> top;
    for (const auto& [id, count] : ranking)
        if (count == ranking.front().second) top.push_back(id);
    if (top.empty())
        for (const Player& p : m_players)
            if (p.alive) top.push_back(p.id);
    if (top.empty()) return;
    const uint8_t chosen = m_rng.pick(top);
    std::string text;
    if (ranking.empty()) text = std::format("Niemand hat gewählt – das Los bestimmt {} zum Hauptmann.", nameOf(chosen));
    else if (top.size() > 1) text = std::format("Gleichstand – das Los entscheidet: {} ist der neue Hauptmann!", nameOf(chosen));
    else text = std::format("{} ist zum Hauptmann gewählt!", nameOf(chosen));
    setCaptain(chosen, std::move(text));
}

void Match::resolveVote() {
    std::map<uint8_t, int> tally;
    for (const Player& p : m_players) {
        if (!p.alive || p.idiotRevealed || p.vote == kNoPlayer) continue;
        const Player* t = find(p.vote);
        if (!t || !t->alive) continue;
        int weight = 1;
        if (!m_election && p.role == Role::Mayor) ++weight;
        if (!m_election && p.id == m_captain) ++weight;
        tally[t->id] += weight;
    }
    if (!m_election && m_marked != kNoPlayer) {
        const Player* marked = find(m_marked);
        if (marked && marked->alive) tally[m_marked] += 2; // der Rabe
        m_marked = kNoPlayer;
    }
    m_phase = Phase::Execution;
    setTimer(kExecutionSeconds);
    std::vector<std::pair<uint8_t, int>> ranking(tally.begin(), tally.end());
    std::stable_sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (!ranking.empty()) {
        std::string summary = m_election ? "Wahl: " : "Stimmen: ";
        for (size_t i = 0; i < ranking.size(); ++i) {
            if (i > 0) summary += ", ";
            summary += std::format("{} ({})", nameOf(ranking[i].first), ranking[i].second);
        }
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, summary);
    }
    if (m_election) {
        resolveElection(ranking);
        return;
    }
    if (ranking.empty()) {
        narrate("Niemand hat abgestimmt. Das Dorf geht ratlos auseinander.");
        return;
    }
    if (ranking.size() > 1 && ranking[0].second == ranking[1].second) {
        for (const Player& p : m_players) {
            if (!p.alive || p.role != Role::Scapegoat) continue;
            narrate("Gleichstand! Das Dorf kann sich nicht einigen – also muss der Sündenbock dran glauben.");
            m_firstExecution = true;
            kill(p.id, DeathCause::Scapegoat, true);
            return;
        }
        narrate("Gleichstand! Das Dorf kann sich nicht einigen – niemand wird hingerichtet.");
        return;
    }
    Player* condemned = findMutable(ranking[0].first);
    if (!condemned) return;
    if (condemned->role == Role::Idiot && !condemned->idiotRevealed) {
        condemned->idiotRevealed = true;
        narrate(std::format("{} wird zum Galgen geführt … und grinst so harmlos, dass das Dorf begreift: Das ist nur der "
                            "Dorfdepp! Man lässt {} laufen – abstimmen darf {} aber nie wieder.",
                            condemned->name, condemned->name, condemned->name));
        markDirty();
        return;
    }
    if (condemned->role == Role::Jester) narrate(std::format("{} lacht schallend, als sich die Schlinge zuzieht …", condemned->name));
    // Die ergebene Magd darf noch eingreifen – bis dahin bleibt die Rolle geheim
    const bool servant = std::any_of(m_players.begin(), m_players.end(), [&](const Player& p) {
        return p.alive && p.role == Role::Servant && p.id != condemned->id;
    });
    if (servant) {
        m_servantPending = condemned->id;
        m_servantTook = false;
    }
    kill(condemned->id, DeathCause::Vote, true);
    m_firstExecution = true;
}

void Match::finishServant() {
    if (m_servantPending == kNoPlayer) return;
    const uint8_t id = m_servantPending;
    m_servantPending = kNoPlayer;
    if (!m_servantTook && m_settings.revealRoles)
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, std::format("{} war: {}.", nameOf(id), roleName(find(id)->role)));
    markDirty();
}

void Match::enterNight() {
    m_phase = Phase::Night;
    setTimer(m_settings.nightSeconds);
    bool bigBadWolf = false;
    for (const Player& p : m_players)
        if (p.alive && p.role == Role::BigBadWolf) bigBadWolf = true;
    m_packKills = static_cast<uint8_t>(1 + (m_wolfRage ? 1 : 0) + (bigBadWolf && !m_wolfDied ? 1 : 0));
    const uint8_t kills = m_packKills;
    m_wolfRage = false;
    m_nightDeaths.clear();
    for (Player& p : m_players) {
        p.nightAbilityUsed = false;
        p.enchantsTonight = 0;
        p.protecting = kNoPlayer;
        p.vote = kNoPlayer;
        p.trackTarget = kNoPlayer;
        p.trackHouses.clear();
        p.trackVictims.clear();
        if (!p.alive) p.buried = true; // die Toten der vergangenen Tage werden begraben
        if (p.alive && p.role == Role::Actor && !p.actorCards.empty()) {
            p.acting = p.actorCards.front();
            p.actorCards.erase(p.actorCards.begin());
            tell(p.id, std::format("Heute Nacht spielst du: {}. {}", roleName(p.acting), roleDescription(p.acting)));
        }
    }
    narrate(std::format("Nacht {} bricht herein. Geht nach Hause und verriegelt die Türen – in der Dunkelheit lauern "
                        "die Werwölfe …",
                        m_day));
    tellWolves(kills > 1 ? std::format("Die Jagd beginnt – heute Nacht dürft ihr {} Opfer reißen!", kills)
                         : "Die Jagd beginnt. Verwandelt euch mit R und reißt ein Opfer mit Linksklick. Verriegelte Türen "
                           "brecht ihr mit Linksklick auf – aber das hört man!");
    // Die rostige Klinge des Ritters fordert ihr Opfer
    if (m_rustyWolf != kNoPlayer) {
        const uint8_t wolf = m_rustyWolf;
        m_rustyWolf = kNoPlayer;
        kill(wolf, DeathCause::Rust, false);
    }
}

void Match::enterMorning() {
    if (m_day == 1) {
        // Wer sich in der ersten Nacht nicht entschieden hat, dem nimmt der Zufall die Wahl ab
        for (Player& c : m_players) {
            if (!c.alive) continue;
            if (c.role == Role::Cupid && c.cupidPicks.size() < 2) {
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
            if (c.role == Role::WildChild && c.model == kNoPlayer) {
                std::vector<uint8_t> pool;
                for (const Player& p : m_players)
                    if (p.alive && p.id != c.id) pool.push_back(p.id);
                if (!pool.empty()) {
                    c.model = m_rng.pick(pool);
                    tell(c.id, std::format("Das Schicksal wählt dein Vorbild: {}.", nameOf(c.model)));
                }
            }
            if (c.role == Role::WolfHound && !c.sideChosen) {
                c.sideChosen = true;
                tell(c.id, "Du hast gezögert – also bleibst du dem Dorf treu.");
            }
        }
    }
    for (Player& p : m_players) {
        p.transform = p.transformTarget = 0.0f;
        p.acting = Role::Unknown;
    }

    // Bericht des Fährtenlesers (auch wenn der Schauspieler ihn gespielt hat)
    for (Player& t : m_players) {
        if (!t.alive || t.trackTarget == kNoPlayer) continue;
        const std::string name = nameOf(t.trackTarget);
        std::string report;
        if (t.trackHouses.empty()) report = std::format("Die Fährte von {} führte in kein fremdes Haus.", name);
        else {
            std::vector<std::string> labels;
            for (int h : t.trackHouses) labels.push_back(houseLabel(h));
            report = std::format("Die Fährte von {} führte in der Nacht in {}.", name, joinNames(labels));
        }
        if (!t.trackVictims.empty()) {
            std::vector<std::string> victims;
            for (uint8_t v : t.trackVictims) victims.push_back(nameOf(v));
            report += std::format(" Und sie endet an der Leiche von {}!", joinNames(victims));
        }
        tell(t.id, report);
    }

    // Verzauberte erfahren voneinander
    std::vector<std::string> enchanted;
    for (const Player& p : m_players)
        if (p.alive && p.enchanted) enchanted.push_back(p.name);
    for (Player& p : m_players) {
        if (!p.newlyEnchanted) continue;
        p.newlyEnchanted = false;
        if (p.alive) tell(p.id, "Eine seltsame Melodie hat dich verzaubert. Verzaubert sind: " + joinNames(enchanted) + ".");
    }

    m_phase = Phase::Morning;
    setTimer(kMorningSeconds);
    narrate("Der Morgen graut über dem Dorf …");
    if (m_nightDeaths.empty()) narrate("Wie durch ein Wunder hat in dieser Nacht niemand das Leben verloren.");
    for (const PendingDeath& d : m_nightDeaths) announce(d.id, d.cause);
    m_nightDeaths.clear();
    if (const Player* marked = find(m_marked); marked && marked->alive)
        narrate(std::format("Ein Rabe hockt auf dem Dach von {} – bei der nächsten Abstimmung hat {} zwei Stimmen gegen sich.",
                            marked->name, marked->name));
}

Team Match::evaluateWinner() const {
    if (m_jesterWinner != kNoPlayer) return Team::Jester;
    if (m_angelWinner != kNoPlayer) return Team::Angel;
    int alive = 0, aliveWolves = 0, wolvesTeam = 0, villageTeam = 0, killers = 0;
    Team killerTeam = Team::None;
    for (const Player& p : m_players) {
        if (!p.alive) continue;
        ++alive;
        if (isWolfRole(p.role)) ++aliveWolves;
        const Team team = teamOf(p.id);
        if (team == Team::Wolves) ++wolvesTeam;
        if (team == Team::Village) ++villageTeam;
        if (isNeutralKiller(p.role)) {
            ++killers;
            killerTeam = roleTeam(p.role);
        }
    }
    if (alive == 0) return Team::Nobody;
    if (mixedCouple() && alive == 2) {
        bool loversAlive = true;
        for (const Player& p : m_players)
            if (p.lover != kNoPlayer && !p.alive) loversAlive = false;
        if (loversAlive) return Team::Lovers;
    }
    // Flötenspieler: alle anderen Lebenden sind verzaubert
    for (const Player& piper : m_players) {
        if (!piper.alive || piper.role != Role::Piper) continue;
        if (std::all_of(m_players.begin(), m_players.end(), [&](const Player& p) { return !p.alive || p.id == piper.id || p.enchanted; }))
            return Team::Piper;
    }
    // Sektierer: nur noch seine Dorfhälfte lebt
    for (const Player& sect : m_players) {
        if (!sect.alive || sect.role != Role::Sectarian) continue;
        if (std::all_of(m_players.begin(), m_players.end(), [&](const Player& p) { return !p.alive || sectOf(p) == sectOf(sect); }))
            return Team::Sectarian;
    }
    // Ein einzelgängerischer Mörder gewinnt, sobald ihm niemand mehr gefährlich werden kann
    if (killers == 1 && alive <= 2) return killerTeam;
    if (killers > 0) return Team::None;
    if (aliveWolves == 0) return villageTeam > 0 ? Team::Village : Team::Nobody;
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
    m_servantPending = kNoPlayer;

    m_winner = w;
    m_phase = Phase::GameOver;
    m_timer = 0.0f;
    m_phaseDuration = 0.0f;
    for (Player& p : m_players) {
        p.transform = p.transformTarget = 0.0f;
        p.successorTimer = 0.0f;
    }
    markDirty();
    auto victory = [&](std::string text) { notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, std::move(text)); };
    switch (w) {
    case Team::Village: victory("Das Dorf hat gesiegt! Kein Werwolf treibt mehr sein Unwesen."); break;
    case Team::Wolves: victory("Die Werwölfe haben gesiegt! Das Dorf ist ihrem Hunger erlegen."); break;
    case Team::Lovers: victory("Die Verliebten haben gesiegt! Ihre Liebe hat alles überdauert."); break;
    case Team::Jester:
        victory(std::format("Der Narr hat gesiegt! {} wollte nichts anderes, als gehängt zu werden.", nameOf(m_jesterWinner)));
        break;
    case Team::Angel:
        victory(std::format("Der Engel hat gesiegt! {} ist schon in der ersten Runde ins Paradies aufgestiegen.", nameOf(m_angelWinner)));
        break;
    case Team::Killer: victory("Der Serienmörder hat gesiegt! Niemand ist mehr übrig, der ihn aufhalten könnte."); break;
    case Team::Arsonist: victory("Der Brandstifter hat gesiegt! Vom Dorf bleibt nur glühende Asche."); break;
    case Team::Piper: victory("Der Flötenspieler hat gesiegt! Das ganze Dorf tanzt nach seiner Melodie."); break;
    case Team::WhiteWolf: victory("Der weiße Werwolf hat gesiegt! Er hat sogar sein eigenes Rudel verraten."); break;
    case Team::Sectarian: victory("Der Sektierer hat gesiegt! Nur seine Hälfte des Dorfes ist übrig."); break;
    default: victory("Niemand hat überlebt. Stille legt sich über das Dorf."); break;
    }
    std::string roles = "Die Rollen: ";
    for (size_t i = 0; i < m_players.size(); ++i) {
        const Player& p = m_players[i];
        if (i > 0) roles += ", ";
        roles += p.startRole != p.role ? std::format("{} ({}, später {})", p.name, roleName(p.startRole), roleName(p.role))
                                       : std::format("{} ({})", p.name, roleName(p.role));
    }
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, roles);
    return true;
}

} // namespace ww
