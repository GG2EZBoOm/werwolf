#pragma once

#include "i18n/Text.h"
#include "server/Bot.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace ww {

// A deliberately small, already filtered view of a conversation. Never pass Match or GameView
// to the inference worker: neither hidden roles nor other channels belong in a language prompt.
struct LanguageRequest {
    struct Reason {
        uint8_t target = kNoPlayer, source = kNoPlayer;
        int day = 0;
        double time = 0;
        float weight = 0;
        std::string kind;
        Text text; // authorized meaning; paraphrases are checked against this source
        std::optional<Bot::Evidence> evidence;
    };
    struct Assessment {
        uint8_t target = kNoPlayer;
        float suspicion = 0, relationship = 0;
    };
    uint8_t bot = kNoPlayer;
    uint8_t focus = kNoPlayer;
    std::string name;
    std::string phase;
    int day = 0;
    std::vector<dialogue::Person> people;
    std::vector<std::pair<std::string, Text>> history;
    Text stance;
    bool preserveStance = false;
    float fear = 0, anger = 0, stress = 0, friendship = 0;
    int style = 0;
    std::vector<Reason> reasons;
    std::vector<Assessment> assessments;
    bool chooseReason = false;
    bool freeConversation = false; // ordinary chat: no evidence selection or fixed sentence to paraphrase
};

struct UnderstandingRequest {
    uint8_t speaker = kNoPlayer;
    std::vector<dialogue::Person> people;
    std::vector<std::pair<std::string, Text>> history; // only conversations every recipient actually heard
    Text message;
};

struct LanguageResult {
    Text text;
    int reason = -1; // provenance survives paraphrasing; never recover it by matching output wording
    std::optional<dialogue::Message> understanding;
};

std::string languagePayload(const LanguageRequest& request, const std::string& feedback = {});
// Parse and structurally check a candidate. The worker also verifies its meaning before publication.
bool languageReply(const LanguageRequest& request, const std::string& response, LanguageResult& result);
std::string languageVerificationPayload(const LanguageRequest& request, const LanguageResult& candidate);
std::string languageRephrasePayload(const LanguageRequest& request, int reason = -1);
bool languageVerified(const std::string& response);
bool languageConversationVerified(const std::string& response);
std::string understandingPayload(const UnderstandingRequest& request);
bool understandingReply(const UnderstandingRequest& request, const std::string& response, dialogue::Message& result);
std::string understandingVerificationPayload(const UnderstandingRequest& request, const std::string& response, bool question = false);
bool understandingVerified(const std::string& response);

class LocalLanguage {
public:
    static constexpr double kReplyTimeout = 12.0;
    struct Options {
        bool enabled = true; // starts only when an optional pack is installed
        uint16_t port = 0;   // 0: managed pack; otherwise an existing loopback server
        std::filesystem::path directory;
        bool diagnostics = false; // explicit local QA only; never logs model content by default
    };
    using Transport = std::function<bool(const std::string&, std::string&)>;
    explicit LocalLanguage(Options options, Transport transport = {});
    ~LocalLanguage();
    LocalLanguage(const LocalLanguage&) = delete;
    LocalLanguage& operator=(const LocalLanguage&) = delete;
    bool available() const { return m_ready.load(); }
    uint64_t submit(LanguageRequest request);
    uint64_t submit(UnderstandingRequest request);
    // A completed request may contain no result (fallback). Never blocks the game loop.
    bool take(uint64_t ticket, std::optional<LanguageResult>& result);
    void cancel(uint64_t ticket);
    static std::filesystem::path packDirectory();
    static bool installed();
private:
    using Request = std::variant<LanguageRequest, UnderstandingRequest>;
    uint64_t enqueue(Request request);
    struct Job { uint64_t id; Request request; double deadline; };
    void run();
    Options m_options;
    Transport m_transport;
    std::atomic<bool> m_stop{false}, m_ready{false};
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_queue;
    std::map<uint64_t, std::optional<LanguageResult>> m_results;
    std::set<uint64_t> m_active;
    uint64_t m_next = 1;
    std::thread m_worker;
};

} // namespace ww
