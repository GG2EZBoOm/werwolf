#pragma once

#include "logic/Match.h"
#include "protocol/Protocol.h"
#include "server/Bot.h"
#include "server/LocalLanguage.h"
#include "world/World.h"

#include "engine/core/Random.h"
#include "engine/net/Net.h"

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ww {

struct ServerOptions {
    uint16_t port = proto::kDefaultPort;
    float timeScale = 1.0f; // > 1 beschleunigt alle Phasen (für Tests)
    bool logChat = true;
    LocalLanguage::Options language;
    LocalLanguage::Transport languageTransport; // optional in-process transport for integration tests
};

// Autoritativer Spielserver: Lobby, Verbindungen, Chat und die laufende Echtzeit-Runde.
// Alle Methoden laufen in genau einem Thread.
class GameServer {
public:
    explicit GameServer(ServerOptions options = {});
    ~GameServer();

    bool start(std::string& error);
    void tick();
    void run(const std::atomic<bool>& running);
    void shutdown();

    uint16_t port() const { return m_options.port; }
    bool gameRunning() const { return m_match != nullptr; }
    size_t memberCount() const { return m_members.size(); }

private:
    struct Member {
        uint8_t id = kNoPlayer;
        std::string name;
        uint64_t token = 0;
        bool bot = false;
        bool online = false;
        std::unique_ptr<eng::net::Connection> conn;
        double chatBudget = 6.0;
        Text removeReason;
        bool removeRequested = false;
    };
    struct Pending {
        std::unique_ptr<eng::net::Connection> conn;
        double since = 0.0;
    };
    struct Closing {
        std::unique_ptr<eng::net::Connection> conn;
        double deadline = 0.0;
    };
    struct HistoryEntry {
        bool isChat = false;
        ChatChannel channel = ChatChannel::Global;
        uint8_t from = kNoPlayer;
        std::string name;
        Notice notice;
        Text text;
    };

    void acceptConnections(double now);
    void updatePending(double now);
    void updateMembers(double now, double realDt);
    void handleHello(Pending& pending, eng::ByteReader& r);
    void handleMessage(uint8_t memberId, const std::vector<uint8_t>& data);
    void processRemovals();
    void onConnectionLost(Member& m, const std::string& reason);

    void startGame(Member& host);
    void returnToLobby();
    void addBot();
    void ensureHost();
    void resetIfEmpty();

    void chatFrom(uint8_t memberId, Text text, bool wolves);
    void queueBotChat(uint8_t speaker, const Text& text, ChatChannel channel);
    void processBotChat(double now);
    void clearBotChat();
    void deliverNotice(const Notice& notice);
    void broadcastNotice(NoticeKind kind, const Text& text);
    void sendNotice(Member& m, NoticeKind kind, const Text& text);
    void routeMatchNotices();
    void sendHistory(Member& m);
    void pushHistory(HistoryEntry entry);
    void broadcastState(double now);
    void broadcastSnapshots();
    void sendCorrections();
    GameView lobbyView(uint8_t viewer) const;
    bool noticeVisible(const Notice& n, uint8_t memberId) const;
    void reject(std::unique_ptr<eng::net::Connection> conn, const Text& reason);

    Member* member(uint8_t id);
    uint8_t freeId() const;
    std::string uniqueName(const std::string& wanted) const;

    ServerOptions m_options;
    eng::net::Listener m_listener;
    eng::Rng m_rng;
    std::vector<std::unique_ptr<Member>> m_members;
    std::vector<Pending> m_pending;
    std::vector<Closing> m_closing;
    std::deque<HistoryEntry> m_history;
    Settings m_settings;
    World m_world;
    std::unique_ptr<Match> m_match;
    std::unique_ptr<LocalLanguage> m_language;
    std::vector<Bot> m_bots;
    struct HeardChat {
        uint8_t speaker;
        Text text;
        ChatChannel channel;
        int day;
        std::vector<uint8_t> audience;
    };
    struct PendingChat {
        HeardChat line;
        Phase phase;
        uint64_t ticket;
        double deadline;
    };
    std::deque<HeardChat> m_languageHistory;
    std::deque<PendingChat> m_languageChats;
    uint8_t m_host = kNoPlayer;
    bool m_dirty = true;
    bool m_lobbyRequested = false;
    double m_lastTick = 0.0;
    double m_lastBroadcast = 0.0;
    double m_lastSnapshot = 0.0;
    double m_noHumansSince = -1.0;
};

// Startet einen GameServer in einem eigenen Thread (für "Spiel hosten" im Client).
class ServerThread {
public:
    ~ServerThread() { stop(); }
    bool start(const ServerOptions& options, std::string& error);
    void stop();
    bool isRunning() const { return m_running; }

private:
    std::unique_ptr<GameServer> m_server;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace ww
