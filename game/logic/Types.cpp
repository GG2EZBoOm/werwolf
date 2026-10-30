#include "logic/Types.h"

#include <algorithm>
#include <cmath>

namespace ww {

namespace {
// Wählt den deutschen oder englischen Text
struct Pick {
    Lang lang;
    const char* operator()(const char* de, const char* en) const { return lang == Lang::English ? en : de; }
};
} // namespace

const char* roleName(Role role, Lang lang) {
    const Pick t{lang};
    switch (role) {
    case Role::Villager: return t("Dorfbewohner", "Villager");
    case Role::Werewolf: return t("Werwolf", "Werewolf");
    case Role::Seer: return t("Seherin", "Seer");
    case Role::Witch: return t("Hexe", "Witch");
    case Role::Hunter: return t("Jäger", "Hunter");
    case Role::Cupid: return t("Amor", "Cupid");
    case Role::Protector: return t("Beschützer", "Protector");
    case Role::Elder: return t("Dorfältester", "Elder");
    case Role::LittleGirl: return t("Kleines Mädchen", "Little Girl");
    case Role::Medium: return t("Medium", "Medium");
    case Role::Tracker: return t("Fährtenleser", "Tracker");
    case Role::Blacksmith: return t("Schmied", "Blacksmith");
    case Role::Priest: return t("Pfarrer", "Priest");
    case Role::Mayor: return t("Bürgermeister", "Mayor");
    case Role::Idiot: return t("Dorfdepp", "Village Idiot");
    case Role::Cursed: return t("Verfluchter", "Cursed");
    case Role::AlphaWolf: return t("Urwolf", "Alpha Wolf");
    case Role::WolfCub: return t("Wolfsjunges", "Wolf Cub");
    case Role::Jester: return t("Narr", "Jester");
    case Role::SerialKiller: return t("Serienmörder", "Serial Killer");
    case Role::Vampire: return t("Vampir", "Vampire");
    case Role::Arsonist: return t("Brandstifter", "Arsonist");
    case Role::Thief: return t("Dieb", "Thief");
    case Role::Scapegoat: return t("Sündenbock", "Scapegoat");
    case Role::Fox: return t("Fuchs", "Fox");
    case Role::BearTamer: return t("Bärenführer", "Bear Tamer");
    case Role::Knight: return t("Ritter", "Knight");
    case Role::Servant: return t("Ergebene Magd", "Devoted Servant");
    case Role::Judge: return t("Stotternder Richter", "Stuttering Judge");
    case Role::Actor: return t("Schauspieler", "Actor");
    case Role::WildChild: return t("Wildes Kind", "Wild Child");
    case Role::WolfHound: return t("Wolfshund", "Wolf Hound");
    case Role::Sister: return t("Schwester", "Sister");
    case Role::Brother: return t("Bruder", "Brother");
    case Role::HonestVillager: return t("Dorfbewohner-Dorfbewohner", "Villager-Villager");
    case Role::Raven: return t("Rabe", "Raven");
    case Role::BigBadWolf: return t("Großer böser Wolf", "Big Bad Wolf");
    case Role::WhiteWolf: return t("Weißer Werwolf", "White Werewolf");
    case Role::Piper: return t("Flötenspieler", "Pied Piper");
    case Role::Angel: return t("Engel", "Angel");
    case Role::Sectarian: return t("Sektierer", "Sectarian");
    default: return t("Unbekannt", "Unknown");
    }
}

Text roleText(Role role) { return {roleName(role, Lang::German), roleName(role, Lang::English)}; }

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
    case Role::Vampire: return {"ein Vampir", "a vampire"};
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

const char* roleDescription(Role role, Lang lang) {
    const Pick t{lang};
    switch (role) {
    case Role::Villager:
        return t("Du hast keine besonderen Fähigkeiten. Beobachte, sammle Hinweise und überführe die Werwölfe in der "
                 "Versammlung.",
                 "You have no special abilities. Watch, gather clues and expose the werewolves at the gathering.");
    case Role::Werewolf:
        return t("Nachts verwandelst du dich (R) und reißt Opfer (Linksklick) – auch verriegelte Türen hältst du nicht "
                 "lange aus. Lass dich nicht beobachten.",
                 "At night you transform (R) and maul your victims (left click) – not even bolted doors keep you out "
                 "for long. Don't let anyone see you.");
    case Role::Seer:
        return t("Einmal pro Nacht siehst du die wahre Rolle eines Spielers in deiner Nähe (anvisieren + E).",
                 "Once per night you see the true role of a player near you (aim + E).");
    case Role::Witch:
        return t("Dein Heiltrank holt ein frisches Opfer ins Leben zurück (E an der Leiche), dein Gifttrank tötet sofort "
                 "(F). Jeder Trank wirkt nur einmal.",
                 "Your healing potion brings a fresh victim back to life (E at the body), your poison kills instantly "
                 "(F). Each potion works only once.");
    case Role::Hunter:
        return t("Stirbst du, bleiben dir 15 Sekunden für einen letzten Schuss (anvisieren + E).",
                 "When you die, you have 15 seconds for one last shot (aim + E).");
    case Role::Cupid:
        return t("In der ersten Nacht verliebst du zwei Spieler (anvisieren + E). Stirbt einer, stirbt auch der andere.",
                 "On the first night you make two players fall in love (aim + E). If one dies, so does the other.");
    case Role::Protector:
        return t("Jede Nacht beschützt du einen Spieler in deiner Nähe vor den Werwölfen (anvisieren + E) – nie "
                 "zweimal hintereinander denselben.",
                 "Every night you protect a player near you from the werewolves (aim + E) – never the same one twice "
                 "in a row.");
    case Role::Elder:
        return t("Du bist zäh: Den ersten Angriff eines Werwolfs überlebst du.",
                 "You are tough: you survive the first werewolf attack.");
    case Role::LittleGirl:
        return t("Nachts lauschst du heimlich: Du liest den Rudel-Chat der Werwölfe mit – nur ihre Namen erfährst du nicht.",
                 "At night you eavesdrop: you can read the werewolves' pack chat – only their names stay hidden.");
    case Role::Medium:
        return t("Nachts sprichst du mit den Toten (Tab wechselt in den Geisterchat). Vielleicht wissen sie, wer sie "
                 "ermordet hat.",
                 "At night you talk to the dead (Tab switches to the ghost chat). Maybe they know who murdered them.");
    case Role::Tracker:
        return t("Einmal pro Nacht heftest du dich an die Fersen eines Spielers (anvisieren + E) und siehst bis zum "
                 "Morgen, wohin er geht.",
                 "Once per night you follow a player's trail (aim + E) and see where they go until morning.");
    case Role::Blacksmith:
        return t("Einmal am Tag verstärkst oder reparierst du eine Tür (E an der Tür). Verstärkte Türen halten "
                 "Werwölfe doppelt so lange auf.",
                 "Once a day you reinforce or repair a door (E at the door). Reinforced doors hold werewolves back "
                 "twice as long.");
    case Role::Priest:
        return t("Einmal im Spiel benetzt du jemanden mit Weihwasser (anvisieren + E). Ist er ein Werwolf, verbrennt er.",
                 "Once per game you sprinkle someone with holy water (aim + E). If they are a werewolf, they burn.");
    case Role::Mayor:
        return t("Alle wissen, wer du bist. Deine Stimme zählt bei der Abstimmung doppelt.",
                 "Everyone knows who you are. Your vote counts double.");
    case Role::Idiot:
        return t("Wirst du hingerichtet, erkennt das Dorf deine Harmlosigkeit und lässt dich laufen – danach darfst du "
                 "aber nicht mehr abstimmen.",
                 "If you are executed, the village sees how harmless you are and lets you go – but afterwards you may "
                 "no longer vote.");
    case Role::Cursed:
        return t("Du gehörst zum Dorf – doch wenn dich ein Werwolf angreift, stirbst du nicht, sondern wirst selbst einer.",
                 "You belong to the village – but if a werewolf attacks you, you don't die, you become one yourself.");
    case Role::AlphaWolf:
        return t("Du bist ein Werwolf. Einmal im Spiel kannst du dein Opfer infizieren statt es zu töten (F) – es wird "
                 "zu einem von euch.",
                 "You are a werewolf. Once per game you can infect your victim instead of killing them (F) – they "
                 "become one of you.");
    case Role::WolfCub:
        return t("Du bist ein junger Werwolf. Stirbst du, ist das Rudel in der folgenden Nacht so rasend, dass es zwei "
                 "Opfer reißen darf.",
                 "You are a young werewolf. If you die, the pack is so enraged the following night that it may take "
                 "two victims.");
    case Role::Jester:
        return t("Du gehörst zu niemandem. Bring das Dorf dazu, dich hinzurichten – dann hast du gewonnen.",
                 "You belong to no one. Get the village to execute you – then you have won.");
    case Role::SerialKiller:
        return t("Du kämpfst allein. Jede Nacht tötest du mit dem Messer (Linksklick), ohne dich zu verwandeln. Werwölfe "
                 "können dir nichts anhaben. Gewinne als Letzter.",
                 "You fight alone. Every night you kill with your knife (left click) without transforming. Werewolves "
                 "cannot harm you. Be the last one standing.");
    case Role::Vampire:
        return t("Du gehörst zum Clan der Vampire und kennst deine Gefährten. Nachts nimmst du deine Vampirgestalt an "
                 "(R): Dann glühen deine Augen rot, niemand sieht deinen Namen, und euer Clan beißt lautlos ein Opfer "
                 "(Linksklick) – man findet es blutleer. Durch geschlossene Türen kommt ihr nicht. Werwölfe sind eure "
                 "Feinde: Ihr gewinnt, wenn kein Werwolf mehr lebt und ihr mindestens so viele seid wie alle anderen.",
                 "You belong to the vampire clan and know your companions. At night you take on your vampire form (R): "
                 "your eyes glow red, nobody sees your name, and your clan silently bites one victim (left click) – it is "
                 "found drained of blood. You cannot pass closed doors. Werewolves are your enemies: you win once no "
                 "werewolf is left and you are at least as many as everyone else.");
    case Role::Arsonist:
        return t("Du kämpfst allein. Nachts tränkst du Häuser mit Öl (E an der Tür) und zündest alle auf einmal an (F) "
                 "– wer drinnen ist, verbrennt. Gewinne als Letzter.",
                 "You fight alone. At night you douse houses with oil (E at the door) and set them all ablaze at once "
                 "(F) – whoever is inside burns. Be the last one standing.");
    case Role::Thief:
        return t("In der ersten Nacht stiehlst du die Rolle eines Spielers in deiner Nähe (anvisieren + E). Er wird zum "
                 "einfachen Dorfbewohner – stiehlst du einen Werwolf, gehörst du zum Rudel.",
                 "On the first night you steal the role of a player near you (aim + E). They become a plain villager "
                 "– steal from a werewolf and you join the pack.");
    case Role::Scapegoat:
        return t("Endet eine Abstimmung im Gleichstand, wirst du an Stelle der anderen hingerichtet.",
                 "If a vote ends in a tie, you are executed in place of the others.");
    case Role::Fox:
        return t("Einmal pro Nacht wittert deine Nase (anvisieren + E), ob die Person oder jemand in ihrer Nähe ein "
                 "Werwolf ist. Findest du keinen, verlierst du deinen Spürsinn.",
                 "Once per night your nose sniffs out (aim + E) whether that person or someone near them is a "
                 "werewolf. If you find none, you lose your scent.");
    case Role::BearTamer:
        return t("Alle kennen dich und deinen Bär. Er brummt, wenn ein Werwolf neben dir sitzt oder dir zu nahe kommt.",
                 "Everyone knows you and your bear. It growls when a werewolf sits next to you or comes too close.");
    case Role::Knight:
        return t("Tötet dich ein Werwolf, bleibt deine rostige Klinge in ihm stecken – er stirbt in der folgenden Nacht.",
                 "If a werewolf kills you, your rusty blade stays stuck in it – it dies the following night.");
    case Role::Servant:
        return t("Wird jemand hingerichtet, kannst du dich in den Sekunden des Urteils zu erkennen geben und seine Rolle "
                 "übernehmen (E). Seine Rolle bleibt dann für alle ein Geheimnis.",
                 "When someone is executed, you can reveal yourself during the verdict and take over their role (E). "
                 "Their role then stays a secret to everyone.");
    case Role::Judge:
        return t("Einmal im Spiel kannst du während einer Abstimmung heimlich eine zweite Abstimmung am selben Abend "
                 "verlangen (F).",
                 "Once per game you can secretly demand a second vote on the same evening during a vote (F).");
    case Role::Actor:
        return t("Jede Nacht spielst du eine andere Sonderrolle (Seherin, Beschützer, Fuchs oder Fährtenleser) – drei "
                 "Nächte lang.",
                 "Every night you play a different special role (Seer, Protector, Fox or Tracker) – for three nights.");
    case Role::WildChild:
        return t("In der ersten Nacht wählst du ein Vorbild (anvisieren + E). Stirbt es, wirst du zum Werwolf.",
                 "On the first night you choose a role model (aim + E). If they die, you become a werewolf.");
    case Role::WolfHound:
        return t("In der ersten Nacht entscheidest du dich: dem Dorf treu bleiben (E) oder zum Werwolf werden (F).",
                 "On the first night you decide: stay loyal to the village (E) or become a werewolf (F).");
    case Role::Sister:
        return t("Du und deine Schwester kennt euch und könnt euch heimlich absprechen (Tab).",
                 "You and your sister know each other and can talk in secret (Tab).");
    case Role::Brother:
        return t("Du und deine zwei Brüder kennt euch und könnt euch heimlich absprechen (Tab).",
                 "You and your two brothers know each other and can talk in secret (Tab).");
    case Role::HonestVillager:
        return t("Alle wissen, dass du ein einfacher, ehrlicher Dorfbewohner bist.",
                 "Everyone knows you are a simple, honest villager.");
    case Role::Raven:
        return t("Einmal pro Nacht belastest du jemanden (anvisieren + E): Bei der nächsten Abstimmung hat er zwei "
                 "Stimmen gegen sich.",
                 "Once per night you accuse someone (aim + E): at the next vote they start with two votes against them.");
    case Role::BigBadWolf:
        return t("Du bist ein Werwolf. Solange noch kein Werwolf gestorben ist, darf das Rudel jede Nacht ein "
                 "zusätzliches Opfer reißen.",
                 "You are a werewolf. As long as no werewolf has died, the pack may take one extra victim every night.");
    case Role::WhiteWolf:
        return t("Du jagst mit dem Rudel, gewinnst aber nur allein. Jede zweite Nacht darfst du einen Werwolf reißen.",
                 "You hunt with the pack but only win alone. Every second night you may kill a werewolf.");
    case Role::Piper:
        return t("Du spielst allein. Jede Nacht verzauberst du bis zu zwei Spieler in deiner Nähe (anvisieren + E). "
                 "Sind alle Lebenden verzaubert, hast du gewonnen.",
                 "You play alone. Every night you enchant up to two players near you (aim + E). Once all the living "
                 "are enchanted, you have won.");
    case Role::Angel:
        return t("Du willst ins Paradies: Stirbst du in der ersten Nacht oder bei der ersten Hinrichtung, gewinnst du "
                 "allein.",
                 "You long for paradise: if you die on the first night or at the first execution, you win alone.");
    case Role::Sectarian:
        return t("Das Dorf ist in zwei Hälften geteilt (Ost und West). Du gewinnst allein, wenn nur noch Bewohner deiner "
                 "Hälfte leben.",
                 "The village is split into two halves (east and west). You win alone once only residents of your "
                 "half are left alive.");
    default:
        return "";
    }
}

Text roleDescriptionText(Role role) { return {roleDescription(role, Lang::German), roleDescription(role, Lang::English)}; }

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
    case Role::Vampire: return "⚰";
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

bool isVampireRole(Role role) { return role == Role::Vampire; }

bool isEvilRole(Role role) { return isWolfRole(role) || isVampireRole(role); }

Faction roleFaction(Role role) {
    switch (role) {
    case Role::WhiteWolf:
    case Role::Piper:
    case Role::Angel:
    case Role::Sectarian:
    case Role::Jester:
    case Role::SerialKiller:
    case Role::Arsonist:
    case Role::Vampire: return Faction::Neutral;
    default: return isWolfRole(role) ? Faction::Wolves : Faction::Village;
    }
}

Team roleTeam(Role role) {
    switch (role) {
    case Role::WhiteWolf: return Team::WhiteWolf;
    case Role::Jester: return Team::Jester;
    case Role::SerialKiller: return Team::Killer;
    case Role::Vampire: return Team::Vampires;
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

const char* teamName(Team team, Lang lang) {
    const Pick t{lang};
    switch (team) {
    case Team::Village: return t("Dorf", "Village");
    case Team::Wolves: return t("Werwölfe", "Werewolves");
    case Team::Lovers: return t("Verliebte", "Lovers");
    case Team::Nobody: return t("Niemand", "Nobody");
    case Team::Jester: return t("Narr (allein)", "Jester (alone)");
    case Team::Killer: return t("Serienmörder (allein)", "Serial Killer (alone)");
    case Team::Arsonist: return t("Brandstifter (allein)", "Arsonist (alone)");
    case Team::Piper: return t("Flötenspieler (allein)", "Pied Piper (alone)");
    case Team::Angel: return t("Engel (allein)", "Angel (alone)");
    case Team::WhiteWolf: return t("Weißer Werwolf (allein)", "White Werewolf (alone)");
    case Team::Sectarian: return t("Sektierer (allein)", "Sectarian (alone)");
    case Team::Vampires: return t("Vampire", "Vampires");
    default: return "";
    }
}

const char* phaseTitle(Phase phase, Lang lang) {
    const Pick t{lang};
    switch (phase) {
    case Phase::Lobby: return t("Lobby", "Lobby");
    case Phase::Day: return t("Tag", "Day");
    case Phase::Assembly: return t("Versammlung", "Gathering");
    case Phase::Vote: return t("Abstimmung", "Vote");
    case Phase::Execution: return t("Urteil", "Verdict");
    case Phase::Night: return t("Nacht", "Night");
    case Phase::Morning: return t("Morgengrauen", "Dawn");
    case Phase::GameOver: return t("Spielende", "Game over");
    }
    return "";
}

const char* channelName(ChatChannel channel, Lang lang) {
    const Pick t{lang};
    switch (channel) {
    case ChatChannel::Global: return t("Alle", "All");
    case ChatChannel::Nearby: return t("Nähe", "Nearby");
    case ChatChannel::Wolves: return t("Rudel", "Pack");
    case ChatChannel::Dead: return t("Geister", "Ghosts");
    case ChatChannel::Family: return t("Geschwister", "Siblings");
    case ChatChannel::Vampires: return t("Clan", "Clan");
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
    wolves = std::min<uint8_t>(wolves, 10);
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

int autoVampireCount(int players) { return players >= 22 ? 3 : players >= 10 ? 2 : 1; }

int autoWolfCount(int players) {
    if (players <= 6) return 1;
    if (players <= 10) return 2;
    if (players <= 14) return 3;
    if (players <= 19) return 4;
    if (players <= 24) return 5;
    if (players <= 28) return 6;
    return 7;
}

std::vector<Role> buildRoleList(const Settings& settings, int players, Text* error) {
    auto fail = [&](Text msg) {
        if (error) *error = std::move(msg);
        return std::vector<Role>{};
    };
    if (players < kMinPlayers)
        return fail(tx("Es werden mindestens {} Spieler benötigt.", "At least {} players are needed.", kMinPlayers));
    if (players > kMaxPlayers) return fail(tx("Es dürfen höchstens {} Spieler sein.", "At most {} players are allowed.", kMaxPlayers));

    const int wolves = settings.wolves ? settings.wolves : autoWolfCount(players);
    if (wolves * 2 >= players)
        return fail(tx("Zu viele Werwölfe für {} Spieler – es müssen weniger als die Hälfte sein.",
                       "Too many werewolves for {} players – they must be fewer than half.", players));
    const int vampires = settings.has(Role::Vampire) ? autoVampireCount(players) : 0;
    if ((wolves + vampires) * 2 >= players)
        return fail(tx("Zu wenige Spieler für Werwölfe und Vampire – zusammen müssen sie weniger als die Hälfte der {} sein.",
                       "Too few players for werewolves and vampires – together they must be fewer than half of the {}.", players));

    // Besondere Wölfe ersetzen normale Werwölfe
    std::vector<Role> roles;
    std::vector<Role> wolfVariants;
    for (Role r : {Role::AlphaWolf, Role::WolfCub, Role::BigBadWolf, Role::WhiteWolf})
        if (settings.has(r)) wolfVariants.push_back(r);
    for (int i = 0; i < wolves; ++i)
        roles.push_back(static_cast<size_t>(i) < wolfVariants.size() ? wolfVariants[static_cast<size_t>(i)] : Role::Werewolf);
    for (int i = 0; i < vampires; ++i) roles.push_back(Role::Vampire);

    // Weitere Sonderrollen in fester Reihenfolge, solange Plätze frei sind
    for (int r = 0; r < static_cast<int>(Role::Count) && static_cast<int>(roles.size()) < players; ++r) {
        const Role role = static_cast<Role>(r);
        if (!isOptionalRole(role) || isEvilRole(role) || !settings.has(role)) continue;
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
