// Tests der Spielregeln (ohne Netzwerk).

#include "TestFramework.h"

#include "logic/Game.h"
#include "server/Bot.h"

#include <algorithm>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace ww;

namespace {

constexpr Role W = Role::Werewolf;
constexpr Role V = Role::Villager;

Settings quickSettings() {
    Settings s;
    s.nightSeconds = 30;
    s.discussionSeconds = 30;
    s.voteSeconds = 30;
    return s;
}

// Startet ein Spiel mit fester Rollenverteilung: Spieler i hat Rolle roles[i] und Id i.
Game makeGame(const std::vector<Role>& roles, Settings settings = quickSettings()) {
    std::vector<Game::Seat> seats;
    for (size_t i = 0; i < roles.size(); ++i)
        seats.push_back({static_cast<uint8_t>(i), "P" + std::to_string(i), false});
    Game g;
    std::string error;
    const bool ok = g.startWithRoles(seats, roles, settings, 42, error);
    CHECK(ok);
    if (!ok) std::printf("    %s\n", error.c_str());
    return g;
}

bool alive(const Game& g, uint8_t id) { return g.find(id) && g.find(id)->alive; }

// Lässt Zeit vergehen, bis die Phase wechselt (höchstens `maxSeconds`).
void runUntilPhaseChange(Game& g, float maxSeconds = 400.0f) {
    const Phase start = g.phase();
    const int day = g.day();
    for (float t = 0; t < maxSeconds && g.phase() == start && g.day() == day; t += 0.25f) g.update(0.25f);
}

void runUntil(Game& g, Phase target, float maxSeconds = 600.0f) {
    for (float t = 0; t < maxSeconds && g.phase() != target; t += 0.25f) g.update(0.25f);
}

} // namespace

TEST(RollenverteilungAutomatisch) {
    Settings s;
    std::string err;
    auto roles = buildRoleList(s, 8, &err);
    CHECK_EQ(roles.size(), 8u);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Werewolf), 2);
    CHECK_EQ(std::count(roles.begin(), roles.end(), Role::Seer), 1);

    CHECK(buildRoleList(s, 3, &err).empty());
    s.wolves = 4;
    CHECK(buildRoleList(s, 8, &err).empty());
    s.wolves = 1;
    s.cupid = s.protector = true;
    CHECK(buildRoleList(s, 5, &err).empty()); // 1 Wolf + 5 Sonderrollen > 5 Spieler
}

TEST(WerwolfToetetNachts) {
    Game g = makeGame({W, V, V, V, V});
    CHECK(g.phase() == Phase::Night);
    CHECK(g.actionFor(0) == ActionKind::WolfVote);
    CHECK(g.actionFor(1) == ActionKind::None);
    CHECK(!g.canTarget(0, 0)); // keine Wölfe fressen
    CHECK(g.select(0, 1).empty());
    CHECK(g.confirm(0).empty());
    runUntilPhaseChange(g, 5.0f); // alle Akteure fertig -> kurze Gnadenfrist
    CHECK(g.phase() == Phase::Morning);
    CHECK(!alive(g, 1));
    runUntilPhaseChange(g);
    CHECK(g.phase() == Phase::Day);
}

TEST(HexeRettetOpfer) {
    Game g = makeGame({W, Role::Witch, V, V, V});
    g.select(0, 2);
    g.confirm(0);
    runUntilPhaseChange(g, 5.0f);
    CHECK(g.phase() == Phase::Witch);
    GameView wv = g.viewFor(1);
    CHECK_EQ(wv.witchVictim, 2);
    CHECK(wv.healAvailable);
    CHECK(g.viewFor(2).witchVictim == kNoPlayer); // andere sehen das Opfer nicht
    CHECK(g.setWitchHeal(1, true).empty());
    CHECK(g.confirm(1).empty());
    runUntilPhaseChange(g, 5.0f);
    CHECK(g.phase() == Phase::Morning);
    CHECK(alive(g, 2));
    CHECK(!g.viewFor(1).healAvailable);
}

TEST(HexeVergiftetLetztenWolf) {
    Game g = makeGame({W, Role::Witch, V, V, V});
    g.select(0, 2);
    g.confirm(0);
    runUntilPhaseChange(g, 5.0f);
    CHECK(g.select(1, 0).empty()); // Gift auf den Wolf
    g.confirm(1);
    runUntilPhaseChange(g, 5.0f);
    CHECK(!alive(g, 0));
    CHECK(!alive(g, 2));
    runUntil(g, Phase::GameOver);
    CHECK(g.winner() == Team::Village);
    CHECK(g.isWinner(1));
    CHECK(!g.isWinner(0));
}

TEST(BeschuetzerRettetUndDarfNichtWiederholen) {
    Game g = makeGame({W, Role::Protector, V, V, V});
    CHECK(g.select(1, 2).empty());
    g.confirm(1);
    g.select(0, 2);
    g.confirm(0);
    runUntilPhaseChange(g, 5.0f);
    CHECK(g.phase() == Phase::Morning);
    CHECK(alive(g, 2));
    runUntil(g, Phase::Night);
    CHECK(!g.canTarget(1, 2));
    CHECK(g.canTarget(1, 3));
}

TEST(VerliebteSterbenGemeinsam) {
    Game g = makeGame({W, Role::Cupid, V, V, V, V});
    CHECK(g.actionFor(1) == ActionKind::CupidPick);
    g.select(1, 2);
    g.select(1, 3);
    CHECK(g.confirm(1).empty());
    g.select(0, 2);
    g.confirm(0);
    runUntilPhaseChange(g, 5.0f);
    CHECK(!alive(g, 2));
    CHECK(!alive(g, 3)); // vor Kummer
    CHECK(g.viewFor(2).players[3].lover);
    CHECK(!g.viewFor(4).players[3].lover); // Unbeteiligte wissen nichts
}

TEST(JaegerSchiesstImTod) {
    Game g = makeGame({W, Role::Hunter, V, V, V});
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Hunter, 60.0f);
    CHECK(g.phase() == Phase::Hunter);
    CHECK(g.actionFor(1) == ActionKind::HunterShot);
    CHECK(g.select(1, 0).empty());
    CHECK(g.confirm(1).empty());
    CHECK(!alive(g, 0));
    CHECK(g.phase() == Phase::GameOver);
    CHECK(g.winner() == Team::Village);
}

TEST(AbstimmungRichtetHin) {
    Game g = makeGame({W, V, V, V, V, V});
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Day, 60.0f);
    // Mehrheit überspringt die Diskussion
    g.setSkip(2, true);
    g.setSkip(3, true);
    CHECK(g.phase() == Phase::Day);
    g.setSkip(4, true);
    CHECK(g.phase() == Phase::Vote);
    for (int id = 2; id <= 5; ++id) CHECK(g.select(static_cast<uint8_t>(id), 0).empty());
    CHECK(!g.select(0, 0).empty()); // nicht für sich selbst
    g.select(0, 2);
    CHECK(g.timeLeft() <= 4.01f); // alle haben gewählt -> verkürzt
    runUntilPhaseChange(g, 10.0f);
    CHECK(g.phase() == Phase::Execution);
    CHECK(!alive(g, 0));
    CHECK(g.viewFor(3).players[2].vote == 0); // Stimmen bleiben sichtbar
    runUntil(g, Phase::GameOver);
    CHECK(g.winner() == Team::Village);
}

TEST(GleichstandOhneHinrichtung) {
    Game g = makeGame({W, V, V, V, V});
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Vote, 400.0f);
    g.select(2, 0);
    g.select(3, 4);
    runUntilPhaseChange(g);
    CHECK(g.phase() == Phase::Execution);
    CHECK(alive(g, 0));
    CHECK(alive(g, 4));
    runUntilPhaseChange(g);
    CHECK(g.phase() == Phase::Night);
    CHECK_EQ(g.day(), 2);
}

TEST(WerwoelfeGewinnenBeiGleichstand) {
    Game g = makeGame({W, V, V, V});
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Vote, 400.0f);
    g.select(0, 2);
    g.select(3, 2);
    g.select(2, 0);
    runUntil(g, Phase::GameOver, 100.0f);
    CHECK(g.phase() == Phase::GameOver);
    CHECK(g.winner() == Team::Wolves);
}

TEST(GemischtesPaarGewinnt) {
    Game g = makeGame({W, Role::Cupid, V, V});
    g.select(1, 0);
    g.select(1, 2);
    g.confirm(1);
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Vote, 400.0f);
    CHECK(g.teamOf(0) == Team::Lovers);
    g.select(0, 3);
    g.select(2, 3);
    runUntil(g, Phase::GameOver, 100.0f);
    CHECK(g.winner() == Team::Lovers);
    CHECK(g.isWinner(0));
    CHECK(g.isWinner(2));
}

TEST(SichtbarkeitDerRollen) {
    Game g = makeGame({W, W, Role::Seer, V, V, V, V, V});
    const GameView villager = g.viewFor(3);
    CHECK(villager.players[0].role == Role::Unknown);
    CHECK(villager.players[3].role == Role::Villager);
    const GameView wolf = g.viewFor(0);
    CHECK(wolf.players[1].role == Role::Werewolf);
    CHECK(wolf.players[2].role == Role::Unknown);

    g.select(2, 1);
    g.confirm(2);
    CHECK(g.viewFor(2).players[1].role == Role::Werewolf);
    CHECK_EQ(g.viewFor(2).seerResults.size(), 1u);

    g.select(0, 3);
    CHECK(g.viewFor(1).players[0].vote == 3);     // Rudel sieht die Wahl
    CHECK(g.viewFor(4).players[0].vote == kNoPlayer); // Dorf nicht
}

TEST(Chatkanaele) {
    Game g = makeGame({W, V, V, V, V});
    CHECK(!g.speakChannel(1).has_value()); // nachts schläft das Dorf
    CHECK(g.speakChannel(0) == ChatChannel::Wolves);
    CHECK(g.canRead(0, ChatChannel::Wolves));
    CHECK(!g.canRead(1, ChatChannel::Wolves));
    g.select(0, 1);
    g.confirm(0);
    runUntil(g, Phase::Day, 60.0f);
    CHECK(g.speakChannel(2) == ChatChannel::Public);
    CHECK(g.speakChannel(1) == ChatChannel::Dead);
    CHECK(g.canRead(1, ChatChannel::Wolves)); // Geister sehen alles
    CHECK(!g.canRead(2, ChatChannel::Dead));
}

TEST(SpielerVerlaesstDasSpiel) {
    Game g = makeGame({W, V, V, V, V});
    g.playerLeft(0);
    CHECK(!alive(g, 0));
    CHECK(g.phase() == Phase::GameOver);
    CHECK(g.winner() == Team::Village);
}

TEST(SpielansichtSerialisierung) {
    Game g = makeGame({W, Role::Witch, Role::Seer, V, V});
    GameView v = g.viewFor(0);
    eng::ByteWriter w;
    v.write(w);
    eng::ByteReader r(w.data());
    GameView copy;
    CHECK(copy.read(r));
    CHECK(r.atEnd());
    CHECK(copy.phase == v.phase);
    CHECK_EQ(copy.players.size(), v.players.size());
    CHECK(copy.players[0].name == "P0");
    CHECK(copy.yourRole == Role::Werewolf);
    CHECK(copy.action == ActionKind::WolfVote);

    // Abgeschnittene Daten werden erkannt
    std::vector<uint8_t> truncated(w.data().begin(), w.data().begin() + 10);
    eng::ByteReader r2(truncated);
    GameView broken;
    CHECK(!broken.read(r2));
}

TEST(BotsSpielenVieleRundenZuEnde) {
    // Viele komplette Partien nur mit Bots: jede muss enden, nichts darf hängen.
    int finished = 0;
    std::map<Team, int> winners;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        eng::Rng rng(seed);
        const int players = rng.range(kMinPlayers, kMaxPlayers);
        Settings s = quickSettings();
        s.cupid = rng.chance(0.5f);
        s.protector = rng.chance(0.5f);
        s.witch = rng.chance(0.8f);
        s.hunter = rng.chance(0.8f);
        s.revealRoles = rng.chance(0.5f);
        std::vector<Game::Seat> seats;
        std::vector<Bot> bots;
        for (int i = 0; i < players; ++i) {
            seats.push_back({static_cast<uint8_t>(i), "Bot" + std::to_string(i), true});
            bots.emplace_back(static_cast<uint8_t>(i), seed * 100 + static_cast<uint64_t>(i));
        }
        Game g;
        std::string error;
        if (!g.start(seats, s, seed, error)) continue; // ungültige Kombination
        const Bot::SayFn say = [](uint8_t, const std::string&) {};
        for (int step = 0; step < 40000 && g.phase() != Phase::GameOver; ++step) {
            g.update(0.5f);
            for (Bot& b : bots) b.update(0.5f, g, say);
            if (rng.chance(0.002f)) g.playerLeft(static_cast<uint8_t>(rng.range(0, players - 1)));
        }
        CHECK(g.phase() == Phase::GameOver);
        if (g.phase() == Phase::GameOver) {
            ++finished;
            ++winners[g.winner()];
        }
        g.takeNotices();
    }
    std::printf("    %d Partien beendet – Dorf %d, Wölfe %d, Verliebte %d, niemand %d\n", finished,
                winners[Team::Village], winners[Team::Wolves], winners[Team::Lovers], winners[Team::Nobody]);
    CHECK(finished > 250);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
