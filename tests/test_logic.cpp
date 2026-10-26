// Tests des Echtzeit-Regelwerks (ohne Netzwerk).

#include "TestFramework.h"

#include "logic/Match.h"
#include "server/Bot.h"
#include "world/World.h"

#include <algorithm>
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
    return s;
}

Match makeMatch(const std::vector<Role>& roles, Settings settings = quickSettings()) {
    std::vector<Match::Seat> seats;
    for (size_t i = 0; i < roles.size(); ++i) seats.push_back({static_cast<uint8_t>(i), "P" + std::to_string(i), false});
    Match m;
    std::string error;
    const bool ok = m.startWithRoles(seats, roles, settings, testWorld(), 42, error);
    CHECK(ok);
    if (!ok) std::printf("    %s\n", error.c_str());
    return m;
}

bool alive(const Match& m, uint8_t id) { return m.find(id) && m.find(id)->alive; }

void run(Match& m, float seconds) {
    for (float t = 0; t < seconds; t += 0.1f) m.update(0.1f);
}

void runUntil(Match& m, Phase target, float maxSeconds = 900.0f) {
    for (float t = 0; t < maxSeconds && m.phase() != target; t += 0.1f) m.update(0.1f);
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
    std::string err;
    auto roles = buildRoleList(s, 8, &err);
    CHECK_EQ(roles.size(), 8u);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Werewolf), 2);
    CHECK(buildRoleList(s, 3, &err).empty());
    s.wolves = 4;
    CHECK(buildRoleList(s, 8, &err).empty());
}

TEST(WeltIstDeterministischUndBegehbar) {
    World a, b;
    a.generate(7u);
    b.generate(7u);
    CHECK_EQ(a.trees().size(), b.trees().size());
    CHECK_EQ(a.houses().size(), 12u);
    CHECK(a.height(10.0f, 3.0f) == b.height(10.0f, 3.0f));
    // Spawnpunkte und Sitzplätze sind frei
    for (int i = 0; i < 16; ++i) CHECK(!a.blocked({a.spawnPoint(i, 16).x, a.spawnPoint(i, 16).z}, kPlayerRadius));
    // Häuser blockieren
    const House& h = a.houses()[0];
    CHECK(a.blocked({h.position.x, h.position.z}, kPlayerRadius));
    CHECK(a.insideBuilding({h.position.x, h.position.z}));
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
}

TEST(BotsSpielenEchtzeitPartienZuEnde) {
    int finished = 0;
    std::map<Team, int> winners;
    double simulated = 0.0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        eng::Rng rng(seed);
        const int players = rng.range(5, 12);
        Settings s;
        s.daySeconds = 40;
        s.discussionSeconds = 20;
        s.voteSeconds = 20;
        s.nightSeconds = 60;
        s.cupid = rng.chance(0.4f);
        s.protector = rng.chance(0.5f);
        std::vector<Match::Seat> seats;
        std::vector<Bot> bots;
        for (int i = 0; i < players; ++i) {
            seats.push_back({static_cast<uint8_t>(i), "Bot" + std::to_string(i), true});
            bots.emplace_back(static_cast<uint8_t>(i), seed * 100 + static_cast<uint64_t>(i));
        }
        Match m;
        std::string error;
        if (!m.start(seats, s, testWorld(), seed, error)) continue;
        const Bot::SayFn say = [](uint8_t, const std::string&, bool) {};
        float t = 0.0f;
        for (; t < 3600.0f && m.phase() != Phase::GameOver; t += 0.1f) {
            m.update(0.1f);
            for (Bot& b : bots) b.update(0.1f, m, say);
            m.takeNotices();
            m.takeCorrections();
        }
        simulated += t;
        CHECK(m.phase() == Phase::GameOver);
        if (m.phase() == Phase::GameOver) {
            ++finished;
            ++winners[m.winner()];
        }
    }
    std::printf("    %d Partien beendet (Ø %.0f s Spielzeit) – Dorf %d, Wölfe %d, Verliebte %d, niemand %d\n", finished,
                simulated / std::max(1, finished), winners[Team::Village], winners[Team::Wolves], winners[Team::Lovers],
                winners[Team::Nobody]);
    CHECK(finished >= 22);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
