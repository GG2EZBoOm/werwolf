#include "logic/Game.h"

#include <algorithm>
#include <format>
#include <map>

namespace ww {

namespace {

constexpr float kWitchSeconds = 25.0f;
constexpr float kHunterSeconds = 25.0f;
constexpr float kMorningSeconds = 5.0f;
constexpr float kMorningPerDeath = 2.5f;
constexpr float kExecutionSeconds = 6.0f;
constexpr float kVoteGraceSeconds = 4.0f;
constexpr float kNightGraceSeconds = 1.5f;

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

bool Game::start(const std::vector<Seat>& seats, const Settings& settings, uint64_t seed, std::string& error) {
    Settings s = settings;
    s.sanitize();
    std::vector<Role> roles = buildRoleList(s, static_cast<int>(seats.size()), &error);
    if (roles.empty()) return false;
    m_rng.seed(seed);
    m_rng.shuffle(roles);
    return startWithRoles(seats, roles, s, m_rng.next64(), error);
}

bool Game::startWithRoles(const std::vector<Seat>& seats, const std::vector<Role>& roles, const Settings& settings,
                          uint64_t seed, std::string& error) {
    if (seats.size() != roles.size()) {
        error = "Rollenzahl passt nicht zur Spielerzahl.";
        return false;
    }
    if (static_cast<int>(seats.size()) < kMinPlayers) {
        error = "Es werden mindestens " + std::to_string(kMinPlayers) + " Spieler benötigt.";
        return false;
    }

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
        m_players.push_back(std::move(p));
    }

    m_phase = Phase::Lobby;
    m_day = 0;
    m_winner = Team::None;
    m_victim = m_protected = m_lastProtected = kNoPlayer;
    m_healAvailable = m_poisonAvailable = roleInGame(Role::Witch);
    m_healUsed = false;
    m_poisonTarget = kNoPlayer;
    m_seerResults.clear();
    m_recentDeaths.clear();
    m_hunterQueue.clear();
    m_hunter = kNoPlayer;
    m_notices.clear();

    std::vector<std::string> wolfNames;
    for (const Player& p : m_players)
        if (p.role == Role::Werewolf) wolfNames.push_back(p.name);

    const size_t wolves = wolfNames.size();
    narrate(std::format("Das Spiel beginnt! {} Seelen leben im Dorf – {} davon {}.", m_players.size(), wolves,
                        wolves == 1 ? "ist ein Werwolf" : "sind Werwölfe"));
    for (const Player& p : m_players) {
        tell(p.id, std::format("Deine Rolle: {}. {}", roleName(p.role), roleDescription(p.role)));
        if (p.role == Role::Werewolf) {
            std::vector<std::string> others;
            for (const Player& o : m_players)
                if (o.role == Role::Werewolf && o.id != p.id) others.push_back(o.name);
            tell(p.id, others.empty() ? std::string("Du jagst allein – kein anderer Werwolf ist im Dorf.")
                                      : "Dein Rudel: " + joinNames(others) + ".");
        }
    }

    enterNight();
    return true;
}

// --- Hilfsfunktionen ---------------------------------------------------------

const Game::Player* Game::find(uint8_t id) const {
    for (const Player& p : m_players)
        if (p.id == id) return &p;
    return nullptr;
}

Game::Player* Game::findMutable(uint8_t id) {
    for (Player& p : m_players)
        if (p.id == id) return &p;
    return nullptr;
}

std::string Game::nameOf(uint8_t id) const {
    const Player* p = find(id);
    return p ? p->name : std::string("?");
}

std::vector<uint8_t> Game::alivePlayers() const {
    std::vector<uint8_t> out;
    for (const Player& p : m_players)
        if (p.alive) out.push_back(p.id);
    return out;
}

bool Game::roleInGame(Role role) const {
    return std::any_of(m_players.begin(), m_players.end(), [&](const Player& p) { return p.role == role; });
}

const Game::Player* Game::aliveWithRole(Role role) const {
    for (const Player& p : m_players)
        if (p.alive && p.role == role) return &p;
    return nullptr;
}

bool Game::isNightActor(const Player& p) const {
    if (!p.alive) return false;
    switch (p.role) {
    case Role::Werewolf:
    case Role::Seer:
    case Role::Protector: return true;
    case Role::Cupid: return m_day == 1;
    default: return false;
    }
}

bool Game::mixedCouple() const {
    for (const Player& p : m_players) {
        if (p.lover == kNoPlayer) continue;
        const Player* partner = find(p.lover);
        if (!partner) return false;
        return (p.role == Role::Werewolf) != (partner->role == Role::Werewolf);
    }
    return false;
}

void Game::setTimer(float seconds) {
    m_timer = seconds;
    m_phaseDuration = seconds;
    markDirty();
}

void Game::resetPhaseState() {
    for (Player& p : m_players) {
        p.selection.clear();
        p.confirmed = false;
        p.skip = false;
    }
}

void Game::notify(Notice::Audience audience, uint8_t player, NoticeKind kind, std::string text) {
    m_notices.push_back(Notice{audience, player, kind, std::move(text)});
}

std::vector<Notice> Game::takeNotices() {
    std::vector<Notice> out;
    out.swap(m_notices);
    return out;
}

bool Game::takeDirty() {
    const bool d = m_dirty;
    m_dirty = false;
    return d;
}

// --- Abfragen ----------------------------------------------------------------

ActionKind Game::actionFor(uint8_t playerId) const {
    const Player* p = find(playerId);
    if (!p) return ActionKind::None;
    switch (m_phase) {
    case Phase::Night:
        if (!p->alive) return ActionKind::None;
        switch (p->role) {
        case Role::Werewolf: return ActionKind::WolfVote;
        case Role::Seer: return ActionKind::Inspect;
        case Role::Protector: return ActionKind::Protect;
        case Role::Cupid: return m_day == 1 ? ActionKind::CupidPick : ActionKind::None;
        default: return ActionKind::None;
        }
    case Phase::Witch:
        return (p->alive && p->role == Role::Witch) ? ActionKind::WitchPotions : ActionKind::None;
    case Phase::Hunter:
        return p->id == m_hunter ? ActionKind::HunterShot : ActionKind::None;
    case Phase::Day:
        return p->alive ? ActionKind::SkipDiscussion : ActionKind::None;
    case Phase::Vote:
        return p->alive ? ActionKind::DayVote : ActionKind::None;
    default:
        return ActionKind::None;
    }
}

bool Game::canTarget(uint8_t playerId, uint8_t targetId) const {
    const Player* p = find(playerId);
    const Player* t = find(targetId);
    if (!p || !t || !t->alive) return false;
    switch (actionFor(playerId)) {
    case ActionKind::WolfVote: return t->role != Role::Werewolf;
    case ActionKind::Inspect:
        if (t->id == p->id) return false;
        return std::none_of(m_seerResults.begin(), m_seerResults.end(),
                            [&](const SeerResult& r) { return r.player == t->id; });
    case ActionKind::Protect: return t->id != m_lastProtected;
    case ActionKind::CupidPick: return true;
    case ActionKind::WitchPotions: return t->id != p->id;
    case ActionKind::HunterShot: return t->id != p->id;
    case ActionKind::DayVote: return t->id != p->id;
    default: return false;
    }
}

std::optional<ChatChannel> Game::speakChannel(uint8_t playerId) const {
    const Player* p = find(playerId);
    if (!p) return std::nullopt;
    return ww::speakChannel(m_phase, p->alive, p->role);
}

bool Game::canRead(uint8_t playerId, ChatChannel channel) const {
    const Player* p = find(playerId);
    if (!p) return channel == ChatChannel::Public;
    return canReadChannel(channel, m_phase, p->alive, p->role);
}

Team Game::teamOf(uint8_t playerId) const {
    const Player* p = find(playerId);
    if (!p) return Team::None;
    if (p->lover != kNoPlayer && mixedCouple()) return Team::Lovers;
    return roleTeam(p->role);
}

bool Game::isWinner(uint8_t playerId) const {
    if (m_winner == Team::None || m_winner == Team::Nobody) return false;
    return teamOf(playerId) == m_winner;
}

GameView Game::viewFor(uint8_t viewer) const {
    GameView v;
    v.phase = m_phase;
    v.day = static_cast<uint16_t>(m_day);
    v.timeLeft = std::max(0.0f, m_timer);
    v.phaseDuration = m_phaseDuration;
    v.you = viewer;
    v.settings = m_settings;
    v.winner = m_winner;

    const Player* me = find(viewer);
    const bool over = m_phase == Phase::GameOver;
    const bool spectator = !me || !me->alive;
    const bool meWolf = me && me->role == Role::Werewolf;

    for (const Player& p : m_players) {
        PlayerView pv;
        pv.id = p.id;
        pv.name = p.name;
        pv.alive = p.alive;
        pv.connected = p.connected;
        pv.bot = p.bot;

        bool knowRole = over || spectator || p.id == viewer || (!p.alive && m_settings.revealRoles) ||
                        (meWolf && p.role == Role::Werewolf) || (me && me->lover == p.id);
        if (!knowRole && me && me->role == Role::Seer) {
            knowRole = std::any_of(m_seerResults.begin(), m_seerResults.end(),
                                   [&](const SeerResult& r) { return r.player == p.id; });
        }
        pv.role = knowRole ? p.role : Role::Unknown;
        pv.lover = p.lover != kNoPlayer &&
                   (over || spectator || p.id == viewer || (me && (me->lover == p.id || me->role == Role::Cupid)));

        if ((m_phase == Phase::Vote || m_phase == Phase::Execution) && !p.selection.empty()) {
            pv.vote = p.selection[0];
        } else if (m_phase == Phase::Night && p.alive && p.role == Role::Werewolf && (meWolf || spectator) &&
                   !p.selection.empty()) {
            pv.vote = p.selection[0];
        }
        if (m_phase == Phase::Day) pv.skip = p.skip;
        pv.winner = over && isWinner(p.id);
        v.players.push_back(std::move(pv));
    }

    if (me) {
        v.yourRole = me->role;
        v.action = actionFor(viewer);
        v.selection = me->selection;
        v.confirmed = me->confirmed;
        v.loverPartner = me->lover;
        if (me->role == Role::Seer) v.seerResults = m_seerResults;
        if (me->role == Role::Protector) v.lastProtected = m_lastProtected;
        if (me->role == Role::Witch) {
            v.healAvailable = m_healAvailable;
            v.poisonAvailable = m_poisonAvailable;
            if (m_phase == Phase::Witch) {
                v.witchVictim = m_victim;
                v.healUsed = m_healUsed;
                v.poisonTarget = m_poisonTarget;
            }
        }
    }
    return v;
}

// --- Aktionen ----------------------------------------------------------------

std::string Game::select(uint8_t playerId, uint8_t target) {
    Player* p = findMutable(playerId);
    if (!p) return "Du nimmst nicht an diesem Spiel teil.";
    const ActionKind action = actionFor(playerId);
    if (action == ActionKind::None || action == ActionKind::SkipDiscussion)
        return "Du kannst gerade niemanden auswählen.";
    if (p->confirmed && action != ActionKind::DayVote) return "Deine Entscheidung steht bereits fest.";

    if (target == kNoPlayer) {
        if (action == ActionKind::WitchPotions) m_poisonTarget = kNoPlayer;
        p->selection.clear();
        markDirty();
        return {};
    }
    if (!canTarget(playerId, target)) return "Dieser Spieler kann nicht gewählt werden.";

    switch (action) {
    case ActionKind::CupidPick: {
        auto it = std::find(p->selection.begin(), p->selection.end(), target);
        if (it != p->selection.end()) {
            p->selection.erase(it);
        } else {
            if (p->selection.size() >= 2) p->selection.erase(p->selection.begin());
            p->selection.push_back(target);
        }
        break;
    }
    case ActionKind::WitchPotions:
        if (!m_poisonAvailable) return "Dein Gifttrank ist bereits verbraucht.";
        m_poisonTarget = target;
        p->selection = {target};
        break;
    default:
        p->selection = {target};
        break;
    }
    markDirty();
    if (action == ActionKind::DayVote) checkVotesComplete();
    return {};
}

std::string Game::confirm(uint8_t playerId) {
    Player* p = findMutable(playerId);
    if (!p) return "Du nimmst nicht an diesem Spiel teil.";
    const ActionKind action = actionFor(playerId);
    switch (action) {
    case ActionKind::CupidPick:
        if (p->selection.size() != 2) return "Wähle genau zwei Spieler aus.";
        break;
    case ActionKind::Protect:
    case ActionKind::Inspect:
    case ActionKind::WolfVote:
    case ActionKind::HunterShot:
        if (p->selection.empty()) return "Wähle zuerst einen Spieler aus.";
        break;
    case ActionKind::WitchPotions:
        break;
    default:
        return "Hier gibt es nichts zu bestätigen.";
    }
    if (p->confirmed) return {};
    p->confirmed = true;
    markDirty();

    switch (action) {
    case ActionKind::Inspect:
        revealSeer(*p);
        checkNightComplete();
        break;
    case ActionKind::CupidPick:
    case ActionKind::Protect:
    case ActionKind::WolfVote:
        checkNightComplete();
        break;
    case ActionKind::WitchPotions:
        m_timer = std::min(m_timer, 1.0f);
        break;
    case ActionKind::HunterShot:
        finishHunter();
        break;
    default:
        break;
    }
    return {};
}

std::string Game::setWitchHeal(uint8_t playerId, bool use) {
    if (actionFor(playerId) != ActionKind::WitchPotions) return "Du kannst gerade keinen Trank benutzen.";
    Player* p = findMutable(playerId);
    if (p->confirmed) return "Deine Entscheidung steht bereits fest.";
    if (use && !m_healAvailable) return "Dein Heiltrank ist bereits verbraucht.";
    if (use && m_victim == kNoPlayer) return "Heute Nacht gibt es niemanden zu retten.";
    m_healUsed = use;
    markDirty();
    return {};
}

std::string Game::setSkip(uint8_t playerId, bool skip) {
    if (actionFor(playerId) != ActionKind::SkipDiscussion) return "Gerade gibt es nichts zu überspringen.";
    Player* p = findMutable(playerId);
    p->skip = skip;
    markDirty();

    int alive = 0, skips = 0;
    for (const Player& o : m_players) {
        if (!o.alive) continue;
        ++alive;
        if (o.skip) ++skips;
    }
    if (skips * 2 > alive) {
        narrate("Die Mehrheit will sofort abstimmen – die Diskussion endet.");
        enterVote();
    }
    return {};
}

void Game::setConnected(uint8_t playerId, bool connected) {
    if (Player* p = findMutable(playerId)) {
        p->connected = connected;
        markDirty();
    }
}

void Game::playerLeft(uint8_t playerId) {
    Player* p = findMutable(playerId);
    if (!p) return;
    p->connected = false;
    markDirty();
    if (m_phase == Phase::GameOver || m_phase == Phase::Lobby || !p->alive) {
        if (m_phase == Phase::Hunter && m_hunter == playerId) finishHunter();
        return;
    }

    m_recentDeaths.clear();
    kill(playerId, DeathCause::Left);
    announceDeaths();
    if (m_hunterQueue.empty() && checkWin()) return;
    checkNightComplete();
    checkVotesComplete();
}

void Game::revealSeer(Player& seer) {
    if (seer.selection.empty()) return;
    const uint8_t targetId = seer.selection[0];
    const Player* target = find(targetId);
    if (!target) return;
    seer.confirmed = true;
    for (const SeerResult& r : m_seerResults)
        if (r.player == targetId) return;
    m_seerResults.push_back({targetId, target->role});
    tell(seer.id, std::format("Deine Vision zeigt dir: {} ist {}.", target->name, roleWithArticle(target->role)));
    markDirty();
}

void Game::checkNightComplete() {
    if (m_phase != Phase::Night) return;
    for (const Player& p : m_players)
        if (isNightActor(p) && !p.confirmed) return;
    if (!m_nightGraceApplied && m_timer > kNightGraceSeconds) m_timer = kNightGraceSeconds;
    m_nightGraceApplied = true;
}

void Game::checkVotesComplete() {
    if (m_phase != Phase::Vote) return;
    for (const Player& p : m_players)
        if (p.alive && p.selection.empty()) return;
    if (!m_voteGraceApplied && m_timer > kVoteGraceSeconds) {
        m_timer = kVoteGraceSeconds;
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Info,
               "Alle Stimmen sind abgegeben – nur noch wenige Sekunden, um es sich anders zu überlegen.");
    }
    m_voteGraceApplied = true;
}

// --- Phasen ------------------------------------------------------------------

void Game::update(float dt) {
    if (m_phase == Phase::Lobby || m_phase == Phase::GameOver) return;
    m_timer -= dt;
    if (m_timer <= 0.0f) advance();
}

void Game::advance() {
    switch (m_phase) {
    case Phase::Night: endNight(); break;
    case Phase::Witch: resolveNight(); break;
    case Phase::Morning: afterDeaths(Phase::Day); break;
    case Phase::Hunter: finishHunter(); break;
    case Phase::Day: enterVote(); break;
    case Phase::Vote: resolveVote(); break;
    case Phase::Execution: afterDeaths(Phase::Night); break;
    default: break;
    }
}

void Game::enterNight() {
    ++m_day;
    m_phase = Phase::Night;
    resetPhaseState();
    m_victim = kNoPlayer;
    m_protected = kNoPlayer;
    m_healUsed = false;
    m_poisonTarget = kNoPlayer;
    m_nightGraceApplied = false;
    setTimer(m_settings.nightSeconds);
    narrate(std::format("Nacht {} bricht herein. Das Dorf schläft ein – und die Werwölfe erwachen …", m_day));
    if (m_day == 1 && aliveWithRole(Role::Cupid)) narrate("Amor spannt seinen Bogen …");
}

void Game::endNight() {
    // Amor verkuppelt zwei Spieler (nur in der ersten Nacht)
    if (m_day == 1) {
        if (const Player* c = aliveWithRole(Role::Cupid)) {
            Player* cupid = findMutable(c->id);
            std::vector<uint8_t> picks;
            for (uint8_t id : cupid->selection) {
                const Player* t = find(id);
                if (t && t->alive && std::find(picks.begin(), picks.end(), id) == picks.end()) picks.push_back(id);
            }
            if (picks.size() < 2) {
                tell(cupid->id, "Du hast dich nicht entschieden – Amors Pfeile fliegen ziellos durch die Nacht.");
                std::vector<uint8_t> pool = alivePlayers();
                m_rng.shuffle(pool);
                for (uint8_t id : pool) {
                    if (picks.size() >= 2) break;
                    if (std::find(picks.begin(), picks.end(), id) == picks.end()) picks.push_back(id);
                }
            }
            if (picks.size() == 2) {
                Player* a = findMutable(picks[0]);
                Player* b = findMutable(picks[1]);
                a->lover = b->id;
                b->lover = a->id;
                tell(cupid->id, std::format("{} und {} sind nun unsterblich ineinander verliebt.", a->name, b->name));
                const bool mixed = mixedCouple();
                for (auto [x, y] : {std::pair{a, b}, std::pair{b, a}}) {
                    tell(x->id, std::format("Amors Pfeil hat dich getroffen! Du bist in {} verliebt (Rolle: {}). "
                                            "Stirbt einer von euch, stirbt auch der andere.",
                                            y->name, roleName(y->role)));
                    if (mixed)
                        tell(x->id, "Ihr gehört verschiedenen Lagern an. Gemeinsam gewinnt ihr nur, wenn ihr beide "
                                    "als Letzte überlebt.");
                }
            }
        }
    }

    // Beschützer
    m_protected = kNoPlayer;
    if (const Player* prot = aliveWithRole(Role::Protector)) {
        if (!prot->selection.empty() && canTarget(prot->id, prot->selection[0])) m_protected = prot->selection[0];
    }
    m_lastProtected = m_protected;

    // Seherin – unbestätigte Auswahl gilt trotzdem
    if (const Player* s = aliveWithRole(Role::Seer)) {
        Player* seer = findMutable(s->id);
        if (!seer->confirmed && !seer->selection.empty()) revealSeer(*seer);
    }

    // Werwölfe: Mehrheit entscheidet, Gleichstand per Los
    std::map<uint8_t, int> tally;
    for (const Player& p : m_players) {
        if (!p.alive || p.role != Role::Werewolf || p.selection.empty()) continue;
        if (canTarget(p.id, p.selection[0])) ++tally[p.selection[0]];
    }
    m_victim = kNoPlayer;
    if (!tally.empty()) {
        int best = 0;
        for (const auto& [id, count] : tally) best = std::max(best, count);
        std::vector<uint8_t> tied;
        for (const auto& [id, count] : tally)
            if (count == best) tied.push_back(id);
        m_victim = m_rng.pick(tied);
        if (tied.size() > 1)
            notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private,
                   std::format("Das Rudel war uneins – das Los fiel auf {}.", nameOf(m_victim)));
    } else {
        std::vector<uint8_t> candidates;
        for (const Player& p : m_players)
            if (p.alive && p.role != Role::Werewolf) candidates.push_back(p.id);
        if (!candidates.empty()) {
            m_victim = m_rng.pick(candidates);
            notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private,
                   std::format("Ihr konntet euch nicht entscheiden – der Hunger trieb euch zu {}.", nameOf(m_victim)));
        }
    }
    if (m_victim != kNoPlayer)
        notify(Notice::Audience::Wolves, kNoPlayer, NoticeKind::Private,
               std::format("Euer Opfer in dieser Nacht: {}.", nameOf(m_victim)));

    if (roleInGame(Role::Witch)) enterWitch();
    else resolveNight();
}

void Game::enterWitch() {
    m_phase = Phase::Witch;
    resetPhaseState();
    m_healUsed = false;
    m_poisonTarget = kNoPlayer;
    const Player* witch = aliveWithRole(Role::Witch);
    // Auch ohne lebende Hexe kurz warten, damit niemand Rückschlüsse ziehen kann
    setTimer(witch ? kWitchSeconds : m_rng.uniform(4.0f, 8.0f));
    narrate("Die Hexe erwacht und betrachtet ihre Tränke …");
    if (!witch) return;
    if (m_victim != kNoPlayer) {
        tell(witch->id, std::format("Die Werwölfe haben {} gewählt.{}", nameOf(m_victim),
                                    m_healAvailable ? " Willst du deinen Heiltrank einsetzen?" : ""));
    } else {
        tell(witch->id, "Heute Nacht haben die Werwölfe kein Opfer gewählt.");
    }
}

void Game::resolveNight() {
    m_recentDeaths.clear();
    if (m_victim != kNoPlayer) {
        const bool saved = m_victim == m_protected || m_healUsed;
        if (!saved) kill(m_victim, DeathCause::Wolves);
    }
    if (m_healUsed) m_healAvailable = false;
    if (m_poisonTarget != kNoPlayer) {
        m_poisonAvailable = false;
        kill(m_poisonTarget, DeathCause::Poison);
    }
    enterMorning();
}

void Game::enterMorning() {
    m_phase = Phase::Morning;
    resetPhaseState();
    const size_t deaths = m_recentDeaths.size();
    narrate("Der Morgen graut über dem Dorf …");
    if (deaths == 0) narrate("Wie durch ein Wunder ist in dieser Nacht niemand gestorben.");
    else announceDeaths();
    setTimer(kMorningSeconds + kMorningPerDeath * static_cast<float>(deaths));
}

void Game::enterHunter(Phase returnTo) {
    m_hunter = m_hunterQueue.front();
    m_hunterQueue.erase(m_hunterQueue.begin());
    m_hunterReturn = returnTo;
    m_phase = Phase::Hunter;
    resetPhaseState();
    setTimer(kHunterSeconds);
    narrate(std::format("{} war der Jäger und legt ein letztes Mal an …", nameOf(m_hunter)));
    tell(m_hunter, "Du bist gestorben – aber nicht ohne Gegenwehr! Wähle, wen du mit in den Tod reißt.");
}

void Game::finishHunter() {
    if (m_phase != Phase::Hunter) return;
    const Player* hunter = find(m_hunter);
    const uint8_t target = (hunter && !hunter->selection.empty()) ? hunter->selection[0] : kNoPlayer;
    const Player* t = find(target);
    m_recentDeaths.clear();
    if (t && t->alive && target != m_hunter) {
        narrate("Ein Schuss hallt durch das Dorf!");
        kill(target, DeathCause::Hunter);
        announceDeaths();
    } else {
        narrate(std::format("{} senkt das Gewehr – der Schuss bleibt aus.", nameOf(m_hunter)));
    }
    m_hunter = kNoPlayer;
    afterDeaths(m_hunterReturn);
}

void Game::enterDay() {
    m_phase = Phase::Day;
    resetPhaseState();
    setTimer(m_settings.discussionSeconds);
    narrate(std::format("Tag {}: Das Dorf versammelt sich am Feuer. Wer von euch ist ein Werwolf?", m_day));
}

void Game::enterVote() {
    m_phase = Phase::Vote;
    resetPhaseState();
    m_voteGraceApplied = false;
    setTimer(m_settings.voteSeconds);
    narrate("Die Abstimmung beginnt! Wer soll hingerichtet werden?");
}

void Game::resolveVote() {
    std::map<uint8_t, int> tally;
    for (const Player& p : m_players) {
        if (!p.alive || p.selection.empty()) continue;
        const Player* t = find(p.selection[0]);
        if (t && t->alive) ++tally[t->id];
    }

    m_recentDeaths.clear();
    if (tally.empty()) {
        narrate("Niemand hat abgestimmt. Das Dorf geht ratlos auseinander.");
    } else {
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
        } else {
            kill(ranking[0].first, DeathCause::Vote);
            announceDeaths();
        }
    }

    // Stimmen bleiben während des Urteils sichtbar
    m_phase = Phase::Execution;
    for (Player& p : m_players) p.confirmed = false;
    setTimer(kExecutionSeconds);
}

void Game::afterDeaths(Phase next) {
    if (!m_hunterQueue.empty()) {
        enterHunter(next);
        return;
    }
    if (checkWin()) return;
    if (next == Phase::Day) enterDay();
    else enterNight();
}

void Game::kill(uint8_t id, DeathCause cause) {
    Player* p = findMutable(id);
    if (!p || !p->alive) return;
    p->alive = false;
    p->selection.clear();
    p->confirmed = false;
    m_recentDeaths.push_back({id, cause});
    if (p->role == Role::Hunter && cause != DeathCause::Left) m_hunterQueue.push_back(id);
    markDirty();
    if (p->lover != kNoPlayer) kill(p->lover, DeathCause::Grief);
}

void Game::announceDeaths() {
    for (const Death& d : m_recentDeaths) {
        const Player* p = find(d.player);
        if (!p) continue;
        std::string text;
        switch (d.cause) {
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
    m_recentDeaths.clear();
}

Team Game::evaluateWinner() const {
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

bool Game::checkWin() {
    const Team w = evaluateWinner();
    if (w == Team::None) return false;
    m_winner = w;
    m_phase = Phase::GameOver;
    m_timer = 0.0f;
    m_phaseDuration = 0.0f;
    resetPhaseState();
    markDirty();

    switch (w) {
    case Team::Village:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory,
               "Das Dorf hat gesiegt! Kein Werwolf treibt mehr sein Unwesen.");
        break;
    case Team::Wolves:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory,
               "Die Werwölfe haben gesiegt! Das Dorf ist ihrem Hunger erlegen.");
        break;
    case Team::Lovers:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory,
               "Die Verliebten haben gesiegt! Ihre Liebe hat alle Gefahren überdauert.");
        break;
    default:
        notify(Notice::Audience::All, kNoPlayer, NoticeKind::Victory,
               "Niemand hat überlebt. Stille legt sich über das verlassene Dorf.");
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
