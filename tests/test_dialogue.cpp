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
    Match match = matchWith();
    Bot bot{1, 55};
    std::vector<Text> replies;
    std::vector<bool> secrets;
    Bot::SayFn say = [&](uint8_t, const Text& text, bool secret) { replies.push_back(text); secrets.push_back(secret); };
    Conversation() { phase(match, Phase::Assembly); bot.update(0.01f, match, say); }
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
    v.said.clear();
    v.human("Ann ist die Hexe");
    v.advance(6.0f);
    CHECK(v.heard(3, "Spieler, stimmt, ich bin die Hexe"));
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

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    return test::runAll();
}
