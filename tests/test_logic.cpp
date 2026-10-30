// Tests des Echtzeit-Regelwerks (ohne Netzwerk).

#include "TestFramework.h"

#include "logic/Match.h"
#include "server/Bot.h"
#include "world/Movement.h"
#include "world/World.h"

#include <algorithm>
#include <chrono>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace ww;

namespace {

constexpr Role W = Role::Werewolf;
constexpr Role V = Role::Villager;

const World& testWorld() {
    static World world = [] {
        World w;
        w.generate(4242u);
        return w;
    }();
    return world;
}

Settings quickSettings() {
    Settings s;
    s.daySeconds = 60;
    s.discussionSeconds = 30;
    s.voteSeconds = 30;
    s.nightSeconds = 60;
    s.captain = false; // Hauptmannwahl nur im eigenen Test
    return s;
}

Match makeMatch(const std::vector<Role>& roles, Settings settings = quickSettings()) {
    std::vector<Match::Seat> seats;
    for (size_t i = 0; i < roles.size(); ++i) seats.push_back({static_cast<uint8_t>(i), "P" + std::to_string(i), false});
    Match m;
    Text error;
    const bool ok = m.startWithRoles(seats, roles, settings, testWorld(), 42, error);
    CHECK(ok);
    if (!ok) std::printf("    %s\n", error.de.c_str());
    return m;
}

bool alive(const Match& m, uint8_t id) { return m.find(id) && m.find(id)->alive; }

void run(Match& m, float seconds) {
    for (float t = 0; t < seconds; t += 0.1f) m.update(0.1f);
}

void runUntil(Match& m, Phase target, float maxSeconds = 900.0f) {
    for (float t = 0; t < maxSeconds && m.phase() != target; t += 0.1f) m.update(0.1f);
}

// Lässt alle Lebenden gegen ein Ziel stimmen (außer dem Ziel selbst) und wartet auf das Urteil
void voteOut(Match& m, uint8_t target) {
    runUntil(m, Phase::Vote);
    for (const auto& p : m.players())
        if (p.alive && p.id != target) m.interact(p.id, Interaction::Vote, target);
    runUntil(m, Phase::Execution);
}

// Stellt zwei Spieler nebeneinander (auf freien Platz neben dem Feuer)
void placeNear(Match& m, uint8_t a, uint8_t b, float distance = 1.5f) {
    const Vec3 base = testWorld().ground(4.0f, 9.0f);
    m.teleport(a, base);
    m.teleport(b, base + Vec3{distance, 0, 0});
}

void becomeWolf(Match& m, uint8_t id) {
    CHECK(m.interact(id, Interaction::Transform, 1).empty());
    run(m, kTransformSeconds + 0.3f);
    CHECK(m.find(id)->wolfForm());
}

} // namespace

TEST(RollenverteilungAutomatisch) {
    Settings s;
    Text err;
    auto roles = buildRoleList(s, 8, &err);
    CHECK_EQ(roles.size(), 8u);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Werewolf), 2);
    CHECK(buildRoleList(s, 3, &err).empty());
    // Zu viele Sonderrollen: es werden nur so viele vergeben, wie Plätze frei sind
    for (int i = 0; i < static_cast<int>(Role::Count); ++i) s.set(static_cast<Role>(i), true);
    roles = buildRoleList(s, 5, &err);
    CHECK_EQ(roles.size(), 5u);
    // Besondere Wölfe ersetzen normale Werwölfe
    roles = buildRoleList(s, 8, &err);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::AlphaWolf), 1);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::WolfCub), 1);
    CHECK_EQ(std::count_if(roles.begin(), roles.end(), isWolfRole), 2);
    s.wolves = 4;
    CHECK(buildRoleList(s, 8, &err).empty());
    // Einstellungen überleben die Übertragung
    Settings a;
    a.set(Role::Arsonist, true);
    a.set(Role::Seer, false);
    eng::ByteWriter w;
    a.write(w);
    eng::ByteReader r(w.data());
    Settings b;
    CHECK(b.read(r));
    CHECK(b.has(Role::Arsonist) && !b.has(Role::Seer) && b.has(Role::Witch));
    CHECK(b.captain && b.revealRoles);
    // Fehler gibt es in beiden Sprachen
    CHECK(err.de.find("Werwölfe") != std::string::npos);
    CHECK(err.en.find("werewolves") != std::string::npos);
    // Geschwister gibt es nur vollständig
    Settings fam;
    fam.roles = 0;
    fam.set(Role::Brother, true);
    fam.set(Role::Sister, true);
    roles = buildRoleList(fam, 7, &err);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Sister), 2);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Brother), 3);
    roles = buildRoleList(fam, 5, &err);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Brother), 0); // kein Platz für alle drei
}

TEST(WeltIstDeterministischUndBegehbar) {
    World a, b;
    a.generate(7u);
    b.generate(7u);
    CHECK_EQ(a.trees().size(), b.trees().size());
    CHECK_EQ(a.houses().size(), 13u); // zwölf Wohnhäuser und die Kapelle
    CHECK(a.height(10.0f, 3.0f) == b.height(10.0f, 3.0f));
    // Spawnpunkte und Sitzplätze sind frei
    for (int i = 0; i < 16; ++i) CHECK(!a.blocked({a.spawnPoint(i, 16).x, a.spawnPoint(i, 16).z}, kPlayerRadius));
    // Häuser sind begehbar: drinnen ist Platz, die Wände blockieren
    const House& h = a.houses()[0];
    const Vec2 center{h.position.x, h.position.z};
    CHECK_EQ(a.houseAt(center), 0);
    CHECK(!a.blocked(a.houseToWorld(0, {-h.width * 0.15f, h.depth * 0.25f}), kPlayerRadius));
    CHECK(a.blocked(a.houseToWorld(0, {0, -h.depth * 0.5f + 0.1f}), kPlayerRadius)); // Rückwand
    CHECK(a.height(center.x, center.y) == h.floorY);
    // Durch die offene Tür kommt man hinein, durch die geschlossene nicht
    const Vec2 outside{h.door.x, h.door.z}, inside{h.doorInside.x, h.doorInside.z};
    CHECK(!a.lineBlocked(outside, inside, 0));
    CHECK(a.lineBlocked(outside, inside, 1u));
    CHECK(a.lineBlocked(outside, center + (center - outside), 0)); // durch die Rückwand
    // Mit geschlossener Tür schiebt die Kollision aus der Türöffnung
    const Vec2 doorway = a.doorCenter(0);
    CHECK(!a.blocked(doorway, kPlayerRadius, 0));
    CHECK(a.blocked(doorway, kPlayerRadius, 1u));
}

TEST(KapelleIstBegehbar) {
    const World& w = testWorld();
    const int c = w.chapelIndex();
    CHECK(c >= 0);
    if (c < 0) return;
    const House& h = w.houses()[static_cast<size_t>(c)];
    CHECK(h.chapel);
    // Durch das offene Portal hinein, Wände, Bänke und Altar halten auf
    const Vec2 outside{h.door.x, h.door.z}, inside{h.doorInside.x, h.doorInside.z};
    CHECK(!w.lineBlocked(outside, inside, 0));
    CHECK(w.lineBlocked(outside, inside, 1u << c));
    CHECK_EQ(w.houseAt(inside), c);
    CHECK(!w.blocked(outside, kPlayerRadius));
    CHECK(!w.blocked(inside, kPlayerRadius));
    CHECK(!w.blocked(w.houseToWorld(c, {0.0f, 0.5f}), kPlayerRadius)); // Mittelgang
    CHECK(w.blocked(w.houseToWorld(c, {1.95f, chapel::pewZ(2)}), kPlayerRadius));
    CHECK(w.blocked(w.houseToWorld(c, chapel::kAltar), kPlayerRadius));
    CHECK(w.lineBlocked(inside, w.houseToWorld(c, {0.0f, -h.depth}), 0)); // Rückwand und Turm
    // Der Altarraum liegt eine Stufe höher
    const Vec2 nave = w.houseToWorld(c, {0.0f, 2.0f}), chancel = w.houseToWorld(c, {0.0f, -4.5f});
    CHECK(std::fabs(w.height(nave.x, nave.y) - h.floorY) < 0.001f);
    CHECK(std::fabs(w.height(chancel.x, chancel.y) - (h.floorY + chapel::kChancelStep)) < 0.001f);
    // Über das Wegenetz kommt man vom Eingang in eine Bankreihe, in den Seitengang und hinter den Altar
    const Vec2 goals[] = {{-2.4f, chapel::pewZ(3) + chapel::kPewSpacing * 0.5f}, {3.45f, -1.0f}, {0.0f, -6.5f}};
    for (const Vec2& local : goals) {
        const Vec2 goal = w.houseToWorld(c, local);
        Mover mover;
        mover.position = w.ground(inside.x, inside.y);
        bool reached = false;
        for (int step = 0; step < 900 && !reached; ++step) {
            const Vec2 pos{mover.position.x, mover.position.z};
            const Vec2 d = w.routeInside(c, pos, goal) - pos;
            mover.move(d.length() > 0.05f ? d.normalized() : Vec2{0, 0}, kWalkSpeed, 1.0f / 30.0f, w, true, 0);
            reached = (Vec2{mover.position.x, mover.position.z} - goal).length() < 0.5f;
        }
        CHECK(reached);
    }
}

TEST(KapelleGehoertNiemandem) {
    std::vector<Role> roles(16, V);
    roles[0] = roles[1] = roles[2] = W;
    Match m = makeMatch(roles);
    const int c = testWorld().chapelIndex();
    for (const Match::Player& p : m.players()) CHECK(p.house != kNoPlayer && p.house != c);
    CHECK_EQ(m.doors().size(), testWorld().houses().size());
    CHECK(m.houseLabel(c).de == "die Kapelle");
    CHECK(m.houseLabel(c).en == "the chapel");
    // Wer sich nachts in die Kapelle flüchtet, kann sie wie ein Haus verriegeln
    runUntil(m, Phase::Night);
    const House& h = testWorld().houses()[static_cast<size_t>(c)];
    m.teleport(5, h.doorInside);
    CHECK(m.interact(5, Interaction::DoorToggle, static_cast<uint8_t>(c)).empty());
    CHECK(m.doors()[static_cast<size_t>(c)].flags & DoorLocked);
}

TEST(StartUndBewegung) {
    Match m = makeMatch({W, V, V, V, V});
    CHECK(m.phase() == Phase::Day);
    CHECK_EQ(m.day(), 1);
    CHECK_EQ(m.takeCorrections().size(), 5u); // alle bekommen ihre Startposition
    const Vec3 start = m.find(1)->position;
    m.update(0.2f);
    m.movePlayer(1, start.x + 0.5f, start.z, 0.0f, 0);
    CHECK(distanceXZ(m.find(1)->position, start + Vec3{0.5f, 0, 0}) < 0.01f);
    m.update(0.1f);
    m.movePlayer(1, start.x + 40.0f, start.z, 0.0f, 0); // Teleport-Versuch
    CHECK(distanceXZ(m.find(1)->position, start) < 1.0f);
    const auto corrections = m.takeCorrections();
    CHECK(std::find(corrections.begin(), corrections.end(), 1) != corrections.end());
}

TEST(WerwolfJagtNachts) {
    Match m = makeMatch({W, V, V, V, V});
    CHECK(!m.interact(0, Interaction::Transform, 1).empty()); // tagsüber nicht
    runUntil(m, Phase::Night);
    CHECK(m.phase() == Phase::Night);
    CHECK(!m.interact(0, Interaction::Attack, 1).empty()); // erst verwandeln
    becomeWolf(m, 0);
    m.teleport(0, testWorld().ground(4.0f, 9.0f));
    m.teleport(1, testWorld().ground(-20.0f, -5.0f));
    CHECK(!m.interact(0, Interaction::Attack, 1).empty()); // zu weit
    placeNear(m, 0, 1);
    CHECK(m.interact(0, Interaction::Attack, 1).empty());
    CHECK(!alive(m, 1));
    CHECK_EQ(m.packKillsLeft(), 0);
    run(m, 2.0f);
    placeNear(m, 0, 2);
    CHECK(!m.interact(0, Interaction::Attack, 2).empty()); // satt
    CHECK(alive(m, 2));
    // Tote liegen als Leiche, Geister sehen nur Tote
    const auto ents = m.entitiesFor(3);
    CHECK(std::any_of(ents.begin(), ents.end(), [](const EntityState& e) { return e.id == 1 && e.kind == EntityCorpse; }));
    CHECK(std::none_of(ents.begin(), ents.end(), [](const EntityState& e) { return e.kind == EntityGhost; }));
    const auto ghostView = m.entitiesFor(1);
    CHECK(std::any_of(ghostView.begin(), ghostView.end(), [](const EntityState& e) { return e.id == 1 && e.kind == EntityGhost; }));
    // Morgens wird verkündet und verwandelt zurück
    m.takeNotices();
    runUntil(m, Phase::Morning);
    const auto notices = m.takeNotices();
    CHECK(std::any_of(notices.begin(), notices.end(), [](const Notice& n) { return n.kind == NoticeKind::Death; }));
    CHECK(!m.find(0)->wolfForm());
    runUntil(m, Phase::Day);
    CHECK_EQ(m.day(), 2);
}

TEST(BeschuetzerBlockiertAngriff) {
    Match m = makeMatch({W, Role::Protector, V, V, V});
    runUntil(m, Phase::Night);
    placeNear(m, 1, 2);
    CHECK(m.interact(1, Interaction::Protect, 2).empty());
    CHECK(!m.interact(1, Interaction::Protect, 3).empty()); // nur einmal pro Nacht
    becomeWolf(m, 0);
    placeNear(m, 0, 2);
    CHECK(m.interact(0, Interaction::Attack, 2).empty());
    CHECK(alive(m, 2));
    CHECK_EQ(m.packKillsLeft(), 1);
}

TEST(SeherinBrauchtSichtUndReichweite) {
    Match m = makeMatch({W, Role::Seer, V, V, V});
    runUntil(m, Phase::Night);
    m.teleport(1, testWorld().ground(4.0f, 9.0f));
    m.teleport(0, testWorld().ground(-30.0f, 10.0f));
    CHECK(!m.interact(1, Interaction::Inspect, 0).empty());
    placeNear(m, 1, 0, 6.0f);
    CHECK(m.interact(1, Interaction::Inspect, 0).empty());
    CHECK(m.viewFor(1).players[0].role == Role::Werewolf);
    CHECK(!m.interact(1, Interaction::Inspect, 2).empty());
}

TEST(HexeHeiltUndVergiftet) {
    Match m = makeMatch({W, Role::Witch, V, V, V, V});
    runUntil(m, Phase::Night);
    becomeWolf(m, 0);
    placeNear(m, 0, 2);
    m.interact(0, Interaction::Attack, 2);
    CHECK(!alive(m, 2));
    m.teleport(1, m.find(2)->deathPosition + Vec3{1.0f, 0, 0});
    CHECK(m.interact(1, Interaction::Heal, 2).empty());
    CHECK(alive(m, 2));
    CHECK(!m.viewFor(1).healAvailable);
    placeNear(m, 1, 0);
    CHECK(m.interact(1, Interaction::Poison, 0).empty());
    CHECK(!alive(m, 0));
    CHECK(m.phase() == Phase::GameOver);
    CHECK(m.winner() == Team::Village);
}

TEST(JaegerLetzterSchuss) {
    Match m = makeMatch({W, Role::Hunter, V, V, V});
    runUntil(m, Phase::Night);
    becomeWolf(m, 0);
    placeNear(m, 0, 1);
    m.interact(0, Interaction::Attack, 1);
    CHECK(!alive(m, 1));
    CHECK(m.viewFor(1).can(AbilityHunterShot));
    CHECK(m.interact(1, Interaction::HunterShot, 0).empty());
    CHECK(!alive(m, 0));
    CHECK(m.phase() == Phase::GameOver);
}

TEST(VerliebteSterbenGemeinsam) {
    Match m = makeMatch({W, Role::Cupid, V, V, V, V});
    runUntil(m, Phase::Night);
    placeNear(m, 1, 2);
    CHECK(m.interact(1, Interaction::CupidPick, 2).empty());
    placeNear(m, 1, 3);
    CHECK(m.interact(1, Interaction::CupidPick, 3).empty());
    CHECK_EQ(m.find(2)->lover, 3);
    becomeWolf(m, 0);
    placeNear(m, 0, 2);
    m.interact(0, Interaction::Attack, 2);
    CHECK(!alive(m, 3));
}

TEST(GlockeVersammlungUndAbstimmung) {
    Match m = makeMatch({W, V, V, V, V, V});
    runUntil(m, Phase::Night);
    runUntil(m, Phase::Day);
    CHECK_EQ(m.day(), 2);
    m.teleport(2, testWorld().bell() + Vec3{1.5f, 0, 0});
    CHECK(!m.interact(2, Interaction::RingBell, 0).empty()); // zu früh
    run(m, 16.0f);
    CHECK(m.interact(2, Interaction::RingBell, 0).empty());
    CHECK(m.phase() == Phase::Assembly);
    for (const auto& p : m.players())
        if (p.alive) CHECK(distanceXZ(p.position, testWorld().bonfire()) < 9.5f);
    // Versammlung: alle hören alle
    m.teleport(3, testWorld().ground(-40.0f, 5.0f));
    CHECK(m.chatChannel(3, false) == ChatChannel::Global);
    m.skipPhase();
    m.update(0.1f);
    CHECK(m.phase() == Phase::Vote);
    for (uint8_t id = 1; id <= 5; ++id) CHECK(m.interact(id, Interaction::Vote, 0).empty());
    CHECK(!m.interact(0, Interaction::Vote, 0).empty()); // nicht für sich selbst
    m.interact(0, Interaction::Vote, 1);
    CHECK(m.timeLeft() <= 4.01f);
    runUntil(m, Phase::GameOver, 30.0f);
    CHECK(!alive(m, 0));
    CHECK(m.winner() == Team::Village);
}

TEST(NaeheChat) {
    Match m = makeMatch({W, V, V, V, V});
    m.teleport(1, testWorld().ground(5.0f, 9.0f));
    m.teleport(2, testWorld().ground(5.0f, 12.0f));
    m.teleport(3, testWorld().ground(-40.0f, 5.0f));
    CHECK(m.chatChannel(1, false) == ChatChannel::Nearby);
    CHECK(m.canHear(2, 1, ChatChannel::Nearby));
    CHECK(!m.canHear(3, 1, ChatChannel::Nearby));
    CHECK(m.chatChannel(0, true) == ChatChannel::Wolves);
    CHECK(m.chatChannel(1, true) == ChatChannel::Nearby); // kein Wolf
    CHECK(!m.canHear(1, 0, ChatChannel::Wolves));
}

TEST(SerialisierungAnsichtUndPositionen) {
    Match m = makeMatch({W, Role::Witch, Role::Seer, V, V});
    GameView v = m.viewFor(0);
    eng::ByteWriter w;
    v.write(w);
    eng::ByteReader r(w.data());
    GameView copy;
    CHECK(copy.read(r));
    CHECK(r.atEnd());
    CHECK(copy.yourRole == Role::Werewolf);
    CHECK_EQ(copy.worldSeed, testWorld().seed());
    CHECK_EQ(copy.players.size(), 5u);

    const auto ents = m.entitiesFor(0);
    eng::ByteWriter we;
    writeEntities(we, ents);
    eng::ByteReader re(we.data());
    std::vector<EntityState> back;
    CHECK(readEntities(re, back));
    CHECK_EQ(back.size(), ents.size());
    CHECK(std::fabs(back[2].x - ents[2].x) < 0.001f);

    eng::ByteWriter wd;
    writeDoors(wd, m.doors());
    eng::ByteReader rd(wd.data());
    std::vector<DoorState> doors;
    CHECK(readDoors(rd, doors));
    CHECK_EQ(doors.size(), m.doors().size());
    CHECK(m.viewFor(0).players[0].house != kNoPlayer);
}

TEST(TuerVerriegelnUndAufbrechen) {
    Match m = makeMatch({W, V, V, V, V});
    const uint8_t home = m.find(1)->house;
    const House& h = testWorld().houses()[home];
    const Vec3 inside = h.doorInside, outside = h.door;
    runUntil(m, Phase::Night);
    m.teleport(1, inside);
    // Von innen geschlossen = verriegelt
    CHECK(m.interact(1, Interaction::DoorToggle, home).empty());
    CHECK(m.doors()[home].flags & DoorLocked);
    CHECK(m.closedDoors() & (1u << home));
    // Niemand kommt hindurch, auch nicht durch Vorhersage beim Client
    m.update(0.5f);
    m.takeCorrections();
    m.movePlayer(1, outside.x, outside.z, 0.0f, 0);
    CHECK(distanceXZ(m.find(1)->position, inside) < 0.01f);
    CHECK(!m.takeCorrections().empty());
    // Der Wolf steht vor der Tür: angreifen geht nicht, öffnen auch nicht
    becomeWolf(m, 0);
    m.teleport(0, outside);
    CHECK(!m.interact(0, Interaction::Attack, 1).empty());
    CHECK(!m.interact(0, Interaction::DoorToggle, home).empty());
    CHECK(!m.interact(2, Interaction::DoorBreak, home).empty()); // Menschen können das nicht
    m.takeNotices();
    int hits = 0;
    while (!(m.doors()[home].flags & DoorBroken) && hits < 10) {
        CHECK(m.interact(0, Interaction::DoorBreak, home).empty());
        ++hits;
        run(m, 1.1f);
    }
    CHECK_EQ(hits, 5);
    const auto notices = m.takeNotices();
    CHECK(std::any_of(notices.begin(), notices.end(), [](const Notice& n) { return n.audience == Notice::Audience::Area; }));
    CHECK(m.interact(0, Interaction::Attack, 1).empty());
    CHECK(!alive(m, 1));
    // Am Morgen wird die Tür wieder eingesetzt
    runUntil(m, Phase::Day);
    CHECK(!(m.doors()[home].flags & DoorBroken));
}

TEST(SchmiedVerstaerktTuer) {
    Match m = makeMatch({W, Role::Blacksmith, V, V, V});
    const uint8_t home = m.find(2)->house;
    const House& h = testWorld().houses()[home];
    m.teleport(1, h.door);
    CHECK(m.interact(1, Interaction::Reinforce, home).empty());
    CHECK(!m.interact(1, Interaction::Reinforce, home).empty()); // einmal am Tag
    CHECK(m.doors()[home].flags & DoorReinforced);
    runUntil(m, Phase::Night);
    m.teleport(2, h.doorInside);
    CHECK(m.interact(2, Interaction::DoorToggle, home).empty());
    becomeWolf(m, 0);
    m.teleport(0, h.door);
    int hits = 0;
    while (!(m.doors()[home].flags & DoorBroken) && hits < 20) {
        m.interact(0, Interaction::DoorBreak, home);
        ++hits;
        run(m, 1.1f);
    }
    CHECK_EQ(hits, 10);
}

TEST(DorfaeltesterUndVerfluchter) {
    Match m = makeMatch({W, Role::Elder, Role::Cursed, V, V, V, V});
    runUntil(m, Phase::Night);
    becomeWolf(m, 0);
    placeNear(m, 0, 1);
    CHECK(m.viewFor(1).shieldLeft);
    CHECK(m.interact(0, Interaction::Attack, 1).empty());
    CHECK(alive(m, 1));
    CHECK(!m.viewFor(1).shieldLeft);
    CHECK_EQ(m.packKillsLeft(), 0);
    runUntil(m, Phase::Day);
    runUntil(m, Phase::Night); // nächste Nacht
    becomeWolf(m, 0);
    placeNear(m, 0, 2);
    CHECK(m.interact(0, Interaction::Attack, 2).empty());
    CHECK(alive(m, 2));
    CHECK(m.find(2)->role == Role::Werewolf);
    CHECK(m.find(2)->startRole == Role::Cursed);
    CHECK(m.viewFor(0).players[2].role == Role::Werewolf); // das Rudel kennt den Neuen
}

TEST(UrwolfInfiziert) {
    Match m = makeMatch({Role::AlphaWolf, V, V, V, V, V});
    runUntil(m, Phase::Night);
    becomeWolf(m, 0);
    placeNear(m, 0, 1);
    CHECK(m.viewFor(0).can(AbilityInfect));
    CHECK(m.interact(0, Interaction::Infect, 1).empty());
    CHECK(alive(m, 1));
    CHECK(isWolfRole(m.find(1)->role));
    CHECK(!m.viewFor(0).can(AbilityInfect));
}

TEST(WolfsjungesMachtRasend) {
    Match m = makeMatch({Role::WolfCub, W, V, V, V, V, V});
    runUntil(m, Phase::Night);
    runUntil(m, Phase::Vote);
    for (uint8_t id = 1; id <= 6; ++id) m.interact(id, Interaction::Vote, 0);
    runUntil(m, Phase::Night);
    CHECK(!alive(m, 0));
    CHECK_EQ(m.packKillsLeft(), 2);
}

TEST(BuergermeisterDorfdeppUndNarr) {
    {
        Match m = makeMatch({W, Role::Mayor, Role::Idiot, V, V, V});
        CHECK(m.viewFor(3).players[1].role == Role::Mayor); // öffentlich bekannt
        runUntil(m, Phase::Night);
        runUntil(m, Phase::Vote);
        // Bürgermeister (2 Stimmen) gegen den Dorfdepp, zwei andere gegen Spieler 3
        m.interact(1, Interaction::Vote, 2);
        m.interact(4, Interaction::Vote, 3);
        m.interact(5, Interaction::Vote, 3);
        m.interact(3, Interaction::Vote, 2);
        runUntil(m, Phase::Execution);
        CHECK(alive(m, 2)); // der Dorfdepp wird begnadigt
        CHECK(m.find(2)->idiotRevealed);
        CHECK(m.viewFor(4).players[2].role == Role::Idiot);
        runUntil(m, Phase::Night);
        runUntil(m, Phase::Vote);
        CHECK(!m.viewFor(2).can(AbilityVote));
        CHECK(!m.interact(2, Interaction::Vote, 3).empty());
    }
    {
        Match m = makeMatch({W, Role::Jester, V, V, V});
        runUntil(m, Phase::Night);
        runUntil(m, Phase::Vote);
        for (int id : {0, 2, 3, 4}) m.interact(static_cast<uint8_t>(id), Interaction::Vote, 1);
        runUntil(m, Phase::GameOver, 60.0f);
        CHECK(m.winner() == Team::Jester);
        CHECK(m.isWinner(1));
        CHECK(!m.isWinner(2));
    }
}

TEST(PfarrerUndSerienmoerder) {
    {
        Match m = makeMatch({W, Role::Priest, V, V, V});
        placeNear(m, 1, 2);
        CHECK(m.interact(1, Interaction::HolyWater, 2).empty());
        CHECK(alive(m, 2)); // kein Werwolf
        CHECK(!m.interact(1, Interaction::HolyWater, 0).empty()); // nur einmal
    }
    {
        Match m = makeMatch({W, Role::Priest, V, V, V});
        placeNear(m, 1, 0);
        CHECK(m.interact(1, Interaction::HolyWater, 0).empty());
        CHECK(!alive(m, 0));
        CHECK(m.winner() == Team::Village);
    }
    {
        Match m = makeMatch({W, Role::SerialKiller, V, V, V, V});
        runUntil(m, Phase::Night);
        becomeWolf(m, 0);
        placeNear(m, 0, 1);
        CHECK(m.interact(0, Interaction::Attack, 1).empty());
        CHECK(alive(m, 1)); // Werwölfe können dem Mörder nichts anhaben
        CHECK_EQ(m.packKillsLeft(), 1);
        run(m, 3.5f);
        placeNear(m, 1, 2);
        CHECK(m.interact(1, Interaction::Stab, 2).empty());
        CHECK(!alive(m, 2));
        CHECK(!m.interact(1, Interaction::Stab, 3).empty()); // einmal pro Nacht
        CHECK(m.find(2)->deathCause == DeathCause::Knife);
    }
}

TEST(BrandstifterZuendetHaeuserAn) {
    Match m = makeMatch({W, Role::Arsonist, V, V, V, V});
    const uint8_t home = m.find(2)->house;
    const House& h = testWorld().houses()[home];
    CHECK(!m.interact(1, Interaction::Douse, home).empty()); // nicht am Tag
    runUntil(m, Phase::Night);
    m.teleport(1, h.door);
    CHECK(m.interact(1, Interaction::Douse, home).empty());
    CHECK(m.viewFor(1).doused & (1u << home));
    CHECK(!m.interact(1, Interaction::Ignite, 0).empty()); // heute Nacht schon gehandelt
    runUntil(m, Phase::Day);
    runUntil(m, Phase::Night);
    m.teleport(2, testWorld().ground(h.position.x, h.position.z));
    CHECK(m.interact(1, Interaction::Ignite, 0).empty());
    CHECK(m.doors()[home].flags & DoorBurning);
    run(m, 4.0f);
    CHECK(!alive(m, 2));
    CHECK(m.find(2)->deathCause == DeathCause::Fire);
    run(m, 10.0f);
    CHECK(m.doors()[home].flags & DoorBurnt);
    CHECK(!(m.closedDoors() & (1u << home)));
}

TEST(MaedchenMediumUndFaehrtenleser) {
    Match m = makeMatch({W, Role::LittleGirl, Role::Medium, Role::Tracker, V, V, V});
    CHECK(!m.canReadChannel(1, ChatChannel::Wolves)); // tagsüber nicht
    runUntil(m, Phase::Night);
    CHECK(m.canHear(1, 0, ChatChannel::Wolves));
    CHECK(m.hidesSpeaker(1, ChatChannel::Wolves));
    CHECK(!m.hidesSpeaker(0, ChatChannel::Wolves));
    // Das Medium spricht nachts mit den Toten
    CHECK(m.chatChannel(2, true) == ChatChannel::Dead);
    CHECK(m.chatChannel(4, true) == ChatChannel::Nearby);
    // Der Fährtenleser folgt dem Wolf zu seinem Opfer
    placeNear(m, 3, 0, 5.0f);
    CHECK(m.interact(3, Interaction::Track, 0).empty());
    CHECK_EQ(m.viewFor(3).tracked, 0);
    becomeWolf(m, 0);
    placeNear(m, 0, 4);
    CHECK(m.interact(0, Interaction::Attack, 4).empty());
    CHECK(m.canHear(2, 4, ChatChannel::Dead)); // das Medium hört den neuen Geist
    m.takeNotices();
    runUntil(m, Phase::Morning);
    const auto notices = m.takeNotices();
    CHECK(std::any_of(notices.begin(), notices.end(), [](const Notice& n) {
        return n.player == 3 && n.text.de.find("Leiche von P4") != std::string::npos && n.text.en.find("body of P4") != std::string::npos;
    }));
}
TEST(HauptmannwahlUndNachfolger) {
    Settings s = quickSettings();
    s.captain = true;
    Match m = makeMatch({W, V, V, V, V}, s);
    runUntil(m, Phase::Vote);
    CHECK(m.election());
    CHECK(m.viewFor(1).election);
    for (int id : {0, 1, 3, 4}) m.interact(static_cast<uint8_t>(id), Interaction::Vote, 2);
    runUntil(m, Phase::Night);
    CHECK_EQ(m.captain(), 2);
    CHECK(m.viewFor(3).players[2].captain);
    becomeWolf(m, 0);
    placeNear(m, 0, 2);
    CHECK(m.interact(0, Interaction::Attack, 2).empty());
    CHECK(m.viewFor(2).can(AbilityNameSuccessor));
    CHECK(m.interact(2, Interaction::NameSuccessor, 4).empty());
    CHECK_EQ(m.captain(), 4);
}

TEST(DiebUndSuendenbock) {
    {
        Match m = makeMatch({W, Role::Thief, Role::Seer, V, V});
        runUntil(m, Phase::Night);
        placeNear(m, 1, 2);
        CHECK(m.interact(1, Interaction::Steal, 2).empty());
        CHECK(m.find(1)->role == Role::Seer);
        CHECK(m.find(2)->role == Role::Villager);
        CHECK(m.viewFor(1).can(AbilityInspect)); // die gestohlene Rolle wirkt sofort
    }
    {
        Match m = makeMatch({W, Role::Scapegoat, V, V, V, V});
        runUntil(m, Phase::Night);
        runUntil(m, Phase::Vote);
        m.interact(2, Interaction::Vote, 3);
        m.interact(4, Interaction::Vote, 3);
        m.interact(3, Interaction::Vote, 5);
        m.interact(5, Interaction::Vote, 2);
        m.interact(0, Interaction::Vote, 5);
        runUntil(m, Phase::Execution); // 3 und 5 je zwei Stimmen
        CHECK(!alive(m, 1));
        CHECK(alive(m, 3) && alive(m, 5));
        CHECK(m.find(1)->deathCause == DeathCause::Scapegoat);
    }
}

TEST(FuchsUndRabe) {
    Match m = makeMatch({W, Role::Fox, Role::Raven, V, V, V});
    runUntil(m, Phase::Night);
    placeNear(m, 1, 0, 4.0f);
    CHECK(m.interact(1, Interaction::Sniff, 0).empty());
    CHECK(m.find(1)->foxActive);
    m.teleport(3, testWorld().ground(-30.0f, 8.0f));
    m.teleport(1, testWorld().ground(-27.0f, 8.0f));
    CHECK(!m.interact(1, Interaction::Sniff, 3).empty()); // einmal pro Nacht
    // Der Rabe belastet Spieler 3: zwei Stimmen bei der nächsten Abstimmung
    placeNear(m, 2, 3, 5.0f);
    CHECK(m.interact(2, Interaction::Mark, 3).empty());
    runUntil(m, Phase::Morning);
    CHECK(m.viewFor(4).players[3].marked);
    runUntil(m, Phase::Vote);
    m.interact(4, Interaction::Vote, 5);
    runUntil(m, Phase::Execution);
    CHECK(!alive(m, 3)); // 2 Rabenstimmen gegen 1
    CHECK(alive(m, 5));
    // In der nächsten Nacht findet der Fuchs keinen Wolf und verliert seine Nase
    runUntil(m, Phase::Night);
    m.teleport(0, testWorld().ground(0.0f, -40.0f)); // der Wolf ist weit weg
    placeNear(m, 1, 4, 3.0f);
    CHECK(m.interact(1, Interaction::Sniff, 4).empty());
    CHECK(!m.find(1)->foxActive);
}

TEST(RitterUndMagd) {
    {
        Match m = makeMatch({W, Role::Knight, V, V, V, V});
        runUntil(m, Phase::Night);
        becomeWolf(m, 0);
        placeNear(m, 0, 1);
        CHECK(m.interact(0, Interaction::Attack, 1).empty());
        CHECK(alive(m, 0));
        runUntil(m, Phase::Day);
        CHECK(alive(m, 0));
        runUntil(m, Phase::GameOver); // in der folgenden Nacht wirkt die rostige Klinge
        CHECK(!alive(m, 0));
        CHECK(m.find(0)->deathCause == DeathCause::Rust);
        CHECK(m.winner() == Team::Village);
    }
    {
        Match m = makeMatch({W, Role::Servant, Role::Seer, V, V, V});
        runUntil(m, Phase::Night);
        m.takeNotices();
        voteOut(m, 2);
        CHECK(!alive(m, 2));
        // Die Rolle bleibt geheim, solange die Magd noch entscheiden kann
        CHECK(m.viewFor(4).players[2].role == Role::Unknown);
        CHECK(m.viewFor(1).can(AbilityTakeRole));
        CHECK(m.interact(1, Interaction::TakeRole, 2).empty());
        CHECK(m.find(1)->role == Role::Seer);
        runUntil(m, Phase::Night);
        CHECK(m.viewFor(1).can(AbilityInspect));
    }
}

TEST(RichterVerlangtZweiteAbstimmung) {
    Match m = makeMatch({W, Role::Judge, V, V, V, V, V});
    runUntil(m, Phase::Night);
    runUntil(m, Phase::Vote);
    CHECK(m.interact(1, Interaction::SecondVote, 0).empty());
    CHECK(!m.viewFor(1).can(AbilitySecondVote));
    for (int id : {0, 1, 2, 4, 5, 6}) m.interact(static_cast<uint8_t>(id), Interaction::Vote, 3);
    runUntil(m, Phase::Execution);
    CHECK(!alive(m, 3));
    runUntil(m, Phase::Vote, 30.0f);
    CHECK(m.phase() == Phase::Vote); // gleich noch einmal
    CHECK_EQ(m.day(), 2);
}

TEST(WildesKindWolfshundUndSchauspieler) {
    Match m = makeMatch({W, Role::WildChild, Role::WolfHound, Role::Actor, V, V, V, V, V});
    runUntil(m, Phase::Night);
    placeNear(m, 1, 4);
    CHECK(m.interact(1, Interaction::ChooseModel, 4).empty());
    CHECK(m.interact(2, Interaction::ChooseSide, 1).empty());
    CHECK(m.find(2)->role == Role::Werewolf);
    // Der Schauspieler spielt heute Nacht eine Sonderrolle
    const Role acting = m.find(3)->acting;
    CHECK(acting == Role::Seer || acting == Role::Protector || acting == Role::Fox || acting == Role::Tracker);
    CHECK(m.viewFor(3).abilities != 0);
    becomeWolf(m, 0);
    placeNear(m, 0, 4);
    CHECK(m.interact(0, Interaction::Attack, 4).empty());
    CHECK(m.find(1)->role == Role::Werewolf); // Vorbild tot -> Werwolf
    runUntil(m, Phase::Morning);
    CHECK(m.find(3)->acting == Role::Unknown);
}

TEST(GrosserBoeserWolfUndWeisserWerwolf) {
    {
        Match m = makeMatch({Role::BigBadWolf, V, V, V, V, V, V});
        runUntil(m, Phase::Night);
        CHECK_EQ(m.packKillsLeft(), 2);
    }
    {
        Match m = makeMatch({W, Role::WhiteWolf, V, V, V, V, V, V});
        CHECK(m.viewFor(0).players[1].role == Role::Werewolf); // getarnt im Rudel
        runUntil(m, Phase::Night);
        becomeWolf(m, 1);
        placeNear(m, 1, 0);
        CHECK(!m.interact(1, Interaction::Attack, 0).empty()); // erste Nacht: noch kein Verrat
        runUntil(m, Phase::Day);
        runUntil(m, Phase::Night);
        becomeWolf(m, 1);
        placeNear(m, 1, 0);
        CHECK(m.viewFor(1).can(AbilityBetray));
        CHECK(m.interact(1, Interaction::Attack, 0).empty());
        CHECK(!alive(m, 0));
        CHECK_EQ(m.packKillsLeft(), 1); // das Rudel darf trotzdem noch fressen
    }
}

TEST(FloetenspielerEngelUndSektierer) {
    {
        Match m = makeMatch({W, Role::Piper, V, V});
        runUntil(m, Phase::Night);
        placeNear(m, 1, 0);
        CHECK(m.interact(1, Interaction::Enchant, 0).empty());
        placeNear(m, 1, 2);
        CHECK(m.interact(1, Interaction::Enchant, 2).empty());
        placeNear(m, 1, 3);
        CHECK(!m.interact(1, Interaction::Enchant, 3).empty()); // nur zwei pro Nacht
        CHECK(m.viewFor(1).players[0].enchanted);
        CHECK(!m.viewFor(3).players[0].enchanted); // nur Verzauberte sehen das
        runUntil(m, Phase::Day);
        runUntil(m, Phase::Night);
        placeNear(m, 1, 3);
        CHECK(m.interact(1, Interaction::Enchant, 3).empty());
        runUntil(m, Phase::GameOver);
        CHECK(m.winner() == Team::Piper);
    }
    {
        Match m = makeMatch({W, Role::Angel, V, V, V});
        runUntil(m, Phase::Night);
        becomeWolf(m, 0);
        placeNear(m, 0, 1);
        CHECK(m.interact(0, Interaction::Attack, 1).empty());
        CHECK(m.phase() == Phase::GameOver);
        CHECK(m.winner() == Team::Angel);
        CHECK(m.isWinner(1));
    }
    {
        Match m = makeMatch({Role::Sectarian, W, V, V, V, V, V, V});
        int east = 0, west = 0;
        for (const auto& p : m.players()) (m.sectOf(p) == 0 ? east : west)++;
        CHECK(east > 0 && west > 0);
    }
}

TEST(GeschwisterUndBaerenfuehrer) {
    Match m = makeMatch({Role::BearTamer, W, Role::Sister, Role::Sister, V, V, V});
    CHECK(m.viewFor(2).players[3].role == Role::Sister);
    CHECK(m.viewFor(4).players[0].role == Role::BearTamer); // öffentlich
    CHECK(m.chatChannel(2, true) == ChatChannel::Family);
    CHECK(m.canHear(3, 2, ChatChannel::Family));
    CHECK(!m.canHear(4, 2, ChatChannel::Family));
    runUntil(m, Phase::Night);
    runUntil(m, Phase::Day);
    m.takeNotices();
    runUntil(m, Phase::Assembly);
    const auto notices = m.takeNotices();
    CHECK(std::any_of(notices.begin(), notices.end(), [](const Notice& n) { return n.text.de.find("brummt bedrohlich") != std::string::npos; }));
}
TEST(BotsSpielenEchtzeitPartienZuEnde) {
    int finished = 0;
    std::map<Team, int> winners;
    double simulated = 0.0;
    int chapelVisits = 0;
    float longestStay = 0.0f;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        eng::Rng rng(seed);
        const int players = rng.range(5, 12);
        Settings s;
        s.daySeconds = 40;
        s.discussionSeconds = 20;
        s.voteSeconds = 20;
        s.nightSeconds = 60;
        for (int r = 0; r < static_cast<int>(Role::Count); ++r)
            if (isOptionalRole(static_cast<Role>(r))) s.set(static_cast<Role>(r), rng.chance(0.3f));
        std::vector<Match::Seat> seats;
        std::vector<Bot> bots;
        for (int i = 0; i < players; ++i) {
            seats.push_back({static_cast<uint8_t>(i), "Bot" + std::to_string(i), true});
            bots.emplace_back(static_cast<uint8_t>(i), seed * 100 + static_cast<uint64_t>(i));
        }
        Match m;
        Text error;
        if (!m.start(seats, s, testWorld(), seed, error)) continue;
        const Bot::SayFn say = [](uint8_t, const Text&, bool) {};
        float t = 0.0f;
        std::map<uint8_t, float> inChapel;
        for (; t < 3600.0f && m.phase() != Phase::GameOver; t += 0.1f) {
            m.update(0.1f);
            for (Bot& b : bots) b.update(0.1f, m, say);
            m.takeNotices();
            m.takeCorrections();
            for (const Match::Player& p : m.players()) {
                const bool inside = p.alive && testWorld().houseAt({p.position.x, p.position.z}) == testWorld().chapelIndex();
                float& stay = inChapel[p.id];
                if (inside && stay <= 0.0f) ++chapelVisits;
                stay = inside ? stay + 0.1f : 0.0f;
                longestStay = std::max(longestStay, stay);
            }
        }
        simulated += t;
        CHECK(m.phase() == Phase::GameOver);
        if (m.phase() == Phase::GameOver) {
            ++finished;
            ++winners[m.winner()];
        }
    }
    std::printf("    %d Partien beendet (Ø %.0f s Spielzeit) – Dorf %d, Wölfe %d, Verliebte %d, niemand %d, Einzelgänger %d\n",
                finished, simulated / std::max(1, finished), winners[Team::Village], winners[Team::Wolves], winners[Team::Lovers],
                winners[Team::Nobody],
                winners[Team::Jester] + winners[Team::Killer] + winners[Team::Arsonist] + winners[Team::Piper] +
                    winners[Team::Angel] + winners[Team::WhiteWolf] + winners[Team::Sectarian]);
    std::printf("    %d Besuche in der Kapelle, längster Aufenthalt %.0f s\n", chapelVisits, longestStay);
    CHECK(finished >= 22);
    CHECK(chapelVisits > 0);
}

TEST(VampirclanBeisstLautlos) {
    Settings s = quickSettings();
    s.set(Role::Vampire, true);
    auto vampires = [](const std::vector<Role>& roles) { return std::count(roles.begin(), roles.end(), Role::Vampire); };
    CHECK_EQ(vampires(buildRoleList(s, 8)), 1);
    CHECK_EQ(vampires(buildRoleList(s, 12)), 2);
    CHECK(isEvilRole(Role::Vampire) && !isWolfRole(Role::Vampire));
    CHECK(roleTeam(Role::Vampire) == Team::Vampires);

    Match m = makeMatch({W, Role::Vampire, Role::Vampire, Role::Seer, V, V, V, V});
    CHECK(m.viewFor(1).player(2)->role == Role::Vampire);   // der Clan kennt sich
    CHECK(m.viewFor(1).player(0)->role == Role::Unknown);   // die Wölfe bleiben ihm verborgen
    CHECK(m.viewFor(4).player(1)->role == Role::Unknown);
    CHECK(m.chatChannel(1, true) == ChatChannel::Vampires);
    CHECK(m.canHear(2, 1, ChatChannel::Vampires));
    CHECK(!m.canHear(4, 1, ChatChannel::Vampires));
    CHECK(!m.canHear(0, 1, ChatChannel::Vampires));
    runUntil(m, Phase::Night);
    auto flagsOf = [&](uint8_t viewer, uint8_t id) {
        uint8_t flags = 0;
        for (const auto& e : m.entitiesFor(viewer))
            if (e.id == id && e.kind == EntityLiving) flags = e.flags;
        return flags;
    };
    // in Menschengestalt unauffällig: Laterne, keine roten Augen, kein Biss
    CHECK(!m.viewFor(1).can(AbilityBite));
    CHECK(!(flagsOf(4, 1) & EntityRedEyes));
    CHECK(flagsOf(4, 1) & EntityLantern);
    placeNear(m, 1, 4);
    CHECK(!m.interact(1, Interaction::Bite, 4).empty());
    // verwandelt: rote Augen, keine Laterne, namenlos – außer für den eigenen Clan
    CHECK(m.interact(1, Interaction::Transform, 1).empty());
    CHECK(m.viewFor(1).can(AbilityBite));
    CHECK(flagsOf(4, 1) & EntityRedEyes);
    CHECK(!(flagsOf(4, 1) & EntityLantern));
    CHECK(m.hidesSpeaker(4, ChatChannel::Nearby, 1));
    CHECK(!m.hidesSpeaker(2, ChatChannel::Nearby, 1));
    m.takeNotices();
    placeNear(m, 1, 2);
    CHECK(!m.interact(1, Interaction::Bite, 2).empty()); // nicht den eigenen Clan
    placeNear(m, 1, 4);
    CHECK(m.interact(1, Interaction::Bite, 4).empty());
    CHECK(!alive(m, 4));
    CHECK(!m.viewFor(2).can(AbilityBite)); // ein Biss je Nacht für den ganzen Clan
    // die Seherin erkennt den Vampir
    placeNear(m, 3, 2);
    CHECK(m.interact(3, Interaction::Inspect, 2).empty());
    CHECK(m.viewFor(3).player(2)->role == Role::Vampire);
    runUntil(m, Phase::Day);
    const auto notices = m.takeNotices();
    CHECK(std::any_of(notices.begin(), notices.end(), [](const Notice& n) { return n.text.de.find("Bissspuren") != std::string::npos; }));
}

TEST(VampirBrauchtEineEinladung) {
    Match m = makeMatch({W, Role::Vampire, V, V, V, V});
    runUntil(m, Phase::Night);
    const uint8_t house = m.find(2)->house;
    const auto& door = testWorld().houses()[house].door;
    m.teleport(2, testWorld().ground(door.x, door.z));
    CHECK(m.interact(2, Interaction::DoorToggle, house).empty()); // Tür zu, von außen: nicht verriegelt
    m.teleport(1, testWorld().ground(door.x, door.z));
    CHECK(!m.interact(1, Interaction::DoorToggle, house).empty()); // ohne Einladung nicht hinein
    m.teleport(0, testWorld().ground(door.x, door.z));
    CHECK(m.interact(0, Interaction::DoorToggle, house).empty()); // ein Werwolf in Menschengestalt schon
    const uint8_t own = m.find(1)->house;
    if (own != house) {
        const auto& ownDoor = testWorld().houses()[own].door;
        m.teleport(1, testWorld().ground(ownDoor.x, ownDoor.z));
        CHECK(m.interact(1, Interaction::DoorToggle, own).empty()); // die eigene Tür natürlich
    }
}

TEST(VampireGewinnenErstOhneWoelfe) {
    {
        Match m = makeMatch({W, Role::Vampire, Role::Priest, V, V, V});
        placeNear(m, 2, 1);
        CHECK(m.interact(2, Interaction::HolyWater, 1).empty()); // Weihwasser vernichtet auch Vampire
        CHECK(!alive(m, 1));
        CHECK(m.winner() == Team::None); // der Werwolf lebt noch
    }
    {
        Match m = makeMatch({W, Role::Vampire, Role::Priest, V, V});
        placeNear(m, 2, 0);
        CHECK(m.interact(2, Interaction::HolyWater, 0).empty());
        CHECK(m.winner() == Team::None); // ohne Wölfe, aber mit Vampir: noch nicht vorbei
        runUntil(m, Phase::Night);
        placeNear(m, 1, 3);
        CHECK(m.interact(1, Interaction::Transform, 1).empty());
        CHECK(m.interact(1, Interaction::Bite, 3).empty());
        runUntil(m, Phase::Day);
        CHECK(!m.find(1)->vampireForm); // am Morgen wieder in Menschengestalt
        voteOut(m, 4);
        CHECK(m.winner() == Team::Vampires); // Vampir gegen Pfarrer: gleichauf
    }
}

TEST(VerwandelteBestienBleibenNamenlos) {
    Match m = makeMatch({W, W, V, V, V, V});
    runUntil(m, Phase::Night);
    CHECK(!m.hidesSpeaker(2, ChatChannel::Nearby, 0)); // in Menschengestalt erkennt man die Stimme
    becomeWolf(m, 0);
    CHECK(m.hidesSpeaker(2, ChatChannel::Nearby, 0));  // als Bestie nicht
    CHECK(!m.hidesSpeaker(1, ChatChannel::Nearby, 0)); // das Rudel schon
    CHECK(!m.hidesSpeaker(2, ChatChannel::Global, 0));
}

TEST(ZweiunddreissigBewohner) {
    Settings s = quickSettings();
    std::vector<Role> roles = buildRoleList(s, 32);
    CHECK_EQ(roles.size(), 32u);
    CHECK_EQ(std::count_if(roles.begin(), roles.end(), isWolfRole), 7);
    s.set(Role::Vampire, true);
    roles = buildRoleList(s, 32);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Vampire), 3);
    CHECK(buildRoleList(s, 33).empty());
    // eine ganze Partie mit 32 Bots läuft durch
    std::vector<Match::Seat> seats;
    std::vector<Bot> bots;
    for (int i = 0; i < 32; ++i) {
        seats.push_back({static_cast<uint8_t>(i), "Bot" + std::to_string(i), true});
        bots.emplace_back(static_cast<uint8_t>(i), 7000u + static_cast<uint64_t>(i), Bot::Personality::random(7000u + static_cast<uint64_t>(i)));
    }
    Match m;
    Text error;
    CHECK(m.start(seats, s, testWorld(), 77, error));
    const Bot::SayFn say = [](uint8_t, const Text&, bool) {};
    const auto started = std::chrono::steady_clock::now();
    float t = 0.0f;
    for (; t < 3600.0f && m.phase() != Phase::GameOver; t += 0.1f) {
        m.update(0.1f);
        for (Bot& b : bots) b.update(0.1f, m, say);
        m.takeNotices();
        m.takeCorrections();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("    32 Bots: Partie nach %.0f s Spielzeit beendet (%s), gerechnet in %.1f s\n", t, teamName(m.winner()), seconds);
    CHECK(m.phase() == Phase::GameOver);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
