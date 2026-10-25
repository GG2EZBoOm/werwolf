#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eng::net {

// Initialisiert den Socket-Stack (WSAStartup unter Windows). Mehrfacher Aufruf ist erlaubt.
bool initialize();

// Lokale IPv4-Adressen ohne Loopback, z. B. um sie Mitspielern anzuzeigen.
std::vector<std::string> localIPv4Addresses();

// Größte erlaubte Nachricht. Größere Rahmen gelten als Protokollfehler.
constexpr size_t kMaxMessageSize = 256 * 1024;

using SocketHandle = std::uintptr_t;

// Nicht blockierende TCP-Verbindung mit Nachrichten-Framing:
// jede Nachricht = 4 Byte Länge (Big Endian) + Nutzdaten.
class Connection {
public:
    enum class State { Disconnected, Connecting, Connected };

    Connection();
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Startet einen nicht blockierenden Verbindungsaufbau (Client).
    bool connect(const std::string& host, uint16_t port, double timeoutSeconds = 6.0);
    // Treibt Verbindungsaufbau, Senden und Empfangen voran. Einmal pro Tick aufrufen.
    void update();
    // Holt die nächste vollständige Nachricht aus dem Empfangspuffer.
    bool receive(std::vector<uint8_t>& payload);
    void send(const std::vector<uint8_t>& payload);
    void send(const uint8_t* data, size_t size);
    // Trennt sofort und verwirft alle Puffer.
    void close();
    // Trennt, sobald alle ausstehenden Daten gesendet wurden.
    void closeAfterFlush();

    State state() const { return m_state; }
    bool isConnected() const { return m_state == State::Connected; }
    const std::string& error() const { return m_error; }
    const std::string& address() const { return m_address; }
    double lastReceiveTime() const { return m_lastReceive; }
    size_t pendingSendBytes() const { return m_send.size() - m_sendPos; }

private:
    friend class Listener;
    void fail(const std::string& reason);
    void closeSocket();
    void flushSend();
    void readAvailable();

    SocketHandle m_socket;
    State m_state = State::Disconnected;
    std::string m_error;
    std::string m_address;
    std::vector<uint8_t> m_send;
    size_t m_sendPos = 0;
    std::vector<uint8_t> m_recv;
    size_t m_recvPos = 0;
    double m_connectDeadline = 0.0;
    double m_lastReceive = 0.0;
    bool m_closeAfterFlush = false;
};

// Nicht blockierender TCP-Listener (Server).
class Listener {
public:
    Listener();
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    bool listen(uint16_t port, std::string* error = nullptr);
    std::unique_ptr<Connection> accept();
    void close();
    bool isListening() const;

private:
    SocketHandle m_socket;
};

} // namespace eng::net
