#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng::net {

// Initialisiert den Socket-Stack (WSAStartup unter Windows). Mehrfacher Aufruf ist erlaubt.
bool initialize();

// Eine eigene Adresse dieses Rechners
struct LocalAddress {
    std::string address;  // ohne Port, IPv6 ohne Klammern
    bool ipv6 = false;
    bool global = false;  // öffentlich erreichbar (IPv6 global unicast bzw. kein privates IPv4)
};

// Adressen ohne Loopback und Link-Local: IPv4 (meist im Heimnetz) und globale IPv6-Adressen.
std::vector<LocalAddress> localAddresses();
// Nur die IPv4-Adressen (z. B. um sie Mitspielern im Heimnetz anzuzeigen)
std::vector<std::string> localIPv4Addresses();
// IPv4-Standard-Gateways (meist der Heimrouter)
std::vector<std::string> defaultGateways();

// "host", "host:port", "1.2.3.4:27777", "[2001:db8::1]:27777" oder "2001:db8::1" zerlegen.
// Ohne Port bleibt port unverändert. Liefert false bei leerer Adresse oder ungültigem Port.
bool parseEndpoint(std::string_view text, std::string& host, uint16_t& port);
// Adresse mit Port für die Anzeige: IPv6 in eckigen Klammern
std::string formatEndpoint(const std::string& host, uint16_t port);
bool isIPv6Literal(std::string_view host);
// Privates IPv4-Netz (10/8, 172.16/12, 192.168/16) oder Carrier-Grade-NAT (100.64/10)
bool isPrivateIPv4(std::string_view address);
// Nur die klassischen Heimnetze 10/8, 172.16/12 und 192.168/16
bool isHomeNetworkIPv4(std::string_view address);
bool isCarrierGradeNat(std::string_view address);

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

    // Startet einen nicht blockierenden Verbindungsaufbau (Client). Alle Adressen des Hosts
    // (IPv6 und IPv4) werden nacheinander versucht.
    bool connect(const std::string& host, uint16_t port, double timeoutSeconds = 10.0);
    // Mehrere Hosts, z. B. die IPv6- und IPv4-Adresse aus einer Einladung
    bool connect(const std::vector<std::string>& hosts, uint16_t port, double timeoutSeconds = 12.0);
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
    // Fehlertexte sind feste deutsche Sätze ohne Platzhalter (der Client übersetzt sie)
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
    // Nächste Adresse versuchen; false, wenn keine mehr übrig ist
    bool startNextAttempt();

    SocketHandle m_socket;
    State m_state = State::Disconnected;
    std::string m_error;
    std::string m_address;
    std::vector<uint8_t> m_send;
    size_t m_sendPos = 0;
    std::vector<uint8_t> m_recv;
    size_t m_recvPos = 0;
    std::vector<std::vector<uint8_t>> m_candidates; // sockaddr-Bytes der möglichen Ziele
    size_t m_nextCandidate = 0;
    double m_attemptDeadline = 0.0;
    double m_connectDeadline = 0.0;
    double m_lastReceive = 0.0;
    bool m_closeAfterFlush = false;
};

// Nicht blockierender TCP-Listener (Server). Lauscht auf IPv6 und IPv4 zugleich, falls möglich.
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
    // Nimmt der Listener auch IPv6-Verbindungen an?
    bool dualStack() const { return m_dualStack; }

private:
    SocketHandle m_socket;
    bool m_dualStack = false;
};

} // namespace eng::net
