#include "TestFramework.h"
#include "server/Bot.h"
#include "server/Dialogue.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace ww;
using namespace ww::dialogue;

namespace {
const std::vector<Person> people = {{0, "Spieler"}, {1, "Anton"}, {2, "Anna"}, {3, "Ann"}, {4, "Berta"}, {5, "Jörg"}};
Message parseLine(const std::string& text, uint8_t previous = kNoPlayer) { return parse(text, people, 0, 1, previous); }
float weight(const Message& m, uint8_t target) {
    for (const auto& c : m.claims) if (c.target == target) return c.weight;
    return 0;
}
const World& world() {
    static const World w = [] { World v; v.generate(4242); return v; }();
    return w;
}
Match matchWith(Role botRole = Role::Villager) {
    Match match;
    Settings settings;
    settings.captain = false;
    settings.daySeconds = 120;
    settings.discussionSeconds = 60;
    settings.voteSeconds = 30;
    settings.nightSeconds = 60;
    const std::vector<Match::Seat> seats = {{0, "Spieler", false}, {1, "Anton", true}, {2, "Anna", false},
        {3, "Ann", false}, {4, "Berta", false}, {5, "Jörg", false}};
    Text error;
    CHECK(match.startWithRoles(seats, {Role::Villager, botRole, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager},
                               settings, world(), 42, error));
    return match;
}
void phase(Match& match, Phase target) {
    for (int i = 0; i < 15 && match.phase() != target; ++i) { match.skipPhase(); match.update(0.1f); }
    CHECK_EQ(match.phase(), target);
}
struct Conversation {
    Match match;
    Bot bot;
    std::vector<Text> replies;
    std::vector<bool> secrets;
    Bot::SayFn say = [&](uint8_t, const Text& text, bool secret) { replies.push_back(text); secrets.push_back(secret); };
    explicit Conversation(Role role = Role::Villager, uint64_t seed = 55) : match(matchWith(role)), bot(1, seed) {
        phase(match, Phase::Assembly);
        bot.update(0.01f, match, say);
    }
    void advance(float seconds) {
        for (float t = 0; t < seconds; t += 0.1f) { match.update(0.1f); bot.update(0.1f, match, say); }
    }
    void hear(const std::string& text, bool reply = true) { bot.hear(match, 0, Text::same(text), ChatChannel::Global, reply); }
    Text ask(const std::string& text) {
        advance(5.1f);
        replies.clear();
        hear(text);
        advance(3.0f);
        CHECK(!replies.empty());
        return replies.empty() ? Text{} : replies.front();
    }
};
}

TEST(NegationAndSeparateTargets) {
    CHECK(weight(parseLine("Anna ist ein Werwolf"), 2) > 1.5f);
    for (const auto& s : {"Anna ist kein Werwolf", "Anna ist nicht der Wolf", "Anna is not a werewolf", "Anna isn't a wolf",
                          "Ich glaube nicht, dass Anna ein Wolf ist"}) {
        const auto m = parseLine(s);
        if (!(weight(m, 2) < 0)) std::printf("    Satz: %s\n", s);
        CHECK(weight(m, 2) < 0);
    }
    CHECK(weight(parseLine("Ich traue Anna nicht"), 2) > 0);
    CHECK(weight(parseLine("Ich vertraue Anna"), 2) < 0);
    CHECK(weight(parseLine("Anna ist nicht unschuldig"), 2) > 0);
    auto m = parseLine("Anna ist unschuldig, aber Berta ist ein Wolf!");
    CHECK(weight(m, 2) < 0);
    CHECK(weight(m, 4) > 0);
    m = parseLine("Anna ist kein Wolf und Berta ist verdaechtig");
    CHECK(weight(m, 2) < 0);
    CHECK(weight(m, 4) > 0);
    m = parseLine("Anna und Berta sind Woelfe");
    CHECK(weight(m, 2) > 0);
    CHECK(weight(m, 4) > 0);
}

TEST(NamesAndQuestionsAreNotAccusations) {
    CHECK(parseLine("Anna war am Brunnen").claims.empty());
    CHECK(parseLine("Anna hat einen Wolf gesehen").claims.empty());
    CHECK(parseLine("Wenn Anna ein Wolf ist, dann verlieren wir").claims.empty());
    CHECK(parseLine("Anna sagt Berta ist ein Wolf").claims.empty());
    CHECK(parseLine("Er sagte: \"Anna ist ein Wolf\"").claims.empty());
    for (const auto& s : {"Ist Anna ein Wolf?", "Anna ist ein Wolf?", "Is Anna a wolf?", "Wer ist der Wolf?"})
        CHECK(parseLine(s).claims.empty());
    auto m = parseLine("ANNA ist VERDÄCHTIG");
    CHECK(weight(m, 2) > 0);
    CHECK_EQ(weight(m, 3), 0.0f); // Ann is not a substring match for Anna
    CHECK(weight(parseLine("JÖRG ist ein WERWOLF"), 5) > 0);
    CHECK(parseLine("Annabelle ist Wolf").claims.empty());
    const auto duplicates = parse("Anna ist Wolf", {{1, "Anna"}, {2, "ANNA"}}, 0);
    CHECK(duplicates.claims.empty());
    const auto longer = parse("Anna Maria ist Wolf", {{1, "Anna"}, {2, "Anna Maria"}}, 0);
    CHECK_EQ(longer.claims.size(), 1u);
    CHECK(weight(longer, 2) > 0);
}

TEST(AddressQuestionsClaimsAndContext) {
    auto m = parseLine("Anton, Anna ist ein Wolf");
    CHECK_EQ(m.addressed, 1);
    CHECK_EQ(weight(m, 1), 0.0f);
    CHECK(weight(m, 2) > 0);
    m = parseLine("Anton, bist du ein Wolf?");
    CHECK_EQ(m.intent, Intent::AskRole);
    CHECK_EQ(m.topic, 1);
    CHECK(m.claims.empty());
    CHECK_EQ(parseLine("Anton, wo warst du letzte Nacht?").intent, Intent::AskAlibi);
    CHECK_EQ(parseLine("Wen wählst du?").intent, Intent::AskVote);
    CHECK_EQ(parseLine("Wen wählst du?").topic, kNoPlayer);
    CHECK_EQ(parseLine("Wen verdächtigst du?").topic, kNoPlayer);
    CHECK_EQ(parseLine("Wer ist verdächtig?").intent, Intent::AskSuspicion);
    CHECK_EQ(parseLine("Wer ist unschuldig?").intent, Intent::AskInnocence);
    CHECK_EQ(parseLine("Warum?", 2).topic, 2);
    CHECK_EQ(parseLine("Er ist unschuldig", 2).topic, 2);
    CHECK(weight(parseLine("Er ist unschuldig", 2), 2) < 0);
    CHECK_EQ(parseLine("Ich bin die Seherin").claimedRole, Role::Seer);
    CHECK_EQ(parseLine("I'm the seer").claimedRole, Role::Seer);
    CHECK_EQ(parseLine("Ich bin nicht die Seherin").claimedRole, Role::Unknown);
    CHECK_EQ(parseLine("Ich war in der Kirche").place, Place::Chapel);
    CHECK_EQ(parseLine("I was at home").place, Place::Home);
    CHECK_EQ(parseLine("Hallo Anton").addressed, 1);
}

TEST(MemoryCapsSpamTracksSourcesAndContradictions) {
    Memory memory;
    const auto accusation = parseLine("Anna ist ein Wolf");
    CHECK(!memory.remember(0, accusation, 1));
    const float once = memory.suspicion(2, 1);
    for (int i = 0; i < 100; ++i) CHECK(!memory.remember(0, accusation, 1));
    CHECK_EQ(memory.suspicion(2, 1), once);
    CHECK_EQ(memory.source(2, 1), 0);
    CHECK(memory.suspicion(2, 3) < once);
    memory.remember(4, accusation, 1);
    CHECK(memory.suspicion(2, 1) > once);
    CHECK(memory.remember(0, parseLine("Anna ist unschuldig"), 1));
    CHECK(memory.credibility(0) < 0.8f);
    CHECK(!memory.remember(0, parseLine("Ich lag falsch. Anna ist ein Wolf"), 1));
    CHECK(!memory.remember(0, parseLine("Ich bin die Seherin"), 1));
    CHECK(memory.remember(0, parseLine("Ich bin die Hexe"), 1));
    CHECK(!memory.remember(0, parseLine("Ich war die ganze Nacht zu Hause"), 1));
    CHECK(memory.remember(0, parseLine("Ich war in der Kirche"), 1));
    CHECK(!memory.remember(0, parseLine("Ich war zu Hause"), 2));
    CHECK(!memory.remember(0, parseLine("Ich war am Brunnen"), 2)); // visiting two places is possible
    for (uint8_t speaker = 0; speaker < 16; ++speaker) memory.remember(speaker, accusation, 2);
    CHECK(memory.suspicion(2, 2) <= 4.0f);
}

TEST(MemoryUsesOnlyRevealedRoles) {
    Memory memory;
    memory.remember(0, parseLine("Anna ist ein Wolf"), 1);
    GameView view;
    PlayerView anna;
    anna.id = 2;
    anna.alive = false;
    anna.role = Role::Unknown;
    view.players.push_back(anna);
    memory.observeRevealed(view);
    CHECK_EQ(memory.credibility(0), 0.8f);
    view.players.front().role = Role::Villager;
    memory.observeRevealed(view);
    CHECK(memory.credibility(0) < 0.8f);
    const float checked = memory.credibility(0);
    memory.observeRevealed(view);
    CHECK_EQ(memory.credibility(0), checked);
}

TEST(BotRespondsToAccusationsAndFollowupWithSource) {
    Conversation c;
    auto text = c.ask("Anton, Anna ist ein Wolf");
    CHECK(text.de.find("Anna") != std::string::npos);
    CHECK(text.de.find("beobachtet") != std::string::npos);
    text = c.ask("Anton, warum?");
    CHECK(text.de.find("Spieler hat Anna beschuldigt") != std::string::npos);
    CHECK(text.de.find("kein Beweis") != std::string::npos);
    CHECK(text.en.find("not proof") != std::string::npos);
    text = c.ask("Anton, Anna ist unschuldig");
    CHECK(text.de.find("widerspricht") != std::string::npos);
}

TEST(BotDefendsItselfAndClarifiesUnknownText) {
    Conversation c;
    CHECK(c.ask("Anton ist ein Wolf").de.starts_with("Spieler, ich widerspreche"));
    CHECK(c.ask("Anton, blablabla").de.find("Wen meinst du") != std::string::npos);
    CHECK(c.ask("Anton, ich bin die Seherin").de.find("geprüft") != std::string::npos);
    CHECK(c.ask("Anton, ich war zu Hause").de.find("bestätigen") != std::string::npos);
}

TEST(BotDoesNotTreatQuestionsOrNamesAsProof) {
    Conversation c;
    for (int i = 0; i < 30; ++i) {
        c.hear("Ist Anna ein Wolf?", false);
        c.hear("Anna war hier", false);
    }
    auto response = c.ask("Anton, warum Anna?");
    CHECK(response.de.find("keinen belastbaren Hinweis") != std::string::npos);
    CHECK(response.de.find("beschuldigt") == std::string::npos);
}

TEST(BotReplyDelayCooldownAndNoBotEcho) {
    Conversation c;
    c.hear("Anton, hallo");
    CHECK(c.replies.empty());
    c.advance(0.5f);
    CHECK(c.replies.empty());
    c.advance(2.5f);
    CHECK_EQ(c.replies.size(), 1u);
    CHECK_EQ(c.bot.replyPriority(c.match, 0, Text::same("Anton, hallo"), ChatChannel::Global), 0);
    c.hear("Anton, hallo");
    c.advance(1.0f);
    CHECK_EQ(c.replies.size(), 1u);
    // A bot's message must not generate either a response or second-hand evidence.
    auto m = matchWith();
    Bot listener(3, 42);
    phase(m, Phase::Assembly);
    listener.update(0.1f, m, [](uint8_t, const Text&, bool) {});
    CHECK_EQ(listener.replyPriority(m, 1, Text::same("Ann, Anna ist Wolf"), ChatChannel::Global), 0);
}

TEST(ChannelDistanceAndPhaseBoundaries) {
    Conversation c;
    CHECK_EQ(c.bot.replyPriority(c.match, 0, Text::same("Anton, hallo"), ChatChannel::Wolves), 0);
    c.hear("Anton, hallo");
    phase(c.match, Phase::Vote);
    c.advance(3.0f);
    CHECK(c.replies.empty()); // queued reply from an earlier phase is discarded
    phase(c.match, Phase::Night);
    c.bot.update(0.1f, c.match, c.say);
    c.match.teleport(0, world().ground(-60, -60));
    c.match.teleport(1, world().ground(60, 60));
    CHECK_EQ(c.bot.replyPriority(c.match, 0, Text::same("Anton, hallo"), ChatChannel::Nearby), 0);
}

TEST(ChatChangesVotesAcrossSeeds) {
    int accusationVotes = 0, defenceVotes = 0;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        for (bool accuse : {false, true}) {
            auto m = matchWith();
            phase(m, Phase::Vote);
            Bot bot(1, seed);
            bot.update(0.01f, m, [](uint8_t, const Text&, bool) {});
            bot.hear(m, 0, Text::same(accuse ? "Anna ist ein Wolf" : "Anna ist kein Wolf"), ChatChannel::Global, false);
            for (int i = 0; i < 70; ++i) {
                m.update(0.1f);
                bot.update(0.1f, m, [](uint8_t, const Text&, bool) {});
            }
            if (m.find(1)->vote == 2) { if (accuse) ++accusationVotes; else ++defenceVotes; }
        }
    }
    std::printf("    Votes for Anna: accusation %d/80, defence %d/80\n", accusationVotes, defenceVotes);
    CHECK(accusationVotes >= 50);
    CHECK(accusationVotes > defenceVotes + 30);
}

TEST(NewEvidenceCanChangeAnAlreadyCastVote) {
    int reconsidered = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        auto m = matchWith();
        phase(m, Phase::Vote);
        Bot bot(1, seed);
        const Bot::SayFn say = [](uint8_t, const Text&, bool) {};
        for (int i = 0; i < 65; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
        const uint8_t previous = m.find(1)->vote;
        const uint8_t accused = previous == 2 ? 4 : 2;
        bot.hear(m, 0, Text::same(m.find(accused)->name + " ist ein Wolf"), ChatChannel::Global, false);
        for (int i = 0; i < 35; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
        if (m.find(1)->vote == accused) ++reconsidered;
    }
    CHECK(reconsidered >= 35);
}

TEST(OwnSeerEvidenceOutweighsChatAndHiddenRolesStayUnknown) {
    auto m = matchWith(Role::Seer);
    phase(m, Phase::Night);
    m.teleport(1, world().ground(4, 9));
    m.teleport(2, world().ground(6, 9));
    CHECK(m.interact(1, Interaction::Inspect, 2).empty());
    phase(m, Phase::Vote);
    Bot bot(1, 42);
    const Bot::SayFn say = [](uint8_t, const Text&, bool) {};
    bot.update(0.01f, m, say);
    for (int i = 0; i < 100; ++i) bot.hear(m, 0, Text::same("Anna ist unschuldig"), ChatChannel::Global, false);
    for (int i = 0; i < 70; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
    CHECK_EQ(m.find(1)->vote, 2);
    std::vector<Text> replies;
    bot.hear(m, 0, Text::same("Anton, wen verdächtigst du?"), ChatChannel::Global, true);
    for (int i = 0; i < 30; ++i) {
        m.update(0.1f);
        bot.update(0.1f, m, [&](uint8_t, const Text& text, bool) { replies.push_back(text); });
    }
    CHECK(!replies.empty());
    if (!replies.empty()) CHECK(replies.front().de.find("Anna als Seherin geprüft") != std::string::npos);
    Conversation unknown;
    const auto answer = unknown.ask("Anton, ist Anna ein Werwolf?");
    CHECK(answer.de.find("keinen belastbaren Hinweis") != std::string::npos);
    CHECK(answer.de.find("geprüft") == std::string::npos);
}

TEST(NoSecretReplyLeaksAfterDawnAndNoDistantReply) {
    auto m = matchWith(Role::Werewolf);
    phase(m, Phase::Night);
    Bot bot(1, 42);
    std::vector<std::pair<Text, bool>> replies;
    const Bot::SayFn say = [&](uint8_t, const Text& t, bool secret) { replies.push_back({t, secret}); };
    bot.update(0.01f, m, say);
    bot.hear(m, 2, Text::same("Anton, hallo"), ChatChannel::Wolves, true);
    for (int i = 0; i < 30; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
    CHECK_EQ(replies.size(), 1u);
    if (!replies.empty()) CHECK(replies.front().second);
    for (int i = 0; i < 55; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
    replies.clear();
    bot.hear(m, 2, Text::same("Anton, Anna ist ein Wolf"), ChatChannel::Wolves, true);
    phase(m, Phase::Morning);
    for (int i = 0; i < 30; ++i) { m.update(0.1f); bot.update(0.1f, m, say); }
    CHECK(replies.empty());
}

TEST(EverydayWordsAndVocatives) {
    CHECK(weight(parseLine("Anna verhält sich komisch"), 2) > 0);
    CHECK(weight(parseLine("Berta raus!"), 4) > 0);
    CHECK(weight(parseLine("ok wir voten alle Berta"), 4) > 0);
    CHECK(weight(parseLine("Anna ist böse"), 2) > 1.5f);
    CHECK(weight(parseLine("Anna ist harmlos"), 2) < 0);
    CHECK(weight(parseLine("Anna is sketchy"), 2) > 0);
    auto m = parseLine("Anton du bist sus");
    CHECK_EQ(m.addressed, 1);
    CHECK(weight(m, 1) > 0);
    CHECK_EQ(parseLine("Anton was denkst du über Anna").addressed, 1);
    CHECK_EQ(parseLine("Anton was denkst du über Anna").topic, 2);
    CHECK_EQ(parseLine("Wen votet ihr?").intent, Intent::AskVote);
    CHECK_EQ(parseLine("Servus Anton").addressed, 1);
    CHECK(parseLine("Anton, bist du ein Wolf?").wolfWord);
    CHECK(parseLine("Ich war draußen am Brunnen").claims.empty());
}

TEST(BotTalksAboutItselfInFirstPerson) {
    Conversation c;
    auto text = c.ask("Anton, was denkst du?");
    CHECK(text.de.find("Anton") == std::string::npos);
    text = c.ask("Anton, bist du ein Werwolf?");
    CHECK(text.de.starts_with("Spieler, nein, ich bin kein Werwolf"));
    CHECK(text.en.find("I'm not a werewolf") != std::string::npos);
    text = c.ask("Anton, kann man dir vertrauen?");
    CHECK(text.de.find("Anton") == std::string::npos);
    CHECK(text.de.find("kein Werwolf") != std::string::npos);
    text = c.ask("Ich vertraue Anton");
    CHECK(text.de.find("danke für dein Vertrauen") != std::string::npos);
    CHECK(text.de.find("Anton") == std::string::npos);
    // The reply continues the sentence after the name: "Spieler, das kann ich …", not "Spieler, Das …".
    text = c.ask("Anton, blablabla");
    CHECK(text.de.starts_with("Spieler, das kann"));
    // asked the same again, a person says "like I said" instead of repeating the sentence
    text = c.ask("Anton, blablabla");
    CHECK(text.de.find("Wie gesagt") != std::string::npos || text.de.find("wie gesagt") != std::string::npos);
}

TEST(QuestionsWaitWhileTheBotIsBusy) {
    Conversation c;
    c.hear("Anton, hallo");
    c.advance(3.0f);
    CHECK_EQ(c.replies.size(), 1u);
    // Right after an answer the bot is busy, but a new question meant for it is queued, not dropped.
    CHECK(c.bot.replyPriority(c.match, 0, Text::same("Anton was denkst du über Anna"), ChatChannel::Global) > 0);
    c.hear("Anton was denkst du über Anna");
    c.hear("Anton, wo warst du letzte Nacht?");
    c.advance(1.0f);
    CHECK_EQ(c.replies.size(), 1u);
    c.advance(9.0f);
    CHECK_EQ(c.replies.size(), 3u);
    if (c.replies.size() == 3) {
        CHECK(c.replies[1].de.find("Anna") != std::string::npos);
        CHECK(c.replies[2].de.find("Nacht") != std::string::npos);
    }
    // While busy, small talk from someone else is left to other bots.
    c.hear("Anton, hallo");
    CHECK_EQ(c.bot.replyPriority(c.match, 3, Text::same("Wer ist verdächtig?"), ChatChannel::Global), 0);
}

TEST(DeadPlayersAreNoticed) {
    // each in a fresh conversation: asked twice in a row, the bot would say "wie gesagt" instead
    for (const char* line : {"Berta ist ein Wolf", "Berta du bist voll sus", "Berta, wo warst du letzte Nacht?"}) {
        Conversation c;
        c.match.playerLeft(4);
        CHECK(!c.match.find(4)->alive);
        CHECK(c.ask(line).de.find("Berta ist schon tot") != std::string::npos);
    }
}

namespace {
// Several bots that hear each other through the same routing as the server.
struct Village {
    Match match;
    std::vector<Bot> bots;
    eng::Rng rng{7};
    std::vector<std::pair<uint8_t, Text>> said;
    Bot::SayFn say = [&](uint8_t id, const Text& text, bool secret) {
        said.push_back({id, text});
        if (const auto channel = match.chatChannel(id, secret)) deliverChat(bots, match, id, text, *channel, rng);
    };
    explicit Village(uint64_t seed, std::vector<Role> roles = {Role::Villager, Role::Seer, Role::Werewolf, Role::Villager,
                                                                Role::Villager, Role::Villager}) {
        Settings settings;
        settings.captain = false;
        settings.discussionSeconds = 60;
        settings.voteSeconds = 30;
        const std::vector<Match::Seat> seats = {{0, "Spieler", false}, {1, "Anton", true}, {2, "Anna", true},
            {3, "Ann", true}, {4, "Berta", true}, {5, "Jörg", true}};
        Text error;
        CHECK(match.startWithRoles(seats, roles, settings, world(), 42, error));
        for (uint8_t id = 1; id <= 5; ++id) bots.emplace_back(id, seed * 10 + id);
    }
    bool heard(uint8_t id, const std::string& part) const {
        return std::any_of(said.begin(), said.end(), [&](const auto& s) { return s.first == id && s.second.de.find(part) != std::string::npos; });
    }
    void advance(float seconds) {
        for (float t = 0; t < seconds; t += 0.1f) {
            match.update(0.1f);
            for (Bot& b : bots) b.update(0.1f, match, say);
        }
    }
    void human(const std::string& text) { deliverChat(bots, match, 0, Text::same(text), *match.chatChannel(0, false), rng); }
    void inspectAnna() {
        phase(match, Phase::Night);
        match.teleport(1, world().ground(4, 9));
        match.teleport(2, world().ground(6, 9));
        CHECK(match.interact(1, Interaction::Inspect, 2).empty());
    }
};
}

TEST(BotsActOnOtherBotsFirstHandEvidence) {
    int withEvidence = 0, without = 0, votes = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        for (bool reveal : {false, true}) {
            Village v(seed);
            if (reveal) v.inspectAnna(); // a seer with a wolf result now reveals proactively at the vote
            phase(v.match, Phase::Assembly);
            v.advance(0.5f);
            if (reveal) {
                v.human("Anton, wen verdächtigst du?");
                v.advance(4.0f);
            }
            phase(v.match, Phase::Vote);
            v.advance(8.0f);
            const uint8_t voters[] = {3, 4, 5};
            for (uint8_t voter : voters) {
                const uint8_t vote = v.match.find(voter)->vote;
                if (reveal) { ++votes; withEvidence += vote == 2; }
                else without += vote == 2;
            }
        }
    }
    std::printf("    Votes of other bots for Anna: after the seer bot's answer %d/%d, without it %d/%d\n",
                withEvidence, votes, without, votes);
    CHECK(withEvidence * 10 >= votes * 6);
    CHECK(withEvidence > without * 2);
}

TEST(AccusedBotDeniesAndAnswersNeverChain) {
    Village v(3);
    v.inspectAnna();
    phase(v.match, Phase::Assembly);
    v.advance(0.5f);
    const Text line = Text::same("Ich bin die Seherin! Anna ist ein Werwolf!");
    const Bot::Speech seer{Bot::Evidence{Bot::Evidence::Kind::Seer, 2, 2.4f}, false};
    Bot::Speech answer = seer;
    answer.reply = true;
    const Bot::Speech rumour;
    Bot& anna = v.bots[1];
    CHECK_EQ(anna.replyPriority(v.match, 1, line, ChatChannel::Global, &seer), 90);
    CHECK_EQ(anna.replyPriority(v.match, 1, line, ChatChannel::Global, &answer), 0);
    CHECK_EQ(v.bots[2].replyPriority(v.match, 1, line, ChatChannel::Global, &rumour), 0);
    CHECK_EQ(v.bots[2].replyPriority(v.match, 1, line, ChatChannel::Global, &seer), 0); // nothing to add
    v.said.clear();
    anna.hear(v.match, 1, line, ChatChannel::Global, true, &seer);
    v.advance(4.0f);
    const bool denied = std::any_of(v.said.begin(), v.said.end(), [](const auto& s) {
        return s.first == 2 && s.second.de.starts_with("Anton, ") && s.second.de.find("Seherin") != std::string::npos;
    });
    CHECK(denied);
}

TEST(SeerResultsInChat) {
    // "sie" is the person named before, never the speaker.
    auto m = parseLine("ich bin seher, ich hab Anna gecheckt, sie ist ein Werwolf");
    CHECK_EQ(m.claimedRole, Role::Seer);
    CHECK(m.seerReport);
    CHECK_EQ(weight(m, 2), kSeerWolf);
    CHECK_EQ(weight(m, 0), 0.0f);
    m = parseLine("ich bin immer noch seher. heute nacht hab ich Berta gecheckt, Berta ist die Hexe");
    CHECK_EQ(m.claimedRole, Role::Seer); // not the witch named later
    CHECK_EQ(m.roleReports.size(), 1u);
    if (!m.roleReports.empty()) CHECK_EQ(m.roleReports.front().role, Role::Witch);
    CHECK_EQ(weight(m, 4), kSeerGood);
    CHECK(weight(parseLine("Anna ist gut"), 2) < 0);
    CHECK(!parseLine("Anna ist gut").seerReport);
    CHECK(parseLine("Ich vertraue Anna").claims.front().fact == false);
    // A seer who claimed earlier: "Anna ist böse" is a result, a vote call is not.
    m = parseLine("Anna ist böse");
    CHECK(presentSeerResults(m));
    CHECK_EQ(weight(m, 2), kSeerWolf);
    m = parseLine("wir müssen Anna voten");
    CHECK(!presentSeerResults(m));
    CHECK_EQ(parseLine("warum glaubt mir keiner?").intent, Intent::AskBelief);
}

TEST(SeerReportsAreCheckedAgainstRevealedRoles) {
    Memory memory;
    auto seer = parseLine("ich bin seher, Anna ist ein Werwolf und Berta ist die Hexe");
    memory.remember(0, seer, 1);
    const float believed = memory.suspicion(2, 1);
    memory.remember(0, parseLine("wir müssen Anna voten"), 1);
    CHECK_EQ(memory.suspicion(2, 1), believed); // a vote call does not weaken the result
    CHECK_EQ(memory.suspicion(2, 3), believed); // nor does a seer's result fade
    CHECK(memory.suspicion(0, 1) < 0);          // a claimed seer is kept alive
    GameView view;
    PlayerView berta;
    berta.id = 4;
    berta.alive = false;
    berta.role = Role::Witch;
    view.players.push_back(berta);
    memory.observeRevealed(view);
    CHECK_EQ(memory.confirmed(0), 1);
    CHECK(memory.credibility(0) > 0.8f);
    Memory liar;
    liar.remember(0, parseLine("ich bin seher, Berta ist ein Werwolf"), 1);
    liar.observeRevealed(view);
    CHECK(liar.liar(0));
    CHECK(liar.suspicion(0, 1) > 2.0f);
    // a "seer" executed and revealed as a werewolf lied – and is no longer a rival to the real one
    Memory rival;
    rival.remember(2, parseLine("ich bin seher"), 1);
    CHECK(rival.otherSeer(0));
    GameView executed;
    PlayerView anna;
    anna.id = 2;
    anna.alive = false;
    anna.role = Role::Werewolf;
    executed.players.push_back(anna);
    rival.observeRevealed(executed);
    CHECK(!rival.otherSeer(0));
    CHECK(rival.liar(2));
}

TEST(NamedBotsConfirmOrRefuteTheSeer) {
    // The human really is the seer here; Ann is the witch, Anna a werewolf.
    Village v(5, {Role::Seer, Role::Villager, Role::Werewolf, Role::Witch, Role::Villager, Role::Villager});
    phase(v.match, Phase::Assembly);
    v.advance(0.5f);
    v.human("ich bin seher");
    v.advance(6.0f);
    v.said.clear();
    v.human("ich hab Anna gecheckt, sie ist ein Werwolf");
    v.advance(8.0f);
    CHECK(v.heard(2, "Du lügst"));           // the wolf denies
    CHECK(v.heard(1, "Anna") || v.heard(3, "Anna") || v.heard(4, "Anna") || v.heard(5, "Anna")); // the room reacts, too
    for (const auto& s : v.said) CHECK(s.second.de.find("Verdacht gegen Spieler") == std::string::npos);
    // After one night the seer knows one result; a second one now would be impossible, so this is a fresh game.
    Village witch(5, {Role::Seer, Role::Villager, Role::Werewolf, Role::Witch, Role::Villager, Role::Villager});
    phase(witch.match, Phase::Assembly);
    witch.advance(0.5f);
    witch.human("ich bin seher, ich hab Ann gecheckt, Ann ist die Hexe");
    witch.advance(6.0f);
    CHECK(witch.heard(3, "Spieler, stimmt, ich bin die Hexe"));
    // In the default village Anton is the real seer and Berta a villager: a false claim is called out.
    Village fake(5);
    phase(fake.match, Phase::Assembly);
    fake.advance(0.5f);
    fake.human("ich bin seher, Berta ist ein Werwolf");
    fake.advance(8.0f);
    CHECK(fake.heard(1, "ich bin die Seherin, nicht du"));
    CHECK(fake.heard(4, "Das ist gelogen") || fake.heard(4, "das ist gelogen"));
}

TEST(VillagersFollowABelievedSeerAndKeepThemAlive) {
    int onWolf = 0, onSeer = 0, votes = 0, counterclaims = 0;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Village v(seed, {Role::Seer, Role::Villager, Role::Werewolf, Role::Witch, Role::Villager, Role::Villager});
        phase(v.match, Phase::Assembly);
        v.advance(0.5f);
        v.human("ich bin seher, ich hab Anna gecheckt, sie ist ein Werwolf");
        v.advance(6.0f);
        counterclaims += v.heard(2, "ich bin die Seherin") || v.heard(2, "Ich bin die Seherin");
        v.human("wir müssen Anna voten");
        v.advance(4.0f);
        phase(v.match, Phase::Vote);
        v.advance(10.0f);
        const uint8_t voters[] = {1, 3, 4, 5};
        for (uint8_t voter : voters) {
            const uint8_t vote = v.match.find(voter)->vote;
            ++votes;
            onWolf += vote == 2;
            onSeer += vote == 0;
        }
    }
    std::printf("    Village votes after the seer's result: Anna (wolf) %d/%d, the seer %d/%d (Anna counterclaimed in %d of 30)\n", onWolf,
                votes, onSeer, votes, counterclaims);
    CHECK(counterclaims > 0); // the wolf turns it around sometimes, but whoever claims only after being accused is doubted
    CHECK(onWolf * 10 >= votes * 8);
    CHECK(onSeer * 20 <= votes); // a stray vote after a wolf's counterclaim, as among people, but no lynch mob
}

TEST(ResultsAboutTheDeadAreCheckedAtOnce) {
    // Naming a role that is already public proves nothing ...
    Conversation truthful;
    truthful.match.playerLeft(3);
    auto text = truthful.ask("ich bin seher, ich habe Ann gecheckt, sie ist eine Dorfbewohnerin");
    CHECK(text.de.find("Ann war ja schon aufgedeckt") != std::string::npos);
    text = truthful.ask("Anton, warum glaubt mir keiner?");
    CHECK(text.de.find("noch hat niemand deine Angaben bestätigt") != std::string::npos);
    // ... but a wrong one gives the liar away.
    Conversation lying;
    lying.match.playerLeft(3);
    text = lying.ask("ich bin seher, Ann ist ein Werwolf");
    CHECK(text.de.find("Ann war aber ein Dorfbewohner") != std::string::npos);
    text = lying.ask("Anton, warum glaubt mir keiner?");
    CHECK(text.de.find("nicht gestimmt") != std::string::npos);
}

TEST(PersonalitiesAndWritingStyles) {
    const auto a = Bot::Personality::random(1), b = Bot::Personality::random(2);
    CHECK(a.talk != b.talk || a.trust != b.trust);
    CHECK(Bot(1, 1).personality().style == Bot::Personality::Style::Formal); // neutral unless chosen
    const Text line{"Ich habe Anna geprüft: Werwolf. Glaubt mir. Wirklich.", "I have checked Anna: werewolf. Believe me. Really."};
    CHECK_EQ(Bot::styled(line, Bot::Personality::Style::Formal).de, line.de);
    CHECK_EQ(Bot::styled(line, Bot::Personality::Style::Casual).de, std::string("hab Anna geprüft: Werwolf. Glaubt mir. Wirklich"));
    CHECK_EQ(Bot::styled(Text::same("Berta hat für Ann gestimmt."), Bot::Personality::Style::Casual).de,
             std::string("Berta hat für Ann gestimmt")); // names keep their capital
    CHECK_EQ(Bot::styled(line, Bot::Personality::Style::Casual).en, std::string("I've checked Anna: werewolf. Believe me. Really"));
    CHECK_EQ(Bot::styled(line, Bot::Personality::Style::Terse).de, std::string("Ich habe Anna geprüft: Werwolf. Glaubt mir"));
}

TEST(FakeSeerBotIsCalledOut) {
    // Anna (wolf bot) pretends to be the seer and accuses Berta; the real seer Anton and Berta know better.
    Village v(4);
    phase(v.match, Phase::Assembly);
    v.advance(0.5f);
    const Text line = Text::same("Ich bin die Seherin! Berta ist ein Werwolf!");
    const Bot::Speech fake{Bot::Evidence{Bot::Evidence::Kind::Seer, 4, kSeerWolf}, false};
    for (Bot& bot : v.bots) bot.hear(v.match, 2, line, ChatChannel::Global, bot.id() == 1 || bot.id() == 4, &fake);
    v.advance(6.0f);
    CHECK(v.heard(1, "ich bin die Seherin, nicht du"));
    CHECK(v.heard(4, "das ist gelogen"));
}

TEST(ExecutionsTeachFromVotes) {
    int reactions = 0, conclusions = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Village v(seed);
        phase(v.match, Phase::Vote);
        v.advance(8.0f);
        const uint8_t voters[] = {0, 1, 2, 4, 5};
        for (uint8_t voter : voters) v.match.interact(voter, Interaction::Vote, 3); // Ann is a villager
        v.advance(1.0f);
        phase(v.match, Phase::Execution);
        v.advance(5.0f);
        CHECK(!v.match.find(3)->alive);
        for (const auto& [id, text] : v.said)
            reactions += text.de.find("unschuldig") != std::string::npos || text.de.find("tut mir leid") != std::string::npos ||
                         text.de.find("Fehler") != std::string::npos;
        v.said.clear();
        phase(v.match, Phase::Assembly);
        v.advance(40.0f);
        for (const auto& [id, text] : v.said) conclusions += text.de.find("für Ann gestimmt, und Ann war unschuldig") != std::string::npos;
    }
    std::printf("    After executing an innocent: %d reactions, %d conclusions from the votes\n", reactions, conclusions);
    CHECK(reactions > 0);
    CHECK(conclusions > 0);
}

TEST(BotsTurnToWhoeverSpeaks) {
    Village v(2);
    phase(v.match, Phase::Assembly);
    v.advance(1.0f);
    v.human("Anton, hallo");
    v.advance(1.5f);
    const auto* anton = v.match.find(1);
    const auto* human = v.match.find(0);
    const float want = std::atan2(human->position.x - anton->position.x, human->position.z - anton->position.z);
    CHECK(std::abs(wrapAngle(want - anton->yaw)) < 0.4f);
}

TEST(VillagersFleeFromAWerewolf) {
    int escaped = 0, shouted = 0, runs = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        Village v(seed);
        phase(v.match, Phase::Night);
        v.advance(0.5f);
        const Vec3 annStart = world().ground(4, 9), annaStart = world().ground(6, 19);
        v.match.teleport(3, annStart);
        v.match.teleport(2, annaStart);
        CHECK(v.match.hasLineOfSight(annStart, annaStart));
        CHECK(v.match.interact(2, Interaction::Transform, 1).empty());
        v.advance(5.0f); // the transformation takes a moment, then Ann runs
        ++runs;
        const auto* ann = v.match.find(3);
        const bool inside = world().houseAt({ann->position.x, ann->position.z}) >= 0;
        escaped += ann->alive && (inside || distanceXZ(ann->position, annStart) > 6.0f);
        shouted += v.heard(3, "Werwolf");
    }
    std::printf("    Ann ran from the werewolf in %d of %d nights and cried for help in %d\n", escaped, runs, shouted);
    CHECK(escaped * 4 >= runs * 3);
    CHECK(shouted > 0);
}

TEST(VampireBotsLieHuntAndAreFeared) {
    CHECK(weight(parseLine("Anna ist ein Vampir"), 2) > 1.5f);
    CHECK_EQ(weight(parseLine("ich bin seher, Anna ist eine Vampirin"), 2), kSeerWolf);
    // a vampire named by the seer denies it instead of confirming
    {
        Village v(3, {Role::Seer, Role::Villager, Role::Vampire, Role::Villager, Role::Villager, Role::Villager});
        phase(v.match, Phase::Assembly);
        v.advance(0.5f);
        v.human("ich bin seher, ich hab Anna gecheckt, sie ist ein Vampir");
        v.advance(8.0f);
        CHECK(!v.heard(2, "stimmt, ich") && !v.heard(2, "Stimmt, ich"));
        CHECK(v.heard(2, "lügst") || v.heard(2, "stimmt nicht") || v.heard(2, "Seherin"));
    }
    // and at night the clan feeds
    int bitten = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Village v(seed, {Role::Villager, Role::Villager, Role::Vampire, Role::Villager, Role::Villager, Role::Villager});
        phase(v.match, Phase::Night);
        v.advance(60.0f);
        for (const auto& p : v.match.players()) bitten += !p.alive && p.deathCause == DeathCause::Vampire;
    }
    std::printf("    The vampire bot found a victim in %d of 6 nights\n", bitten);
    CHECK(bitten >= 3);
}

TEST(NoSeerResultsBeforeTheFirstNight) {
    // Day 1 with a captain's election: the village gathers before anyone could have checked anyone.
    Match m;
    Settings settings;
    settings.captain = true;
    settings.discussionSeconds = 60;
    const std::vector<Match::Seat> seats = {{0, "Spieler", false}, {1, "Anton", true}, {2, "Anna", false},
        {3, "Ann", false}, {4, "Berta", false}, {5, "Jörg", false}};
    Text error;
    CHECK(m.startWithRoles(seats, {Role::Seer, Role::Villager, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager},
                           settings, world(), 42, error));
    phase(m, Phase::Assembly);
    CHECK_EQ(m.day(), 1);
    Bot bot(1, 55);
    std::vector<Text> replies;
    const Bot::SayFn say = [&](uint8_t, const Text& text, bool) { replies.push_back(text); };
    auto advance = [&](float seconds) {
        for (float t = 0; t < seconds; t += 0.1f) { m.update(0.1f); bot.update(0.1f, m, say); }
    };
    bot.update(0.01f, m, say);
    advance(1.0f);
    bot.hear(m, 0, Text::same("ich bin seher, ich hab Anna gecheckt, sie ist ein Werwolf"), ChatChannel::Global, true);
    advance(6.0f);
    CHECK(std::any_of(replies.begin(), replies.end(), [](const Text& t) { return t.de.find("noch gar keine Nacht") != std::string::npos; }));
    // just claiming the role is fine on day 1 – only results are impossible
    CHECK_EQ(Bot::nightsSoFar(m), 0);

    // Whoever is not named in a seer's report takes up to ~6 s to react, longer than ask() waits.
    auto reply = [](Conversation& c, const std::string& text) {
        c.advance(5.1f);
        c.replies.clear();
        c.hear(text);
        c.advance(7.0f);
        CHECK(!c.replies.empty());
        return c.replies.empty() ? Text{} : c.replies.front();
    };
    // after one night: one result is plausible, two are not
    Conversation one;
    CHECK(reply(one, "ich bin seher, ich hab Anna gecheckt, sie ist ein Werwolf").de.find("Nacht") == std::string::npos);
    Conversation two;
    CHECK(reply(two, "ich bin seher, Anna ist ein Werwolf und Berta ist gut").de.find("erst eine Nacht") != std::string::npos);

    // day 3: two nights, so two results are fine – a third one is not
    Conversation later;
    phase(later.match, Phase::Night);
    phase(later.match, Phase::Assembly);
    CHECK_EQ(Bot::nightsSoFar(later.match), 2);
    CHECK(reply(later, "ich bin seher, Anna ist ein Werwolf und Berta ist gut").de.find("Nacht") == std::string::npos);
    CHECK(reply(later, "Ann ist auch gut").de.find("Nacht") == std::string::npos); // an opinion, not a third check
    CHECK(reply(later, "ich habe auch Jörg gecheckt, er ist gut").de.find("erst 2 Nächte") != std::string::npos);
}

TEST(AnOpinionIsNotACheck) {
    // the seer claims, gives an opinion, and reports one check during the first night – which is possible
    Match m;
    Settings settings;
    settings.captain = false;
    settings.nightSeconds = 120;
    const std::vector<Match::Seat> seats = {{0, "Spieler", false}, {1, "Anton", true}, {2, "Anna", false},
        {3, "Ann", false}, {4, "Berta", false}, {5, "Jörg", false}};
    Text error;
    CHECK(m.startWithRoles(seats, {Role::Seer, Role::Villager, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager},
                           settings, world(), 42, error));
    phase(m, Phase::Night);
    CHECK_EQ(Bot::nightsSoFar(m), 1);
    m.teleport(0, world().ground(4, 9));
    m.teleport(1, world().ground(5, 9));
    Bot bot(1, 55);
    std::vector<Text> replies;
    const Bot::SayFn say = [&](uint8_t, const Text& text, bool) { replies.push_back(text); };
    for (const char* line : {"ich bin seher", "Berta ist unschuldig", "ich habe Anna geprüft, sie ist ein Werwolf"}) {
        replies.clear();
        for (int i = 0; i < 52; ++i) { m.update(0.1f); bot.update(0.1f, m, say); m.teleport(1, world().ground(5, 9)); }
        bot.hear(m, 0, Text::same(line), ChatChannel::Nearby, true);
        for (int i = 0; i < 80; ++i) { m.update(0.1f); bot.update(0.1f, m, say); m.teleport(1, world().ground(5, 9)); }
        CHECK(!replies.empty());
        for (const auto& text : replies) CHECK(text.de.find("So viele Ergebnisse") == std::string::npos);
    }
}

TEST(BotsNameTheirRolesTruthfullyOrNot) {
    CHECK_EQ(parseLine("Wer ist der Jäger?").intent, Intent::AskRole);
    CHECK_EQ(parseLine("Wer ist der Jäger?").askedRole, Role::Hunter);
    CHECK_EQ(parseLine("Anton, bist du die Hexe?").askedRole, Role::Witch);
    CHECK_EQ(parseLine("Anton, welche Rolle hast du?").intent, Intent::AskRole);

    int hunterReveals = 0, consistent = 0, wolfTruths = 0, wolfLies = 0, villagerReveals = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Conversation hunter(Role::Hunter, seed);
        const Text first = hunter.ask("Anton, welche Rolle hast du?");
        if (first.de.find("Jäger") != std::string::npos) {
            ++hunterReveals;
            consistent += hunter.ask("Anton, welche Rolle hast du?").de.find("Wie gesagt") != std::string::npos ||
                          hunter.replies.front().de.find("wie gesagt") != std::string::npos;
        }
        Conversation villager(Role::Villager, seed);
        villagerReveals += villager.ask("Anton, welche Rolle hast du?").de.find("Dorfbewohner") != std::string::npos;
        Conversation wolf(Role::Werewolf, seed);
        const Text lie = wolf.ask("Anton, welche Rolle hast du?");
        wolfTruths += lie.de.find("Werwolf") != std::string::npos && lie.de.find("kein") == std::string::npos;
        wolfLies += lie.de.find("Dorfbewohner") != std::string::npos || lie.de.find("Jäger") != std::string::npos;
    }
    std::printf("    Asked for their role: hunter told %d/20 (consistent %d), villager %d/20, wolf lied %d/20, wolf told the truth %d/20\n",
                hunterReveals, consistent, villagerReveals, wolfLies, wolfTruths);
    CHECK(hunterReveals >= 6 && hunterReveals <= 18); // often, not always
    CHECK_EQ(consistent, hunterReveals);
    CHECK(villagerReveals >= 5);
    CHECK(wolfLies >= 5);
    CHECK_EQ(wolfTruths, 0);

    // whoever really has the role knows a claimant is lying
    Conversation real(Role::Hunter);
    CHECK(real.ask("ich bin der Jäger").de.find("ich bin der Jäger, nicht du") != std::string::npos);
    // a second claim on the same role draws attention
    Conversation room;
    room.ask("ich bin der Jäger");
    room.advance(5.1f);
    room.replies.clear();
    room.bot.hear(room.match, 3, Text::same("ich bin der Jäger"), ChatChannel::Global, true);
    room.advance(3.0f);
    CHECK(!room.replies.empty() && room.replies.front().de.find("einer von euch lügt") != std::string::npos);
    // a wolf bot faking the hunter is called out by the real hunter bot
    Village v(4, {Role::Villager, Role::Hunter, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager});
    phase(v.match, Phase::Assembly);
    v.advance(0.5f);
    const Bot::Speech fake{Bot::Evidence{Bot::Evidence::Kind::RoleClaim, 2, -0.5f, Role::Hunter}, false};
    for (Bot& b : v.bots) b.hear(v.match, 2, Text::same("Ich bin der Jäger."), ChatChannel::Global, b.id() == 1, &fake);
    v.advance(4.0f);
    CHECK(v.heard(1, "ich bin der Jäger, nicht du"));
}

TEST(BotsNameTheirRoleOnlyWhereItMakesSense) {
    CHECK(parseLine("möchte jemand seine rolle nennen").question);
    CHECK(parseLine("möchte jemand seine rolle nennen").toAll);
    CHECK_EQ(parseLine("Welche Rollen habt ihr?").intent, Intent::AskRole);
    CHECK(parseLine("Wer will seine Rolle sagen?").toAll);
    CHECK(!parseLine("Anton, welche Rolle hast du?").toAll);
    CHECK(!parseLine("kann sein, dass Anna ein Werwolf ist").question);
    CHECK(weight(parseLine("kann sein, dass Anna ein Werwolf ist"), 2) > 0.0f);

    // a question to the whole round gets several answers – the seer bot keeps her secret
    int rounds = 0, severalAnswers = 0, seerTold = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Village v(seed);
        phase(v.match, Phase::Assembly);
        v.advance(0.5f);
        v.said.clear();
        v.human("Möchte jemand seine Rolle nennen?");
        v.advance(12.0f);
        std::vector<uint8_t> speakers;
        for (const auto& [id, text] : v.said)
            if (text.de.find("Spieler") != std::string::npos && std::find(speakers.begin(), speakers.end(), id) == speakers.end())
                speakers.push_back(id);
        ++rounds;
        severalAnswers += speakers.size() >= 2;
        seerTold += v.heard(1, "bin die Seherin");
    }
    std::printf("    Asked the round: several answers in %d of %d, the seer told %d times\n", severalAnswers, rounds, seerTold);
    CHECK(severalAnswers * 3 >= rounds * 2);
    CHECK(seerTold <= 1);

    // unasked, only some name their role – and only where it makes sense; wolves lie about it
    struct Tally { Role role; int volunteered = 0; int truthful = 0; };
    std::vector<Tally> tally = {{Role::Villager}, {Role::Hunter}, {Role::Elder}, {Role::Seer}, {Role::Witch},
                                {Role::Protector}, {Role::Fox}, {Role::Werewolf}};
    for (Tally& t : tally)
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Conversation c(t.role, seed);
            c.advance(55.0f);
            for (const Text& line : c.replies)
                if (line.de.find("Damit ihr") != std::string::npos || line.de.find("Zur Info") != std::string::npos) {
                    ++t.volunteered;
                    t.truthful += line.de.find(roleName(t.role, Lang::German)) != std::string::npos;
                    break;
                }
        }
    for (const Tally& t : tally) std::printf("    %s: named the role unasked in %d of 20\n", roleName(t.role, Lang::German), t.volunteered);
    auto of = [&](Role role) -> const Tally& { return *std::find_if(tally.begin(), tally.end(), [&](const Tally& t) { return t.role == role; }); };
    CHECK(of(Role::Hunter).volunteered >= 2 && of(Role::Hunter).volunteered < 20);
    CHECK(of(Role::Villager).volunteered >= 1 && of(Role::Villager).volunteered < 15); // some, not all
    CHECK_EQ(of(Role::Hunter).truthful, of(Role::Hunter).volunteered);
    for (Role quiet : {Role::Seer, Role::Witch, Role::Protector, Role::Fox}) CHECK_EQ(of(quiet).volunteered, 0);
    CHECK(of(Role::Werewolf).volunteered >= 1);
    CHECK_EQ(of(Role::Werewolf).truthful, 0);
}

TEST(TheSeerWhispersToAProtectorSheTrusts) {
    CHECK(parseLine("Seherin, ich passe heute Nacht auf dich auf").protectOffer);
    CHECK(parseLine("ich beschütze die Seherin").seerWord);
    CHECK(!parseLine("Anna ist ein Werwolf").protectOffer);
    const std::string offer = "Ich bin der Beschützer. Seherin, sag mir heimlich, wer du bist – ich passe heute Nacht auf dich auf.";

    // Anton is the seer, Anna a wolf. The offer is made in front of everyone, the answer comes quietly the next day.
    int whispered = 0, warned = 0, wolfLied = 0, doubted = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        Village v(seed);
        phase(v.match, Phase::Assembly);
        v.advance(0.5f);
        v.human(offer);
        v.advance(8.0f);
        warned += v.heard(3, "verrate dich nicht") || v.heard(4, "verrate dich nicht") || v.heard(5, "verrate dich nicht");
        phase(v.match, Phase::Day);
        v.advance(60.0f);
        whispered += v.heard(1, "Psst, Spieler: Ich bin die Seherin");
        wolfLied += v.heard(2, "Psst, Spieler: Ich bin die Seherin");

        // a second "protector" in the room: she tells neither of them
        Village rival(seed);
        phase(rival.match, Phase::Assembly);
        rival.advance(0.5f);
        Bot::Speech claim;
        claim.evidence = Bot::Evidence{Bot::Evidence::Kind::RoleClaim, 4, -0.5f, Role::Protector};
        for (Bot& b : rival.bots) b.hear(rival.match, 4, Text::same("Ich bin der Beschützer."), ChatChannel::Global, false, &claim);
        rival.human(offer);
        rival.advance(8.0f);
        phase(rival.match, Phase::Day);
        rival.advance(60.0f);
        doubted += rival.heard(1, "Psst");
    }
    std::printf("    Offer to the seer: she whispered back in %d of 10, the wolf faked it in %d, warnings in %d; with a rival protector %d\n",
                whispered, wolfLied, warned, doubted);
    CHECK(whispered >= 2 && whispered < 10);
    CHECK(wolfLied >= 1);
    CHECK(warned >= 5);
    CHECK_EQ(doubted, 0);

    // a wolf bot's offer is a trap: the real protector calls it out
    Village trap(3, {Role::Villager, Role::Seer, Role::Werewolf, Role::Protector, Role::Villager, Role::Villager});
    phase(trap.match, Phase::Assembly);
    trap.advance(0.5f);
    Bot::Speech lure;
    lure.evidence = Bot::Evidence{Bot::Evidence::Kind::RoleClaim, 2, -0.5f, Role::Protector};
    lure.protectOffer = true;
    for (Bot& b : trap.bots) b.hear(trap.match, 2, Text::same(offer), ChatChannel::Global, true, &lure);
    trap.advance(6.0f);
    CHECK(trap.heard(3, "ich bin der Beschützer, nicht du"));
}

TEST(TheProtectorGuardsAKnownSeerAndTheWolvesHuntHer) {
    // the human seer has made herself known: the protector bot goes to her at night and watches over her
    int guarded = 0, nights = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Village v(seed, {Role::Seer, Role::Protector, Role::Villager, Role::Villager, Role::Villager, Role::Werewolf});
        phase(v.match, Phase::Assembly);
        v.advance(0.5f);
        v.human("Ich bin die Seherin.");
        v.advance(6.0f);
        phase(v.match, Phase::Night);
        v.advance(50.0f);
        ++nights;
        guarded += v.match.find(1)->lastProtected == 0;
    }
    std::printf("    The protector bot watched over the known seer in %d of %d nights\n", guarded, nights);
    CHECK(guarded * 3 >= nights * 2);

    // whoever whispers "I'm the seer" to a wolf is the pack's next victim
    int toldTheWolf = 0, keptQuiet = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed)
        for (const bool whisper : {true, false}) {
            Village v(seed, {Role::Seer, Role::Villager, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager});
            phase(v.match, Phase::Day);
            v.advance(0.5f);
            if (whisper) {
                v.match.teleport(0, world().ground(4, 9));
                v.match.teleport(2, world().ground(5, 9));
                v.bots[1].hear(v.match, 0, Text::same("Psst, Anna: Ich bin die Seherin. Pass heute Nacht auf mich auf."), ChatChannel::Nearby, false);
            }
            phase(v.match, Phase::Night);
            v.advance(60.0f);
            (whisper ? toldTheWolf : keptQuiet) += !v.match.find(0)->alive;
        }
    std::printf("    The seer was killed in %d of 8 nights after whispering to the wolf, %d of 8 otherwise\n", toldTheWolf, keptQuiet);
    CHECK(toldTheWolf > keptQuiet);
}

namespace {
// An open spot far from the fire and the houses: a victim, someone right beside them and a watcher 6 m away.
std::optional<std::array<Vec3, 3>> openSpot(std::optional<Vec2> awayFrom = {}) {
    const Match probe = matchWith();
    for (int r = 20; r <= 64; r += 4)
        for (int a = 0; a < 24; ++a) {
            const float angle = static_cast<float>(a) * 0.2618f;
            const Vec2 c{static_cast<float>(r) * std::cos(angle), static_cast<float>(r) * std::sin(angle)};
            if (awayFrom && (c - *awayFrom).length() < 30.0f) continue;
            const std::array<Vec2, 3> spots = {c, c + Vec2{1.0f, 0.0f}, c + Vec2{0.0f, 6.0f}};
            bool clear = true;
            for (Vec2 s : spots)
                clear = clear && world().houseAt(s, 1.0f) < 0 && (world().resolveCollision(s, 0.6f) - s).length() < 0.01f;
            for (const House& h : world().houses()) clear = clear && (Vec2{h.door.x, h.door.z} - c).length() > 12.0f;
            const std::array<Vec3, 3> at = {world().ground(spots[0].x, spots[0].y), world().ground(spots[1].x, spots[1].y),
                                            world().ground(spots[2].x, spots[2].y)};
            if (clear && probe.hasLineOfSight(at[2], at[0]) && probe.hasLineOfSight(at[2], at[1])) return at;
        }
    return std::nullopt;
}

// Plays through the morning into the assembly, so the bots review the night and speak.
void toTheAssembly(Village& v) {
    phase(v.match, Phase::Morning);
    v.advance(1.0f);
    phase(v.match, Phase::Day);
    v.advance(1.0f);
    phase(v.match, Phase::Assembly);
    v.advance(20.0f);
}
}

TEST(SeenNearTheBodyMeansSuspicious) {
    // what a person reports: whoever was seen there is suspicious – not the victim, and not an alibi
    CHECK(parseLine("Ich habe Anna heute Nacht bei der Leiche gesehen").sighting);
    CHECK(weight(parseLine("Ich habe Anna heute Nacht bei der Leiche gesehen"), 2) > 0.5f);
    const auto house = parseLine("Ich habe Anna heute Nacht beim Haus von Berta gesehen");
    CHECK(weight(house, 2) > 0.5f);
    CHECK_EQ(weight(house, 4), 0.0f);
    CHECK_EQ(house.topic, 2);
    CHECK(weight(parseLine("Anna war heute Nacht in der Nähe von Bertas Haus"), 2) > 0.5f);
    CHECK_EQ(weight(parseLine("Ich habe Anna nicht beim Haus gesehen"), 2), 0.0f);
    CHECK(weight(parseLine("Anna war die ganze Nacht bei mir im Haus"), 2) <= 0.0f);
    CHECK_EQ(weight(parseLine("Ich war heute Nacht zuhause"), 0), 0.0f);
    // the accused bot explains itself
    Conversation accused;
    const Text reply = accused.ask("Anton, ich habe dich heute Nacht bei der Leiche gesehen");
    CHECK(reply.de.find("Heimweg") != std::string::npos || reply.de.find("niemandem") != std::string::npos ||
          reply.de.find("Bestie") != std::string::npos);

    // what a bot sees: Anton watches the human stab Ann far from the houses and the fire, and tells the village
    // in the morning
    const auto spot = openSpot();
    CHECK(spot.has_value());
    if (!spot) return;
    const auto [victim, killer, watcher] = *spot;
    int told = 0, games = 0;
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        Village v(seed, {Role::SerialKiller, Role::Villager, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager});
        phase(v.match, Phase::Night);
        auto hold = [&](float seconds) {
            for (float t = 0; t < seconds; t += 0.1f) {
                v.match.teleport(0, killer);
                if (v.match.find(3)->alive) v.match.teleport(3, victim);
                v.match.teleport(1, watcher);
                v.match.update(0.1f);
                for (Bot& b : v.bots) b.update(0.1f, v.match, v.say);
            }
        };
        hold(3.0f);
        CHECK(v.match.interact(0, Interaction::Stab, 3).empty());
        hold(2.0f);
        CHECK(!v.match.find(3)->alive);
        toTheAssembly(v);
        ++games;
        told += v.heard(1, "Spieler heute Nacht ungefähr zur Tatzeit dort gesehen, wo Ann gestorben ist");
    }
    std::printf("    The watcher reported the killer seen at the time of the killing in %d of %d games\n", told, games);
    CHECK(told * 5 >= games * 4);
}

TEST(WhoeverSpentTheNightBesideABotHasAnAlibi) {
    CHECK(weight(parseLine("Anna war die ganze Nacht bei mir"), 2) < 0.0f);
    CHECK(weight(parseLine("Ich war die ganze Nacht mit Anna zusammen"), 2) < 0.0f);
    CHECK_EQ(weight(parseLine("Ich war heute Nacht bei Anna"), 2), 0.0f); // dropping by is no alibi

    // Anton spends the night with Berta while the human stabs Ann far away: in the morning he vouches for her
    const auto murder = openSpot();
    CHECK(murder.has_value());
    if (!murder) return;
    const auto company = openSpot(Vec2{(*murder)[0].x, (*murder)[0].z});
    CHECK(company.has_value());
    if (!company) return;
    int vouched = 0, games = 0;
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        Village v(seed, {Role::SerialKiller, Role::Villager, Role::Werewolf, Role::Villager, Role::Villager, Role::Villager});
        phase(v.match, Phase::Night);
        auto hold = [&](float seconds) {
            for (float t = 0; t < seconds; t += 0.1f) {
                v.match.teleport(0, (*murder)[1]);
                if (v.match.find(3)->alive) v.match.teleport(3, (*murder)[0]);
                v.match.teleport(1, (*company)[2]);
                v.match.teleport(4, (*company)[0]);
                v.match.update(0.1f);
                for (Bot& b : v.bots) b.update(0.1f, v.match, v.say);
            }
        };
        hold(20.0f);
        if (!v.match.interact(0, Interaction::Stab, 3).empty()) continue; // the wolf got there first
        hold(20.0f);
        toTheAssembly(v);
        ++games;
        vouched += v.heard(1, "Berta war heute Nacht die ganze Zeit in meiner Nähe");
    }
    std::printf("    Anton gave Berta an alibi in %d of %d games\n", vouched, games);
    CHECK(games >= 3);
    CHECK(vouched * 5 >= games * 3);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
