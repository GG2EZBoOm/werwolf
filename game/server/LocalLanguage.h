#pragma once

#include "i18n/Text.h"
#include "server/Dialogue.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace ww {

// A deliberately small, already filtered view of a conversation. Never pass Match or GameView
// to the inference worker: neither hidden roles nor other channels belong in a language prompt.
struct LanguageRequest {
    uint8_t bot = kNoPlayer;
    std::string name;
    std::string phase;
    int day = 0;
    std::vector<dialogue::Person> people;
    std::vector<std::pair<std::string, Text>> history;
    Text stance;
    bool preserveStance = false;
    float fear = 0, anger = 0, stress = 0, friendship = 0;
    int style = 0;
};

std::string languagePayload(const LanguageRequest& request);
// Reject malformed output, invented role claims/evidence and overlong messages.
bool languageReply(const LanguageRequest& request, const std::string& response, Text& result);

class LocalLanguage {
public:
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
    // A completed request may contain no result (fallback). Never blocks the game loop.
    bool take(uint64_t ticket, std::optional<Text>& result);
    void cancel(uint64_t ticket);
    static std::filesystem::path packDirectory();
    static bool installed();
private:
    struct Job { uint64_t id; LanguageRequest request; double deadline; };
    void run();
    Options m_options;
    Transport m_transport;
    std::atomic<bool> m_stop{false}, m_ready{false};
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_queue;
    std::map<uint64_t, std::optional<Text>> m_results;
    std::set<uint64_t> m_active;
    uint64_t m_next = 1;
    std::thread m_worker;
};

} // namespace ww
