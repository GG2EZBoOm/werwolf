#include "TestFramework.h"
#include "server/Bot.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace ww;
namespace {
constexpr Role V = Role::Villager, W = Role::Werewolf;
const World& world() {
    static const World w = [] { World result; result.generate(4242); return result; }();
    return w;
}
struct Scene {
    Match match;
    Bot bot;
    struct Line { Text text; double time; std::optional<Bot::Evidence> evidence; };
    std::vector<Line> lines;
    Bot::SayFn say = [&](uint8_t, const Text& text, bool) {
        const auto* speech = bot.speaking();
        lines.push_back({text, match.clock(), speech ? speech->evidence : std::nullopt});
    };
    Scene(Role role = V, uint64_t seed = 12, bool reveal = true, std::vector<Role> roles = {}) : bot(1, seed) {
        if (roles.empty()) roles = {V, role, W, V, V, V, W, V};
        Settings settings;
        settings.captain = false;
        settings.revealRoles = reveal;
        settings.daySeconds = 120;
        settings.discussionSeconds = 60;
        settings.voteSeconds = 30;
        settings.nightSeconds = 120;
        const char* names[] = {"Spieler", "Anton", "Anna", "Ann", "Berta", "Clara", "Dieter", "Emil"};
        std::vector<Match::Seat> seats;
        for (size_t i = 0; i < roles.size(); ++i) seats.push_back({static_cast<uint8_t>(i), names[i], i != 0});
        Text error;
        CHECK(match.startWithRoles(seats, roles, settings, world(), 42, error));
        bot.update(0.01f, match, say);
    }
    void phase(Phase target) {
        for (int i = 0; i < 15 && match.phase() != target; ++i) { match.skipPhase(); match.update(0.1f); }
        CHECK_EQ(match.phase(), target);
        bot.update(0.01f, match, say);
    }
    void advance(float seconds) {
        for (float t = 0; t < seconds; t += 0.1f) { match.update(0.1f); bot.update(0.1f, match, say); }
    }
    void hear(const std::string& text, bool reply = true) {
        bot.hear(match, 0, Text::same(text), *match.chatChannel(0, false), reply);
    }
    bool has(const std::string& text) const {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& l) { return l.text.de.find(text) != std::string::npos; });
    }
    bool seerSpoke() const {
        return std::any_of(lines.begin(), lines.end(), [](const auto& l) { return l.evidence && l.evidence->kind == Bot::Evidence::Kind::Seer; });
    }
    void inspect(uint8_t id) {
        phase(Phase::Night);
        match.teleport(1, world().ground(4, 9));
        match.teleport(id, world().ground(6, 9));
        CHECK(match.interact(1, Interaction::Inspect, id).empty());
    }
};
}

TEST(AccusationsCauseStressFasterTypingAndNoSpamFarming) {
    Scene calm, pressured;
    calm.phase(Phase::Assembly); pressured.phase(Phase::Assembly);
    calm.hear("Anton, hallo");
    pressured.hear("Anton ist ein Wolf");
    const float stress = pressured.bot.feelings().stress;
    CHECK(stress > 0.3f);
    CHECK(pressured.bot.relationship(0) < -0.2f);
    for (int i = 0; i < 40; ++i) pressured.hear("Anton ist ein Wolf", false);
    CHECK_EQ(pressured.bot.feelings().stress, stress);
    calm.advance(3); pressured.advance(3);
    CHECK(!calm.lines.empty() && !pressured.lines.empty());
    if (!calm.lines.empty() && !pressured.lines.empty()) CHECK(pressured.lines.front().time < calm.lines.front().time);
    CHECK(pressured.has("!"));
    pressured.advance(15);
    CHECK(pressured.bot.feelings().stress < stress);
}

TEST(BotsBuildFriendshipFromSupportAndTimeTogether) {
    Scene s;
    const Bot::Speech support{Bot::Evidence{Bot::Evidence::Kind::Opinion, 1, -1.0f}, false};
    s.match.teleport(1, world().ground(4, 9));
    s.match.teleport(3, world().ground(6, 9));
    s.bot.hear(s.match, 3, Text::same("Anton ist unschuldig"), ChatChannel::Nearby, false, &support);
    const float supported = s.bot.relationship(3);
    CHECK(supported >= 0.3f);
    for (int i = 0; i < 30; ++i) s.bot.hear(s.match, 3, Text::same("Anton ist unschuldig"), ChatChannel::Nearby, false, &support);
    CHECK_EQ(s.bot.relationship(3), supported);
    for (int i = 0; i < 600; ++i) {
        s.match.teleport(1, world().ground(4, 9)); s.match.teleport(3, world().ground(6, 9));
        s.advance(0.1f);
    }
    CHECK(s.bot.relationship(3) >= 0.45f);
    s.phase(Phase::Assembly); s.lines.clear();
    s.hear("Anton, Ann ist verdächtig"); s.advance(3);
    CHECK(s.has("meinen Freunden"));
}

TEST(FearOnlyAfterVisibleOrAnnouncedDeathAndDecays) {
    Scene s(V, 12, true, {Role::Witch, V, W, V, V, V, W, V});
    s.phase(Phase::Night);
    s.match.teleport(1, world().ground(60, 60));
    s.match.teleport(0, world().ground(4, 9)); s.match.teleport(3, world().ground(6, 9));
    CHECK(s.match.interact(0, Interaction::Poison, 3).empty());
    s.bot.update(0.1f, s.match, s.say);
    CHECK_EQ(s.bot.feelings().fear, 0.0f); // distant death at night is not known yet
    s.phase(Phase::Morning);
    const float frightened = s.bot.feelings().fear;
    CHECK(frightened >= 0.24f);
    s.advance(2);
    CHECK(s.bot.feelings().fear < frightened);
    CHECK(s.bot.feelings().fear > 0.2f); // repeated updates do not add another murder
}

TEST(WrongfulExecutionCreatesAngerAndEnemiesWithoutHiddenKnowledge) {
    for (bool reveal : {true, false}) {
        Scene s(V, 21, reveal);
        s.phase(Phase::Vote);
        CHECK(s.match.interact(0, Interaction::Vote, 3).empty());
        CHECK(s.match.interact(4, Interaction::Vote, 3).empty());
        s.advance(0.2f); // observe public votes before execution
        s.phase(Phase::Execution);
        CHECK(!s.match.find(3)->alive);
        if (reveal) {
            CHECK(s.bot.feelings().anger >= 0.39f);
            CHECK(s.bot.relationship(0) <= -0.24f);
            s.phase(Phase::Assembly); s.advance(20);
            CHECK(s.has("wütend"));
        } else {
            CHECK_EQ(s.bot.feelings().anger, 0.0f);
            CHECK(s.bot.relationship(0) > -0.1f);
        }
    }
}

TEST(HunterAndWitchThreatenOnlyWithTheirActualResources) {
    Scene hunter(Role::Hunter), witch(Role::Witch);
    hunter.phase(Phase::Assembly); witch.phase(Phase::Assembly);
    hunter.hear("Anton ist ein Wolf"); witch.hear("Anton ist ein Wolf");
    hunter.advance(3); witch.advance(3);
    CHECK(hunter.has("Hängt mich"));
    CHECK(witch.has("Gifttrank"));
    witch.phase(Phase::Night);
    witch.match.teleport(1, world().ground(4, 9)); witch.match.teleport(3, world().ground(6, 9));
    CHECK(witch.match.interact(1, Interaction::Poison, 3).empty());
    CHECK(!witch.match.viewFor(1).poisonAvailable);
    witch.phase(Phase::Assembly); witch.lines.clear();
    witch.hear("Anton ist ein Wolf"); witch.advance(4);
    CHECK(!witch.has("Gifttrank"));
    CHECK(witch.has("Leben retten"));
}

TEST(SeerHidesSafeChecksUntilAnInnocentNeedsSaving) {
    Scene s(Role::Seer);
    s.inspect(3);
    s.phase(Phase::Assembly);
    s.hear("Anton, welche Rolle hast du?"); s.advance(8);
    CHECK(!s.seerSpoke());
    s.phase(Phase::Vote);
    CHECK(s.match.interact(0, Interaction::Vote, 3).empty());
    CHECK(s.match.interact(4, Interaction::Vote, 3).empty());
    s.advance(7);
    CHECK(s.seerSpoke());
    CHECK(s.has("keinen Werwolf ergeben"));
}

TEST(SeerKeepsWolfResultUntilPublicDiscussionThenRevealsIt) {
    Scene s(Role::Seer);
    s.inspect(2);
    s.phase(Phase::Day); s.advance(10);
    CHECK(!s.seerSpoke());
    s.phase(Phase::Assembly); s.advance(8);
    CHECK(s.seerSpoke());
    CHECK(s.has("Anna als Seherin geprüft"));
}

TEST(WolfSacrificesExposedPartnerAndGainsPublicTrust) {
    Scene s(W, 9, true, {V, W, W, V, V, V, V, V});
    Bot observer(3, 88);
    s.phase(Phase::Vote);
    for (uint8_t id : std::array<uint8_t, 3>{0, 4, 5}) CHECK(s.match.interact(id, Interaction::Vote, 2).empty());
    s.advance(4);
    CHECK_EQ(s.match.find(1)->vote, 2);
    CHECK(s.has("zu lange vertraut"));
    for (const auto& line : s.lines) CHECK(!line.evidence || line.evidence->kind != Bot::Evidence::Kind::Seer);
    observer.update(0.1f, s.match, [](uint8_t, const Text&, bool) {});
    const float trustBefore = observer.relationship(1);
    s.phase(Phase::Execution);
    observer.update(0.1f, s.match, [](uint8_t, const Text&, bool) {});
    CHECK(!s.match.find(2)->alive);
    CHECK(observer.relationship(1) > trustBefore);
}

TEST(WolvesDoNotSacrificeWithoutPressureOrNearVictory) {
    Scene noPressure(W, 9, true, {V, W, W, V, V, V, V, V});
    noPressure.phase(Phase::Vote); noPressure.advance(7);
    CHECK(noPressure.match.find(1)->vote != 2);
    Scene nearWin(W, 9, true, {V, W, W, V, V});
    nearWin.phase(Phase::Vote);
    for (uint8_t id : std::array<uint8_t, 3>{0, 3, 4}) CHECK(nearWin.match.interact(id, Interaction::Vote, 2).empty());
    nearWin.advance(3);
    CHECK(nearWin.match.find(1)->vote != 2);
    CHECK(!nearWin.has("zu lange vertraut"));
}

TEST(AngerInfluencesTheNextVoteAndKnownEvidenceStillWins) {
    int revengeVotes = 0;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Scene s(V, seed);
        s.phase(Phase::Vote);
        s.match.interact(0, Interaction::Vote, 3);
        s.match.interact(4, Interaction::Vote, 3);
        s.advance(0.2f);
        s.phase(Phase::Execution);
        s.phase(Phase::Assembly);
        s.phase(Phase::Vote);
        s.advance(8);
        const uint8_t target = s.match.find(1)->vote;
        revengeVotes += target == 0 || target == 4;
    }
    std::printf("    Revenge votes against the two voters behind the wrongful execution: %d/30\n", revengeVotes);
    CHECK(revengeVotes >= 20);
    // A friendship cannot protect a werewolf from the seer's own result.
    Scene seer(Role::Seer);
    seer.phase(Phase::Assembly);
    const Bot::Speech support{Bot::Evidence{Bot::Evidence::Kind::Opinion, 1, -1.0f}, false};
    seer.bot.hear(seer.match, 2, Text::same("Anton ist unschuldig"), ChatChannel::Global, false, &support);
    CHECK(seer.bot.relationship(2) >= 0.3f);
    seer.inspect(2); seer.phase(Phase::Vote); seer.advance(8);
    CHECK_EQ(seer.match.find(1)->vote, 2);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
