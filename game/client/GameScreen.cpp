#include "client/App.h"
#include "client/Theme.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <map>

namespace ww {

using eng::Color;
using eng::Rect;

namespace {

uint16_t figureKey(uint8_t id, uint8_t kind) { return static_cast<uint16_t>(id * 4 + kind); }

float wrap01(float v) {
    v = std::fmod(v, 1.0f);
    return v < 0.0f ? v + 1.0f : v;
}

std::string clockText(float seconds) {
    const int s = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    return std::format("{}:{:02}", s / 60, s % 60);
}

Color promptColor(Interaction kind) {
    switch (kind) {
    case Interaction::Attack:
    case Interaction::DoorBreak: return theme::blood;
    case Interaction::Infect: return theme::roleColor(Role::AlphaWolf);
    case Interaction::Inspect: return theme::roleColor(Role::Seer);
    case Interaction::Protect: return theme::roleColor(Role::Protector);
    case Interaction::Heal:
    case Interaction::Poison: return theme::roleColor(Role::Witch);
    case Interaction::CupidPick: return theme::roleColor(Role::Cupid);
    case Interaction::HunterShot: return theme::roleColor(Role::Hunter);
    case Interaction::Track: return theme::roleColor(Role::Tracker);
    case Interaction::Reinforce: return theme::roleColor(Role::Blacksmith);
    case Interaction::HolyWater: return theme::roleColor(Role::Priest);
    case Interaction::Stab: return theme::roleColor(Role::SerialKiller);
    case Interaction::Bite: return theme::roleColor(Role::Vampire);
    case Interaction::Douse:
    case Interaction::Ignite: return theme::roleColor(Role::Arsonist);
    case Interaction::DoorToggle: return Color::hex(0xC9B48C);
    case Interaction::Steal: return theme::roleColor(Role::Thief);
    case Interaction::Enchant: return theme::roleColor(Role::Piper);
    case Interaction::Sniff: return theme::roleColor(Role::Fox);
    case Interaction::TakeRole: return theme::roleColor(Role::Servant);
    case Interaction::SecondVote: return theme::roleColor(Role::Judge);
    case Interaction::ChooseModel: return theme::roleColor(Role::WildChild);
    case Interaction::ChooseSide: return theme::roleColor(Role::WolfHound);
    case Interaction::Mark: return theme::roleColor(Role::Raven);
    case Interaction::NameSuccessor: return theme::goldLight;
    default: return theme::gold;
    }
}

// Zielt die Aktion auf einen Spieler (und nicht auf ein Haus oder auf nichts)?
bool targetsPlayer(Interaction kind) {
    switch (kind) {
    case Interaction::Transform:
    case Interaction::RingBell:
    case Interaction::DoorToggle:
    case Interaction::DoorBreak:
    case Interaction::Reinforce:
    case Interaction::Douse:
    case Interaction::Ignite:
    case Interaction::SecondVote:
    case Interaction::ChooseSide: return false;
    default: return true;
    }
}

} // namespace

bool App::imAlive() const {
    const PlayerView* me = m_net.view().me();
    return me && me->alive;
}

std::string App::headerTitle(const GameView& v) const {
    switch (v.phase) {
    case Phase::Day: return Lf("Tag {}", "Day {}", v.day);
    case Phase::Assembly: return v.election ? L("Hauptmannwahl", "Captain election") : L("Versammlung", "Gathering");
    case Phase::Vote: return v.election ? L("Hauptmannwahl", "Captain election") : L("Abstimmung", "Vote");
    case Phase::Execution: return L("Das Urteil", "The verdict");
    case Phase::Night: return Lf("Nacht {}", "Night {}", v.day);
    case Phase::Morning: return L("Morgengrauen", "Dawn");
    case Phase::GameOver: return L("Spielende", "Game over");
    default: return "";
    }
}

std::string App::phaseWithDay(const GameView& v) const {
    if (v.phase == Phase::Day || v.phase == Phase::Night || v.day <= 0) return headerTitle(v);
    return Lf("{} · Tag {}", "{} · day {}", headerTitle(v), v.day);
}

// --- Einstieg ------------------------------------------------------------------

void App::enterMatch() {
    const GameView& v = m_net.view();
    ensureWorld(v.worldSeed);
    m_inMatch = true;
    m_figures.clear();
    m_entities.clear();
    m_player = Mover{};
    m_player.position = m_world.spawnPoint(0, 1);
    m_pauseOpen = false;
    m_deathTime = -100.0;
    m_wasAlive = true;
    m_chatWolves = false;
    m_bubbles.clear();
    m_chatScroll = 0.0f;
    m_myTransform = 0.0f;
    m_timeOfDay = timeOfDay(v.phase, v.timeLeft, v.phaseDuration);
    m_window.setMouseCaptured(true);
    m_autoTarget = {0, 8};
    m_journalOpen = false;
    m_pauseSettings = false;
    // Zu Beginn wird die eigene Rolle feierlich aufgedeckt (nicht beim Wiederverbinden mitten im Spiel)
    m_revealDone = v.day > 1 || v.phase == Phase::GameOver || m_dev.autoPlay || m_dev.journal || m_dev.pause;
    m_revealStart = m_time;
    m_soundEvents.reset();
    if (!m_revealDone) playSound(Sound::Reveal, 0.7f);
    m_journalOpen = m_dev.journal;
    m_pauseOpen = m_dev.pause;
    if (m_journalOpen || m_pauseOpen) m_window.setMouseCaptured(false);
}

// --- Aktionen und Zielauswahl ----------------------------------------------------

void App::buildPrompts(const GameView& v) {
    m_prompts.clear();
    const Vec3 me = m_player.position;
    const Vec2 me2{me.x, me.z};
    const Vec2 fwd{std::sin(m_camera.yaw), std::cos(m_camera.yaw)};
    const bool alive = imAlive();
    const DoorMask closed = m_net.closedDoors();
    auto name = [&](uint8_t id) {
        const PlayerView* p = v.player(id);
        return p ? p->name : std::string("?");
    };
    // Bestes Ziel in Blickrichtung innerhalb der Reichweite
    auto aim = [&](float range, bool corpses, auto filter) {
        uint8_t best = kNoPlayer;
        float bestScore = 1e9f;
        for (const EntityState& e : m_entities) {
            if (corpses ? e.kind != EntityCorpse : e.kind != EntityLiving) continue;
            if (!corpses && e.id == v.you) continue;
            if (!filter(e)) continue;
            const Vec2 d{e.x - me.x, e.z - me.z};
            const float dist = d.length();
            if (dist > range) continue;
            if (alive && m_world.lineBlocked(me2, {e.x, e.z}, closed)) continue; // hinter Wänden und Türen

            const float angle = dist < 0.05f ? 0.0f : std::acos(std::clamp((fwd.x * d.x + fwd.y * d.y) / dist, -1.0f, 1.0f));
            const float maxAngle = dist < 3.0f ? 0.95f : 0.42f;
            if (angle > maxAngle) continue;
            const float score = angle * 4.0f + dist * 0.05f;
            if (score < bestScore) {
                bestScore = score;
                best = e.id;
            }
        }
        return best;
    };
    auto any = [](const EntityState&) { return true; };
    auto add = [&](Interaction kind, const char* key, std::string label, uint8_t target, bool ready, uint8_t param = 0) {
        m_prompts.push_back(Prompt{kind, key, std::move(label), target, ready, param});
    };

    // Beschriftung mit dem Namen des Ziels, sonst ein Hinweis, wen man anschauen soll
    auto label = [&](uint8_t t, std::string_view de, std::string_view en, const char* noneDe, const char* noneEn) {
        return t != kNoPlayer ? Lf(de, en, name(t)) : std::string(L(noneDe, noneEn));
    };

    if (v.can(AbilityNameSuccessor)) {
        const uint8_t t = aim(60.0f, false, any);
        add(Interaction::NameSuccessor, "E",
            label(t, "Hauptmann-Amt an {} übergeben", "Hand the captaincy to {}", "Schau deinen Nachfolger an", "Look at your successor"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityTakeRole) && v.condemned != kNoPlayer)
        add(Interaction::TakeRole, "E", Lf("Als ergebene Magd die Rolle von {} übernehmen", "As the devoted servant, take the role of {}", name(v.condemned)),
            v.condemned, true);
    if (v.can(AbilitySecondVote)) add(Interaction::SecondVote, "F", L("Heimlich eine zweite Abstimmung verlangen", "Secretly demand a second vote"), 0, true);
    if (v.can(AbilityChooseSide)) {
        add(Interaction::ChooseSide, "E", L("Dem Dorf treu bleiben", "Stay loyal to the village"), 0, true);
        add(Interaction::ChooseSide, "F", L("Zum Werwolf werden", "Become a werewolf"), 0, true, 1);
    }
    if (v.can(AbilitySteal)) {
        const uint8_t t = aim(kStealRange, false, any);
        add(Interaction::Steal, "E", label(t, "Rolle stehlen: {}", "Steal role: {}", "Schleich dich an jemanden heran (Dieb)", "Sneak up on someone (thief)"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityEnchant)) {
        const uint8_t t = aim(kEnchantRange, false, [&](const EntityState& e) {
            const PlayerView* p = v.player(e.id);
            return !p || !p->enchanted;
        });
        add(Interaction::Enchant, "E", label(t, "Verzaubern: {}", "Enchant: {}", "Spiel jemandem in der Nähe vor (Flöte)", "Play for someone nearby (flute)"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilitySniff)) {
        const uint8_t t = aim(kSniffRange, false, any);
        add(Interaction::Sniff, "E", label(t, "Wittern: {} und Umgebung", "Sniff: {} and surroundings", "Schau jemanden an (Fuchsnase)", "Look at someone (fox nose)"),
            t, t != kNoPlayer);
    }
    if (v.can(AbilityMark)) {
        const uint8_t t = aim(kMarkRange, false, any);
        add(Interaction::Mark, "E", label(t, "Rabe auf das Dach von {}", "Raven on the roof of {}", "Schau jemanden an (Rabe)", "Look at someone (raven)"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityChooseModel)) {
        const uint8_t t = aim(kModelRange, false, any);
        add(Interaction::ChooseModel, "E", label(t, "Vorbild wählen: {}", "Choose role model: {}", "Schau dein Vorbild an", "Look at your role model"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityHunterShot)) {
        const uint8_t t = aim(kHunterRange, false, any);
        add(Interaction::HunterShot, "E",
            label(t, "Letzter Schuss auf {}", "Last shot at {}", "Ziele auf jemanden – dein letzter Schuss", "Aim at someone – your last shot"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityVote)) {
        const uint8_t t = aim(60.0f, false, any);
        if (t != kNoPlayer && v.vote == t)
            add(Interaction::Vote, "E",
                v.election ? Lf("Stimme für {} zurückziehen", "Withdraw vote for {}", name(t)) : Lf("Stimme gegen {} zurückziehen", "Withdraw vote against {}", name(t)),
                kNoPlayer, true);
        else if (t != kNoPlayer)
            add(Interaction::Vote, "E", v.election ? Lf("Stimme für {}", "Vote for {}", name(t)) : Lf("Stimme gegen {}", "Vote against {}", name(t)), t, true);
        else add(Interaction::Vote, "E", L("Schau jemanden an, um abzustimmen", "Look at someone to vote"), kNoPlayer, false);
    }
    if (v.can(AbilityRingBell) && distanceXZ(me, m_world.bell()) < kBellRange)
        add(Interaction::RingBell, "E", L("Glocke läuten – Versammlung einberufen", "Ring the bell – call a gathering"), 0, true);
    if (v.can(AbilityTransform)) {
        const bool wolf = m_myTransform > 0.5f;
        add(Interaction::Transform, "R", wolf ? L("Zurückverwandeln", "Change back") : L("In einen Werwolf verwandeln", "Turn into a werewolf"), 0, true,
            wolf ? 0 : 1);
    }
    auto notPack = [&](const EntityState& e) {
        const PlayerView* p = v.player(e.id);
        return !p || !isWolfRole(p->role);
    };
    if (v.can(AbilityAttack) || v.can(AbilityBetray)) {
        const uint8_t t = aim(kAttackRange + 0.3f, false, [&](const EntityState& e) {
            return v.can(AbilityBetray) || (notPack(e) && v.can(AbilityAttack));
        });
        const PlayerView* tp = v.player(t);
        const bool packmate = tp && isWolfRole(tp->role);
        add(Interaction::Attack, "Klick",
            packmate ? label(t, "Verrat: {}", "Betray: {}", "", "")
                     : label(t, "Reißen: {}", "Maul: {}", "Komm nah genug an ein Opfer heran", "Get close enough to a victim"),
            t, t != kNoPlayer);
    }
    if (v.can(AbilityInfect)) {
        const uint8_t t = aim(kAttackRange + 0.3f, false, notPack);
        if (t != kNoPlayer) add(Interaction::Infect, "F", Lf("Infizieren: {} wird zum Werwolf", "Infect: {} becomes a werewolf", name(t)), t, true);
    }
    if (v.can(AbilityStab)) {
        const uint8_t t = aim(kAttackRange + 0.3f, false, any);
        add(Interaction::Stab, "Klick", label(t, "Erstechen: {}", "Stab: {}", "Schleich dich an ein Opfer heran", "Sneak up on a victim"), t, t != kNoPlayer);
    }
    if (v.can(AbilityBite)) {
        const auto notClan = [&](const EntityState& e) {
            const PlayerView* p = v.player(e.id);
            return !p || !isVampireRole(p->role);
        };
        const uint8_t t = aim(kAttackRange + 0.3f, false, notClan);
        add(Interaction::Bite, "Klick", label(t, "Beißen: {}", "Bite: {}", "Schleich dich lautlos an ein Opfer heran", "Sneak up on a victim silently"),
            t, t != kNoPlayer);
    }
    if (v.can(AbilityTrack)) {
        const uint8_t t = aim(kTrackRange, false, any);
        add(Interaction::Track, "E", label(t, "Fährte aufnehmen: {}", "Follow the trail: {}", "Schau jemanden an (Fährte)", "Look at someone (trail)"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityHolyWater)) {
        const uint8_t t = aim(kHolyWaterRange, false, any);
        if (t != kNoPlayer) add(Interaction::HolyWater, "E", Lf("Weihwasser auf {}", "Holy water on {}", name(t)), t, true);
    }
    if (v.can(AbilityIgnite)) {
        const int count = std::popcount(v.doused);
        add(Interaction::Ignite, "F",
            count == 1 ? std::string(L("1 getränktes Haus anzünden", "Set 1 doused house on fire"))
                       : Lf("{} getränkte Häuser anzünden", "Set {} doused houses on fire", count),
            0, true);
    }

    // Türen: nächste Tür in Reichweite
    const std::vector<DoorState>& doors = m_net.doors();
    int door = -1;
    float doorDist = kDoorRange;
    for (size_t i = 0; i < m_world.houses().size() && i < doors.size(); ++i) {
        const float d = (m_world.doorCenter(static_cast<int>(i)) - me2).length();
        if (d < doorDist) {
            doorDist = d;
            door = static_cast<int>(i);
        }
    }
    if (alive && door >= 0) {
        const DoorState& d = doors[static_cast<size_t>(door)];
        const uint8_t house = static_cast<uint8_t>(door);
        const bool inside = m_world.houseAt(me2, 0.3f) == door;
        const bool shut = !(d.flags & DoorOpen);
        if (!(d.flags & (DoorBroken | DoorBurnt))) {
            if (!shut)
                add(Interaction::DoorToggle, "Q", inside ? L("Tür schließen und verriegeln", "Close and bolt the door") : L("Tür schließen", "Close the door"), house,
                    true);
            else if ((d.flags & DoorLocked) && !inside)
                add(Interaction::DoorToggle, "Q", L("Die Tür ist von innen verriegelt", "The door is bolted from the inside"), house, false);
            else
                add(Interaction::DoorToggle, "Q", (d.flags & DoorLocked) ? L("Riegel lösen und öffnen", "Unbolt and open") : L("Tür öffnen", "Open the door"),
                    house, true);
        }
        if (v.can(AbilityBreakDoor) && shut && (d.flags & DoorLocked) && !inside && !(d.flags & (DoorBroken | DoorBurnt)))
            add(Interaction::DoorBreak, "Klick",
                (d.flags & DoorReinforced) ? Lf("Beschlagene Tür einschlagen ({} %)", "Break the reinforced door ({} %)", d.health)
                                           : Lf("Tür einschlagen ({} %)", "Break the door ({} %)", d.health),
                house, true);
        if (v.can(AbilityReinforce) && !(d.flags & DoorBurnt)) {
            const bool done = (d.flags & DoorReinforced) && !(d.flags & DoorBroken) && d.health >= 100;
            add(Interaction::Reinforce, "E",
                done                        ? L("Diese Tür ist schon beschlagen", "This door is already reinforced")
                : (d.flags & DoorBroken) ? L("Tür reparieren und beschlagen", "Repair and reinforce the door")
                                            : L("Tür mit Eisen beschlagen", "Reinforce the door with iron"),
                house, !done);
        }
        if (v.can(AbilityDouse) && !(d.flags & (DoorBurnt | DoorBurning))) {
            const bool doused = v.doused & (1u << door);
            const bool chapel = door == m_world.chapelIndex();
            add(Interaction::Douse, "E",
                doused ? (chapel ? L("Die Kapelle ist schon getränkt", "The chapel is already doused") : L("Dieses Haus ist schon getränkt", "This house is already doused"))
                       : (chapel ? L("Kapelle mit Öl tränken", "Douse the chapel with oil") : L("Haus mit Öl tränken", "Douse the house with oil")),
                house, !doused);
        }
    }
    if (v.can(AbilityInspect)) {
        const uint8_t t = aim(kInspectRange, false, any);
        add(Interaction::Inspect, "E", label(t, "Vision: Wer ist {}?", "Vision: who is {}?", "Schau jemanden an (Vision)", "Look at someone (vision)"), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityProtect)) {
        uint8_t t = aim(kProtectRange, false, any);
        if (t == kNoPlayer) t = v.you;
        const bool allowed = t != v.lastProtected;
        add(Interaction::Protect, "E", t == v.you ? std::string(L("Dich selbst beschützen", "Protect yourself")) : Lf("Beschützen: {}", "Protect: {}", name(t)), t,
            allowed);
    }
    if (v.can(AbilityCupid)) {
        uint8_t t = aim(kCupidRange, false, [&](const EntityState& e) {
            return std::find(v.cupidPicks.begin(), v.cupidPicks.end(), e.id) == v.cupidPicks.end();
        });
        if (t == kNoPlayer && std::find(v.cupidPicks.begin(), v.cupidPicks.end(), v.you) == v.cupidPicks.end()) t = v.you;
        add(Interaction::CupidPick, "E",
            t == v.you ? std::string(L("Amors Pfeil auf dich selbst", "Cupid's arrow at yourself")) : Lf("Amors Pfeil: {}", "Cupid's arrow: {}", name(t)), t,
            t != kNoPlayer);
    }
    if (v.can(AbilityHeal)) {
        const uint8_t t = aim(kHealRange + 0.5f, true, any);
        if (t != kNoPlayer) add(Interaction::Heal, "E", Lf("Heiltrank für {}", "Healing potion for {}", name(t)), t, true);
    }
    if (v.can(AbilityPoison)) {
        const uint8_t t = aim(kPoisonRange, false, any);
        add(Interaction::Poison, "F", label(t, "Gift für {}", "Poison for {}", "Gift: geh nah an jemanden heran", "Poison: get close to someone"), t,
            t != kNoPlayer);
    }
    m_aimTarget = kNoPlayer;
    for (const Prompt& p : m_prompts) {
        if (p.target != kNoPlayer && targetsPlayer(p.kind)) {
            m_aimTarget = p.target;
            break;
        }
    }
}

bool App::canMoveFreely() const {
    const GameView& v = m_net.view();
    return v.phase != Phase::GameOver;
}

void App::updateMatch() {
    const GameView& v = m_net.view();
    eng::Input& in = m_window.input();
    const float dt = m_dt;
    if (v.worldSeed != 0 && v.worldSeed != m_world.seed()) ensureWorld(v.worldSeed);

    if (auto c = m_net.takeCorrection()) {
        m_player.position = m_world.ground(c->x, c->z);
        m_player.velocity = {0, 0};
        m_player.yaw = c->yaw;
        m_camera.yaw = c->yaw;
    }

    const bool alive = imAlive();
    if (m_wasAlive && !alive) {
        m_deathTime = m_time;
        playSound(Sound::Death, 0.8f);
    }
    m_wasAlive = alive;

    // Maus und Menüs
    const bool chatFocused = m_ui.isFocused("chat");
    const bool over = v.phase == Phase::GameOver;
    if (over) m_window.setMouseCaptured(false);
    if (m_chatWasFocused && !chatFocused && !m_pauseOpen && !over) m_window.setMouseCaptured(true);
    m_chatWasFocused = chatFocused;
    // Rollenkarte zu Beginn: Leertaste, Klick oder Esc deckt auf und schließt
    if (!m_revealDone) {
        const double age = m_time - m_revealStart;
        if (age > 9.0 || (age > 1.8 && (in.pressed(eng::Key::Space) || in.mousePressed() || in.pressed(eng::Key::Escape) ||
                                        in.pressed(eng::Key::Enter)))) {
            m_revealDone = true;
            in.consumeMouse();
        }
    } else if (!chatFocused && !over && in.pressed(eng::Key::J) && !m_pauseOpen) {
        m_journalOpen = !m_journalOpen;
        m_window.setMouseCaptured(!m_journalOpen);
    } else if (!chatFocused && !over && in.pressed(eng::Key::Escape)) {
        if (m_journalOpen) {
            m_journalOpen = false;
            m_window.setMouseCaptured(true);
        } else if (m_pauseSettings) {
            m_pauseSettings = false;
        } else if (m_window.isMouseCaptured()) {
            m_window.setMouseCaptured(false);
            m_pauseOpen = true;
            m_pauseFocus = 0;
        } else {
            m_pauseOpen = false;
            m_window.setMouseCaptured(true);
        }
    }
    if (!chatFocused && !over && in.pressed(eng::Key::Enter) && !m_pauseOpen && !m_journalOpen && m_revealDone) {
        m_ui.focus("chat");
        m_chatFocusTime = m_time;
        m_window.setMouseCaptured(false);
    }
    if (!m_window.isMouseCaptured() && !m_pauseOpen && !m_journalOpen && m_revealDone && !over && !chatFocused && in.mousePressed()) {
        const bool onHud = std::any_of(m_hudBlockers.begin(), m_hudBlockers.end(), [&](const Rect& r) { return r.contains(in.mouse); });
        if (!onHud) {
            m_window.setMouseCaptured(true);
            in.consumeMouse();
        }
    }
    const bool medium = (v.yourRole == Role::Medium || v.acting == Role::Medium) && v.phase == Phase::Night;
    const bool family = v.yourRole == Role::Sister || v.yourRole == Role::Brother;
    const bool secretChat = isWolfRole(v.yourRole) || isVampireRole(v.yourRole) || medium || family;
    if (in.pressed(eng::Key::Tab) && secretChat && alive) m_chatWolves = !m_chatWolves;
    if (!secretChat) m_chatWolves = false;
    const bool captured = m_window.isMouseCaptured();

    // Figuren aus den Snapshots
    m_entities = m_net.entities(m_time);
    for (auto& [key, f] : m_figures) f.present = false;
    for (const EntityState& e : m_entities) {
        if (e.id == v.you && e.kind == EntityLiving) {
            m_myTransform = e.transform;
            continue;
        }
        if (e.id == v.you && e.kind == EntityGhost) continue;
        Figure& f = m_figures[figureKey(e.id, e.kind)];
        const Vec3 pos = m_world.ground(e.x, e.z);
        if (f.mover.position.lengthSq() == 0.0f) {
            f.mover.position = pos;
            f.mover.yaw = e.yaw;
        }
        f.mover.animateTo(pos, e.yaw, dt);
        f.transform = e.transform;
        f.flags = e.flags;
        f.kind = e.kind;
        f.present = true;
    }
    std::erase_if(m_figures, [](const auto& kv) { return !kv.second.present; });
    if (!alive) m_myTransform = 0.0f;

    // Eigene Bewegung (vorhergesagt, Server prüft)
    m_camera.update(in, captured);
    Vec2 wish = (captured && !chatFocused) ? inputDirection(in, m_camera.yaw) : Vec2{0, 0};
    if (m_dev.autoPlay && !over) {
        const Vec2 pos{m_player.position.x, m_player.position.z};
        if ((m_autoTarget - pos).length() < 1.5f || m_devRng.chance(0.002f)) {
            const float a = m_devRng.uniform(0.0f, 2.0f * eng::kPi);
            m_autoTarget = m_world.resolveCollision({std::cos(a) * m_devRng.uniform(5.0f, 30.0f), std::sin(a) * m_devRng.uniform(5.0f, 30.0f)}, 0.6f);
        }
        if (v.phase == Phase::Day || v.phase == Phase::Night) wish = (m_autoTarget - pos).normalized();
        m_camera.yaw += wrapAngle(m_player.yaw - m_camera.yaw) * std::min(1.0f, dt * 1.5f);
    }
    const bool sprint = in.down(eng::Key::Shift);
    float speed = sprint ? kSprintSpeed : kWalkSpeed;
    if (!alive) speed = kGhostSpeed;
    else if (m_myTransform >= 0.99f) speed = kWolfSpeed;
    else if (m_myTransform > 0.01f) speed = 0.0f; // während der Verwandlung erstarrt
    if (!canMoveFreely()) speed = 0.0f;
    m_player.move(wish, speed, dt, m_world, alive, m_net.closedDoors());
    m_worldRenderer.updateHouses(m_net.doors(), dt);
    m_worldRenderer.setHiddenRoof(m_world.houseAt({m_player.position.x, m_player.position.z}, 0.25f));
    if (m_time - m_lastMoveSent >= proto::kMoveInterval) {
        m_net.sendMove(m_player.position.x, m_player.position.z, m_player.yaw, sprint && wish.length() > 0.1f ? EntitySprint : 0);
        m_lastMoveSent = m_time;
    }

    // Aktionen
    buildPrompts(v);
    if (captured && !chatFocused) {
        auto trigger = [&](const char* key) {
            for (const Prompt& p : m_prompts) {
                if (std::string_view(p.key) != key || !p.ready) continue;
                const bool param = p.kind == Interaction::Transform || p.kind == Interaction::ChooseSide;
                m_net.sendInteract(p.kind, param ? p.param : p.target);
                return;
            }
        };
        if (in.pressed(eng::Key::E)) trigger("E");
        if (in.pressed(eng::Key::F)) trigger("F");
        if (in.pressed(eng::Key::Q)) trigger("Q");
        if (in.pressed(eng::Key::R)) trigger("R");
        if (in.mousePressed()) trigger("Klick");
    }

    // Tageszeit folgt weich der Spielphase
    const float target = timeOfDay(v.phase, m_net.timeLeft(m_time), v.phaseDuration);
    float diff = target - m_timeOfDay;
    if (diff > 0.5f) diff -= 1.0f;
    if (diff < -0.5f) diff += 1.0f;
    m_timeOfDay = wrap01(m_timeOfDay + diff * std::min(1.0f, dt * 1.2f));
}

// --- 3D ------------------------------------------------------------------------

void App::renderMatch3D() {
    const GameView& v = m_net.view();
    const float t = static_cast<float>(m_time - m_startTime);
    const bool alive = imAlive();

    m_lastCamera = m_camera.camera(m_player.position + Vec3{0, alive ? 0.0f : 0.4f, 0}, m_world);
    eng::Environment env = m_worldRenderer.environment(m_timeOfDay, m_player.position, t);
    applyGraphics(env);
    if (!alive && v.phase != Phase::GameOver) {
        env.saturation *= 0.45f;
        env.gradeTint = mul(env.gradeTint, Vec3{0.85f, 0.95f, 1.2f});
    }
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), m_lastCamera, env);
    m_worldRenderer.draw(m_renderer3d, env, t);
    const float night = env.emissive;

    auto drawFigure = [&](uint8_t id, const Mover& m, float transform, uint8_t flags, uint8_t kind) {
        CharacterPose pose;
        pose.position = m.position;
        pose.yaw = m.yaw;
        pose.walkPhase = m.walkPhase;
        pose.speed = m.speed;
        pose.wolf = transform;
        pose.attack = (flags & EntityAttack) ? 1.0f : 0.0f;
        pose.time = t + static_cast<float>(id) * 1.7f;
        pose.dead = kind == EntityCorpse;
        pose.ghost = kind == EntityGhost;
        pose.lantern = (flags & EntityLantern) != 0 && kind == EntityLiving && night > 0.3f;
        pose.redEyes = (flags & EntityRedEyes) != 0 && kind == EntityLiving ? std::clamp(night * 1.4f - 0.2f, 0.0f, 1.0f) : 0.0f;
        m_characters.draw(m_renderer3d, characterLook(id), pose);
        // ein Hauch von Rot im Gesicht – aufmerksame Augenzeugen erkennen so einen Vampir
        if (pose.redEyes > 0.0f)
            m_renderer3d.light({m.position + Vec3{0.0f, m_characters.headHeight(pose) - 0.2f, 0.0f}, 1.8f, {1.0f, 0.12f, 0.08f}, 0.7f * pose.redEyes});
        if (pose.lantern) m_renderer3d.light({m_characters.lanternPosition(pose), 6.5f, {1.0f, 0.62f, 0.28f}, 1.3f * night});
        if (transform > 0.02f && transform < 0.98f) {
            for (int i = 0; i < 14; ++i) {
                const float a = static_cast<float>(i) * 0.45f + t * 4.0f;
                const Vec3 p = m.position + Vec3{std::cos(a) * 0.8f, 0.3f + std::fmod(t * 2.5f + static_cast<float>(i) * 0.27f, 2.0f),
                                                 std::sin(a) * 0.8f};
                m_renderer3d.particle(p, 0.22f, {3.5f, 0.7f, 0.25f}, 1.0f - std::fabs(transform - 0.5f) * 2.0f);
            }
        }
        if (pose.ghost) m_renderer3d.particle(m.position + Vec3{0, 1.2f, 0}, 2.4f, {0.12f, 0.2f, 0.45f}, 0.3f);
    };

    // eigene Figur
    {
        uint8_t flags = 0;
        for (const EntityState& e : m_entities)
            if (e.id == v.you && e.kind == EntityLiving) flags = e.flags;
        const bool lantern = night > 0.3f && m_myTransform < 0.01f && !isVampireRole(v.yourRole);
        if (alive) drawFigure(v.you, m_player, m_myTransform, flags | (lantern ? EntityLantern : 0), EntityLiving);
        else drawFigure(v.you, m_player, 0.0f, 0, EntityGhost);
    }
    for (const auto& [key, f] : m_figures) drawFigure(static_cast<uint8_t>(key / 4), f.mover, f.transform, f.flags, f.kind);

    // Fährtenleser: die verfolgte Person leuchtet auch durch Wände
    if (v.tracked != kNoPlayer && alive) {
        for (const EntityState& e : m_entities) {
            if (e.id != v.tracked || e.kind != EntityLiving) continue;
            const Vec3 base = m_world.ground(e.x, e.z);
            const Color c = theme::roleColor(Role::Tracker);
            for (int i = 0; i < 10; ++i) {
                const float fi = static_cast<float>(i);
                const float life = std::fmod(t * 0.6f + fi * 0.1f, 1.0f);
                m_renderer3d.particle(base + Vec3{0, 2.4f + life * 3.0f, 0}, 0.35f, Vec3{c.r, c.g, c.b} * 2.5f, 1.0f - life);
            }
        }
    }

    // Zielmarkierung am Boden
    if (m_aimTarget != kNoPlayer && !m_prompts.empty()) {
        Color c = theme::gold;
        Vec3 pos;
        bool found = false;
        for (const Prompt& p : m_prompts) {
            if (p.target == m_aimTarget) {
                c = promptColor(p.kind);
                break;
            }
        }
        for (const EntityState& e : m_entities) {
            if (e.id != m_aimTarget || e.kind == EntityGhost) continue;
            pos = m_world.ground(e.x, e.z);
            found = true;
            if (e.kind == EntityLiving) break;
        }
        if (m_aimTarget == v.you) {
            pos = m_player.position;
            found = true;
        }
        if (found) {
            for (int i = 0; i < 24; ++i) {
                const float a = static_cast<float>(i) / 24.0f * 2.0f * eng::kPi + t * 1.5f;
                m_renderer3d.particle(pos + Vec3{std::cos(a) * 0.75f, 0.08f, std::sin(a) * 0.75f}, 0.16f,
                                      Vec3{c.r, c.g, c.b} * 3.0f, 0.9f);
            }
        }
    }
    m_renderer3d.endFrame();
}

// --- HUD -----------------------------------------------------------------------

void App::drawNameTags(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float scale = m_renderer.dpiScale();
    const bool night = m_timeOfDay > 0.78f || m_timeOfDay < 0.22f;
    std::map<uint8_t, int> votes;
    for (const PlayerView& p : v.players)
        if (p.vote != kNoPlayer) ++votes[p.vote];
    const bool seeThroughWalls = !imAlive() || v.phase == Phase::GameOver;
    const DoorMask closed = m_net.closedDoors();

    auto tag = [&](uint8_t id, Vec3 feet, float height, bool corpse, bool ghost) {
        const PlayerView* p = v.player(id);
        if (!p) return;
        Vec2 screen;
        const Vec3 head = feet + Vec3{0, height, 0};
        if (!m_renderer3d.worldToScreen(head, screen)) return;
        const bool tracked = id == v.tracked && !corpse && !ghost;
        const float dist = (head - m_lastCamera.position).length();
        const float maxDist = tracked ? 80.0f : corpse ? 14.0f : night ? 11.0f : 26.0f;
        if (dist > maxDist) return;
        if (!tracked && !seeThroughWalls &&
            m_world.lineBlocked({m_player.position.x, m_player.position.z}, {feet.x, feet.z}, closed))
            return;
        const float alpha = eng::clamp01((maxDist - dist) / 5.0f);
        const Vec2 pt = screen / scale;

        std::string label = p->name;
        if (corpse) label = "† " + label;
        if (tracked) label = std::string(roleIcon(Role::Tracker)) + " " + label;
        if (p->captain) label = "♛ " + label;
        if (p->enchanted) label += " ♪";
        if (p->marked) label += " ♠";
        if (p->role != Role::Unknown && id != v.you) label = std::string(roleIcon(p->role)) + " " + label;
        const float w = m_fonts.smallBold.measure(label) + 16.0f;
        const Rect plate{pt.x - w * 0.5f, pt.y - 11.0f, w, 22.0f};
        Color fg = corpse ? theme::muted : ghost ? Color::hex(0xB8CCF2) : theme::playerColor(id);
        if (id == m_aimTarget) r.roundRectOutline(plate.shrink(-2.0f), 12.0f, 1.5f, theme::gold.fade(alpha));
        r.roundRect(plate, 11.0f, Color::hex(0x0A0D1A, 0.62f * alpha));
        r.textCentered(m_fonts.smallBold, plate, label, fg.fade(alpha));

        if (!corpse && votes.count(id)) {
            const Rect badge{plate.right() + 4.0f, plate.y, 24.0f, 22.0f};
            r.roundRect(badge, 11.0f, theme::blood.fade(alpha));
            r.textCentered(m_fonts.smallBold, badge, std::to_string(votes[id]), Color::hex(0xFFFFFF, alpha));
        }

        // Sprechblase
        auto it = m_bubbles.find(id);
        if (!corpse && it != m_bubbles.end()) {
            const double age = m_time - it->second.time;
            if (age < 6.0) {
                const float ba = alpha * eng::clamp01(static_cast<float>(6.0 - age) / 0.8f);
                std::vector<std::string> lines = m_fonts.small.wrap(it->second.text.str(), 230.0f);
                if (lines.size() > 3) lines.resize(3);
                float bw = 0.0f;
                for (const std::string& l : lines) bw = std::max(bw, m_fonts.small.measure(l));
                const float lh = m_fonts.small.lineHeight();
                const Rect box{pt.x - bw * 0.5f - 10.0f, plate.y - 12.0f - lh * static_cast<float>(lines.size()) - 12.0f,
                               bw + 20.0f, lh * static_cast<float>(lines.size()) + 12.0f};
                Color bg = Color::hex(0xF5F1E8, 0.94f * ba), text = Color::hex(0x1B1406, ba);
                if (it->second.channel == ChatChannel::Wolves) {
                    bg = Color::hex(0x5A1A16, 0.94f * ba);
                    text = Color::hex(0xF8DCD8, ba);
                } else if (it->second.channel == ChatChannel::Vampires) {
                    bg = Color::hex(0x3E0F1E, 0.94f * ba);
                    text = Color::hex(0xF2D6DF, ba);
                } else if (it->second.channel == ChatChannel::Dead) {
                    bg = Color::hex(0x34405E, 0.85f * ba);
                    text = Color::hex(0xDCE3F2, ba);
                }
                r.roundRect(box, 9.0f, bg);
                r.triangle({pt.x - 6.0f, box.bottom() - 1.0f}, {pt.x + 6.0f, box.bottom() - 1.0f}, {pt.x, box.bottom() + 7.0f}, bg);
                float y = box.y + 6.0f;
                for (const std::string& l : lines) {
                    r.text(m_fonts.small, {box.x + 10.0f, y}, l, text);
                    y += lh;
                }
            }
        }
    };

    for (const auto& [key, f] : m_figures) {
        const uint8_t id = static_cast<uint8_t>(key / 4);
        CharacterPose pose;
        pose.wolf = f.transform;
        pose.dead = f.kind == EntityCorpse;
        tag(id, f.mover.position, m_characters.headHeight(pose) + 0.35f, f.kind == EntityCorpse, f.kind == EntityGhost);
    }
}

void App::drawPhaseBox(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const eng::Vec2 c{W * 0.5f, 74.0f};
    const float R = 54.0f;
    const float timeLeft = m_net.timeLeft(m_time);
    const float progress = v.phaseDuration > 0.0f ? eng::clamp01(1.0f - timeLeft / v.phaseDuration) : 1.0f;
    const bool night = v.phase == Phase::Night;

    r.glow({c.x, c.y - 10.0f}, 150.0f, palette::ink.withAlpha(0.55f));
    // Himmelsbogen: der Weg von Sonne bzw. Mond über das Dorf
    constexpr int kSegments = 40;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = eng::kPi + eng::kPi * static_cast<float>(i) / kSegments;
        const float a1 = eng::kPi + eng::kPi * static_cast<float>(i + 1) / kSegments;
        const bool done = static_cast<float>(i) / kSegments < progress;
        r.line(c + eng::Vec2{std::cos(a0) * R, std::sin(a0) * R}, c + eng::Vec2{std::cos(a1) * R, std::sin(a1) * R}, 1.4f,
               done ? palette::brass.withAlpha(0.8f) : palette::smoke.withAlpha(0.35f));
    }
    m_kit.diamond(c + eng::Vec2{-R, 0}, 2.5f, palette::brass.withAlpha(0.6f));
    m_kit.diamond(c + eng::Vec2{R, 0}, 2.5f, palette::brass.withAlpha(0.6f));
    const float a = eng::kPi + eng::kPi * progress;
    const eng::Vec2 body = c + eng::Vec2{std::cos(a) * R, std::sin(a) * R};
    const Color bodyColor = night ? palette::moon : Color::hex(0xF6C86A);
    r.glow(body, 26.0f, bodyColor.withAlpha(0.35f));
    r.circle(body, 7.0f, bodyColor);
    if (night) r.circle(body + eng::Vec2{3.0f, -2.0f}, 6.0f, palette::ink.withAlpha(0.85f)); // Mondsichel

    m_kit.spacedCentered(m_fonts.menu, {c.x - 200.0f, c.y - 22.0f, 400.0f, 30.0f}, upper(headerTitle(v)), 3.0f, palette::brassLight);
    if (v.phaseDuration > 0.0f && v.phase != Phase::GameOver) {
        const bool urgent = timeLeft < 10.0f;
        const Color tc = urgent ? palette::blood.fade(0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 8.0f)) : palette::ash;
        r.textCentered(m_fonts.uiBold, {c.x - 60.0f, c.y + 6.0f, 120.0f, 20.0f}, clockText(timeLeft), tc);
    }
    int alive = 0;
    for (const PlayerView& p : v.players)
        if (p.alive) ++alive;
    m_kit.label({c.x + R + 10.0f, c.y - 2.0f}, Lf("{} leben", "{} alive", alive), palette::ash);
    if (v.day > 0 && v.phase != Phase::Day && v.phase != Phase::Night) {
        const std::string day = Lf("Tag {}", "Day {}", v.day);
        m_kit.label({c.x - R - 10.0f - m_kit.labelWidth(day), c.y - 2.0f}, day, palette::ash);
    }
}

void App::drawRoleCard(Rect area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    const Role role = v.yourRole;
    std::vector<std::pair<std::string, Color>> lines = roleStatusLines(v);
    if (lines.size() > 4) lines.resize(4);
    const float h = 92.0f + static_cast<float>(lines.size()) * m_fonts.hand.lineHeight();
    const Rect card{area.x, area.bottom() - h - 30.0f, 300.0f, h};
    m_kit.light({card.right() + 120.0f, card.y - 160.0f}, 0.9f);
    m_kit.beginTilt(card, -0.018f);
    m_kit.paper(card, 13000u + static_cast<uint32_t>(role), 0.97f);
    const Vec2 icon{card.x + 44.0f, card.y + 44.0f};
    r.circleOutline(icon, 26.0f, 1.3f, inkSoft);
    r.circleOutline(icon, 30.0f, 0.7f, inkFaded.withAlpha(0.7f));
    r.textCentered(m_fonts.iconLarge, {icon.x - 24.0f, icon.y - 24.0f, 48.0f, 48.0f}, roleIcon(role),
                   eng::lerp(theme::roleColor(role), inkDark, 0.6f));
    r.text(m_fonts.scriptBold, {card.x + 84.0f, card.y + 16.0f}, roleName(role), inkDark);
    r.text(m_fonts.paperItalic, {card.x + 86.0f, card.y + 50.0f}, teamName(roleTeam(role)), inkFaded);
    float y = card.y + 84.0f;
    for (const auto& [text, color] : lines) {
        r.text(m_fonts.hand, {card.x + 22.0f, y}, m_fonts.hand.ellipsize(text, card.w - 40.0f), eng::lerp(color, inkDark, 0.82f));
        y += m_fonts.hand.lineHeight();
    }
    m_kit.endTilt();
    m_kit.hint({card.x + 8.0f, card.bottom() + 6.0f}, "J", L("Tagebuch", "Journal"), 0.85f);
}
void App::drawPrompts(const GameView&) {
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    if (m_prompts.empty()) return;
    float y = r.height() - 40.0f - static_cast<float>(m_prompts.size()) * 44.0f;
    int index = 0;
    for (const Prompt& p : m_prompts) {
        const float tw = m_fonts.paper.measure(p.label);
        // "Klick" ist zugleich die interne Kennung der Maustaste
        const char* key = std::string_view(p.key) == "Klick" ? L("Klick", "Click") : p.key;
        const float kw = m_kit.keycapWidth(key);
        const float w = kw + tw + 52.0f;
        const Rect strip{std::round(r.width() * 0.5f - w * 0.5f), y, w, 38.0f};
        const float a = m_kit.animFrom(std::format("prompt/{}", p.label), 0.0f, 1.0f, 8.0f);
        const float alpha = (p.ready ? 1.0f : 0.7f) * a;
        m_kit.beginTilt(strip, (index % 2 == 0 ? -0.01f : 0.012f));
        m_kit.paper(strip.offset(0, (1.0f - a) * 14.0f), 14000u + static_cast<uint32_t>(index), alpha);
        m_kit.keycap({strip.x + 14.0f, strip.y + 8.0f + (1.0f - a) * 14.0f, kw, 22.0f}, key, p.ready ? promptColor(p.kind) : smoke, alpha);
        r.text(m_fonts.paper, {strip.x + kw + 26.0f, strip.center().y - m_fonts.paper.lineHeight() * 0.5f + (1.0f - a) * 14.0f}, p.label,
               (p.ready ? inkDark : inkFaded).withAlpha(alpha));
        m_kit.endTilt();
        y += 44.0f;
        ++index;
    }
}
void App::drawVotePanel(const GameView& v) {
    if (v.phase != Phase::Vote && v.phase != Phase::Execution) return;
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    std::map<uint8_t, int> votes;
    for (const PlayerView& p : v.players)
        if (p.vote != kNoPlayer) ++votes[p.vote];
    std::vector<const PlayerView*> alive;
    for (const PlayerView& p : v.players)
        if (p.alive) alive.push_back(&p);
    const float rowH = 30.0f;
    const Rect panel{28.0f, 150.0f, 260.0f, 70.0f + rowH * static_cast<float>(alive.size())};
    m_hudBlockers.push_back(panel);
    m_kit.beginTilt(panel, 0.012f);
    m_kit.paper(panel, 15000u);
    m_kit.nail({panel.center().x, panel.y + 12.0f});
    r.text(m_fonts.scriptBold, {panel.x + 22.0f, panel.y + 18.0f}, v.election ? L("Hauptmannwahl", "Captain election") : L("Wer soll hängen?", "Who shall hang?"),
           inkRed);
    float y = panel.y + 58.0f;
    const bool canVote = v.can(AbilityVote);
    for (const PlayerView* p : alive) {
        const Rect row{panel.x + 14.0f, y, panel.w - 28.0f, rowH - 2.0f};
        const bool mine = v.vote == p->id;
        const bool clickable = canVote && p->id != v.you && !m_window.isMouseCaptured();
        if (m_ui.clickArea(std::format("vote/{}", p->id), row, clickable)) m_net.sendInteract(Interaction::Vote, mine ? kNoPlayer : p->id);
        const bool hover = clickable && m_ui.hovered(row);
        if (hover) m_ui.setCursor(eng::Cursor::Hand);
        std::string name = p->name;
        if (p->captain) name = "♛ " + name;
        r.text(m_fonts.hand, {row.x + 8.0f, row.center().y - m_fonts.hand.lineHeight() * 0.5f}, name,
               (hover ? inkRed : eng::lerp(theme::playerColor(p->id), inkDark, 0.72f)));
        if (mine) m_kit.swash({row.x + 8.0f, row.center().y + 9.0f}, m_fonts.hand.measure(name), inkRed.withAlpha(0.8f), 1.0f, p->id);
        // Striche wie auf einer Kerbholz-Liste
        const int n = votes.count(p->id) ? votes[p->id] : 0;
        for (int k = 0; k < n && k < 12; ++k) {
            const float x = row.right() - 70.0f + static_cast<float>(k % 5) * 7.0f + static_cast<float>(k / 5) * 40.0f;
            if (k % 5 == 4) m_kit.inkLine({x - 30.0f, row.y + 18.0f}, {x + 2.0f, row.y + 8.0f}, 1.6f, inkRed, 1.0f, static_cast<uint32_t>(k));
            else m_kit.inkLine({x, row.y + 6.0f}, {x - 1.0f, row.y + 21.0f}, 1.6f, v.election ? inkDark : inkRed, 1.0f, static_cast<uint32_t>(k + 9));
        }
        y += rowH;
    }
    m_kit.endTilt();
}
void App::drawMinimap(Rect area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    const float size = 168.0f;
    const eng::Vec2 c{area.right() - size * 0.5f - 34.0f, 34.0f + size * 0.5f};
    const float R = size * 0.5f;
    const float scale = (R - 8.0f) / 70.0f; // 70 m Radius
    auto toMap = [&](float x, float z) { return eng::Vec2{c.x + x * scale, c.y + z * scale}; };
    auto inside = [&](eng::Vec2 p) { return (p - c).length() < R - 6.0f; };

    // Handgezeichnete Karte auf Pergament, in Messing gefasst
    m_kit.light({c.x - 120.0f, c.y - 140.0f}, 0.9f);
    m_kit.paperDisc(c, R, 777u);
    r.circleOutline(c, R + 1.0f, 3.0f, brassDark);
    r.circleOutline(c, R + 2.5f, 1.2f, brass.withAlpha(0.9f));
    // Wege
    const std::vector<Vec2>& path = m_world.pathPoints();
    for (size_t i = 0; i + 1 < path.size(); i += 2) {
        const eng::Vec2 a = toMap(path[i].x, path[i].y), b = toMap(path[i + 1].x, path[i + 1].y);
        if (inside(a) && inside(b)) r.line(a, b, 1.2f, inkFaded.withAlpha(0.7f));
    }
    r.circleOutline(toMap(0, 0), World::kSquareRadius * scale, 1.0f, inkSoft.withAlpha(0.8f));
    r.circle(toMap(0, 0), 2.2f, ember);
    const PlayerView* self = v.me();
    const std::vector<DoorState>& doors = m_net.doors();
    eng::Color chapelInk = inkDark;
    for (size_t i = 0; i < m_world.houses().size(); ++i) {
        const House& h = m_world.houses()[i];
        const eng::Vec2 p = toMap(h.position.x, h.position.z);
        const uint8_t flags = i < doors.size() ? doors[i].flags : 0;
        const bool mine = self && self->house == i;
        eng::Color fill{0, 0, 0, 0};
        if (flags & DoorBurnt) fill = inkDark.withAlpha(0.85f);
        else if (flags & DoorBurning) fill = ember.fade(0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 9.0f));
        else if (v.doused & (1u << i)) fill = theme::roleColor(Role::Arsonist).withAlpha(0.8f);
        else if (mine) fill = inkRed.withAlpha(0.85f);
        if (h.chapel) {
            if (fill.a > 0.0f) chapelInk = fill.withAlpha(1.0f);
            continue;
        }
        r.pushTransform(eng::Affine2::around(p, -h.yaw));
        const eng::Rect hr{p.x - 4.0f, p.y - 3.5f, 8.0f, 7.0f};
        if (fill.a > 0.0f) r.rect(hr, fill);
        r.roundRectOutline(hr, 0.0f, 1.0f, inkDark.withAlpha(0.85f));
        r.popTransform();
    }
    // Kapelle als Kreuz, Glocke als kleiner Kreis
    const eng::Vec2 chapel = toMap(m_world.chapel().x, m_world.chapel().z);
    if (inside(chapel)) {
        r.line(chapel + eng::Vec2{0, -6}, chapel + eng::Vec2{0, 6}, 1.4f, chapelInk);
        r.line(chapel + eng::Vec2{-4, -2}, chapel + eng::Vec2{4, -2}, 1.4f, chapelInk);
    }
    r.circleOutline(toMap(m_world.bell().x, m_world.bell().z), 2.5f, 1.0f, inkDark);
    // Fährtenleser sieht die Spur auf der Karte
    for (const EntityState& e : m_entities) {
        if (e.id != v.tracked || e.kind != EntityLiving) continue;
        eng::Vec2 p = toMap(e.x, e.z);
        const eng::Vec2 delta = p - c;
        if (delta.length() > R - 6.0f) p = c + delta.normalized() * (R - 6.0f);
        r.circle(p, 3.5f, inkRed);
    }
    // Windrose
    const char* dirs[] = {"N", L("O", "E"), "S", "W"};
    const eng::Vec2 offs[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int i = 0; i < 4; ++i) {
        const eng::Vec2 p = c + offs[i] * (R + 14.0f);
        r.textCentered(m_fonts.scriptBold, {p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f}, dirs[i], i == 0 ? Color::hex(0xD8644E) : parchment.withAlpha(0.7f));
    }
    // eigene Position mit Blickrichtung
    eng::Vec2 me = toMap(m_player.position.x, m_player.position.z);
    const eng::Vec2 delta = me - c;
    if (delta.length() > R - 8.0f) me = c + delta.normalized() * (R - 8.0f);
    const eng::Vec2 dir{std::sin(m_camera.yaw), std::cos(m_camera.yaw)};
    const eng::Vec2 side{-dir.y, dir.x};
    r.triangle(me + dir * 9.0f, me - dir * 5.0f + side * 5.0f, me - dir * 5.0f - side * 5.0f, imAlive() ? inkRed : inkFaded);
}
void App::drawBanner() {
    const double age = m_time - m_bannerTime;
    constexpr double kDuration = 3.6;
    if (age < 0.0 || age > kDuration || !m_revealDone) return;
    eng::Renderer2D& r = m_renderer;
    const float fadeIn = eng::smoothstep(0.0f, 0.5f, static_cast<float>(age));
    const float fadeOut = 1.0f - eng::smoothstep(static_cast<float>(kDuration) - 0.9f, static_cast<float>(kDuration), static_cast<float>(age));
    const float a = fadeIn * fadeOut;
    const float W = r.width();
    const float y = r.height() * 0.26f + (1.0f - fadeIn) * 12.0f;
    r.gradientV({0, y - 30.0f, W, 70.0f}, palette::ink.withAlpha(0.0f), palette::ink.withAlpha(0.55f * a));
    r.gradientV({0, y + 40.0f, W, 90.0f}, palette::ink.withAlpha(0.55f * a), palette::ink.withAlpha(0.0f));
    m_kit.spacedCentered(m_fonts.display, {0, y, W, 56.0f}, upper(m_bannerTitle), 8.0f, palette::brassLight.fade(a));
    m_kit.divider({W * 0.5f, y + 66.0f}, 360.0f * eng::smoothstep(0.1f, 0.9f, static_cast<float>(age)), palette::brass.fade(a));
    r.textCentered(m_fonts.narration, {0, y + 76.0f, W, 22.0f}, m_bannerSubtitle, palette::parchment.fade(a));
}

void App::drawPauseMenu(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    const float W = r.width(), H = r.height();
    const float a = m_kit.anim("pause/fade", 1.0f, 6.0f);
    m_kit.fade(0.4f * a);
    m_hudBlockers.push_back({0, 0, W, H});

    // Dieselbe Dorftafel wie im Hauptmenü
    const Rect board{std::round(std::max(32.0f, W * 0.045f)) - (1.0f - a) * 60.0f, 36.0f, 430.0f, H - 96.0f};
    m_kit.light({board.right() - 10.0f, board.y + 60.0f}, a);
    m_kit.candleLight({board.right() + 10.0f, board.y + 40.0f}, 520.0f, a);
    m_kit.board(board, a);
    const Rect head{board.x + 26.0f, board.y + 30.0f, board.w - 52.0f, 120.0f};
    m_kit.beginTilt(head, 0.02f);
    m_kit.paper(head, 1777u, a);
    m_kit.nail({head.center().x, head.y + 14.0f}, a);
    const char* pauseTitle = L("Atempause", "Respite");
    const float tw = m_fonts.calligraphyMid.measure(pauseTitle);
    r.text(m_fonts.calligraphyMid, {head.center().x - tw * 0.5f, head.y + 64.0f - m_fonts.calligraphyMid.ascent()}, pauseTitle, inkDark.withAlpha(a));
    const std::string when = phaseWithDay(v);
    r.text(m_fonts.script, {head.center().x - m_fonts.script.measure(when) * 0.5f, head.y + 76.0f}, when, inkSoft.withAlpha(a));
    m_kit.endTilt();

    struct Item {
        const char* label;
        int action;
        float angle;
    };
    std::vector<Item> items = {{L("Weiterspielen", "Resume"), 0, -0.025f}, {L("Tagebuch", "Journal"), 1, 0.02f}, {L("Einstellungen", "Settings"), 2, -0.015f}};
    const bool leaveArmed = m_time - m_leaveArmedTime < 3.0;
    const bool abortArmed = m_time - m_abortArmedTime < 3.0;
    if (m_net.isHost() && v.phase != Phase::GameOver)
        items.push_back({abortArmed ? L("Wirklich abbrechen?", "Really abort?") : L("Runde abbrechen", "Abort round"), 3, 0.03f});
    items.push_back({leaveArmed ? L("Wirklich gehen?", "Really leave?") : L("Das Dorf verlassen", "Leave the village"), 4, -0.02f});

    eng::Input& in = m_window.input();
    const int count = static_cast<int>(items.size());
    if (!m_pauseSettings) {
        if (in.pressed(eng::Key::Down)) m_pauseFocus = (m_pauseFocus + 1) % count;
        if (in.pressed(eng::Key::Up)) m_pauseFocus = (m_pauseFocus + count - 1) % count;
    }
    m_pauseFocus = std::clamp(m_pauseFocus, 0, count - 1);
    int activated = -1;
    for (int i = 0; i < count; ++i) {
        const Item& it = items[static_cast<size_t>(i)];
        const Rect rc{board.x + 34.0f + (i % 2 == 0 ? 0.0f : 14.0f), head.bottom() + 34.0f + static_cast<float>(i) * 70.0f, board.w - 82.0f, 56.0f};
        const bool selected = m_pauseFocus == i || (m_pauseSettings && it.action == 2);
        if (m_kit.slip(std::string("pause/slip/") + std::to_string(it.action), rc, it.label, it.angle, selected, 2300u + static_cast<uint32_t>(i) * 13u, a))
            activated = it.action;
        if (m_ui.hovered(rc)) m_pauseFocus = i;
    }
    if (!m_pauseSettings && in.pressed(eng::Key::Enter)) activated = items[static_cast<size_t>(m_pauseFocus)].action;
    switch (activated) {
    case 0:
        m_pauseOpen = false;
        m_pauseSettings = false;
        m_window.setMouseCaptured(true);
        break;
    case 1:
        m_pauseOpen = false;
        m_pauseSettings = false;
        m_journalOpen = true;
        break;
    case 2:
        m_pauseSettings = !m_pauseSettings;
        m_kit.anim("pause/settings/open", 0.0f, 1000.0f);
        break;
    case 3:
        if (abortArmed) m_net.sendReturnToLobby();
        else m_abortArmedTime = m_time;
        break;
    case 4:
        if (leaveArmed) leaveSession();
        else m_leaveArmedTime = m_time;
        break;
    default: break;
    }

    if (m_pauseSettings) {
        const float px = board.right() + std::max(36.0f, W * 0.04f);
        const Rect area{px, 36.0f, std::min(680.0f, W - px - 36.0f), std::min(H - 96.0f, 680.0f)};
        const float open = eng::smoothstep(0.0f, 1.0f, m_kit.anim("pause/settings/open", 1.0f, 3.2f));
        const float sy = 0.04f + 0.96f * open;
        r.pushTransform({1.0f, 0.0f, 0.0f, sy, 0.0f, area.y * (1.0f - sy)});
        if (drawSettings(area)) m_pauseSettings = false;
        r.popTransform();
    }
    drawFooterHints(true);
}
void App::drawGameOver(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    using namespace palette;
    const float W = r.width(), H = r.height();
    const float a = m_kit.anim("over/fade", 1.0f, 1.5f);
    m_kit.fade(0.55f * a);
    m_kit.vignette(1.0f);
    m_kit.embers({0, H * 0.3f, W, H * 0.7f}, 24, 0.6f * a);
    m_hudBlockers.push_back({0, 0, W, H});
    m_kit.light({W * 0.5f, -40.0f}, 1.1f);
    m_kit.candleLight({W * 0.5f, 0.0f}, 800.0f, a);

    std::string title;
    switch (v.winner) {
    case Team::Village: title = L("Das Dorf hat gesiegt", "The village has won"); break;
    case Team::Wolves: title = L("Die Werwölfe haben gesiegt", "The werewolves have won"); break;
    case Team::Lovers: title = L("Die Liebe hat gesiegt", "Love has won"); break;
    case Team::Jester: title = L("Der Narr hat gesiegt", "The jester has won"); break;
    case Team::Killer: title = L("Der Serienmörder hat gesiegt", "The serial killer has won"); break;
    case Team::Arsonist: title = L("Der Brandstifter hat gesiegt", "The arsonist has won"); break;
    case Team::Piper: title = L("Der Flötenspieler hat gesiegt", "The pied piper has won"); break;
    case Team::Angel: title = L("Der Engel hat gesiegt", "The angel has won"); break;
    case Team::WhiteWolf: title = L("Der weiße Werwolf hat gesiegt", "The white werewolf has won"); break;
    case Team::Sectarian: title = L("Der Sektierer hat gesiegt", "The sectarian has won"); break;
    default: title = L("Niemand hat überlebt", "Nobody survived"); break;
    }
    // Kopfblatt
    const Rect head{W * 0.5f - 300.0f, 30.0f - (1.0f - a) * 40.0f, 600.0f, 150.0f};
    m_kit.beginTilt(head, -0.012f);
    m_kit.paper(head, 19000u, a);
    m_kit.nail({head.x + 22.0f, head.y + 16.0f}, a);
    m_kit.nail({head.right() - 22.0f, head.y + 16.0f}, a);
    const char* epilog = L("Epilog", "Epilogue");
    const float ew = m_fonts.calligraphyMid.measure(epilog);
    r.text(m_fonts.calligraphyMid, {head.center().x - ew * 0.5f, head.y + 62.0f - m_fonts.calligraphyMid.ascent()}, epilog, inkDark.withAlpha(a));
    const float tw = m_fonts.scriptLarge.measure(title);
    m_kit.writeText(m_fonts.scriptLarge, {head.center().x - tw * 0.5f, head.y + 70.0f}, title, inkRed.withAlpha(a), eng::smoothstep(0.3f, 1.0f, a));
    const PlayerView* me = v.me();
    const bool won = me && me->winner;
    const char* mine = won ? L("Du hast gewonnen.", "You have won.") : L("Du hast verloren.", "You have lost.");
    r.text(m_fonts.paperItalic, {head.center().x - m_fonts.paperItalic.measure(mine) * 0.5f, head.y + 116.0f}, mine, inkSoft.withAlpha(a));
    m_kit.endTilt();

    // Steckbriefe aller Bewohner
    const int n = static_cast<int>(v.players.size());
    const int cols = std::min(n, W > 1150.0f ? 8 : 6);
    const float cw = 118.0f, ch = 160.0f, gap = 16.0f;
    const int rows = (n + cols - 1) / std::max(1, cols);
    const float gy = head.bottom() + 30.0f;
    for (int i = 0; i < n; ++i) {
        const PlayerView& p = v.players[static_cast<size_t>(i)];
        const int row = i / cols, col = i % cols;
        const int inRow = std::min(cols, n - row * cols);
        const float rowW = static_cast<float>(inRow) * cw + static_cast<float>(inRow - 1) * gap;
        const float delay = 0.15f + static_cast<float>(i) * 0.05f;
        const float ca = eng::smoothstep(delay, delay + 0.35f, a);
        if (ca <= 0.01f) continue;
        const Rect card{W * 0.5f - rowW * 0.5f + static_cast<float>(col) * (cw + gap), gy + static_cast<float>(row) * (ch + gap) - (1.0f - ca) * 20.0f, cw, ch};
        const float angle = (static_cast<float>((i * 37) % 7) - 3.0f) * 0.012f;
        m_kit.beginTilt(card, angle);
        m_kit.paper(card, 20000u + static_cast<uint32_t>(i) * 7u, ca);
        m_kit.nail({card.center().x, card.y + 10.0f}, ca);
        const Color rc = eng::lerp(theme::roleColor(p.role), inkDark, 0.6f);
        r.textCentered(m_fonts.iconLarge, {card.x, card.y + 24.0f, card.w, 50.0f}, roleIcon(p.role), rc.withAlpha(ca));
        r.textCentered(m_fonts.hand, {card.x + 4.0f, card.y + 82.0f, card.w - 8.0f, 24.0f}, m_fonts.hand.ellipsize(p.name, card.w - 12.0f),
                       inkDark.withAlpha(ca));
        r.textCentered(m_fonts.paperItalic, {card.x + 4.0f, card.y + 108.0f, card.w - 8.0f, 20.0f}, m_fonts.paperItalic.ellipsize(roleName(p.role), card.w - 10.0f),
                       inkSoft.withAlpha(ca));
        if (!p.alive) {
            // durchgekreuzt
            m_kit.inkLine({card.x + 14.0f, card.y + 26.0f}, {card.right() - 14.0f, card.bottom() - 20.0f}, 2.2f, inkRed.withAlpha(0.75f * ca), 1.0f, 3u + static_cast<uint32_t>(i));
            m_kit.inkLine({card.right() - 14.0f, card.y + 26.0f}, {card.x + 14.0f, card.bottom() - 20.0f}, 2.2f, inkRed.withAlpha(0.75f * ca), 1.0f, 9u + static_cast<uint32_t>(i));
        }
        if (p.winner) m_kit.seal({card.right() - 18.0f, card.bottom() - 18.0f}, 14.0f, wax, "★", ca, static_cast<uint32_t>(i));
        m_kit.endTilt();
    }

    const float by = std::min(H - 74.0f, gy + static_cast<float>(rows) * (ch + gap) + 16.0f);
    const Rect slip{W * 0.5f - 260.0f, by, 520.0f, 58.0f};
    m_kit.beginTilt(slip, 0.01f);
    m_kit.paper(slip, 21000u, a);
    if (m_net.isHost()) {
        if (m_kit.inkButton("over/lobby", {slip.x + 16.0f, slip.y + 7.0f, 270.0f, 44.0f}, L("Neue Nacht im Dorf", "Another night in the village"), true))
            m_net.sendReturnToLobby();
        if (m_kit.inkButton("over/leave", {slip.right() - 196.0f, slip.y + 10.0f, 180.0f, 38.0f}, L("Dorf verlassen", "Leave village"))) leaveSession();
    } else if (m_kit.inkButton("over/leave", {slip.center().x - 90.0f, slip.y + 10.0f, 180.0f, 38.0f}, L("Dorf verlassen", "Leave village"))) {
        leaveSession();
    }
    m_kit.endTilt();
}
void App::drawMatchHud() {
    const GameView& v = m_net.view();
    eng::Renderer2D& r = m_renderer;
    m_hudBlockers.clear();
    const float W = r.width(), H = r.height();
    const bool over = v.phase == Phase::GameOver;

    // Sterben färbt das Bild kurz rot
    if (m_time - m_deathTime < 5.0 && !over) {
        const float a = eng::clamp01(static_cast<float>(5.0 - (m_time - m_deathTime)) / 1.5f);
        m_kit.vignette(1.2f * a);
        r.gradientV({0, H * 0.7f, W, H * 0.3f}, palette::blood.withAlpha(0.0f), palette::blood.withAlpha(0.18f * a));
    } else {
        m_kit.vignette(0.35f);
    }

    drawNameTags(v);
    if (m_window.isMouseCaptured()) {
        r.circle({W * 0.5f, H * 0.5f}, 2.2f, palette::parchment.withAlpha(0.85f));
        r.circleOutline({W * 0.5f, H * 0.5f}, 8.0f, 1.0f, palette::parchment.withAlpha(0.2f));
    }
    if (!over) {
        drawPhaseBox(v);
        drawMinimap({0, 0, W, H}, v);
    }
    drawBanner();
    if (!over) {
        drawRoleCard({24.0f, 0, 320.0f, H - 24.0f}, v);
        drawPrompts(v);
        drawVotePanel(v);
        drawSubtitles();
    }

    // Chat tritt in den Hintergrund, wenn nichts passiert
    const Rect chat{W - 404.0f, H - 294.0f, 380.0f, 270.0f};
    m_hudBlockers.push_back(chat);
    const bool chatActive = m_ui.isFocused("chat") || !m_config.fadeChat || m_time - m_lastChatActivity < 10.0 ||
                            !m_window.isMouseCaptured() || over;
    const float chatA = m_kit.anim("hud/chat", chatActive ? 1.0f : 0.25f, 2.5f);
    if (chatA > 0.3f) drawChat(chat, true);
    else m_kit.hint({chat.right() - 140.0f, chat.bottom() - 24.0f}, "Enter", L("Sprechen", "Talk"), 0.6f);

    if (m_time - m_deathTime < 5.0 && !over) {
        const float a = eng::clamp01(static_cast<float>(5.0 - (m_time - m_deathTime)) / 1.5f);
        m_kit.spacedCentered(m_fonts.display, {0, H * 0.4f, W, 60.0f}, L("DU BIST GESTORBEN", "YOU HAVE DIED"), 8.0f, palette::blood.withAlpha(a));
        r.textCentered(m_fonts.subtitle, {0, H * 0.4f + 70.0f, W, 26.0f},
                       L("Als Geist wanderst du ungesehen durchs Dorf.", "As a ghost you wander the village unseen."), palette::parchment.withAlpha(a));
    }
    if (m_config.showFps)
        r.text(m_fonts.small, {12.0f, 10.0f}, std::format("{:.0f} FPS", 1.0f / std::max(m_dt, 0.001f)), palette::ash);

    if (!m_revealDone) drawRoleReveal(v);
    else if (over) drawGameOver(v);
    else if (m_journalOpen) drawJournal(v);
    else if (m_pauseOpen) drawPauseMenu(v);
    else if (!m_window.isMouseCaptured() && !m_ui.isFocused("chat")) {
        const float hw = 380.0f;
        const Rect hint{W * 0.5f - hw * 0.5f, H * 0.5f + 40.0f, hw, 34.0f};
        r.gradientH({hint.x, hint.y, hint.w * 0.5f, hint.h}, palette::ink.withAlpha(0.0f), palette::ink.withAlpha(0.75f));
        r.gradientH({hint.center().x, hint.y, hint.w * 0.5f, hint.h}, palette::ink.withAlpha(0.75f), palette::ink.withAlpha(0.0f));
        r.textCentered(m_fonts.small, hint, L("Klicke ins Bild, um weiterzuspielen  ·  Esc: Pause", "Click into the picture to resume  ·  Esc: pause"),
                       palette::parchment);
    }
    if (!m_pauseOpen && !over) m_kit.anim("pause/fade", 0.0f, 100.0f);
    if (!m_journalOpen) m_kit.anim("journal/open", 0.0f, 100.0f);
    if (!over) m_kit.anim("over/fade", 0.0f, 100.0f);
}

} // namespace ww
