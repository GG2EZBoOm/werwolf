#pragma once

#include "engine/net/Net.h"
#include "engine/net/Upnp.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ww {

// Macht einen gehosteten Server aus dem Internet erreichbar: ermittelt die eigenen Adressen,
// gibt den Port per UPnP im Router frei (IPv4-Weiterleitung, IPv6-Firewall) und fragt die
// öffentliche IPv4-Adresse ab. Läuft in einem eigenen Thread; räumt beim Beenden wieder auf.
class HostNetwork {
public:
    enum class Router { Searching, Opened, Failed, Disabled };

    struct Status {
        std::vector<std::string> lan;     // IPv4 im Heimnetz
        std::vector<std::string> vpn;     // andere IPv4-Adressen (z. B. Radmin, Hamachi, Tailscale)
        std::vector<std::string> ipv6;    // globale IPv6-Adressen dieses Rechners
        std::string publicIPv4;           // wie das Internet uns sieht
        std::string routerIPv4;           // externe Adresse laut Router
        Router router = Router::Searching;
        bool ipv6Opened = false;          // Router-Firewall für IPv6 geöffnet
        bool publicChecked = false;
        std::string routerError;
        // Kein eigenes öffentliches IPv4 (Carrier-Grade-NAT, DS-Lite): nur IPv6 oder VPN helfen
        bool cgnat() const;
    };

    ~HostNetwork() { stop(); }
    void start(uint16_t port, bool useUpnp);
    void stop();
    bool running() const { return m_thread.joinable(); }
    Status status() const;

private:
    void run();

    uint16_t m_port = 0;
    bool m_useUpnp = true;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::atomic<bool> m_cancel{false};
    Status m_status;
};

} // namespace ww
