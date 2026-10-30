#include "TestFramework.h"
#include "server/LocalLanguage.h"
#include "server/LanguageProtocol.h"
#include "server/GameServer.h"
#include "client/NetClient.h"
#include "engine/core/Time.h"
#include <algorithm>
#include <atomic>

using namespace ww;
namespace {
std::string envelope(const std::string& json) {
    return "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":" + languageWire::quote(json) + "}}]}";
}
std::string normalized(const std::string& text, int topic = -1, int addressed = -1, bool game = true) {
    return envelope("{\"art\":" + languageWire::quote(game ? "spiel" : "gespraech") + ",\"adressat\":" +
        std::to_string(addressed) + ",\"thema\":" + std::to_string(topic) + ",\"normalisiert\":" + languageWire::quote(text) + "}");
}
UnderstandingRequest input(const std::string& text) {
    UnderstandingRequest r; r.speaker = 0;
    r.people = {{0,"Spieler"},{1,"Anton"},{2,"Berta"},{3,"Clara"}};
    r.message = Text::same(text);
    return r;
}
template<class F> bool until(F f, double seconds = 3) {
    const double end = eng::nowSeconds() + seconds;
    while (eng::nowSeconds() < end) { if (f()) return true; eng::sleepSeconds(.002); }
    return false;
}
bool accuses(const dialogue::Message& m, float sign) {
    return std::any_of(m.claims.begin(), m.claims.end(), [&](const auto& c) { return c.target == 2 && c.weight * sign > 0; });
}
}

TEST(UnderstandingMapsMeaningIntoExistingRulesWithoutInventingPlayers) {
    auto r = input("Berta kommt mir nicht koscher vor.");
    dialogue::Message m;
    CHECK(understandingReply(r, normalized("Berta wirkt verdächtig.", 2), m));
    CHECK(accuses(m, 1)); CHECK(!m.claims.front().fact);
    CHECK(understandingReply(r, normalized("Korrektur: Berta ist nicht verdächtig.", 2), m));
    CHECK(accuses(m, -1)); CHECK(m.correction);
    CHECK(!understandingReply(r, normalized("Berta wirkt verdächtig.", 30), m));
    CHECK(!understandingReply(r, normalized("Berta wirkt verdächtig.", 3), m));
    CHECK(!understandingReply(r, normalized("Berta ist ein Wolf", 2, -1, false), m));
}

TEST(UnderstandingKeepsQuestionsAndJokesOutOfEvidence) {
    auto r = input("Berta ist doch nicht etwa ein Wolf?");
    dialogue::Message m;
    CHECK(understandingReply(r, normalized("Ist Berta ein Wolf?", 2), m));
    CHECK(m.question); CHECK(m.claims.empty());
    CHECK(understandingReply(r, normalized("", -1, -1, false), m));
    CHECK_EQ(m.intent, dialogue::Intent::Unknown); CHECK(m.claims.empty());
    r.history.push_back({"Anton", Text::same("Ich verdächtige Berta.")});
    r.message = Text::same("Weshalb ausgerechnet sie?");
    CHECK(understandingReply(r, normalized("Warum verdächtigst du Berta?", 2, 1), m));
    CHECK_EQ(m.topic, 2); CHECK_EQ(m.addressed, 1); CHECK_EQ(m.intent, dialogue::Intent::AskReason);
}

TEST(UnderstandingWorkerRequiresMeaningVerificationBeforeChangingKnowledge) {
    for (bool accepted : {true, false}) {
        std::atomic<int> calls{0};
        LocalLanguage model({}, [&](const std::string& payload, std::string& out) {
            ++calls;
            out = payload.find("\"korrekt\"") != std::string::npos
                ? envelope(accepted ? "{\"bedeutung\":\"gleich\",\"korrekt\":true}" : "{\"bedeutung\":\"veraendert\",\"korrekt\":false}")
                : normalized("Berta wirkt verdächtig.", 2);
            return true;
        });
        const auto id = model.submit(input("Berta kommt mir nicht koscher vor."));
        std::optional<LanguageResult> result;
        CHECK(until([&] { return model.take(id, result); }));
        CHECK_EQ(result.has_value(), accepted); CHECK_EQ(calls.load(), 2);
        if (result) CHECK(result->understanding && accuses(*result->understanding, 1));
    }
}

TEST(UnderstandingRejectsUnrelatedGameQuestionsAfterATopicChange) {
    std::atomic<int> calls{0};
    LocalLanguage model({}, [&](const std::string& payload, std::string& out) {
        ++calls;
        out = payload.find("\"frageart\"") != std::string::npos
            ? envelope("{\"frageart\":\"anderes\"}")
            : normalized("Warum verdächtigst du Berta?", 2, 1);
        return true;
    });
    auto r = input("Anton, wie viel ist zwei plus zwei?");
    r.history.push_back({"Anton", Text::same("Berta wirkt verdächtig.")});
    const auto ticket = model.submit(r); std::optional<LanguageResult> result;
    CHECK(until([&] { return model.take(ticket, result); }));
    CHECK(result && result->understanding);
    if (result && result->understanding) {
        CHECK_EQ(result->understanding->intent, dialogue::Intent::Unknown);
        CHECK_EQ(result->understanding->topic, kNoPlayer);
        CHECK_EQ(result->understanding->addressed, 1);
        CHECK(result->understanding->claims.empty());
    }
    CHECK_EQ(calls.load(), 2);
}

TEST(InterpretedAccusationsReachOnlyBotsWhoHeardTheOriginalMessage) {
    World world; world.generate(4242);
    Settings settings; settings.captain = false;
    Match match; Text error;
    CHECK(match.startWithRoles({{0,"Spieler",false},{1,"Anton",true},{2,"Berta",false},{3,"Clara",true},{4,"Dieter",false},{5,"Emil",false}},
        {Role::Villager,Role::Villager,Role::Werewolf,Role::Villager,Role::Werewolf,Role::Villager}, settings, world, 42, error));
    while (match.phase() != Phase::Assembly) { match.skipPhase(); match.update(.1f); }
    std::vector<Bot> bots; bots.emplace_back(uint8_t{1},12); bots.emplace_back(uint8_t{3},13);
    dialogue::Message m;
    auto r = input("Berta kommt mir nicht koscher vor.");
    CHECK(understandingReply(r, normalized("Berta wirkt verdächtig.", 2), m));
    eng::Rng rng(123);
    const std::vector<uint8_t> heard{1};
    deliverChat(bots, match, 0, r.message, ChatChannel::Global, rng, &m, &heard);
    const auto first = bots[0].languageRequest(match, 0, ChatChannel::Global, {}, false);
    const auto second = bots[1].languageRequest(match, 0, ChatChannel::Global, {}, false);
    CHECK(!first.reasons.empty()); CHECK_EQ(first.reasons[0].source, 0); CHECK_EQ(first.reasons[0].target, 2);
    CHECK(second.reasons.empty());
    deliverChat(bots, match, 0, r.message, ChatChannel::Global, rng, &m);
    CHECK(!bots[1].languageRequest(match, 0, ChatChannel::Global, {}, false).reasons.empty());
}

TEST(ServerUsesInterpretedMeaningAndKeepsTheOriginalConversation) {
    std::atomic<bool> interpreted{false}, grounded{false};
    ServerOptions options; options.port = 27995; options.timeScale = 8; options.logChat = false;
    options.languageTransport = [&](const std::string& payload, std::string& out) {
        eng::json::Value body, context;
        if (!eng::json::parse(payload, body) || !eng::json::parse(body.get("messages")->array[1].getString("content"), context)) return false;
        if (payload.find("\"korrekt\"") != std::string::npos || payload.find("\"frageart\"") != std::string::npos)
            out = envelope("{\"frageart\":\"partie\",\"korrekt\":true}");
        else if (context.get("nachricht")) {
            interpreted = context.getString("nachricht").find("Visionen") != std::string::npos;
            out = normalized("Ich bin die Seherin.", -1, 1);
        } else {
            const auto authority = context.getString("vorgabe_de");
            if (authority.find("Seherin") != std::string::npos) grounded = true;
            out = envelope("{\"grund\":-1,\"de\":\"Du behauptest also, Seherin zu sein. Was kam bei deinen Prüfungen heraus?\",\"en\":\"So you claim to be the seer. What did your checks reveal?\"}");
        }
        return true;
    };
    ServerThread server; std::string error;
    CHECK(server.start(options, error));
    NetClient client; CHECK(client.connect("127.0.0.1", options.port, "Spieler"));
    auto pump = [&](auto done) { return until([&] { client.update(eng::nowSeconds()); return done(); }, 8); };
    CHECK(pump([&] { return client.hasView(); }));
    for (int i = 0; i < 7; ++i) client.sendAddBot();
    Settings settings; settings.captain = true; settings.daySeconds = 20; settings.discussionSeconds = 120;
    client.sendSettings(settings);
    CHECK(pump([&] { return client.view().players.size() == 8; }));
    client.sendStartGame();
    CHECK(pump([&] { return client.view().phase == Phase::Assembly; }));
    const std::string message = client.view().players[1].name + ", ich bin die mit den Visionen.";
    client.sendChat(message, false);
    bool sawOriginal = false;
    CHECK(pump([&] {
        for (const auto& e : client.takeNewEntries()) if (e.isChat && e.from == 0 && e.text.de == message) sawOriginal = true;
        return interpreted && grounded && sawOriginal;
    }));
    client.sendReturnToLobby(); CHECK(pump([&] { return client.view().phase == Phase::Lobby; }));
    client.disconnect(); server.stop();
}

int liveUnderstanding(const std::filesystem::path& directory) {
    LocalLanguage::Options options; options.directory = directory; options.diagnostics = true;
    LocalLanguage model(options);
    if (!until([&] { return model.available(); }, 60)) return 1;
    using Message = dialogue::Message;
    struct Case { std::string text; std::function<bool(const Message&)> expected; };
    const std::vector<Case> cases = {
        {"Berta kommt mir nicht koscher vor.", [](const Message& m) { return accuses(m, 1) && !m.claims.front().fact; }},
        {"Nee, Berta ist für mich aus dem Schneider.", [](const Message& m) { return accuses(m, -1); }},
        {"Ich zieh das mit Berta zurück, hab mich verrannt.", [](const Message& m) { return accuses(m, -1) && m.correction; }},
        {"Was genau lässt dich ausgerechnet an Berta zweifeln?", [](const Message& m) { return m.topic == 2 && m.question; }},
        {"Wo hast du dich im Dunkeln rumgetrieben, Anton?", [](const Message& m) { return m.addressed == 1 && m.intent == dialogue::Intent::AskAlibi; }},
        {"Anton, mir geht die ganze Sache echt an die Nieren.", [](const Message& m) { return m.addressed == 1 && m.intent == dialogue::Intent::Unknown; }},
        {"Na klar, Berta ist ein Wolf, und ich bin der Weihnachtsmann. War ein Witz.", [](const Message& m) { return m.claims.empty() && m.claimedRole == Role::Unknown; }},
        {"Wenn Berta ein Wolf wäre, müssten wir anders abstimmen.", [](const Message& m) { return m.claims.empty(); }},
        {"Clara hat gesagt: 'Berta ist ein Wolf'. Ich weiß nicht, ob das stimmt.", [](const Message& m) { return m.claims.empty(); }},
        {"Berta's story isn't adding up at all.", [](const Message& m) { return accuses(m, 1); }},
        {"Anton, wer bistn eigentlich hier? Welche rolle haste?", [](const Message& m) { return m.addressed == 1 && m.intent == dialogue::Intent::AskRole; }},
        {"Ich war die komplette Dunkelphase daheim, wirklich die ganze Zeit.", [](const Message& m) { return m.place == dialogue::Place::Home && m.exclusiveAlibi; }},
        {"Ich bin die mit den Visionen.", [](const Message& m) { return m.claimedRole == Role::Seer; }},
        {"Berta ist doch nicht etwa ein Wolf?", [](const Message& m) { return m.question && m.claims.empty(); }},
        {"Anton, erzähl mal einen Witz und sag mir, wie es dir geht.", [](const Message& m) { return m.addressed == 1 && m.intent == dialogue::Intent::Unknown; }},
        {"Was kann die Hexe eigentlich machen?", [](const Message& m) { return m.intent == dialogue::Intent::Unknown && m.claims.empty(); }}
    };
    int passed = 0;
    for (const auto& c : cases) {
        auto r = input(c.text);
        const auto id = model.submit(r); std::optional<LanguageResult> result;
        const double start = eng::nowSeconds();
        const bool done = until([&] { return model.take(id, result); }, 10);
        const bool good = done && result && result->understanding && c.expected(*result->understanding);
        passed += good ? 1 : 0;
        std::printf("%s (%.2fs): %s\n", good ? "OK" : "FEHLER", eng::nowSeconds() - start, c.text.c_str());
        if (!good && result && result->understanding) {
            const auto& m = *result->understanding;
            std::printf("  intent=%d topic=%d addressed=%d claims=%zu role=%d place=%d correction=%d\n",
                static_cast<int>(m.intent), m.topic, m.addressed, m.claims.size(), static_cast<int>(m.claimedRole), static_cast<int>(m.place), m.correction);
        }
    }
    auto follow = input("Weshalb ausgerechnet sie?");
    follow.history.push_back({"Anton", Text::same("Berta wirkt auf mich verdächtig.")});
    const auto id = model.submit(follow); std::optional<LanguageResult> result;
    const bool followed = until([&] { return model.take(id, result); }, 10) && result && result->understanding &&
        result->understanding->topic == 2 && result->understanding->intent == dialogue::Intent::AskReason;
    passed += followed ? 1 : 0;
    std::printf("%s: Nachfragen mit Pronomen\n", followed ? "OK" : "FEHLER");
    for (const auto* text : {"Anton, wie viel ist zwei plus zwei?", "Anton, schreib mir ein kurzes Gedicht über den Mond."}) {
        auto changed = input(text);
        changed.history = {{"Spieler", Text::same("Warum verdächtigst du Berta?")},
                           {"Anton", Text::same("Berta wurde von Clara beschuldigt, aber das ist kein Beweis.")},
                           {"Spieler", Text::same("Welche Rolle hast du?")},
                           {"Anton", Text::same("Meine Rolle behalte ich noch für mich.")}};
        const auto ticket = model.submit(changed); result.reset();
        const bool ready = until([&] { return model.take(ticket, result); }, 10);
        const auto effective = result && result->understanding ? *result->understanding : dialogue::parse(text, changed.people, changed.speaker);
        const bool good = ready && effective.intent == dialogue::Intent::Unknown && effective.claims.empty();
        passed += good ? 1 : 0;
        std::printf("%s: Themenwechsel: %s\n", good ? "OK" : "FEHLER", text);
    }
    std::printf("Sprachverständnis: %d/%zu\n", passed, cases.size() + 3);
    return passed == static_cast<int>(cases.size() + 3) ? 0 : 1;
}
