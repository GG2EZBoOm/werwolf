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

Text roleWithArticle(Role role) {
    switch (role) {
    case Role::Villager: return {"ein Dorfbewohner", "a villager"};
    case Role::Werewolf: return {"ein Werwolf", "a werewolf"};
    case Role::Seer: return {"die Seherin", "the seer"};
    case Role::Witch: return {"die Hexe", "the witch"};
    case Role::Hunter: return {"der Jäger", "the hunter"};
    case Role::Cupid: return {"Amor", "Cupid"};
    case Role::Protector: return {"der Beschützer", "the protector"};
    case Role::Elder: return {"der Dorfälteste", "the elder"};
    case Role::LittleGirl: return {"das kleine Mädchen", "the little girl"};
    case Role::Medium: return {"das Medium", "the medium"};
    case Role::Tracker: return {"der Fährtenleser", "the tracker"};
    case Role::Blacksmith: return {"der Schmied", "the blacksmith"};
    case Role::Priest: return {"der Pfarrer", "the priest"};
    case Role::Mayor: return {"der Bürgermeister", "the mayor"};
    case Role::Idiot: return {"der Dorfdepp", "the village idiot"};
    case Role::Cursed: return {"der Verfluchte", "the cursed one"};
    case Role::AlphaWolf: return {"der Urwolf", "the alpha wolf"};
    case Role::WolfCub: return {"das Wolfsjunge", "the wolf cub"};
    case Role::Jester: return {"der Narr", "the jester"};
    case Role::SerialKiller: return {"der Serienmörder", "the serial killer"};
    case Role::Arsonist: return {"der Brandstifter", "the arsonist"};
    case Role::Thief: return {"der Dieb", "the thief"};
    case Role::Scapegoat: return {"der Sündenbock", "the scapegoat"};
    case Role::Fox: return {"der Fuchs", "the fox"};
    case Role::BearTamer: return {"der Bärenführer", "the bear tamer"};
    case Role::Knight: return {"der Ritter mit der rostigen Klinge", "the knight with the rusty sword"};
    case Role::Servant: return {"die ergebene Magd", "the devoted servant"};
    case Role::Judge: return {"der stotternde Richter", "the stuttering judge"};
    case Role::Actor: return {"der Schauspieler", "the actor"};
    case Role::WildChild: return {"das wilde Kind", "the wild child"};
    case Role::WolfHound: return {"der Wolfshund", "the wolf hound"};
    case Role::Sister: return {"eine der zwei Schwestern", "one of the two sisters"};
    case Role::Brother: return {"einer der drei Brüder", "one of the three brothers"};
    case Role::HonestVillager: return {"ein ehrlicher Dorfbewohner", "an honest villager"};
    case Role::Raven: return {"der Rabe", "the raven"};
    case Role::BigBadWolf: return {"der große böse Wolf", "the big bad wolf"};
    case Role::WhiteWolf: return {"der weiße Werwolf", "the white werewolf"};
    case Role::Piper: return {"der Flötenspieler", "the pied piper"};
    case Role::Angel: return {"der Engel", "the angel"};
    case Role::Sectarian: return {"der Sektierer", "the sectarian"};
    default: return {"unbekannt", "unknown"};
    }
}

bool isNeutralKiller(Role role) { return role == Role::SerialKiller || role == Role::Arsonist || role == Role::WhiteWolf; }

} // namespace

// --- Start -------------------------------------------------------------------

bool Match::start(const std::vector<Seat>& seats, const Settings& settings, const World& world, uint64_t seed,
                  Text& error) {
    Settings s = settings;
    s.sanitize();
    std::vector<Role> roles = buildRoleList(s, static_cast<int>(seats.size()), &error);
    if (roles.empty()) return false;
    m_rng.seed(seed);
    m_rng.shuffle(roles);
    return startWithRoles(seats, roles, s, world, m_rng.next64(), error);
}

bool Match::startWithRoles(const std::vector<Seat>& seats, const std::vector<Role>& roles, const Settings& settings,
                           const World& world, uint64_t seed, Text& error) {
    if (seats.size() != roles.size()) {
        error = {"Rollenzahl passt nicht zur Spielerzahl.", "The number of roles does not match the number of players."};
        return false;
    }
    if (static_cast<int>(seats.size()) < kMinPlayers) {
        error = tx("Es werden mindestens {} Spieler benötigt.", "At least {} players are needed.", kMinPlayers);
        return false;
    }
    m_world = &world;
    m_settings = settings;
    m_settings.sanitize();
    m_rng.seed(seed);

    // Häuser verteilen: jeder bekommt eines, bei vielen Spielern wird geteilt (die Kapelle gehört allen)
    const size_t houseCount = std::min<size_t>(world.houses().size(), 32);
    std::vector<uint8_t> homes;
    for (size_t i = 0; i < houseCount; ++i)
        if (!world.houses()[i].chapel) homes.push_back(static_cast<uint8_t>(i));
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
    narrate(wolves.size() == 1
                ? tx("Willkommen im Dorf! {} Seelen leben hier – eine davon ist ein Werwolf.",
                     "Welcome to the village! {} souls live here – one of them is a werewolf.", m_players.size())
                : tx("Willkommen im Dorf! {} Seelen leben hier – {} davon sind Werwölfe.",
                     "Welcome to the village! {} souls live here – {} of them are werewolves.", m_players.size(), wolves.size()));
    if (neutral) narrate({"Doch nicht jeder im Dorf verfolgt dieselben Ziele …", "But not everyone in the village shares the same goals …"});
    for (const Player& p : m_players) {
        if (p.role == Role::Mayor)
            narrate(tx("{} ist der Bürgermeister – seine Stimme zählt doppelt.", "{} is the mayor – their vote counts double.", p.name));
        if (p.role == Role::BearTamer)
            narrate(tx("{} ist der Bärenführer. Sein Bär brummt, wenn ein Werwolf in der Nähe ist.",
                       "{} is the bear tamer. Their bear growls when a werewolf is near.", p.name));
        if (p.role == Role::HonestVillager)
            narrate(tx("{} ist bekanntermaßen ein ehrlicher, einfacher Dorfbewohner.", "{} is known to be an honest, simple villager.", p.name));
    }

    for (const Player& p : m_players) {
        tell(p.id, tx("Deine Rolle: {}. {}", "Your role: {}. {}", roleText(p.role), roleDescriptionText(p.role)));
        if (p.house != kNoPlayer)
            tell(p.id, {"Dein Haus ist auf der Karte markiert. Schließt du die Tür von innen (Q), ist sie verriegelt.",
                        "Your house is marked on the map. If you close the door from inside (Q), it is bolted."});
        if (isWolfRole(p.role)) {
            std::vector<Text> others;
            for (const Player& o : m_players) {
                if (!isWolfRole(o.role) || o.id == p.id) continue;
                // Der weiße Werwolf gibt sich als gewöhnlicher Werwolf aus
                const Role shown = o.role == Role::WhiteWolf ? Role::Werewolf : o.role;
                others.push_back(tx("{} ({})", "{} ({})", o.name, roleText(shown)));
            }
            tell(p.id, others.empty() ? Text{"Du jagst allein – kein anderer Werwolf ist im Dorf.", "You hunt alone – there is no other werewolf in the village."}
                                      : tx("Dein Rudel: {}. Euer Rudel-Chat ist geheim.", "Your pack: {}. Your pack chat is secret.", joinTexts(others)));
            if (littleGirl)
                tell(p.id, {"Vorsicht: Ein kleines Mädchen spitzt nachts die Ohren und belauscht euren Rudel-Chat.",
                            "Careful: a little girl pricks up her ears at night and listens in on your pack chat."});
        }
        if (p.role == Role::Sister || p.role == Role::Brother) {
            std::vector<std::string> family;
            for (const Player& o : m_players)
                if (o.id != p.id && o.role == p.role) family.push_back(o.name);
            tell(p.id, p.role == Role::Sister
                           ? tx("Deine Schwester: {}. Mit Tab sprecht ihr heimlich miteinander.", "Your sister: {}. Press Tab to talk in secret.", joinNames(family))
                           : tx("Deine Brüder: {}. Mit Tab sprecht ihr heimlich miteinander.", "Your brothers: {}. Press Tab to talk in secret.", joinNames(family)));
        }
        if (p.role == Role::Sectarian) {
            std::vector<std::string> mine, other;
            for (const Player& o : m_players)
                if (o.id != p.id) (sectOf(o) == sectOf(p) ? mine : other).push_back(o.name);
            const Text nobody{"niemand", "nobody"};
            tell(p.id, tx("Deine Hälfte des Dorfes ({}): {}. Die andere Hälfte muss verschwinden: {}.",
                          "Your half of the village ({}): {}. The other half has to go: {}.",
                          sectOf(p) == 0 ? Text{"Osten", "east"} : Text{"Westen", "west"}, mine.empty() ? nobody : joinNames(mine),
                          other.empty() ? nobody : joinNames(other)));
        }
        if (p.role == Role::Actor) {
            std::vector<Text> cards;
            for (Role r : p.actorCards) cards.push_back(roleText(r));
            tell(p.id, tx("Deine Rollen für die nächsten Nächte: {}.", "Your roles for the coming nights: {}.", joinTexts(cards)));
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

Text Match::houseLabel(int house, bool dative) const {
    if (m_world && house == m_world->chapelIndex()) return {dative ? "der Kapelle" : "die Kapelle", "the chapel"};
    std::vector<std::string> owners;
    for (const Player& p : m_players)
        if (p.house == house) owners.push_back(p.name);
    if (owners.empty()) return {dative ? "einem leerstehenden Haus" : "ein leerstehendes Haus", "an empty house"};
    return dative ? tx("dem Haus von {}", "the house of {}", joinNames(owners)) : tx("das Haus von {}", "the house of {}", joinNames(owners));
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
        if (p.protecting == victim) tell(p.id, {"Dein Schutz hat heute Nacht ein Leben gerettet!", "Your protection saved a life tonight!"});
}

void Match::setTimer(float seconds) {
    m_timer = seconds;
    m_phaseDuration = seconds;
    m_phaseElapsed = 0.0f;
    markDirty();
}

void Match::notify(Notice::Audience audience, uint8_t player, NoticeKind kind, Text text) {
    Notice n;
    n.audience = audience;
    n.player = player;
    n.kind = kind;
    n.text = std::move(text);
    m_notices.push_back(std::move(n));
}

void Match::notifyArea(Vec2 at, float radius, NoticeKind kind, Text text) {
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
        if (std::fabs(local.x) < h.doorWidth * 0.5f + kPlayerRadius &&
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

void Match::setCaptain(uint8_t id, Text text) {
    m_captain = id;
    narrate(std::move(text));
    if (id != kNoPlayer)
        tell(id, {"Als Hauptmann zählt deine Stimme doppelt. Stirbst du, bestimmst du deinen Nachfolger.",
                  "As captain your vote counts double. If you die, you name your successor."});
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

Text Match::checkDoor(const Player& p, uint8_t house, float range) const {
    if (house >= m_doors.size()) return {"Hier ist keine Tür.", "There is no door here."};
    if (m_doors[house].flags & DoorBurnt)
        return house == m_world->chapelIndex() ? Text{"Von der Kapelle ist nur Asche übrig.", "Only ashes are left of the chapel."}
                                               : Text{"Von diesem Haus ist nur Asche übrig.", "Only ashes are left of this house."};
    const Vec2 door = m_world->doorCenter(house);
    if ((Vec2{p.position.x, p.position.z} - door).length() > range + kRangeTolerance) return {"Geh näher an die Tür.", "Move closer to the door."};
    return {};
}

Text Match::checkInteraction(uint8_t id, Interaction kind, uint8_t targetId) const {
    const Player* p = find(id);
    if (!p) return {"Du nimmst nicht an diesem Spiel teil.", "You are not part of this game."};
    const Player* t = find(targetId);
    const uint32_t abilities = abilitiesFor(id);
    auto distTo = [&](const Player& other) { return distanceXZ(p->position, other.position); };
    auto visible = [&](const Player& other) { return hasLineOfSight(p->position, other.position); };
    const Text pickLiving{"Wähle einen lebenden Mitspieler.", "Choose a living player."};
    const Text cannotSee{"Du kannst die Person nicht sehen.", "You cannot see that person."};
    const Text tooFarAway{"Zu weit entfernt.", "Too far away."};
    const Text closer{"Geh näher heran.", "Move closer."};
    const Text nobodyHere{"Hier ist niemand.", "There is nobody here."};
    const Text windingUp{"Du holst noch aus …", "You are still winding up …"};
    const Text blocked{"Etwas versperrt dir den Weg.", "Something is in your way."};
    const Text cannotAttack{"Du kannst niemanden angreifen.", "You cannot attack anyone."};
    const Text enoughTonight{"Für heute Nacht hast du genug getan.", "You have done enough for tonight."};
    // Gemeinsame Prüfung für Fähigkeiten, die auf einen anderen lebenden Spieler in Reichweite zielen
    auto nearOther = [&](float range, const Text& tooFar) -> Text {
        if (!t || !t->alive || t->id == id) return pickLiving;
        if (distTo(*t) > range + kRangeTolerance) return tooFar;
        if (!visible(*t)) return cannotSee;
        return {};
    };

    switch (kind) {
    case Interaction::Transform:
        if (!(abilities & AbilityTransform)) return {"Verwandeln kannst du dich nur nachts.", "You can only transform at night."};
        return {};
    case Interaction::Attack:
    case Interaction::Infect: {
        if (kind == Interaction::Infect && !(abilities & AbilityInfect)) {
            if (p->role != Role::AlphaWolf) return {"Du kannst niemanden infizieren.", "You cannot infect anyone."};
            if (p->infectUsed) return {"Deine Infektion hast du schon weitergegeben.", "You have already passed on your infection."};
        }
        const bool betray = kind == Interaction::Attack && t && t->alive && t->id != id && isWolfRole(t->role) &&
                            (abilities & AbilityBetray);
        if (!betray && !(abilities & AbilityAttack)) {
            if (!isWolfRole(p->role) || m_phase != Phase::Night) return cannotAttack;
            if (!p->wolfForm()) return {"Verwandle dich zuerst (R).", "Transform first (R)."};
            return {"Das Rudel hat heute Nacht schon gefressen.", "The pack has already fed tonight."};
        }
        if (p->attackCooldown > 0.0f) return windingUp;
        if (!t || !t->alive || t->id == id) return nobodyHere;
        if (isWolfRole(t->role) && !betray) return {"Du greifst kein Mitglied deines Rudels an.", "You don't attack a member of your pack."};
        if (distTo(*t) > kAttackRange + kRangeTolerance) return tooFarAway;
        if (!visible(*t)) return blocked;
        return {};
    }
    case Interaction::Stab:
        if (!(abilities & AbilityStab)) {
            if (p->role != Role::SerialKiller || m_phase != Phase::Night) return cannotAttack;
            return {"Dein Messer hat heute Nacht schon Blut geschmeckt.", "Your knife has already tasted blood tonight."};
        }
        if (p->attackCooldown > 0.0f) return windingUp;
        if (!t || !t->alive || t->id == id) return nobodyHere;
        if (distTo(*t) > kAttackRange + kRangeTolerance) return tooFarAway;
        if (!visible(*t)) return blocked;
        return {};
    case Interaction::Inspect:
        if (!(abilities & AbilityInspect)) return {"Deine Vision ist für heute Nacht verbraucht.", "Your vision is spent for tonight."};
        return nearOther(kInspectRange, {"Zu weit entfernt für eine Vision.", "Too far away for a vision."});
    case Interaction::Protect:
        if (!(abilities & AbilityProtect)) return {"Du hast heute Nacht schon jemanden beschützt.", "You have already protected someone tonight."};
        if (!t || !t->alive) return pickLiving;
        if (t->id == p->lastProtected) return {"Diesen Spieler hast du letzte Nacht schon beschützt.", "You already protected this player last night."};
        if (t->id != id && distTo(*t) > kProtectRange + kRangeTolerance) return closer;
        if (t->id != id && !visible(*t)) return cannotSee;
        return {};
    case Interaction::Heal:
        if (!(abilities & AbilityHeal)) return {"Dein Heiltrank ist verbraucht.", "Your healing potion is used up."};
        if (!t || t->alive || t->buried) return {"Hier liegt niemand, den du retten könntest.", "There is nobody here you could save."};
        if (!t->diedAtNight || t->deathDay != m_day ||
            (t->deathCause != DeathCause::Wolves && t->deathCause != DeathCause::Knife))
            return {"Für diese Seele kommt jede Hilfe zu spät.", "It is too late to help this soul."};
        if (distanceXZ(p->position, t->deathPosition) > kHealRange + kRangeTolerance) return closer;
        if (!hasLineOfSight(p->position, t->deathPosition)) return {"Du kommst nicht an die Person heran.", "You cannot reach that person."};
        return {};
    case Interaction::Poison:
        if (!(abilities & AbilityPoison)) return {"Dein Gifttrank ist verbraucht.", "Your poison is used up."};
        return nearOther(kPoisonRange, closer);
    case Interaction::CupidPick:
        if (!(abilities & AbilityCupid)) return {"Amors Pfeile sind verschossen.", "Cupid's arrows are spent."};
        if (!t || !t->alive) return pickLiving;
        if (std::find(p->cupidPicks.begin(), p->cupidPicks.end(), t->id) != p->cupidPicks.end())
            return {"Diesen Spieler hast du schon gewählt.", "You have already chosen this player."};
        if (t->id != id && distTo(*t) > kCupidRange + kRangeTolerance) return closer;
        if (t->id != id && !visible(*t)) return cannotSee;
        return {};
    case Interaction::HunterShot:
        if (!(abilities & AbilityHunterShot)) return {"Du hast keinen Schuss mehr.", "You have no shot left."};
        if (!t || !t->alive || t->id == id) return {"Ziele auf einen lebenden Spieler.", "Aim at a living player."};
        if (distanceXZ(p->deathPosition, t->position) > kHunterRange) return tooFarAway;
        if (!hasLineOfSight(p->deathPosition, t->position)) return {"Kein freies Schussfeld.", "No clear line of fire."};
        return {};
    case Interaction::Track:
        if (!(abilities & AbilityTrack)) return {"Du verfolgst heute Nacht schon eine Fährte.", "You are already following a trail tonight."};
        return nearOther(kTrackRange, tooFarAway);
    case Interaction::HolyWater:
        if (!(abilities & AbilityHolyWater)) {
            if (p->role != Role::Priest) return {"Du hast kein Weihwasser.", "You have no holy water."};
            if (p->holyWaterUsed) return {"Dein Weihwasser ist aufgebraucht.", "Your holy water is used up."};
            return {"Jetzt ist nicht die Zeit dafür.", "Now is not the time for that."};
        }
        return nearOther(kHolyWaterRange, closer);
    case Interaction::Steal:
        if (!(abilities & AbilitySteal)) return {"Stehlen kannst du nur in der ersten Nacht.", "You can only steal on the first night."};
        return nearOther(kStealRange, {"Schleich dich näher heran.", "Sneak closer."});
    case Interaction::Enchant:
        if (!(abilities & AbilityEnchant)) return {"Deine Flöte schweigt für heute Nacht.", "Your flute is silent for tonight."};
        if (t && t->enchanted) return {"Diese Person ist schon verzaubert.", "This person is already enchanted."};
        return nearOther(kEnchantRange, {"Geh näher heran, damit sie deine Melodie hört.", "Move closer so they can hear your tune."});
    case Interaction::Sniff:
        if (!(abilities & AbilitySniff))
            return p->foxActive ? Text{"Du hast heute Nacht schon gewittert.", "You have already sniffed tonight."}
                                : Text{"Deine Nase hat dich verlassen.", "Your nose has failed you."};
        return nearOther(kSniffRange, {"Zu weit entfernt für deine Nase.", "Too far away for your nose."});
    case Interaction::Mark:
        if (!(abilities & AbilityMark)) return {"Du hast heute Nacht schon jemanden belastet.", "You have already accused someone tonight."};
        return nearOther(kMarkRange, tooFarAway);
    case Interaction::ChooseModel:
        if (!(abilities & AbilityChooseModel)) return {"Du hast dein Vorbild schon gewählt.", "You have already chosen your role model."};
        return nearOther(kModelRange, closer);
    case Interaction::ChooseSide:
        if (!(abilities & AbilityChooseSide)) return {"Du hast dich bereits entschieden.", "You have already decided."};
        if (targetId > 1) return {"Unbekannte Entscheidung.", "Unknown choice."};
        return {};
    case Interaction::SecondVote:
        if (!(abilities & AbilitySecondVote))
            return p->judgeUsed ? Text{"Du hast deine zweite Abstimmung schon verlangt.", "You have already demanded your second vote."}
                                : Text{"Das geht nur während einer Abstimmung.", "That only works during a vote."};
        return {};
    case Interaction::TakeRole:
        if (!(abilities & AbilityTakeRole)) return {"Hier gibt es keine Rolle zu übernehmen.", "There is no role to take over here."};
        if (targetId != m_servantPending) return {"Du kannst nur die Rolle des Hingerichteten übernehmen.", "You can only take the role of the executed."};
        return {};
    case Interaction::NameSuccessor:
        if (!(abilities & AbilityNameSuccessor)) return {"Du bist nicht der Hauptmann.", "You are not the captain."};
        if (!t || !t->alive || t->id == id) return {"Wähle einen lebenden Nachfolger.", "Choose a living successor."};
        return {};
    case Interaction::Vote:
        if (!(abilities & AbilityVote)) {
            if (p->idiotRevealed && m_phase == Phase::Vote) return {"Als Dorfdepp darfst du nicht mehr abstimmen.", "As the village idiot you may no longer vote."};
            return {"Gerade wird nicht abgestimmt.", "There is no vote right now."};
        }
        if (targetId == kNoPlayer) return {};
        if (!t || !t->alive || t->id == id) return {"Für diesen Spieler kannst du nicht stimmen.", "You cannot vote for this player."};
        return {};
    case Interaction::RingBell:
        if (!(abilities & AbilityRingBell)) {
            if (m_phase == Phase::Day && m_day < 2) return {"Am ersten Tag wird keine Versammlung einberufen.", "No gathering is called on the first day."};
            return {"Die Glocke kann gerade nicht geläutet werden.", "The bell cannot be rung right now."};
        }
        if (distanceXZ(p->position, m_world->bell()) > kBellRange + kRangeTolerance) return {"Geh zur Glocke.", "Go to the bell."};
        if (m_phaseElapsed < 15.0f) return {"Lasst dem Dorf noch etwas Zeit.", "Give the village a little more time."};
        return {};
    case Interaction::DoorToggle: {
        if (!p->alive) return {"Geister gleiten durch Türen hindurch.", "Ghosts glide through doors."};
        if (m_phase == Phase::GameOver) return {"Das Spiel ist vorbei.", "The game is over."};
        if (Text why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if (d.flags & DoorBroken) return {"Die Tür ist zerschmettert und lässt sich nicht mehr schließen.", "The door is smashed and can no longer be closed."};
        const bool inside = m_world->houseAt({p->position.x, p->position.z}, 0.3f) == targetId;
        if (!(d.flags & DoorOpen) && (d.flags & DoorLocked) && !inside) return {"Die Tür ist von innen verriegelt.", "The door is bolted from the inside."};
        if ((d.flags & DoorOpen) && doorwayOccupied(targetId)) return {"Jemand steht in der Tür.", "Someone is standing in the doorway."};
        return {};
    }
    case Interaction::DoorBreak: {
        if (!(abilities & AbilityBreakDoor)) {
            if (isWolfRole(p->role) && m_phase == Phase::Night) return {"Verwandle dich zuerst (R).", "Transform first (R)."};
            return {"Du bist nicht stark genug, um eine Tür einzuschlagen.", "You are not strong enough to break down a door."};
        }
        if (Text why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if (d.flags & (DoorOpen | DoorBroken)) return {"Die Tür steht doch offen.", "The door is already open."};
        if (!(d.flags & DoorLocked)) return {"Die Tür ist nicht verriegelt – öffne sie einfach (Q).", "The door is not bolted – just open it (Q)."};
        if (p->attackCooldown > 0.0f) return windingUp;
        return {};
    }
    case Interaction::Reinforce: {
        if (!(abilities & AbilityReinforce)) {
            if (p->role != Role::Blacksmith) return {"Du bist kein Schmied.", "You are not a blacksmith."};
            if (m_phase != Phase::Day) return {"Schmieden kannst du nur tagsüber.", "You can only forge during the day."};
            return {"Für heute ist deine Esse erkaltet.", "Your forge has gone cold for today."};
        }
        if (Text why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const DoorState& d = m_doors[targetId];
        if ((d.flags & DoorReinforced) && !(d.flags & DoorBroken) && d.health >= 100)
            return {"Diese Tür ist bereits mit Eisen beschlagen.", "This door is already reinforced with iron."};
        return {};
    }
    case Interaction::Douse: {
        if (!(abilities & AbilityDouse)) {
            if (p->role != Role::Arsonist) return {"Du hast kein Öl dabei.", "You have no oil with you."};
            if (m_phase != Phase::Night) return {"Am helllichten Tag würde man dich sehen.", "Someone would see you in broad daylight."};
            return enoughTonight;
        }
        if (Text why = checkDoor(*p, targetId, kDoorRange); !why.empty()) return why;
        const bool chapel = targetId == m_world->chapelIndex();
        if (m_doused & (1u << targetId))
            return chapel ? Text{"Die Kapelle ist schon mit Öl getränkt.", "The chapel is already doused with oil."}
                          : Text{"Dieses Haus ist schon mit Öl getränkt.", "This house is already doused with oil."};
        if (m_doors[targetId].flags & DoorBurning)
            return chapel ? Text{"Die Kapelle brennt bereits.", "The chapel is already burning."}
                          : Text{"Dieses Haus brennt bereits.", "This house is already burning."};
        return {};
    }
    case Interaction::Ignite:
        if (!(abilities & AbilityIgnite)) {
            if (p->role != Role::Arsonist) return {"Du hast nichts zum Anzünden.", "You have nothing to set on fire."};
            if (!m_doused) return {"Noch ist kein Haus mit Öl getränkt.", "No house is doused with oil yet."};
            if (m_phase != Phase::Night) return {"Warte auf die Dunkelheit.", "Wait for darkness."};
            return enoughTonight;
        }
        return {};
    }
    return {"Unbekannte Aktion.", "Unknown action."};
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

Text Match::interact(uint8_t id, Interaction kind, uint8_t targetId) {
    Text why = checkInteraction(id, kind, targetId);
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
                tell(p->id, {"Eine unsichtbare Macht schützt dieses Opfer!", "An invisible power protects this victim!"});
                break;
            }
            narrate({"Ein grauenvoller Schrei zerreißt die Stille der Nacht …", "A horrible scream tears through the silence of the night …"});
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
            tell(p->id, {"Jemand wacht über dieses Opfer – du musst fliehen, bevor man dich erkennt!",
                         "Someone is watching over this victim – flee before you are recognised!"});
            notifyProtectors(t->id);
            break;
        }
        narrate({"Ein erstickter Schrei hallt durch die Nacht …", "A muffled scream echoes through the night …"});
        kill(t->id, DeathCause::Knife, false, p->id);
        break;
    case Interaction::Inspect:
        p->nightAbilityUsed = true;
        if (std::none_of(p->seerResults.begin(), p->seerResults.end(), [&](const SeerResult& r) { return r.player == t->id; }))
            p->seerResults.push_back({t->id, t->role});
        tell(p->id, tx("Deine Vision zeigt dir: {} ist {}.", "Your vision shows you: {} is {}.", t->name, roleWithArticle(t->role)));
        break;
    case Interaction::Protect:
        p->nightAbilityUsed = true;
        p->protecting = t->id;
        p->lastProtected = t->id;
        tell(p->id, tx("Du wachst heute Nacht über {}.", "Tonight you watch over {}.",
                       t->id == p->id ? Text{"dich selbst", "yourself"} : Text::same(t->name)));
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
            tell(r.id, {"Ein bitterer Trank benetzt deine Lippen … du lebst!", "A bitter potion wets your lips … you are alive!"});
        };
        revive(*t);
        if (t->lover != kNoPlayer) {
            Player* l = findMutable(t->lover);
            if (l && !l->alive && l->deathCause == DeathCause::Grief && l->deathDay == m_day) revive(*l);
        }
        tell(p->id, tx("Dein Heiltrank hat {} ins Leben zurückgeholt.", "Your healing potion brought {} back to life.", t->name));
        break;
    }
    case Interaction::Poison:
        m_poisonAvailable = false;
        tell(p->id, tx("Dein Gift wirkt – {} sinkt leblos zu Boden.", "Your poison works – {} sinks lifeless to the ground.", t->name));
        kill(t->id, DeathCause::Poison, false, p->id);
        break;
    case Interaction::CupidPick:
        p->cupidPicks.push_back(t->id);
        if (p->cupidPicks.size() == 2) makeLovers(p->cupidPicks[0], p->cupidPicks[1]);
        else tell(p->id, tx("{} ist getroffen. Wähle die zweite Person.", "{} is struck. Choose the second person.", t->name));
        break;
    case Interaction::HunterShot:
        p->hunterTimer = 0.0f;
        narrate(tx("Ein Schuss hallt durch das Dorf! {} hat mit dem letzten Atemzug abgedrückt.",
                   "A shot rings out through the village! {} pulled the trigger with their last breath.", p->name));
        kill(t->id, DeathCause::Hunter, true, p->id);
        break;
    case Interaction::Track:
        p->nightAbilityUsed = true;
        p->trackTarget = t->id;
        p->trackHouses.clear();
        p->trackVictims.clear();
        tell(p->id, tx("Du heftest dich an die Fersen von {}. Bis zum Morgen siehst du, wohin die Fährte führt.",
                       "You follow the trail of {}. Until morning you can see where it leads.", t->name));
        break;
    case Interaction::HolyWater: {
        p->holyWaterUsed = true;
        const bool night = m_phase == Phase::Night;
        if (isWolfRole(t->role)) {
            tell(p->id, tx("Das Weihwasser zischt auf der Haut von {} – ein Werwolf!", "The holy water hisses on the skin of {} – a werewolf!", t->name));
            if (!night)
                narrate(tx("{} besprengt {} mit Weihwasser – die Haut zischt und brennt! Ein Werwolf!",
                           "{} sprinkles {} with holy water – the skin hisses and burns! A werewolf!", p->name, t->name));
            kill(t->id, DeathCause::HolyWater, !night, p->id);
        } else {
            tell(p->id, tx("Das Weihwasser perlt einfach ab – {} ist kein Werwolf.", "The holy water simply rolls off – {} is no werewolf.", t->name));
            tell(t->id, tx("{} hat dich mit Weihwasser besprengt. Es perlt einfach ab.", "{} sprinkled you with holy water. It simply rolls off.", p->name));
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
        tell(t->id, {"Im Schlaf hat dir jemand deine Rolle gestohlen – du bist jetzt ein einfacher Dorfbewohner.",
                     "Someone stole your role while you slept – you are now a plain villager."});
        tell(p->id, tx("Du hast {} die Rolle gestohlen: {}. {}", "You stole the role of {}: {}. {}", t->name, roleText(stolen),
                       roleDescriptionText(stolen)));
        if (isWolfRole(stolen)) {
            tellWolves(tx("{} hat die Rolle von {} gestohlen und jagt nun mit euch.", "{} stole the role of {} and now hunts with you.",
                          p->name, t->name));
            tell(t->id, {"Dein Rudel hat dich verloren – du gehörst jetzt zum Dorf.", "Your pack has lost you – you now belong to the village."});
        }
        break;
    }
    case Interaction::Enchant:
        ++p->enchantsTonight;
        t->enchanted = true;
        t->newlyEnchanted = true;
        tell(p->id, tx("Deine Melodie umschmeichelt {} – verzaubert!", "Your melody charms {} – enchanted!", t->name));
        break;
    case Interaction::Sniff: {
        p->nightAbilityUsed = true;
        bool wolf = false;
        for (const Player& o : m_players)
            if (o.alive && isWolfRole(o.role) && distanceXZ(o.position, t->position) <= kSniffRadius) wolf = true;
        if (wolf) {
            tell(p->id, tx("Deine Nase zuckt: Bei {} oder ganz in der Nähe wittert du einen Werwolf!",
                           "Your nose twitches: you smell a werewolf at {} or very close by!", t->name));
        } else {
            p->foxActive = false;
            tell(p->id, tx("Kein Werwolf bei {} – und deine Nase hat dich verlassen. Du bist jetzt ein einfacher Fuchs.",
                           "No werewolf near {} – and your nose has failed you. You are now just a plain fox.", t->name));
        }
        break;
    }
    case Interaction::Mark:
        p->nightAbilityUsed = true;
        m_marked = t->id;
        tell(p->id, tx("Du hast {} belastet. Morgen erfährt das ganze Dorf davon.", "You accused {}. Tomorrow the whole village will know.", t->name));
        break;
    case Interaction::ChooseModel:
        p->model = t->id;
        tell(p->id, tx("{} ist dein Vorbild. Stirbt {}, wirst du zum Werwolf.", "{} is your role model. If {} dies, you become a werewolf.",
                       t->name, t->name));
        break;
    case Interaction::ChooseSide:
        p->sideChosen = true;
        if (targetId == 1) becomeWolf(*p, {"Du folgst dem Ruf des Rudels.", "You follow the call of the pack."});
        else tell(p->id, {"Du bleibst dem Dorf treu – ein guter Hund.", "You stay loyal to the village – good dog."});
        break;
    case Interaction::SecondVote:
        p->judgeUsed = true;
        m_secondVote = true;
        tell(p->id, {"Nach diesem Urteil folgt eine zweite Abstimmung.", "A second vote will follow this verdict."});
        break;
    case Interaction::TakeRole: {
        m_servantTook = true;
        const Role taken = t->role;
        narrate(tx("{} tritt vor: Die ergebene Magd übernimmt die Rolle von {} – welche, bleibt ihr Geheimnis.",
                   "{} steps forward: the devoted servant takes over the role of {} – which one stays a secret.", p->name, t->name));
        p->role = taken;
        p->shield = taken == Role::Elder;
        t->role = Role::Servant;
        tell(p->id, tx("Deine neue Rolle: {}. {}", "Your new role: {}. {}", roleText(taken), roleDescriptionText(taken)));
        if (isWolfRole(taken))
            tellWolves(tx("{} hat die Rolle des Hingerichteten übernommen und gehört jetzt zum Rudel.",
                          "{} took over the role of the executed and now belongs to the pack.", p->name));
        break;
    }
    case Interaction::NameSuccessor:
        p->successorTimer = 0.0f;
        setCaptain(t->id, tx("{} übergibt mit letzter Kraft das Amt des Hauptmanns an {}.",
                             "With their last strength, {} hands the office of captain to {}.", p->name, t->name));
        break;
    case Interaction::Vote:
        p->vote = targetId;
        checkVotesComplete();
        break;
    case Interaction::RingBell:
        m_bellRung = true;
        enterAssembly(tx("{} läutet die Glocke! Alle Bewohner versammeln sich am Feuer.",
                         "{} rings the bell! All villagers gather at the fire.", p->name),
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
                       tx("Mit einem Krachen birst die Tür – {} steht offen!", "With a crash the door bursts – {} stands open!",
                          houseLabel(targetId)));
            m_doorNoise[targetId] = kDoorNoiseInterval;
        } else if (m_doorNoise[targetId] <= 0.0f) {
            notifyArea(door, kDoorNoiseRadius, NoticeKind::Warning,
                       tx("Lautes Krachen von Holz! Etwas hämmert gegen die Tür – {}.",
                          "Loud cracking of wood! Something is pounding on the door – {}.", houseLabel(targetId)));
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
        tell(p->id, tx("{} – du beschlägst die Tür mit Eisen. Sie wird Werwölfe doppelt so lange aufhalten.",
                       "{} – you reinforce the door with iron. It will hold werewolves back twice as long.",
                       broken ? Text{"Du setzt die zerschmetterte Tür wieder ein", "You put the smashed door back in"}
                              : Text{"Hammer, Amboss, Nieten", "Hammer, anvil, rivets"}));
        break;
    }
    case Interaction::Douse:
        p->nightAbilityUsed = true;
        m_doused |= 1u << targetId;
        tell(p->id, tx("Du tränkst {} mit Öl. Zünde es an, wann immer du willst (F).", "You douse {} with oil. Set it alight whenever you like (F).",
                       houseLabel(targetId)));
        break;
    case Interaction::Ignite: {
        p->nightAbilityUsed = true;
        std::vector<Text> names;
        for (size_t i = 0; i < m_doors.size(); ++i) {
            if (!(m_doused & (1u << i))) continue;
            m_doors[i].flags |= DoorBurning;
            m_fire[i] = kFireSeconds;
            names.push_back(houseLabel(static_cast<int>(i), true));
        }
        m_doused = 0;
        narrate(tx("Feuer! Flammen schlagen aus {}! Wer drinnen ist, muss sofort hinaus!",
                   "Fire! Flames burst from {}! Whoever is inside must get out at once!", joinTexts(names)));
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
        tell(wolf.id, {"Eine unsichtbare Macht schützt dieses Opfer – deine Klauen gleiten ab!",
                       "An invisible power protects this victim – your claws slide off!"});
        notifyProtectors(victim.id);
        return;
    }
    if (victim.role == Role::SerialKiller) {
        wolf.attackCooldown = 3.0f;
        tell(wolf.id, {"Dein Opfer reißt eine Klinge hoch und wehrt dich ab – das ist kein gewöhnlicher Mensch!",
                       "Your victim raises a blade and fends you off – this is no ordinary person!"});
        tell(victim.id, tx("Ein Werwolf hat dich angefallen – doch dein Messer war schneller. Es war {}.",
                           "A werewolf attacked you – but your knife was faster. It was {}.", wolf.name));
        return;
    }
    --m_packKills;
    if (infect) {
        wolf.infectUsed = true;
        becomeWolf(victim, {"Ein Biss in der Dunkelheit … das Gift des Urwolfs fließt durch deine Adern.",
                            "A bite in the dark … the alpha wolf's venom flows through your veins."});
        tellWolves(tx("Der Urwolf hat {} infiziert – {} gehört jetzt zum Rudel!", "The alpha wolf infected {} – {} now belongs to the pack!",
                      victim.name, victim.name));
    } else if (victim.role == Role::Cursed) {
        becomeWolf(victim, {"Die Klauen reißen deine Haut auf … und der Fluch erwacht!", "The claws tear your skin … and the curse awakens!"});
        tellWolves(tx("{} überlebt den Angriff – der Fluch hat {} zu einem von euch gemacht!",
                      "{} survives the attack – the curse has made {} one of you!", victim.name, victim.name));
    } else if (victim.shield) {
        victim.shield = false;
        tell(victim.id, {"Du hast den Angriff mit letzter Kraft überstanden. Ein zweites Mal wirst du nicht so viel Glück haben.",
                         "You survived the attack with your last strength. You won't be so lucky a second time."});
        tell(wolf.id, {"Dein Opfer wehrt sich zäh und entkommt deinen Klauen – es ist der Dorfälteste!",
                       "Your victim fights back hard and escapes your claws – it is the elder!"});
        narrate({"Kampfgeräusche dringen durch die Nacht … dann ist es wieder still.", "Sounds of a struggle drift through the night … then all is quiet again."});
    } else {
        narrate({"Ein grauenvoller Schrei zerreißt die Stille der Nacht …", "A horrible scream tears through the silence of the night …"});
        if (victim.role == Role::Knight) m_rustyWolf = wolf.id;
        kill(victim.id, DeathCause::Wolves, false, wolf.id);
    }
    if (m_packKills == 0 && m_phase == Phase::Night) tellWolves({"Das Rudel ist für heute Nacht gesättigt.", "The pack is sated for tonight."});
}

void Match::becomeWolf(Player& p, const Text& reason) {
    p.role = Role::Werewolf;
    p.shield = false;
    std::vector<std::string> pack;
    for (const Player& o : m_players)
        if (o.alive && isWolfRole(o.role) && o.id != p.id) pack.push_back(o.name);
    tell(p.id, tx("{} Du bist jetzt ein Werwolf! Dein Rudel: {}. Verwandle dich nachts mit R.",
                  "{} You are now a werewolf! Your pack: {}. Transform at night with R.", reason,
                  pack.empty() ? Text{"niemand", "nobody"} : joinNames(pack)));
    markDirty();
}

void Match::makeLovers(uint8_t a, uint8_t b) {
    Player* pa = findMutable(a);
    Player* pb = findMutable(b);
    if (!pa || !pb || a == b) return;
    pa->lover = b;
    pb->lover = a;
    for (const Player& p : m_players)
        if (p.role == Role::Cupid)
            tell(p.id, tx("{} und {} sind nun unsterblich ineinander verliebt.", "{} and {} are now madly in love.", pa->name, pb->name));
    const bool mixed = mixedCouple();
    for (auto [x, y] : {std::pair{pa, pb}, std::pair{pb, pa}}) {
        tell(x->id, tx("Amors Pfeil hat dich getroffen! Du bist in {} verliebt (Rolle: {}). Stirbt einer von euch, stirbt auch der andere.",
                       "Cupid's arrow has struck you! You are in love with {} (role: {}). If one of you dies, so does the other.",
                       y->name, roleText(y->role)));
        if (mixed)
            tell(x->id, {"Ihr gehört verschiedenen Lagern an. Gemeinsam gewinnt ihr nur, wenn ihr als Letzte überlebt.",
                         "You belong to different sides. You only win together if you are the last ones alive."});
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
        tell(p->id, {"Du stirbst – doch dein Gewehr ist geladen! Ziele auf jemanden und drücke E.",
                     "You are dying – but your rifle is loaded! Aim at someone and press E."});
    }
    if (m_captain == id) {
        if (cause == DeathCause::Left) {
            std::vector<uint8_t> candidates;
            for (const Player& o : m_players)
                if (o.alive) candidates.push_back(o.id);
            if (!candidates.empty()) {
                const uint8_t next = m_rng.pick(candidates);
                setCaptain(next, tx("{} wird neuer Hauptmann.", "{} becomes the new captain.", nameOf(next)));
            } else {
                m_captain = kNoPlayer;
            }
        } else {
            p->successorTimer = kSuccessorSeconds;
            tell(p->id, {"Als Hauptmann bestimmst du jetzt deinen Nachfolger: Schau jemanden an und drücke E.",
                         "As captain you now name your successor: look at someone and press E."});
        }
    }
    switch (cause) {
    case DeathCause::Wolves:
        tell(p->id, {"Klauen aus der Dunkelheit – du wurdest von einem Werwolf zerrissen.", "Claws from the darkness – you were torn apart by a werewolf."});
        break;
    case DeathCause::Poison: tell(p->id, {"Ein brennender Schmerz … du wurdest vergiftet.", "A burning pain … you were poisoned."}); break;
    case DeathCause::Knife: tell(p->id, {"Eine Klinge blitzt im Dunkeln … du wurdest erstochen.", "A blade flashes in the dark … you were stabbed."}); break;
    case DeathCause::Fire: tell(p->id, {"Rauch, Hitze, Flammen … du bist im Feuer umgekommen.", "Smoke, heat, flames … you died in the fire."}); break;
    case DeathCause::HolyWater:
        tell(p->id, {"Das Weihwasser brennt wie Feuer – deine Wolfsgestalt zerfällt zu Asche.", "The holy water burns like fire – your wolf form crumbles to ash."});
        break;
    case DeathCause::Rust:
        tell(p->id, {"Die Wunde von der rostigen Klinge des Ritters hat sich entzündet … du stirbst.",
                     "The wound from the knight's rusty sword has festered … you are dying."});
        break;
    default: break;
    }
    if (isWolfRole(p->role)) m_wolfDied = true;
    if (p->role == Role::WolfCub) {
        m_wolfRage = true;
        tellWolves({"Das Wolfsjunge ist tot! Rasend vor Zorn dürft ihr in der nächsten Nacht ein Opfer mehr reißen.",
                    "The wolf cub is dead! Mad with rage, you may take one more victim next night."});
    }
    if (cause == DeathCause::Vote && p->role == Role::Jester && m_jesterWinner == kNoPlayer) m_jesterWinner = p->id;
    if (p->role == Role::Angel && m_angelWinner == kNoPlayer &&
        ((p->diedAtNight && m_day == 1) || (cause == DeathCause::Vote && !m_firstExecution)))
        m_angelWinner = p->id;

    // Wilde Kinder verlieren ihr Vorbild
    for (Player& c : m_players)
        if (c.alive && c.role == Role::WildChild && c.model == id)
            becomeWolf(c, tx("{} ist tot – dein Vorbild hat dich verlassen und das Wilde in dir erwacht.",
                             "{} is dead – your role model has left you and the wildness within you awakens.", p->name));

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
    Text text;
    const std::string& n = p->name;
    switch (cause) {
    case DeathCause::Wolves:
    case DeathCause::Poison: text = tx("{} wurde tot aufgefunden.", "{} was found dead.", n); break;
    case DeathCause::Grief: text = tx("{} stirbt vor Kummer um {}.", "{} dies of grief for {}.", n, nameOf(p->lover)); break;
    case DeathCause::Vote: text = tx("{} wurde vom Dorf hingerichtet.", "{} was executed by the village.", n); break;
    case DeathCause::Scapegoat: text = tx("{} wurde als Sündenbock hingerichtet.", "{} was executed as the scapegoat.", n); break;
    case DeathCause::Hunter: text = tx("{} wurde vom Jäger erschossen.", "{} was shot by the hunter.", n); break;
    case DeathCause::Left: text = tx("{} hat das Dorf für immer verlassen.", "{} has left the village for good.", n); break;
    case DeathCause::Fire: text = tx("{} ist in den Flammen umgekommen.", "{} died in the flames.", n); break;
    case DeathCause::Knife:
        text = tx("{} wurde erstochen aufgefunden – ein Messer, keine Klauen!", "{} was found stabbed – a knife, not claws!", n);
        break;
    case DeathCause::HolyWater:
        text = tx("{} wurde vom Weihwasser zu Asche verbrannt – ein Werwolf!", "{} was burned to ash by holy water – a werewolf!", n);
        break;
    case DeathCause::Rust:
        text = tx("{} ist an einer Blutvergiftung gestorben – die rostige Klinge des Ritters!",
                  "{} died of blood poisoning – the knight's rusty sword!", n);
        break;
    }
    if (m_settings.revealRoles && id != m_servantPending) text = tx("{} (Rolle: {})", "{} (role: {})", text, roleText(p->role));
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Death, std::move(text));
}

void Match::checkVotesComplete() {
    if (m_phase != Phase::Vote) return;
    for (const Player& p : m_players)
        if (p.alive && !p.idiotRevealed && p.vote == kNoPlayer) return;
    if (!m_voteGraceApplied && m_timer > kVoteGraceSeconds) {
        m_timer = kVoteGraceSeconds;
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info,
               {"Alle Stimmen sind abgegeben – noch wenige Sekunden zum Umentscheiden.", "All votes are in – a few seconds left to change your mind."});
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
                narrate(tx("{} war der Jäger – doch der letzte Schuss blieb aus.", "{} was the hunter – but the last shot never came.", p.name));
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
                    setCaptain(next, tx("{} hat keinen Nachfolger bestimmt – das Los fällt auf {}.",
                                        "{} named no successor – the lot falls on {}.", p.name, nameOf(next)));
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
        if (p.burn == 0.0f) tell(p.id, {"Das Haus brennt! Raus hier, sofort!", "The house is on fire! Get out, now!"});
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
                       tx("Der Bär von {} brummt plötzlich bedrohlich …", "The bear of {} suddenly growls menacingly …", tamer.name));
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
                enterAssembly({"Die Sonne sinkt – das Dorf versammelt sich, um einen Hauptmann zu wählen.",
                               "The sun is setting – the village gathers to elect a captain."},
                              std::max(20.0f, static_cast<float>(m_settings.discussionSeconds) * 0.5f));
            } else {
                enterNight();
            }
        } else {
            enterAssembly({"Die Sonne sinkt – das Dorf versammelt sich am Feuer.", "The sun is setting – the village gathers at the fire."},
                          m_settings.discussionSeconds);
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
            narrate({"Der Richter räuspert sich und verlangt stotternd eine zweite Abstimmung!",
                     "The judge clears his throat and stammers a demand for a second vote!"});
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
                    ? Text{"Tag 1: Erkundet das Dorf und lernt euch kennen. Am Abend wählt ihr einen Hauptmann – danach "
                           "erwachen die Werwölfe.",
                           "Day 1: Explore the village and get to know each other. In the evening you elect a captain – "
                           "then the werewolves awaken."}
                    : Text{"Tag 1: Erkundet das Dorf und lernt euch kennen. Bei Einbruch der Nacht erwachen die Werwölfe – "
                           "verriegelt dann eure Türen!",
                           "Day 1: Explore the village and get to know each other. At nightfall the werewolves awaken – "
                           "bolt your doors then!"});
    } else {
        setTimer(m_settings.daySeconds);
        narrate(tx("Tag {}: Sucht nach Spuren und sprecht miteinander. Die Glocke ruft zur Versammlung.",
                   "Day {}: Look for clues and talk to each other. The bell calls a gathering.", m_day));
    }
}

void Match::enterAssembly(const Text& reason, float seconds) {
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
            narrate(tx("Der Bär von {} brummt bedrohlich – neben ihm sitzt ein Werwolf!",
                       "The bear of {} growls menacingly – a werewolf is sitting next to it!", alive[i]->name));
        else
            narrate(tx("Der Bär von {} schnauft friedlich.", "The bear of {} snorts peacefully.", alive[i]->name));
    }
}

void Match::enterVote() {
    m_phase = Phase::Vote;
    m_voteGraceApplied = false;
    for (Player& p : m_players) p.vote = kNoPlayer;
    setTimer(m_settings.voteSeconds);
    if (m_election) narrate({"Wählt euren Hauptmann! Schaut eure Wahl an und drückt E.", "Elect your captain! Look at your choice and press E."});
    else
        narrate({"Die Abstimmung beginnt! Schaut die Person an, die hingerichtet werden soll, und drückt E.",
                 "The vote begins! Look at the person who should be executed and press E."});
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
    Text text;
    if (ranking.empty())
        text = tx("Niemand hat gewählt – das Los bestimmt {} zum Hauptmann.", "Nobody voted – the lot makes {} captain.", nameOf(chosen));
    else if (top.size() > 1)
        text = tx("Gleichstand – das Los entscheidet: {} ist der neue Hauptmann!", "A tie – the lot decides: {} is the new captain!", nameOf(chosen));
    else text = tx("{} ist zum Hauptmann gewählt!", "{} has been elected captain!", nameOf(chosen));
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
        std::string summary;
        for (size_t i = 0; i < ranking.size(); ++i) {
            if (i > 0) summary += ", ";
            summary += std::format("{} ({})", nameOf(ranking[i].first), ranking[i].second);
        }
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info,
               m_election ? tx("Wahl: {}", "Election: {}", summary) : tx("Stimmen: {}", "Votes: {}", summary));
    }
    if (m_election) {
        resolveElection(ranking);
        return;
    }
    if (ranking.empty()) {
        narrate({"Niemand hat abgestimmt. Das Dorf geht ratlos auseinander.", "Nobody voted. The village disperses, at a loss."});
        return;
    }
    if (ranking.size() > 1 && ranking[0].second == ranking[1].second) {
        for (const Player& p : m_players) {
            if (!p.alive || p.role != Role::Scapegoat) continue;
            narrate({"Gleichstand! Das Dorf kann sich nicht einigen – also muss der Sündenbock dran glauben.",
                     "A tie! The village cannot agree – so the scapegoat has to pay."});
            m_firstExecution = true;
            kill(p.id, DeathCause::Scapegoat, true);
            return;
        }
        narrate({"Gleichstand! Das Dorf kann sich nicht einigen – niemand wird hingerichtet.", "A tie! The village cannot agree – nobody is executed."});
        return;
    }
    Player* condemned = findMutable(ranking[0].first);
    if (!condemned) return;
    if (condemned->role == Role::Idiot && !condemned->idiotRevealed) {
        condemned->idiotRevealed = true;
        narrate(tx("{} wird zum Galgen geführt … und grinst so harmlos, dass das Dorf begreift: Das ist nur der "
                   "Dorfdepp! Man lässt {} laufen – abstimmen darf {} aber nie wieder.",
                   "{} is led to the gallows … and grins so harmlessly that the village realises: it's only the village "
                   "idiot! They let {} go – but {} may never vote again.",
                   condemned->name, condemned->name, condemned->name));
        markDirty();
        return;
    }
    if (condemned->role == Role::Jester)
        narrate(tx("{} lacht schallend, als sich die Schlinge zuzieht …", "{} laughs out loud as the noose tightens …", condemned->name));
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
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, tx("{} war: {}.", "{} was: {}.", nameOf(id), roleText(find(id)->role)));
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
            tell(p.id, tx("Heute Nacht spielst du: {}. {}", "Tonight you play: {}. {}", roleText(p.acting), roleDescriptionText(p.acting)));
        }
    }
    narrate(tx("Nacht {} bricht herein. Geht nach Hause und verriegelt die Türen – in der Dunkelheit lauern die Werwölfe …",
               "Night {} falls. Go home and bolt your doors – the werewolves lurk in the dark …", m_day));
    tellWolves(kills > 1 ? tx("Die Jagd beginnt – heute Nacht dürft ihr {} Opfer reißen!", "The hunt begins – tonight you may take {} victims!", kills)
                         : Text{"Die Jagd beginnt. Verwandelt euch mit R und reißt ein Opfer mit Linksklick. Verriegelte Türen "
                                "brecht ihr mit Linksklick auf – aber das hört man!",
                                "The hunt begins. Transform with R and take a victim with left click. Break bolted doors with "
                                "left click – but people will hear it!"});
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
                    tell(c.id, {"Amors Pfeile flogen ziellos durch die Nacht …", "Cupid's arrows flew aimlessly through the night …"});
                    makeLovers(c.cupidPicks[0], c.cupidPicks[1]);
                }
            }
            if (c.role == Role::WildChild && c.model == kNoPlayer) {
                std::vector<uint8_t> pool;
                for (const Player& p : m_players)
                    if (p.alive && p.id != c.id) pool.push_back(p.id);
                if (!pool.empty()) {
                    c.model = m_rng.pick(pool);
                    tell(c.id, tx("Das Schicksal wählt dein Vorbild: {}.", "Fate chooses your role model: {}.", nameOf(c.model)));
                }
            }
            if (c.role == Role::WolfHound && !c.sideChosen) {
                c.sideChosen = true;
                tell(c.id, {"Du hast gezögert – also bleibst du dem Dorf treu.", "You hesitated – so you stay loyal to the village."});
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
        Text report;
        if (t.trackHouses.empty()) report = tx("Die Fährte von {} führte in kein fremdes Haus.", "The trail of {} led into no other house.", name);
        else {
            std::vector<Text> labels;
            for (int h : t.trackHouses) labels.push_back(houseLabel(h));
            report = tx("Die Fährte von {} führte in der Nacht in {}.", "During the night the trail of {} led into {}.", name, joinTexts(labels));
        }
        if (!t.trackVictims.empty()) {
            std::vector<std::string> victims;
            for (uint8_t v : t.trackVictims) victims.push_back(nameOf(v));
            report = tx("{} Und sie endet an der Leiche von {}!", "{} And it ends at the body of {}!", report, joinNames(victims));
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
        if (p.alive)
            tell(p.id, tx("Eine seltsame Melodie hat dich verzaubert. Verzaubert sind: {}.",
                          "A strange melody has enchanted you. The enchanted are: {}.", joinNames(enchanted)));
    }

    m_phase = Phase::Morning;
    setTimer(kMorningSeconds);
    narrate({"Der Morgen graut über dem Dorf …", "Dawn breaks over the village …"});
    if (m_nightDeaths.empty())
        narrate({"Wie durch ein Wunder hat in dieser Nacht niemand das Leben verloren.", "As if by a miracle, nobody lost their life tonight."});
    for (const PendingDeath& d : m_nightDeaths) announce(d.id, d.cause);
    m_nightDeaths.clear();
    if (const Player* marked = find(m_marked); marked && marked->alive)
        narrate(tx("Ein Rabe hockt auf dem Dach von {} – bei der nächsten Abstimmung hat {} zwei Stimmen gegen sich.",
                   "A raven perches on the roof of {} – at the next vote {} starts with two votes against them.", marked->name, marked->name));
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
    auto victory = [&](Text text) { notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory, std::move(text)); };
    switch (w) {
    case Team::Village: victory({"Das Dorf hat gesiegt! Kein Werwolf treibt mehr sein Unwesen.", "The village has won! No werewolf haunts it any longer."}); break;
    case Team::Wolves: victory({"Die Werwölfe haben gesiegt! Das Dorf ist ihrem Hunger erlegen.", "The werewolves have won! The village has fallen to their hunger."}); break;
    case Team::Lovers: victory({"Die Verliebten haben gesiegt! Ihre Liebe hat alles überdauert.", "The lovers have won! Their love has outlasted everything."}); break;
    case Team::Jester:
        victory(tx("Der Narr hat gesiegt! {} wollte nichts anderes, als gehängt zu werden.",
                   "The jester has won! {} wanted nothing more than to be hanged.", nameOf(m_jesterWinner)));
        break;
    case Team::Angel:
        victory(tx("Der Engel hat gesiegt! {} ist schon in der ersten Runde ins Paradies aufgestiegen.",
                   "The angel has won! {} ascended to paradise in the very first round.", nameOf(m_angelWinner)));
        break;
    case Team::Killer:
        victory({"Der Serienmörder hat gesiegt! Niemand ist mehr übrig, der ihn aufhalten könnte.",
                 "The serial killer has won! Nobody is left who could stop them."});
        break;
    case Team::Arsonist: victory({"Der Brandstifter hat gesiegt! Vom Dorf bleibt nur glühende Asche.", "The arsonist has won! Only glowing ashes remain of the village."}); break;
    case Team::Piper: victory({"Der Flötenspieler hat gesiegt! Das ganze Dorf tanzt nach seiner Melodie.", "The pied piper has won! The whole village dances to their tune."}); break;
    case Team::WhiteWolf:
        victory({"Der weiße Werwolf hat gesiegt! Er hat sogar sein eigenes Rudel verraten.", "The white werewolf has won! It even betrayed its own pack."});
        break;
    case Team::Sectarian: victory({"Der Sektierer hat gesiegt! Nur seine Hälfte des Dorfes ist übrig.", "The sectarian has won! Only their half of the village is left."}); break;
    default: victory({"Niemand hat überlebt. Stille legt sich über das Dorf.", "Nobody survived. Silence falls over the village."}); break;
    }
    std::vector<Text> roles;
    for (const Player& p : m_players)
        roles.push_back(p.startRole != p.role ? tx("{} ({}, später {})", "{} ({}, later {})", p.name, roleText(p.startRole), roleText(p.role))
                                              : tx("{} ({})", "{} ({})", p.name, roleText(p.role)));
    Text list;
    for (size_t i = 0; i < roles.size(); ++i) list = i == 0 ? roles[i] : tx("{}, {}", "{}, {}", list, roles[i]);
    notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info, tx("Die Rollen: {}", "The roles: {}", list));
    return true;
}

} // namespace ww
