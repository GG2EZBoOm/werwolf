#include "client/HostNetwork.h"

#include "engine/core/Log.h"
#include "engine/net/Http.h"

#include <chrono>

namespace ww {

namespace {
constexpr int kPinholeLease = 7200;            // Sekunden; wird regelmäßig erneuert
constexpr auto kRefreshInterval = std::chrono::minutes(50);

bool looksLikeIPv4(const std::string& s) {
    if (s.empty() || s.size() > 15) return false;
    for (char c : s)
        if ((c < '0' || c > '9') && c != '.') return false;
    return true;
}
} // namespace

bool HostNetwork::Status::cgnat() const {
    if (routerIPv4.empty()) return false;
    // DS-Lite meldet oft 0.0.0.0 oder eine Adresse aus 192.0.0.0/29
    if (routerIPv4 == "0.0.0.0" || routerIPv4.rfind("192.0.0.", 0) == 0) return true;
    if (eng::net::isPrivateIPv4(routerIPv4) || eng::net::isCarrierGradeNat(routerIPv4)) return true;
    return !publicIPv4.empty() && publicIPv4 != routerIPv4;
}

void HostNetwork::start(uint16_t port, bool useUpnp) {
    stop();
    m_port = port;
    m_useUpnp = useUpnp;
    m_stop = false;
    m_cancel = false;
    {
        std::lock_guard lock(m_mutex);
        m_status = {};
        for (const eng::net::LocalAddress& a : eng::net::localAddresses()) {
            if (a.ipv6) m_status.ipv6.push_back(a.address);
            else if (eng::net::isHomeNetworkIPv4(a.address)) m_status.lan.push_back(a.address);
            else m_status.vpn.push_back(a.address);
        }
        m_status.router = useUpnp ? Router::Searching : Router::Disabled;
    }
    m_thread = std::thread([this] { run(); });
}

void HostNetwork::stop() {
    if (!m_thread.joinable()) return;
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cancel = true;
    m_wake.notify_all();
    m_thread.join();
}

HostNetwork::Status HostNetwork::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void HostNetwork::run() {
    eng::log::setThreadTag("netz");
    eng::net::UpnpGateway gateway;
    bool haveGateway = false, mapped = false;
    std::vector<std::string> pinholes;
    std::vector<std::string> ipv6;
    {
        std::lock_guard lock(m_mutex);
        ipv6 = m_status.ipv6;
    }

    auto openPinholes = [&] {
        bool opened = false;
        for (const std::string& id : pinholes) eng::net::upnpClosePinhole(gateway, id);
        pinholes.clear();
        for (const std::string& address : ipv6) {
            std::string id, error;
            if (eng::net::upnpOpenPinhole(gateway, address, m_port, kPinholeLease, id, error)) {
                pinholes.push_back(id);
                opened = true;
            } else {
                eng::log::info("IPv6-Freigabe für {} nicht möglich: {}", address, error);
            }
        }
        return opened;
    };

    if (m_useUpnp) {
        std::string error;
        haveGateway = eng::net::upnpDiscover(gateway, 2.5, error);
        if (haveGateway) {
            std::string external;
            eng::net::upnpExternalAddress(gateway, external);
            mapped = eng::net::upnpAddPortMapping(gateway, m_port, "Werwolf", error);
            if (mapped) eng::log::info("Port {} im Router freigegeben (extern {})", m_port, external);
            else eng::log::warn("Portfreigabe im Router fehlgeschlagen: {}", error);
            const bool opened = !ipv6.empty() && openPinholes();
            std::lock_guard lock(m_mutex);
            m_status.routerIPv4 = external;
            m_status.router = mapped ? Router::Opened : Router::Failed;
            m_status.routerError = mapped ? std::string() : error;
            m_status.ipv6Opened = opened;
        } else {
            eng::log::info("UPnP: {}", error);
            std::lock_guard lock(m_mutex);
            m_status.router = Router::Failed;
            m_status.routerError = error;
        }
    }

    // Öffentliche IPv4-Adresse erfragen, um Carrier-Grade-NAT zu erkennen
    {
        eng::net::HttpOptions options;
        options.timeoutMs = 5000;
        options.maxBytes = 256;
        options.cancel = &m_cancel;
        std::vector<uint8_t> body;
        std::string error, address;
        if (eng::net::httpGet("https://api.ipify.org", body, error, options)) address.assign(body.begin(), body.end());
        while (!address.empty() && (address.back() == '\n' || address.back() == '\r' || address.back() == ' ')) address.pop_back();
        if (!looksLikeIPv4(address)) address.clear();
        std::lock_guard lock(m_mutex);
        m_status.publicIPv4 = address;
        m_status.publicChecked = true;
    }

    // Freigaben regelmäßig erneuern, solange gehostet wird
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        m_wake.wait_for(lock, kRefreshInterval, [&] { return m_stop; });
        if (m_stop || !haveGateway) continue;
        lock.unlock();
        std::string error;
        if (mapped) eng::net::upnpAddPortMapping(gateway, m_port, "Werwolf", error);
        const bool opened = !pinholes.empty() && openPinholes();
        lock.lock();
        m_status.ipv6Opened = opened;
    }
    lock.unlock();

    if (haveGateway) {
        if (mapped) eng::net::upnpDeletePortMapping(gateway, m_port);
        for (const std::string& id : pinholes) eng::net::upnpClosePinhole(gateway, id);
        eng::log::info("Portfreigaben im Router wieder entfernt");
    }
}

} // namespace ww
