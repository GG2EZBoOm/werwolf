#pragma once

#include "logic/Game.h"
#include "protocol/Protocol.h"
#include "server/Bot.h"

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
};

// Autoritativer Spielserver: verwaltet Lobby, Verbindungen, Chat und das laufende Spiel.
// Alle Methoden laufen in genau einem Thread.
class GameServer {
public:
    explicit GameServer(ServerOptions options = {});
    ~GameServer();

    bool start(std::string& error);
    void tick();
    // Läuft, bis `running` false wird.
    void run(const std::atomic<bool>& running);
    void shutdown();

    uint16_t port() const { return m_options.port; }
    bool gameRunning() const { return m_game != nullptr; }
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
        std::string removeReason; // gesetzt = wird nach dem Tick entfernt
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
        ChatChannel channel = ChatChannel::Public;
        uint8_t from = kNoPlayer;
        std::string name;
        Notice notice;
        std::string text;
    };

    void acceptConnections(double now);
    void updatePending(double now);
    void updateMembers(double now, float dt);
    void handleHello(Pending& pending, eng::ByteReader& r);
    void handleMessage(uint8_t memberId, const std::vector<uint8_t>& data);
    void processRemovals();
    void onConnectionLost(Member& m, const std::string& reason);

    void startGame(Member& host);
    void returnToLobby();
    void addBot();
    void ensureHost();
    void resetIfEmpty();

    void chatFrom(uint8_t memberId, const std::string& text);
    void broadcastChat(ChatChannel channel, uint8_t from, const std::string& name, const std::string& text);
    void broadcastNotice(NoticeKind kind, const std::string& text);
    void deliverNotice(const Notice& notice);
    void sendNotice(Member& m, NoticeKind kind, const std::string& text);
    void routeGameNotices();
    void sendHistory(Member& m);
    void pushHistory(HistoryEntry entry);
    void broadcastState(double now);
    GameView lobbyView(uint8_t viewer) const;
    bool canRead(uint8_t memberId, ChatChannel channel) const;
    bool noticeVisible(const Notice& n, uint8_t memberId) const;
    void reject(std::unique_ptr<eng::net::Connection> conn, const std::string& reason);

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
    std::unique_ptr<Game> m_game;
    std::vector<Bot> m_bots;
    uint8_t m_host = kNoPlayer;
    bool m_dirty = true;
    bool m_lobbyRequested = false;
    double m_lastTick = 0.0;
    double m_lastBroadcast = 0.0;
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
