#include "logic/Types.h"

#include <algorithm>

namespace ww {

const char* roleName(Role role) {
    switch (role) {
    case Role::Villager: return "Dorfbewohner";
    case Role::Werewolf: return "Werwolf";
    case Role::Seer: return "Seherin";
    case Role::Witch: return "Hexe";
    case Role::Hunter: return "Jäger";
    case Role::Cupid: return "Amor";
    case Role::Protector: return "Beschützer";
    default: return "Unbekannt";
    }
}

const char* roleDescription(Role role) {
    switch (role) {
    case Role::Villager:
        return "Du hast keine besonderen Fähigkeiten. Finde die Werwölfe und stimme sie tagsüber aus dem Dorf.";
    case Role::Werewolf:
        return "Jede Nacht wählst du mit deinem Rudel ein Opfer. Tagsüber musst du unerkannt bleiben.";
    case Role::Seer:
        return "Jede Nacht darfst du die wahre Rolle eines Mitspielers erfahren.";
    case Role::Witch:
        return "Du besitzt einen Heiltrank, der das Opfer der Werwölfe rettet, und einen Gifttrank, der einen "
               "Spieler tötet. Jeder Trank wirkt nur ein einziges Mal.";
    case Role::Hunter:
        return "Wenn du stirbst, reißt du mit deinem letzten Schuss einen Spieler deiner Wahl mit in den Tod.";
    case Role::Cupid:
        return "In der ersten Nacht verliebst du zwei Spieler ineinander. Stirbt einer, stirbt auch der andere "
               "vor Kummer.";
    case Role::Protector:
        return "Jede Nacht beschützt du einen Spieler vor den Werwölfen – aber nie zweimal hintereinander "
               "denselben.";
    default:
        return "";
    }
}

const char* roleIcon(Role role) {
    switch (role) {
    case Role::Villager: return "⌂";
    case Role::Werewolf: return "☾";
    case Role::Seer: return "◉";
    case Role::Witch: return "⚗";
    case Role::Hunter: return "⌖";
    case Role::Cupid: return "♥";
    case Role::Protector: return "⛨";
    default: return "?";
    }
}

Team roleTeam(Role role) { return role == Role::Werewolf ? Team::Wolves : Team::Village; }

const char* teamName(Team team) {
    switch (team) {
    case Team::Village: return "Dorf";
    case Team::Wolves: return "Werwölfe";
    case Team::Lovers: return "Verliebte";
    case Team::Nobody: return "Niemand";
    default: return "";
    }
}

const char* phaseTitle(Phase phase) {
    switch (phase) {
    case Phase::Lobby: return "Lobby";
    case Phase::Night: return "Nacht";
    case Phase::Witch: return "Nacht – Die Hexe";
    case Phase::Morning: return "Morgengrauen";
    case Phase::Hunter: return "Der letzte Schuss";
    case Phase::Day: return "Tag";
    case Phase::Vote: return "Abstimmung";
    case Phase::Execution: return "Urteil";
    case Phase::GameOver: return "Spielende";
    }
    return "";
}

const char* channelName(ChatChannel channel) {
    switch (channel) {
    case ChatChannel::Public: return "Alle";
    case ChatChannel::Wolves: return "Rudel";
    case ChatChannel::Dead: return "Geister";
    }
    return "";
}

// --- Einstellungen -----------------------------------------------------------

void Settings::sanitize() {
    wolves = std::min<uint8_t>(wolves, 6);
    nightSeconds = std::clamp<uint16_t>(nightSeconds, 15, 180);
    discussionSeconds = std::clamp<uint16_t>(discussionSeconds, 20, 600);
    voteSeconds = std::clamp<uint16_t>(voteSeconds, 15, 180);
}

void Settings::write(eng::ByteWriter& w) const {
    w.u8(wolves);
    uint8_t flags = 0;
    flags |= seer ? 1 : 0;
    flags |= witch ? 2 : 0;
    flags |= hunter ? 4 : 0;
    flags |= cupid ? 8 : 0;
    flags |= protector ? 16 : 0;
    flags |= revealRoles ? 32 : 0;
    w.u8(flags);
    w.u16(nightSeconds);
    w.u16(discussionSeconds);
    w.u16(voteSeconds);
}

bool Settings::read(eng::ByteReader& r) {
    wolves = r.u8();
    const uint8_t flags = r.u8();
    seer = flags & 1;
    witch = flags & 2;
    hunter = flags & 4;
    cupid = flags & 8;
    protector = flags & 16;
    revealRoles = flags & 32;
    nightSeconds = r.u16();
    discussionSeconds = r.u16();
    voteSeconds = r.u16();
    return r.ok();
}

int autoWolfCount(int players) {
    if (players <= 6) return 1;
    if (players <= 10) return 2;
    if (players <= 14) return 3;
    return 4;
}

std::vector<Role> buildRoleList(const Settings& settings, int players, std::string* error) {
    auto fail = [&](std::string msg) {
        if (error) *error = std::move(msg);
        return std::vector<Role>{};
    };
    if (players < kMinPlayers)
        return fail("Es werden mindestens " + std::to_string(kMinPlayers) + " Spieler benötigt.");
    if (players > kMaxPlayers) return fail("Es dürfen höchstens " + std::to_string(kMaxPlayers) + " Spieler sein.");

    const int wolves = settings.wolves ? settings.wolves : autoWolfCount(players);
    if (wolves * 2 >= players)
        return fail("Zu viele Werwölfe für " + std::to_string(players) + " Spieler – es müssen weniger als die "
                    "Hälfte sein.");

    std::vector<Role> roles(static_cast<size_t>(wolves), Role::Werewolf);
    if (settings.seer) roles.push_back(Role::Seer);
    if (settings.witch) roles.push_back(Role::Witch);
    if (settings.hunter) roles.push_back(Role::Hunter);
    if (settings.cupid) roles.push_back(Role::Cupid);
    if (settings.protector) roles.push_back(Role::Protector);
    if (static_cast<int>(roles.size()) > players)
        return fail("Zu viele Sonderrollen für " + std::to_string(players) + " Spieler.");
    while (static_cast<int>(roles.size()) < players) roles.push_back(Role::Villager);
    return roles;
}

// --- Spielansicht ------------------------------------------------------------

const PlayerView* GameView::player(uint8_t id) const {
    for (const PlayerView& p : players)
        if (p.id == id) return &p;
    return nullptr;
}

void GameView::write(eng::ByteWriter& w) const {
    w.u8(static_cast<uint8_t>(phase));
    w.u16(day);
    w.f32(timeLeft);
    w.f32(phaseDuration);
    w.u8(you);
    w.u8(host);
    w.u8(static_cast<uint8_t>(yourRole));
    settings.write(w);

    w.u8(static_cast<uint8_t>(players.size()));
    for (const PlayerView& p : players) {
        w.u8(p.id);
        w.str(p.name);
        uint8_t flags = 0;
        flags |= p.alive ? 1 : 0;
        flags |= p.connected ? 2 : 0;
        flags |= p.bot ? 4 : 0;
        flags |= p.lover ? 8 : 0;
        flags |= p.skip ? 16 : 0;
        flags |= p.winner ? 32 : 0;
        w.u8(flags);
        w.u8(static_cast<uint8_t>(p.role));
        w.u8(p.vote);
    }

    w.u8(static_cast<uint8_t>(action));
    w.u8(static_cast<uint8_t>(selection.size()));
    for (uint8_t id : selection) w.u8(id);
    w.boolean(confirmed);
    w.u8(loverPartner);
    w.u8(static_cast<uint8_t>(seerResults.size()));
    for (const SeerResult& s : seerResults) {
        w.u8(s.player);
        w.u8(static_cast<uint8_t>(s.role));
    }
    w.u8(lastProtected);
    w.u8(witchVictim);
    uint8_t witchFlags = 0;
    witchFlags |= healAvailable ? 1 : 0;
    witchFlags |= poisonAvailable ? 2 : 0;
    witchFlags |= healUsed ? 4 : 0;
    w.u8(witchFlags);
    w.u8(poisonTarget);
    w.u8(static_cast<uint8_t>(winner));
}

bool GameView::read(eng::ByteReader& r) {
    auto readRole = [&]() {
        const uint8_t v = r.u8();
        return v < static_cast<uint8_t>(Role::Count) ? static_cast<Role>(v) : Role::Unknown;
    };

    const uint8_t ph = r.u8();
    if (ph > static_cast<uint8_t>(Phase::GameOver)) return false;
    phase = static_cast<Phase>(ph);
    day = r.u16();
    timeLeft = r.f32();
    phaseDuration = r.f32();
    you = r.u8();
    host = r.u8();
    yourRole = readRole();
    if (!settings.read(r)) return false;

    const uint8_t count = r.u8();
    if (count > kMaxPlayers) return false;
    players.clear();
    for (uint8_t i = 0; i < count; ++i) {
        PlayerView p;
        p.id = r.u8();
        p.name = r.str(128);
        const uint8_t flags = r.u8();
        p.alive = flags & 1;
        p.connected = flags & 2;
        p.bot = flags & 4;
        p.lover = flags & 8;
        p.skip = flags & 16;
        p.winner = flags & 32;
        p.role = readRole();
        p.vote = r.u8();
        players.push_back(std::move(p));
    }

    const uint8_t act = r.u8();
    action = act <= static_cast<uint8_t>(ActionKind::DayVote) ? static_cast<ActionKind>(act) : ActionKind::None;
    const uint8_t selCount = r.u8();
    selection.clear();
    for (uint8_t i = 0; i < selCount && r.ok(); ++i) selection.push_back(r.u8());
    confirmed = r.boolean();
    loverPartner = r.u8();
    const uint8_t seerCount = r.u8();
    seerResults.clear();
    for (uint8_t i = 0; i < seerCount && r.ok(); ++i) {
        SeerResult s;
        s.player = r.u8();
        s.role = readRole();
        seerResults.push_back(s);
    }
    lastProtected = r.u8();
    witchVictim = r.u8();
    const uint8_t witchFlags = r.u8();
    healAvailable = witchFlags & 1;
    poisonAvailable = witchFlags & 2;
    healUsed = witchFlags & 4;
    poisonTarget = r.u8();
    const uint8_t win = r.u8();
    winner = win <= static_cast<uint8_t>(Team::Nobody) ? static_cast<Team>(win) : Team::None;
    return r.ok();
}

// --- Chatregeln --------------------------------------------------------------

std::optional<ChatChannel> speakChannel(Phase phase, bool alive, Role role) {
    if (phase == Phase::Lobby || phase == Phase::GameOver) return ChatChannel::Public;
    if (!alive) return ChatChannel::Dead;
    if (phase == Phase::Night || phase == Phase::Witch) {
        if (role == Role::Werewolf) return ChatChannel::Wolves;
        return std::nullopt; // das Dorf schläft
    }
    return ChatChannel::Public;
}

bool canReadChannel(ChatChannel channel, Phase phase, bool alive, Role role) {
    switch (channel) {
    case ChatChannel::Public: return true;
    case ChatChannel::Wolves: return role == Role::Werewolf || !alive || phase == Phase::GameOver;
    case ChatChannel::Dead: return !alive || phase == Phase::GameOver;
    }
    return false;
}

} // namespace ww
