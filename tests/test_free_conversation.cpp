#include "TestFramework.h"
#include "server/LocalLanguage.h"
#include "server/LanguageProtocol.h"
#include "engine/core/Time.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

using namespace ww;
namespace {
template<class F> bool until(F done, double seconds = 10) {
    const double end = eng::nowSeconds() + seconds;
    while (eng::nowSeconds() < end) { if (done()) return true; eng::sleepSeconds(.002); }
    return false;
}
}

TEST(FreeConversationKeepsEnoughContextForLaterFollowups) {
    LanguageRequest r; r.name = "Anton"; r.freeConversation = true;
    r.history.push_back({"Spieler", Text::same("Bitte merke dir: Mein Hund heißt Mokka.")});
    for (int i = 0; i < 12; ++i) r.history.push_back({i % 2 ? "Spieler" : "Anton", Text::same("Ein anderes Gesprächsthema.")});
    r.history.push_back({"Spieler", Text::same("Wie heißt mein Hund?")});
    eng::json::Value body, context;
    CHECK(eng::json::parse(languagePayload(r), body));
    CHECK(eng::json::parse(body.get("messages")->array[1].getString("content"), context));
    const auto* history = context.get("verlauf");
    CHECK(history && history->array.size() == 14);
    CHECK(history->array.front().getString("text").find("Mokka") != std::string::npos);
    CHECK_EQ(context.getString("antwort_auf"), "Wie heißt mein Hund?");
}

TEST(FreeConversationAcceptsPlainTextSymbolsAndNormalizesLineBreaks) {
    LanguageRequest r; r.name = "Anton"; r.freeConversation = true;
    const std::string line = "2 < 3 ist wahr.\nBeispiel: if (2 < 3) { return true; }";
    const std::string response = "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":" +
        languageWire::quote("{\"grund\":-1,\"de\":" + languageWire::quote(line) +
                            ",\"en\":\"2 < 3 is true. Example: if (2 < 3) { return true; }\"}") + "}}]}";
    LanguageResult result;
    CHECK(languageReply(r, response, result));
    CHECK(result.text.de.find("2 < 3") != std::string::npos);
    CHECK(result.text.de.find("{ return true; }") != std::string::npos);
    CHECK(result.text.de.find('\n') == std::string::npos);
}

TEST(FreeConversationRejectsSpecificProblemsDespiteAnOverallPositiveVerdict) {
    auto review = [](const std::string& claim, const std::string& answer, const std::string& rules) {
        return "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":" +
            languageWire::quote("{\"korrekt\":true,\"spielbehauptung\":" + languageWire::quote(claim) +
                ",\"antwort\":" + languageWire::quote(answer) + ",\"regeln\":" + languageWire::quote(rules) + "}") + "}}]}";
    };
    CHECK(languageConversationVerified(review("keine", "beantwortet", "passt")));
    CHECK(!languageConversationVerified(review("rolle", "beantwortet", "passt")));
    CHECK(!languageConversationVerified(review("alibi", "beantwortet", "passt")));
    CHECK(!languageConversationVerified(review("keine", "nur_zusage", "passt")));
    CHECK(!languageConversationVerified(review("keine", "falsch", "passt")));
    CHECK(!languageConversationVerified(review("keine", "beantwortet", "widerspruch")));
    CHECK(!languageConversationVerified(review("", "", "")));
}

int writeConversationChecks(const std::filesystem::path& file) {
    struct Case { std::string question; Text candidate; bool expected; };
    const std::vector<Case> cases = {
        {"Erzähl eine erfundene Geschichte über deine Zeit als Bäcker.",
         {"Ich war Bäcker und habe ein Brot so groß wie ein Haus gebacken.", "I was a baker and baked a loaf as big as a house."}, true},
        {"Wollen wir erst mal in Ruhe miteinander reden?",
         {"Ja, ich werde erst einmal zuhören und darüber nachdenken.", "Yes, I will listen first and think about it."}, true},
        {"Erzähl in Ich-Form eine erfundene Geschichte über eine Hexe.",
         {"In dieser Geschichte bin ich die Hexe, die den Mond in einen Kessel lockt.", "In this story I am the witch who lures the moon into a cauldron."}, true},
        {"Was hast du diese Nacht beobachtet?",
         {"Ich habe Berta als Seherin geprüft: Sie ist ein Werwolf.", "I checked Berta as the seer: she is a werewolf."}, false},
        {"Wo warst du in dieser Nacht?",
         {"Ich war die ganze Nacht mit Berta im Haus, sie kann das bestätigen.", "I was in the house with Berta all night; she can confirm that."}, false},
        {"Was ergibt 17 plus 25?", {"17 plus 25 ergibt 43.", "17 plus 25 equals 43."}, false},
        {"Wie viele Tränke hat die Hexe?",
         {"Die Hexe bekommt jede Nacht zwei neue Tränke.", "The witch gets two new potions every night."}, false},
        {"Schreibe ein kurzes Gedicht.", {"Das kann ich machen.", "I can do that."}, false}
    };
    std::ofstream out(file);
    out << '[';
    for (size_t i = 0; i < cases.size(); ++i) {
        LanguageRequest r; r.name = "Anton"; r.freeConversation = true;
        r.history = {{"Spieler", Text::same(cases[i].question)}};
        LanguageResult candidate; candidate.text = cases[i].candidate;
        if (i) out << ',';
        out << "{\"expected\":" << (cases[i].expected ? "true" : "false") << ",\"question\":" << languageWire::quote(cases[i].question)
            << ",\"payload\":" << languageWire::quote(languageVerificationPayload(r, candidate)) << '}';
    }
    out << ']';
    return out.good() ? 0 : 1;
}

int liveConversation(const std::filesystem::path& directory) {
    LocalLanguage::Options options; options.directory = directory; options.diagnostics = true;
    LocalLanguage model(options);
    if (!until([&] { return model.available(); }, 60)) return 1;
    struct Case { std::string question; std::vector<std::string> expected; bool follow = false; };
    const std::vector<Case> cases = {
        {"Was ergibt 17 plus 25?", {"42", "zweiundvierzig"}},
        {"Wie heißt die Hauptstadt von Frankreich?", {"Paris"}},
        {"Erkläre mir kurz, warum Pflanzen Licht brauchen.", {"Licht", "Photosynthese", "Fotosynthese"}},
        {"Übersetze 'Good morning' ins Deutsche.", {"Guten Morgen", "guten Morgen"}},
        {"Erzähl eine erfundene Geschichte in Ich-Form: Du warst früher Bäcker.", {"Brot", "Bäcker", "back", "Teig"}},
        {"Erzähl diese Geschichte weiter. Was passierte dann?", {}, true},
        {"Was würdest du tun, wenn du fliegen könntest?", {"flieg", "flog", "Himmel", "Wolken", "Luft"}},
        {"Schreib ein kurzes Gedicht über den Mond.", {"Mond", "mond"}},
        {"Gib mir ein einfaches Rezept für Kartoffelsuppe.", {"Kartoffel", "kartoffel"}},
        {"Kannst du das auf einen Satz kürzen?", {"Kartoffel", "kartoffel", "schälen", "würfeln", "köcheln"}, true},
        {"Ich hatte heute einen miesen Tag und brauche etwas Aufmunterung.", {}},
        {"Warum ist die Banane krumm?", {"Licht", "Sonne", "wachs", "Wachstum"}},
        {"Erzähl mir einen kurzen Witz.", {}},
        {"Noch einen anderen bitte.", {}, true},
        {"Was kann die Hexe in diesem Spiel machen?", {"Heiltrank", "heilen", "wiederbeleben"}},
        {"Tu so, als wärst du ein sprechender Baum, und begrüße mich.", {}},
        {"Welches Essen magst du am liebsten?", {}},
        {"Warum magst du gerade dieses Essen?", {}, true},
        {"Was bedeutet der Ausdruck 2 < 3?", {"kleiner", "weniger"}},
        {"Zeig mir ein winziges JavaScript-Beispiel, das Hallo ausgibt.", {"console.log", "alert("}},
    };
    LanguageRequest r; r.bot = 1; r.name = "Anton"; r.phase = "Versammlung"; r.day = 2;
    r.freeConversation = true; r.people = {{0,"Spieler"},{1,"Anton"},{2,"Berta"}};
    int accepted = 0;
    for (const auto& c : cases) {
        if (!c.follow) r.history.clear();
        r.history.push_back({"Spieler", Text::same(c.question)});
        const double start = eng::nowSeconds();
        const auto ticket = model.submit(r); std::optional<LanguageResult> result;
        const bool ready = until([&] { return model.take(ticket, result); });
        const bool relevant = ready && result && (c.expected.empty() || std::any_of(c.expected.begin(), c.expected.end(), [&](const auto& word) {
            return result->text.de.find(word) != std::string::npos;
        }));
        std::printf("\n%s (%.2fs): %s\n", relevant ? "OK" : "FEHLER", eng::nowSeconds() - start, c.question.c_str());
        if (result) {
            std::printf("DE: %s\nEN: %s\n", result->text.de.c_str(), result->text.en.c_str());
            r.history.push_back({r.name, result->text});
        } else std::puts("KEINE FREIE ANTWORT");
        accepted += relevant ? 1 : 0;
    }
    std::printf("\nFreie Gesprächsbeispiele: %d/%zu\n", accepted, cases.size());
    return accepted == static_cast<int>(cases.size()) ? 0 : 1;
}
