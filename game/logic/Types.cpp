#include "logic/Types.h"

#include <algorithm>
#include <cmath>

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
        return "Du hast keine besonderen Fähigkeiten. Beobachte, sammle Hinweise und überführe die Werwölfe in der "
               "Versammlung.";
    case Role::Werewolf:
        return "Nachts verwandelst du dich (R) und reißt Opfer (Linksklick). Lass dich nicht dabei beobachten – "
               "tagsüber musst du unerkannt bleiben.";
    case Role::Seer:
        return "Einmal pro Nacht siehst du die wahre Rolle eines Spielers in deiner Nähe (anvisieren + E).";
    case Role::Witch:
        return "Dein Heiltrank holt ein frisches Opfer ins Leben zurück, dein Gifttrank tötet sofort (jeweils E). "
               "Jeder Trank wirkt nur einmal.";
    case Role::Hunter:
        return "Stirbst du, bleiben dir 15 Sekunden für einen letzten Schuss (anvisieren + E).";
    case Role::Cupid:
        return "In der ersten Nacht verliebst du zwei Spieler (anvisieren + E). Stirbt einer, stirbt auch der andere.";
    case Role::Protector:
        return "Jede Nacht beschützt du einen Spieler in deiner Nähe vor den Werwölfen (anvisieren + E) – nie "
               "zweimal hintereinander denselben.";
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
    case Phase::Day: return "Tag";
    case Phase::Assembly: return "Versammlung";
    case Phase::Vote: return "Abstimmung";
    case Phase::Execution: return "Urteil";
    case Phase::Night: return "Nacht";
    case Phase::Morning: return "Morgengrauen";
    case Phase::GameOver: return "Spielende";
    }
    return "";
}

const char* channelName(ChatChannel channel) {
    switch (channel) {
    case ChatChannel::Global: return "Alle";
    case ChatChannel::Nearby: return "Nähe";
    case ChatChannel::Wolves: return "Rudel";
    case ChatChannel::Dead: return "Geister";
    }
    return "";
}

// --- Einstellungen -----------------------------------------------------------

void Settings::sanitize() {
    wolves = std::min<uint8_t>(wolves, 6);
    daySeconds = std::clamp<uint16_t>(daySeconds, 30, 900);
    discussionSeconds = std::clamp<uint16_t>(discussionSeconds, 20, 600);
    voteSeconds = std::clamp<uint16_t>(voteSeconds, 15, 180);
    nightSeconds = std::clamp<uint16_t>(nightSeconds, 30, 600);
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
    w.u16(daySeconds);
    w.u16(discussionSeconds);
    w.u16(voteSeconds);
    w.u16(nightSeconds);
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
    daySeconds = r.u16();
    discussionSeconds = r.u16();
    voteSeconds = r.u16();
    nightSeconds = r.u16();
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
    w.u32(worldSeed);
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
        flags |= p.winner ? 16 : 0;
        w.u8(flags);
        w.u8(static_cast<uint8_t>(p.role));
        w.u8(p.vote);
    }

    w.u16(abilities);
    w.u8(vote);
    w.u8(loverPartner);
    w.u8(static_cast<uint8_t>(seerResults.size()));
    for (const SeerResult& s : seerResults) {
        w.u8(s.player);
        w.u8(static_cast<uint8_t>(s.role));
    }
    w.u8(static_cast<uint8_t>(cupidPicks.size()));
    for (uint8_t id : cupidPicks) w.u8(id);
    w.u8(protectedTarget);
    w.u8(lastProtected);
    uint8_t potions = 0;
    potions |= healAvailable ? 1 : 0;
    potions |= poisonAvailable ? 2 : 0;
    w.u8(potions);
    w.u8(packKillsLeft);
    w.f32(hunterTimeLeft);
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
    worldSeed = r.u32();
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
        p.winner = flags & 16;
        p.role = readRole();
        p.vote = r.u8();
        players.push_back(std::move(p));
    }

    abilities = r.u16();
    vote = r.u8();
    loverPartner = r.u8();
    const uint8_t seerCount = r.u8();
    seerResults.clear();
    for (uint8_t i = 0; i < seerCount && r.ok(); ++i) {
        SeerResult s;
        s.player = r.u8();
        s.role = readRole();
        seerResults.push_back(s);
    }
    const uint8_t pickCount = r.u8();
    cupidPicks.clear();
    for (uint8_t i = 0; i < pickCount && r.ok(); ++i) cupidPicks.push_back(r.u8());
    protectedTarget = r.u8();
    lastProtected = r.u8();
    const uint8_t potions = r.u8();
    healAvailable = potions & 1;
    poisonAvailable = potions & 2;
    packKillsLeft = r.u8();
    hunterTimeLeft = r.f32();
    const uint8_t win = r.u8();
    winner = win <= static_cast<uint8_t>(Team::Nobody) ? static_cast<Team>(win) : Team::None;
    return r.ok();
}

// --- Positionen ----------------------------------------------------------------

void writeEntities(eng::ByteWriter& w, const std::vector<EntityState>& entities) {
    w.u8(static_cast<uint8_t>(std::min<size_t>(entities.size(), 64)));
    size_t n = 0;
    for (const EntityState& e : entities) {
        if (n++ >= 64) break;
        w.u8(e.id);
        w.u8(e.kind);
        w.f32(e.x);
        w.f32(e.z);
        const float yaw = std::fmod(e.yaw, 2.0f * 3.14159265f);
        w.u16(static_cast<uint16_t>((yaw < 0 ? yaw + 2.0f * 3.14159265f : yaw) / (2.0f * 3.14159265f) * 65535.0f));
        w.u8(e.flags);
        w.u8(static_cast<uint8_t>(std::clamp(e.transform, 0.0f, 1.0f) * 255.0f));
    }
}

bool readEntities(eng::ByteReader& r, std::vector<EntityState>& entities) {
    entities.clear();
    const uint8_t count = r.u8();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        EntityState e;
        e.id = r.u8();
        e.kind = r.u8();
        e.x = r.f32();
        e.z = r.f32();
        e.yaw = static_cast<float>(r.u16()) / 65535.0f * 2.0f * 3.14159265f;
        e.flags = r.u8();
        e.transform = static_cast<float>(r.u8()) / 255.0f;
        entities.push_back(e);
    }
    return r.ok();
}

bool isGatheringPhase(Phase phase) {
    return phase == Phase::Lobby || phase == Phase::Assembly || phase == Phase::Vote || phase == Phase::Execution ||
           phase == Phase::GameOver;
}

float timeOfDay(Phase phase, float timeLeft, float phaseDuration) {
    const float p = phaseDuration > 0.0f ? std::clamp(1.0f - timeLeft / phaseDuration, 0.0f, 1.0f) : 0.0f;
    switch (phase) {
    case Phase::Day: return 0.28f + p * 0.42f;
    case Phase::Assembly: return 0.70f + p * 0.04f;
    case Phase::Vote: return 0.74f + p * 0.02f;
    case Phase::Execution: return 0.76f + p * 0.025f;
    case Phase::Night: return std::fmod(0.79f + p * 0.41f, 1.0f);
    case Phase::Morning: return 0.2f + p * 0.08f;
    case Phase::GameOver: return 0.73f;
    default: return 0.9f;
    }
}

} // namespace ww
