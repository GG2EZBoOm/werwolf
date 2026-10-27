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
    case Role::Elder: return "Dorfältester";
    case Role::LittleGirl: return "Kleines Mädchen";
    case Role::Medium: return "Medium";
    case Role::Tracker: return "Fährtenleser";
    case Role::Blacksmith: return "Schmied";
    case Role::Priest: return "Pfarrer";
    case Role::Mayor: return "Bürgermeister";
    case Role::Idiot: return "Dorfdepp";
    case Role::Cursed: return "Verfluchter";
    case Role::AlphaWolf: return "Urwolf";
    case Role::WolfCub: return "Wolfsjunges";
    case Role::Jester: return "Narr";
    case Role::SerialKiller: return "Serienmörder";
    case Role::Arsonist: return "Brandstifter";
    case Role::Thief: return "Dieb";
    case Role::Scapegoat: return "Sündenbock";
    case Role::Fox: return "Fuchs";
    case Role::BearTamer: return "Bärenführer";
    case Role::Knight: return "Ritter";
    case Role::Servant: return "Ergebene Magd";
    case Role::Judge: return "Stotternder Richter";
    case Role::Actor: return "Schauspieler";
    case Role::WildChild: return "Wildes Kind";
    case Role::WolfHound: return "Wolfshund";
    case Role::Sister: return "Schwester";
    case Role::Brother: return "Bruder";
    case Role::HonestVillager: return "Dorfbewohner-Dorfbewohner";
    case Role::Raven: return "Rabe";
    case Role::BigBadWolf: return "Großer böser Wolf";
    case Role::WhiteWolf: return "Weißer Werwolf";
    case Role::Piper: return "Flötenspieler";
    case Role::Angel: return "Engel";
    case Role::Sectarian: return "Sektierer";
    default: return "Unbekannt";
    }
}

const char* roleDescription(Role role) {
    switch (role) {
    case Role::Villager:
        return "Du hast keine besonderen Fähigkeiten. Beobachte, sammle Hinweise und überführe die Werwölfe in der "
               "Versammlung.";
    case Role::Werewolf:
        return "Nachts verwandelst du dich (R) und reißt Opfer (Linksklick) – auch verriegelte Türen hältst du nicht "
               "lange aus. Lass dich nicht beobachten.";
    case Role::Seer:
        return "Einmal pro Nacht siehst du die wahre Rolle eines Spielers in deiner Nähe (anvisieren + E).";
    case Role::Witch:
        return "Dein Heiltrank holt ein frisches Opfer ins Leben zurück (E an der Leiche), dein Gifttrank tötet sofort "
               "(F). Jeder Trank wirkt nur einmal.";
    case Role::Hunter:
        return "Stirbst du, bleiben dir 15 Sekunden für einen letzten Schuss (anvisieren + E).";
    case Role::Cupid:
        return "In der ersten Nacht verliebst du zwei Spieler (anvisieren + E). Stirbt einer, stirbt auch der andere.";
    case Role::Protector:
        return "Jede Nacht beschützt du einen Spieler in deiner Nähe vor den Werwölfen (anvisieren + E) – nie "
               "zweimal hintereinander denselben.";
    case Role::Elder:
        return "Du bist zäh: Den ersten Angriff eines Werwolfs überlebst du.";
    case Role::LittleGirl:
        return "Nachts lauschst du heimlich: Du liest den Rudel-Chat der Werwölfe mit – nur ihre Namen erfährst du nicht.";
    case Role::Medium:
        return "Nachts sprichst du mit den Toten (Tab wechselt in den Geisterchat). Vielleicht wissen sie, wer sie "
               "ermordet hat.";
    case Role::Tracker:
        return "Einmal pro Nacht heftest du dich an die Fersen eines Spielers (anvisieren + E) und siehst bis zum "
               "Morgen, wohin er geht.";
    case Role::Blacksmith:
        return "Einmal am Tag verstärkst oder reparierst du eine Tür (E an der Tür). Verstärkte Türen halten "
               "Werwölfe doppelt so lange auf.";
    case Role::Priest:
        return "Einmal im Spiel benetzt du jemanden mit Weihwasser (anvisieren + E). Ist er ein Werwolf, verbrennt er.";
    case Role::Mayor:
        return "Alle wissen, wer du bist. Deine Stimme zählt bei der Abstimmung doppelt.";
    case Role::Idiot:
        return "Wirst du hingerichtet, erkennt das Dorf deine Harmlosigkeit und lässt dich laufen – danach darfst du "
               "aber nicht mehr abstimmen.";
    case Role::Cursed:
        return "Du gehörst zum Dorf – doch wenn dich ein Werwolf angreift, stirbst du nicht, sondern wirst selbst einer.";
    case Role::AlphaWolf:
        return "Du bist ein Werwolf. Einmal im Spiel kannst du dein Opfer infizieren statt es zu töten (F) – es wird "
               "zu einem von euch.";
    case Role::WolfCub:
        return "Du bist ein junger Werwolf. Stirbst du, ist das Rudel in der folgenden Nacht so rasend, dass es zwei "
               "Opfer reißen darf.";
    case Role::Jester:
        return "Du gehörst zu niemandem. Bring das Dorf dazu, dich hinzurichten – dann hast du gewonnen.";
    case Role::SerialKiller:
        return "Du kämpfst allein. Jede Nacht tötest du mit dem Messer (Linksklick), ohne dich zu verwandeln. Werwölfe "
               "können dir nichts anhaben. Gewinne als Letzter.";
    case Role::Arsonist:
        return "Du kämpfst allein. Nachts tränkst du Häuser mit Öl (E an der Tür) und zündest alle auf einmal an (F) "
               "– wer drinnen ist, verbrennt. Gewinne als Letzter.";
    case Role::Thief:
        return "In der ersten Nacht stiehlst du die Rolle eines Spielers in deiner Nähe (anvisieren + E). Er wird zum "
               "einfachen Dorfbewohner – stiehlst du einen Werwolf, gehörst du zum Rudel.";
    case Role::Scapegoat:
        return "Endet eine Abstimmung im Gleichstand, wirst du an Stelle der anderen hingerichtet.";
    case Role::Fox:
        return "Einmal pro Nacht wittert deine Nase (anvisieren + E), ob die Person oder jemand in ihrer Nähe ein "
               "Werwolf ist. Findest du keinen, verlierst du deinen Spürsinn.";
    case Role::BearTamer:
        return "Alle kennen dich und deinen Bär. Er brummt, wenn ein Werwolf neben dir sitzt oder dir zu nahe kommt.";
    case Role::Knight:
        return "Tötet dich ein Werwolf, bleibt deine rostige Klinge in ihm stecken – er stirbt in der folgenden Nacht.";
    case Role::Servant:
        return "Wird jemand hingerichtet, kannst du dich in den Sekunden des Urteils zu erkennen geben und seine Rolle "
               "übernehmen (E). Seine Rolle bleibt dann für alle ein Geheimnis.";
    case Role::Judge:
        return "Einmal im Spiel kannst du während einer Abstimmung heimlich eine zweite Abstimmung am selben Abend "
               "verlangen (F).";
    case Role::Actor:
        return "Jede Nacht spielst du eine andere Sonderrolle (Seherin, Beschützer, Fuchs oder Fährtenleser) – drei "
               "Nächte lang.";
    case Role::WildChild:
        return "In der ersten Nacht wählst du ein Vorbild (anvisieren + E). Stirbt es, wirst du zum Werwolf.";
    case Role::WolfHound:
        return "In der ersten Nacht entscheidest du dich: dem Dorf treu bleiben (E) oder zum Werwolf werden (F).";
    case Role::Sister:
        return "Du und deine Schwester kennt euch und könnt euch heimlich absprechen (Tab).";
    case Role::Brother:
        return "Du und deine zwei Brüder kennt euch und könnt euch heimlich absprechen (Tab).";
    case Role::HonestVillager:
        return "Alle wissen, dass du ein einfacher, ehrlicher Dorfbewohner bist.";
    case Role::Raven:
        return "Einmal pro Nacht belastest du jemanden (anvisieren + E): Bei der nächsten Abstimmung hat er zwei "
               "Stimmen gegen sich.";
    case Role::BigBadWolf:
        return "Du bist ein Werwolf. Solange noch kein Werwolf gestorben ist, darf das Rudel jede Nacht ein "
               "zusätzliches Opfer reißen.";
    case Role::WhiteWolf:
        return "Du jagst mit dem Rudel, gewinnst aber nur allein. Jede zweite Nacht darfst du einen Werwolf reißen.";
    case Role::Piper:
        return "Du spielst allein. Jede Nacht verzauberst du bis zu zwei Spieler in deiner Nähe (anvisieren + E). "
               "Sind alle Lebenden verzaubert, hast du gewonnen.";
    case Role::Angel:
        return "Du willst ins Paradies: Stirbst du in der ersten Nacht oder bei der ersten Hinrichtung, gewinnst du "
               "allein.";
    case Role::Sectarian:
        return "Das Dorf ist in zwei Hälften geteilt (Ost und West). Du gewinnst allein, wenn nur noch Bewohner deiner "
               "Hälfte leben.";
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
    case Role::Elder: return "⌛";
    case Role::LittleGirl: return "✿";
    case Role::Medium: return "☯";
    case Role::Tracker: return "➳";
    case Role::Blacksmith: return "⚒";
    case Role::Priest: return "✝";
    case Role::Mayor: return "⚑";
    case Role::Idiot: return "☺";
    case Role::Cursed: return "✵";
    case Role::AlphaWolf: return "✪";
    case Role::WolfCub: return "☽";
    case Role::Jester: return "♣";
    case Role::SerialKiller: return "⚔";
    case Role::Arsonist: return "♨";
    case Role::Thief: return "✂";
    case Role::Scapegoat: return "♈";
    case Role::Fox: return "✣";
    case Role::BearTamer: return "♉";
    case Role::Knight: return "♞";
    case Role::Servant: return "⚜";
    case Role::Judge: return "⚖";
    case Role::Actor: return "☻";
    case Role::WildChild: return "❀";
    case Role::WolfHound: return "☋";
    case Role::Sister: return "♀";
    case Role::Brother: return "♂";
    case Role::HonestVillager: return "☑";
    case Role::Raven: return "♠";
    case Role::BigBadWolf: return "☠";
    case Role::WhiteWolf: return "❄";
    case Role::Piper: return "♫";
    case Role::Angel: return "☼";
    case Role::Sectarian: return "☸";
    default: return "?";
    }
}

bool isWolfRole(Role role) {
    return role == Role::Werewolf || role == Role::AlphaWolf || role == Role::WolfCub || role == Role::BigBadWolf ||
           role == Role::WhiteWolf;
}

Faction roleFaction(Role role) {
    switch (role) {
    case Role::WhiteWolf:
    case Role::Piper:
    case Role::Angel:
    case Role::Sectarian:
    case Role::Jester:
    case Role::SerialKiller:
    case Role::Arsonist: return Faction::Neutral;
    default: return isWolfRole(role) ? Faction::Wolves : Faction::Village;
    }
}

Team roleTeam(Role role) {
    switch (role) {
    case Role::WhiteWolf: return Team::WhiteWolf;
    case Role::Jester: return Team::Jester;
    case Role::SerialKiller: return Team::Killer;
    case Role::Arsonist: return Team::Arsonist;
    case Role::Piper: return Team::Piper;
    case Role::Angel: return Team::Angel;
    case Role::Sectarian: return Team::Sectarian;
    default: return isWolfRole(role) ? Team::Wolves : Team::Village;
    }
}

bool isPublicRole(Role role) { return role == Role::Mayor || role == Role::HonestVillager || role == Role::BearTamer; }

int roleCardCount(Role role) {
    if (role == Role::Sister) return 2;
    if (role == Role::Brother) return 3;
    return 1;
}

bool isOptionalRole(Role role) {
    return role != Role::Unknown && role != Role::Villager && role != Role::Werewolf && role != Role::Count;
}

const char* teamName(Team team) {
    switch (team) {
    case Team::Village: return "Dorf";
    case Team::Wolves: return "Werwölfe";
    case Team::Lovers: return "Verliebte";
    case Team::Nobody: return "Niemand";
    case Team::Jester: return "Narr (allein)";
    case Team::Killer: return "Serienmörder (allein)";
    case Team::Arsonist: return "Brandstifter (allein)";
    case Team::Piper: return "Flötenspieler (allein)";
    case Team::Angel: return "Engel (allein)";
    case Team::WhiteWolf: return "Weißer Werwolf (allein)";
    case Team::Sectarian: return "Sektierer (allein)";
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
    case ChatChannel::Family: return "Geschwister";
    }
    return "";
}

// --- Einstellungen -----------------------------------------------------------

Settings::Settings() {
    for (Role r : {Role::Seer, Role::Witch, Role::Hunter, Role::Elder, Role::Blacksmith}) set(r, true);
}

void Settings::set(Role role, bool enabled) {
    const uint64_t bit = uint64_t{1} << static_cast<uint64_t>(role);
    roles = enabled ? (roles | bit) : (roles & ~bit);
}

void Settings::sanitize() {
    wolves = std::min<uint8_t>(wolves, 6);
    uint64_t allowed = 0;
    for (int r = 0; r < static_cast<int>(Role::Count); ++r)
        if (isOptionalRole(static_cast<Role>(r))) allowed |= uint64_t{1} << r;
    roles &= allowed;
    daySeconds = std::clamp<uint16_t>(daySeconds, 30, 900);
    discussionSeconds = std::clamp<uint16_t>(discussionSeconds, 20, 600);
    voteSeconds = std::clamp<uint16_t>(voteSeconds, 15, 180);
    nightSeconds = std::clamp<uint16_t>(nightSeconds, 30, 600);
}

void Settings::write(eng::ByteWriter& w) const {
    w.u8(wolves);
    w.u64(roles);
    w.u8(static_cast<uint8_t>((revealRoles ? 1 : 0) | (captain ? 2 : 0)));
    w.u16(daySeconds);
    w.u16(discussionSeconds);
    w.u16(voteSeconds);
    w.u16(nightSeconds);
}

bool Settings::read(eng::ByteReader& r) {
    wolves = r.u8();
    roles = r.u64();
    const uint8_t flags = r.u8();
    revealRoles = flags & 1;
    captain = flags & 2;
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

    // Besondere Wölfe ersetzen normale Werwölfe
    std::vector<Role> roles;
    std::vector<Role> wolfVariants;
    for (Role r : {Role::AlphaWolf, Role::WolfCub, Role::BigBadWolf, Role::WhiteWolf})
        if (settings.has(r)) wolfVariants.push_back(r);
    for (int i = 0; i < wolves; ++i)
        roles.push_back(static_cast<size_t>(i) < wolfVariants.size() ? wolfVariants[static_cast<size_t>(i)] : Role::Werewolf);

    // Weitere Sonderrollen in fester Reihenfolge, solange Plätze frei sind
    for (int r = 0; r < static_cast<int>(Role::Count) && static_cast<int>(roles.size()) < players; ++r) {
        const Role role = static_cast<Role>(r);
        if (!isOptionalRole(role) || isWolfRole(role) || !settings.has(role)) continue;
        // Geschwister gibt es nur vollständig
        const int cards = roleCardCount(role);
        if (static_cast<int>(roles.size()) + cards > players) continue;
        for (int c = 0; c < cards; ++c) roles.push_back(role);
    }
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
        flags |= p.enchanted ? 32 : 0;
        flags |= p.captain ? 64 : 0;
        flags |= p.marked ? 128 : 0;
        w.u8(flags);
        w.u8(static_cast<uint8_t>(p.role));
        w.u8(p.vote);
        w.u8(p.house);
    }

    w.u32(abilities);
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
    uint8_t bits = 0;
    bits |= healAvailable ? 1 : 0;
    bits |= poisonAvailable ? 2 : 0;
    bits |= shieldLeft ? 4 : 0;
    w.u8(bits);
    w.u8(packKillsLeft);
    w.f32(hunterTimeLeft);
    w.u8(tracked);
    w.u32(doused);
    w.boolean(election);
    w.u8(static_cast<uint8_t>(acting));
    w.u8(model);
    w.u8(condemned);
    w.f32(successorTimeLeft);
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
        p.enchanted = flags & 32;
        p.captain = flags & 64;
        p.marked = flags & 128;
        p.role = readRole();
        p.vote = r.u8();
        p.house = r.u8();
        players.push_back(std::move(p));
    }

    abilities = r.u32();
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
    const uint8_t bits = r.u8();
    healAvailable = bits & 1;
    poisonAvailable = bits & 2;
    shieldLeft = bits & 4;
    packKillsLeft = r.u8();
    hunterTimeLeft = r.f32();
    tracked = r.u8();
    doused = r.u32();
    election = r.boolean();
    acting = readRole();
    model = r.u8();
    condemned = r.u8();
    successorTimeLeft = r.f32();
    const uint8_t win = r.u8();
    winner = win <= static_cast<uint8_t>(Team::Sectarian) ? static_cast<Team>(win) : Team::None;
    return r.ok();
}

// --- Positionen und Türen -------------------------------------------------------

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

void writeDoors(eng::ByteWriter& w, const std::vector<DoorState>& doors) {
    w.u8(static_cast<uint8_t>(std::min<size_t>(doors.size(), 32)));
    for (size_t i = 0; i < doors.size() && i < 32; ++i) {
        w.u8(doors[i].flags);
        w.u8(doors[i].health);
    }
}

bool readDoors(eng::ByteReader& r, std::vector<DoorState>& doors) {
    doors.clear();
    const uint8_t count = r.u8();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        DoorState d;
        d.flags = r.u8();
        d.health = r.u8();
        doors.push_back(d);
    }
    return r.ok();
}

uint32_t closedDoorMask(const std::vector<DoorState>& doors) {
    uint32_t mask = 0;
    for (size_t i = 0; i < doors.size() && i < 32; ++i)
        if (!(doors[i].flags & (DoorOpen | DoorBroken | DoorBurnt))) mask |= 1u << i;
    return mask;
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
