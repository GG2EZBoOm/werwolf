#pragma once

#include "logic/Types.h"
#include "protocol/Protocol.h"

#include "engine/net/Net.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace ww {

// Ein Eintrag im Chatverlauf (Chatnachricht oder Erzählertext).
struct LogEntry {
    bool isChat = false;
    ChatChannel channel = ChatChannel::Global;
    NoticeKind kind = NoticeKind::Info;
    uint8_t from = kNoPlayer;
    std::string name;  // leer bei verborgenem Sprecher (das kleine Mädchen hört nur "einen Werwolf")
    Text text;
    double time = 0.0;
    // Zwischenspeicher für den Zeilenumbruch im Chatfenster
    float wrapWidth = -1.0f;
    Lang wrapLang = Lang::German;
    std::vector<std::string> lines;
};

// Feste Fehlertexte der Netzwerkschicht in beiden Sprachen
Text netErrorText(const std::string& error);

struct Correction {
    float x = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
};

// Netzwerk-Client ohne Grafik: Verbindung, Protokoll, Spielzustand und Positions-Interpolation.
class NetClient {
public:
    enum class Status { Idle, Connecting, Handshake, Connected, Failed };

    bool connect(const std::string& host, uint16_t port, const std::string& name, uint64_t token = 0);
    // Mehrere Adressen desselben Servers (z. B. IPv6 und IPv4 aus einer Einladung)
    bool connect(const std::vector<std::string>& hosts, uint16_t port, const std::string& name, uint64_t token = 0);
    void disconnect();
    void update(double now);

    Status status() const { return m_status; }
    const std::string& error() const { return m_error.str(); }
    bool hasView() const { return m_hasView; }
    const GameView& view() const { return m_view; }
    uint8_t myId() const { return m_myId; }
    uint64_t token() const { return m_token; }
    bool isHost() const { return m_hasView && m_view.host == m_myId && m_myId != kNoPlayer; }
    const std::string& host() const { return m_host; }
    const std::vector<std::string>& hosts() const { return m_hosts; }
    // Tatsächlich verbundene Adresse mit Port, z. B. "[2001:db8::1]:27777"
    const std::string& address() const { return m_conn.address(); }
    uint16_t port() const { return m_port; }
    const std::string& name() const { return m_name; }
    float timeLeft(double now) const;

    // Positionen aller Figuren, leicht verzögert und weich interpoliert
    std::vector<EntityState> entities(double now) const;
    bool hasSnapshot() const { return !m_snapshots.empty(); }
    // Zustand der Haustüren aus dem letzten Snapshot (Index = Haus)
    const std::vector<DoorState>& doors() const { return m_doors; }
    uint32_t closedDoors() const { return closedDoorMask(m_doors); }
    std::optional<Correction> takeCorrection();

    std::deque<LogEntry>& log() { return m_log; }
    std::vector<LogEntry> takeNewEntries();
    void clearLog() { m_log.clear(); }

    void sendChat(const std::string& text, bool wolves);
    void sendMove(float x, float z, float yaw, uint8_t flags);
    void sendInteract(Interaction kind, uint8_t target);
    void sendStartGame();
    void sendSettings(const Settings& settings);
    void sendAddBot();
    void sendKick(uint8_t player);
    void sendReturnToLobby();

private:
    struct Snapshot {
        double time;
        std::vector<EntityState> entities;
    };

    void send(const eng::ByteWriter& w);
    void handle(const std::vector<uint8_t>& data, double now);
    void addEntry(LogEntry entry);

    eng::net::Connection m_conn;
    Status m_status = Status::Idle;
    Text m_error;
    std::vector<std::string> m_hosts;
    std::string m_host;
    uint16_t m_port = 0;
    std::string m_name;
    uint64_t m_token = 0;
    uint8_t m_myId = kNoPlayer;
    GameView m_view;
    bool m_hasView = false;
    double m_viewTime = 0.0;
    double m_lastPing = 0.0;
    std::deque<Snapshot> m_snapshots;
    std::vector<DoorState> m_doors;
    std::optional<Correction> m_correction;
    std::deque<LogEntry> m_log;
    std::vector<LogEntry> m_newEntries;
};

} // namespace ww
