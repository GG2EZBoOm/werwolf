#include "engine/net/Net.h"

#include "engine/core/Log.h"
#include "engine/core/Time.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace eng::net {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
const SocketHandle kInvalid = static_cast<SocketHandle>(INVALID_SOCKET);
int lastError() { return WSAGetLastError(); }
bool isWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool isInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
void closeNative(NativeSocket s) { closesocket(s); }
constexpr int kSendFlags = 0;
#else
using NativeSocket = int;
const SocketHandle kInvalid = static_cast<SocketHandle>(-1);
int lastError() { return errno; }
bool isWouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool isInProgress(int e) { return e == EINPROGRESS; }
void closeNative(NativeSocket s) { ::close(s); }
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

NativeSocket native(SocketHandle h) { return static_cast<NativeSocket>(h); }

constexpr size_t kMaxSendBuffer = 8 * 1024 * 1024;
constexpr double kAttemptSeconds = 4.0; // so lange bekommt jede einzelne Adresse

bool setNonBlocking(NativeSocket s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void setNoDelay(NativeSocket s) {
    int yes = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof yes);
}

std::string ipText(const sockaddr* addr) {
    char buffer[INET6_ADDRSTRLEN] = {};
    if (addr->sa_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(addr)->sin_addr, buffer, sizeof buffer);
        return buffer;
    }
    if (addr->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(addr);
        // IPv4-Verbindungen auf einem Dual-Stack-Socket erscheinen als ::ffff:a.b.c.d
        if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
            inet_ntop(AF_INET, reinterpret_cast<const uint8_t*>(&a6->sin6_addr) + 12, buffer, sizeof buffer);
            return buffer;
        }
        inet_ntop(AF_INET6, &a6->sin6_addr, buffer, sizeof buffer);
        return buffer;
    }
    return "?";
}

uint16_t portOf(const sockaddr* addr) {
    if (addr->sa_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(addr)->sin_port);
    if (addr->sa_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(addr)->sin6_port);
    return 0;
}

std::string formatAddress(const sockaddr* addr) { return formatEndpoint(ipText(addr), portOf(addr)); }

bool parsePortText(std::string_view text, uint16_t& port) {
    if (text.empty() || text.size() > 5) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value <= 0 || value > 65535) return false;
    port = static_cast<uint16_t>(value);
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '/')) s.remove_suffix(1);
    return s;
}

} // namespace

bool initialize() {
#ifdef _WIN32
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!ok) log::error("WSAStartup fehlgeschlagen");
    });
    return ok;
#else
    return true;
#endif
}

bool isIPv6Literal(std::string_view host) { return std::count(host.begin(), host.end(), ':') >= 2; }

bool isHomeNetworkIPv4(std::string_view address) {
    unsigned a = 0, b = 0;
    if (std::sscanf(std::string(address).c_str(), "%u.%u", &a, &b) != 2) return false;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

bool isPrivateIPv4(std::string_view address) { return isHomeNetworkIPv4(address) || isCarrierGradeNat(address); }

bool isCarrierGradeNat(std::string_view address) {
    unsigned a = 0, b = 0;
    if (std::sscanf(std::string(address).c_str(), "%u.%u", &a, &b) != 2) return false;
    return a == 100 && b >= 64 && b <= 127;
}

bool parseEndpoint(std::string_view text, std::string& host, uint16_t& port) {
    text = trim(text);
    // Ein vorangestelltes Schema (z. B. aus einem Link) ignorieren
    if (const size_t scheme = text.find("://"); scheme != std::string_view::npos) text.remove_prefix(scheme + 3);
    if (text.empty()) return false;
    if (text.front() == '[') {
        const size_t close = text.find(']');
        if (close == std::string_view::npos || close == 1) return false;
        host = std::string(text.substr(1, close - 1));
        const std::string_view rest = text.substr(close + 1);
        if (rest.empty()) return true;
        if (rest.front() != ':') return false;
        return parsePortText(rest.substr(1), port);
    }
    const size_t colons = static_cast<size_t>(std::count(text.begin(), text.end(), ':'));
    if (colons == 1) {
        const size_t colon = text.find(':');
        host = std::string(text.substr(0, colon));
        if (host.empty()) return false;
        return parsePortText(text.substr(colon + 1), port);
    }
    host = std::string(text); // kein Port oder eine IPv6-Adresse ohne Klammern
    return !host.empty();
}

std::string formatEndpoint(const std::string& host, uint16_t port) {
    return isIPv6Literal(host) ? "[" + host + "]:" + std::to_string(port) : host + ":" + std::to_string(port);
}

std::vector<LocalAddress> localAddresses() {
    initialize();
    std::vector<LocalAddress> result;
    auto add = [&](const sockaddr* addr, unsigned long validLifetime, std::vector<std::pair<unsigned long, LocalAddress>>& v6) {
        LocalAddress a;
        a.address = ipText(addr);
        if (addr->sa_family == AF_INET) {
            if (a.address.rfind("127.", 0) == 0 || a.address.rfind("169.254.", 0) == 0) return;
            a.global = !isPrivateIPv4(a.address);
            if (std::none_of(result.begin(), result.end(), [&](const LocalAddress& r) { return r.address == a.address; }))
                result.push_back(a);
        } else if (addr->sa_family == AF_INET6) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr);
            // nur globale Unicast-Adressen (2000::/3), keine Link-Local- oder ULA-Adressen
            if ((bytes[0] & 0xE0) != 0x20) return;
            // Teredo-Tunnel (2001:0::/32) sind von außen kaum erreichbar
            if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00) return;
            a.ipv6 = true;
            a.global = true;
            v6.push_back({validLifetime, a});
        }
    };
    std::vector<std::pair<unsigned long, LocalAddress>> v6;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        rc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (rc == NO_ERROR) {
        for (auto* ad = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); ad; ad = ad->Next) {
            if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* u = ad->FirstUnicastAddress; u; u = u->Next) {
                if (u->DadState != IpDadStatePreferred) continue;
                add(u->Address.lpSockaddr, u->ValidLifetime, v6);
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* it = list; it; it = it->ifa_next)
            if (it->ifa_addr) add(it->ifa_addr, 0, v6);
        freeifaddrs(list);
    }
#endif
    // Langlebige (stabile) IPv6-Adressen zuerst, temporäre Datenschutz-Adressen danach
    std::stable_sort(v6.begin(), v6.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& [lifetime, a] : v6)
        if (std::none_of(result.begin(), result.end(), [&](const LocalAddress& r) { return r.address == a.address; }))
            result.push_back(a);
    return result;
}

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::string> out;
    for (const LocalAddress& a : localAddresses())
        if (!a.ipv6) out.push_back(a.address);
    return out;
}

std::vector<std::string> defaultGateways() {
    initialize();
    std::vector<std::string> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* ad = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); ad; ad = ad->Next) {
        if (ad->OperStatus != IfOperStatusUp) continue;
        for (auto* g = ad->FirstGatewayAddress; g; g = g->Next) {
            const std::string ip = ipText(g->Address.lpSockaddr);
            if (ip != "0.0.0.0" && std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
        }
    }
#endif
    return out;
}

// --- Connection --------------------------------------------------------------

Connection::Connection() : m_socket(kInvalid) {}

Connection::~Connection() { close(); }

bool Connection::connect(const std::string& host, uint16_t port, double timeoutSeconds) {
    return connect(std::vector<std::string>{host}, port, timeoutSeconds);
}

bool Connection::connect(const std::vector<std::string>& hosts, uint16_t port, double timeoutSeconds) {
    close();
    m_error.clear();
    m_candidates.clear();
    m_nextCandidate = 0;
    if (!initialize()) {
        m_error = "Netzwerk konnte nicht initialisiert werden";
        return false;
    }

    // Alle Adressen auflösen; IPv6 und IPv4 abwechselnd, damit eine kaputte Familie nicht alles aufhält
    std::vector<std::vector<uint8_t>> v6, v4;
    const std::string portText = std::to_string(port);
    for (const std::string& host : hosts) {
        if (host.empty()) continue;
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0 || !res) {
            log::warn("Adresse \"{}\" wurde nicht gefunden", host);
            continue;
        }
        for (addrinfo* it = res; it; it = it->ai_next) {
            if (it->ai_family != AF_INET && it->ai_family != AF_INET6) continue;
            std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t*>(it->ai_addr),
                                       reinterpret_cast<const uint8_t*>(it->ai_addr) + it->ai_addrlen);
            auto& list = it->ai_family == AF_INET6 ? v6 : v4;
            if (std::find(list.begin(), list.end(), bytes) == list.end()) list.push_back(std::move(bytes));
        }
        freeaddrinfo(res);
    }
    for (size_t i = 0; i < std::max(v6.size(), v4.size()); ++i) {
        if (i < v6.size()) m_candidates.push_back(v6[i]);
        if (i < v4.size()) m_candidates.push_back(v4[i]);
    }
    if (m_candidates.empty()) {
        m_error = "Adresse wurde nicht gefunden";
        return false;
    }
    m_connectDeadline = nowSeconds() + timeoutSeconds;
    m_lastReceive = nowSeconds();
    if (!startNextAttempt()) {
        m_state = State::Disconnected;
        return false;
    }
    return true;
}

bool Connection::startNextAttempt() {
    closeSocket();
    while (m_nextCandidate < m_candidates.size()) {
        const std::vector<uint8_t>& bytes = m_candidates[m_nextCandidate++];
        const auto* addr = reinterpret_cast<const sockaddr*>(bytes.data());
        NativeSocket s = ::socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
        if (static_cast<SocketHandle>(s) == kInvalid) {
            m_error = "Socket konnte nicht erstellt werden";
            continue;
        }
        setNonBlocking(s);
        setNoDelay(s);
        m_address = formatAddress(addr);
        const int rc = ::connect(s, addr, static_cast<socklen_type>(bytes.size()));
        const int err = rc == 0 ? 0 : lastError();
        m_socket = static_cast<SocketHandle>(s);
        if (rc == 0) {
            m_state = State::Connected;
            m_lastReceive = nowSeconds();
            return true;
        }
        if (isInProgress(err)) {
            m_state = State::Connecting;
            m_attemptDeadline = std::min(m_connectDeadline, nowSeconds() + kAttemptSeconds);
            return true;
        }
        log::info("Verbindung zu {} fehlgeschlagen (Fehler {})", m_address, err);
        m_error = "Verbindung fehlgeschlagen";
        closeSocket();
    }
    if (m_error.empty()) m_error = "Server nicht erreichbar";
    return false;
}

void Connection::fail(const std::string& reason) {
    if (m_error.empty()) m_error = reason;
    closeSocket();
    m_state = State::Disconnected;
    // Empfangspuffer bleibt erhalten, damit z. B. eine letzte Nachricht noch gelesen werden kann
    m_send.clear();
    m_sendPos = 0;
}

void Connection::closeSocket() {
    if (m_socket != kInvalid) {
        closeNative(native(m_socket));
        m_socket = kInvalid;
    }
}

void Connection::close() {
    closeSocket();
    m_state = State::Disconnected;
    m_send.clear();
    m_sendPos = 0;
    m_recv.clear();
    m_recvPos = 0;
    m_closeAfterFlush = false;
    m_candidates.clear();
    m_nextCandidate = 0;
}

void Connection::closeAfterFlush() { m_closeAfterFlush = true; }

void Connection::update() {
    if (m_state == State::Connecting) {
        fd_set writeSet, errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(native(m_socket), &writeSet);
        FD_SET(native(m_socket), &errorSet);
        timeval tv{0, 0};
        const int r = ::select(static_cast<int>(native(m_socket)) + 1, nullptr, &writeSet, &errorSet, &tv);
        bool attemptFailed = false;
        std::string reason;
        if (r > 0) {
            int soError = 0;
            socklen_type len = sizeof soError;
            getsockopt(native(m_socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
            if (soError == 0 && FD_ISSET(native(m_socket), &writeSet)) {
                m_state = State::Connected;
                m_lastReceive = nowSeconds();
                m_error.clear();
            } else {
                log::info("{} nicht erreichbar (Fehler {})", m_address, soError);
                attemptFailed = true;
                reason = "Server nicht erreichbar";
            }
        } else if (r < 0) {
            attemptFailed = true;
            reason = "Verbindung fehlgeschlagen";
        } else if (nowSeconds() > m_attemptDeadline) {
            log::info("{} antwortet nicht", m_address);
            attemptFailed = true;
            reason = "Zeitüberschreitung beim Verbinden";
        } else {
            return;
        }
        if (attemptFailed) {
            m_error = reason;
            // die nächste Adresse versuchen, solange Zeit bleibt
            if (nowSeconds() < m_connectDeadline && startNextAttempt()) {
                if (m_state == State::Connecting) return;
            } else {
                fail(reason);
                return;
            }
        }
    }
    if (m_state != State::Connected) return;

    flushSend();
    if (m_state != State::Connected) return;
    readAvailable();
    if (m_state == State::Connected && m_closeAfterFlush && pendingSendBytes() == 0) {
        closeSocket();
        m_state = State::Disconnected;
    }
}

void Connection::flushSend() {
    while (m_sendPos < m_send.size()) {
        const size_t remaining = m_send.size() - m_sendPos;
        const int chunk = static_cast<int>(std::min<size_t>(remaining, 1 << 20));
        const auto n = ::send(native(m_socket), reinterpret_cast<const char*>(m_send.data() + m_sendPos), chunk,
                              kSendFlags);
        if (n > 0) {
            m_sendPos += static_cast<size_t>(n);
            continue;
        }
        const int e = lastError();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        if (isWouldBlock(e)) break;
        fail("Sendefehler (Verbindung abgebrochen)");
        return;
    }
    if (m_sendPos == m_send.size()) {
        m_send.clear();
        m_sendPos = 0;
    } else if (m_sendPos > 64 * 1024) {
        m_send.erase(m_send.begin(), m_send.begin() + static_cast<std::ptrdiff_t>(m_sendPos));
        m_sendPos = 0;
    }
}

void Connection::readAvailable() {
    uint8_t buffer[16 * 1024];
    for (;;) {
        const auto n = ::recv(native(m_socket), reinterpret_cast<char*>(buffer), sizeof buffer, 0);
        if (n > 0) {
            m_recv.insert(m_recv.end(), buffer, buffer + n);
            m_lastReceive = nowSeconds();
            if (m_recv.size() - m_recvPos > kMaxMessageSize * 4) {
                fail("Zu viele Daten empfangen");
                return;
            }
            continue;
        }
        if (n == 0) {
            fail("Verbindung wurde getrennt");
            return;
        }
        const int e = lastError();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        if (isWouldBlock(e)) return;
        fail("Verbindung abgebrochen");
        return;
    }
}

bool Connection::receive(std::vector<uint8_t>& payload) {
    const size_t available = m_recv.size() - m_recvPos;
    if (available < 4) {
        if (m_recvPos > 0 && available == 0) {
            m_recv.clear();
            m_recvPos = 0;
        }
        return false;
    }
    const uint8_t* p = m_recv.data() + m_recvPos;
    const uint32_t len = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    if (len > kMaxMessageSize) {
        fail("Ungültige Nachricht empfangen");
        m_recv.clear();
        m_recvPos = 0;
        return false;
    }
    if (available < 4 + static_cast<size_t>(len)) return false;
    payload.assign(p + 4, p + 4 + len);
    m_recvPos += 4 + len;
    if (m_recvPos == m_recv.size()) {
        m_recv.clear();
        m_recvPos = 0;
    } else if (m_recvPos > 256 * 1024) {
        m_recv.erase(m_recv.begin(), m_recv.begin() + static_cast<std::ptrdiff_t>(m_recvPos));
        m_recvPos = 0;
    }
    return true;
}

void Connection::send(const std::vector<uint8_t>& payload) { send(payload.data(), payload.size()); }

void Connection::send(const uint8_t* data, size_t size) {
    if (m_state == State::Disconnected || m_closeAfterFlush) return;
    if (size > kMaxMessageSize) {
        log::error("Nachricht zu groß ({} Bytes) – wird verworfen", size);
        return;
    }
    if (pendingSendBytes() + size > kMaxSendBuffer) {
        fail("Sendepuffer übergelaufen (Gegenstelle reagiert nicht)");
        return;
    }
    const uint32_t len = static_cast<uint32_t>(size);
    const uint8_t header[4] = {static_cast<uint8_t>(len >> 24), static_cast<uint8_t>(len >> 16),
                               static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len)};
    m_send.insert(m_send.end(), header, header + 4);
    m_send.insert(m_send.end(), data, data + size);
    if (m_state == State::Connected) flushSend();
}

// --- Listener ----------------------------------------------------------------

Listener::Listener() : m_socket(kInvalid) {}

Listener::~Listener() { close(); }

bool Listener::listen(uint16_t port, std::string* error) {
    close();
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        log::error("Listener: {}", msg);
        close();
        return false;
    };
    if (!initialize()) return fail("Netzwerk konnte nicht initialisiert werden");

    auto exclusive = [](NativeSocket s) {
#ifdef _WIN32
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
#else
        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif
    };

    // Bevorzugt ein Dual-Stack-Socket: IPv6 und (als ::ffff:a.b.c.d) IPv4 auf demselben Port
    NativeSocket s = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (static_cast<SocketHandle>(s) != kInvalid) {
        int v6only = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof v6only);
        exclusive(s);
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addr.sin6_addr = in6addr_any;
        if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0 && ::listen(s, 16) == 0) {
            setNonBlocking(s);
            m_socket = static_cast<SocketHandle>(s);
            m_dualStack = true;
            return true;
        }
        const int err = lastError();
        closeNative(s);
#ifdef _WIN32
        if (err == WSAEADDRINUSE || err == WSAEACCES)
#else
        if (err == EADDRINUSE || err == EACCES)
#endif
            return fail("Port ist bereits belegt oder nicht verfügbar");
        log::warn("IPv6 nicht verfügbar (Fehler {}) – lausche nur auf IPv4", err);
    }

    s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (static_cast<SocketHandle>(s) == kInvalid) return fail("Socket konnte nicht erstellt werden");
    m_socket = static_cast<SocketHandle>(s);
    exclusive(s);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0)
        return fail("Port ist bereits belegt oder nicht verfügbar");
    if (::listen(s, 16) != 0) return fail("Verbindung fehlgeschlagen");
    setNonBlocking(s);
    m_dualStack = false;
    return true;
}

std::unique_ptr<Connection> Listener::accept() {
    if (m_socket == kInvalid) return nullptr;
    sockaddr_storage addr{};
    socklen_type len = sizeof addr;
    NativeSocket c = ::accept(native(m_socket), reinterpret_cast<sockaddr*>(&addr), &len);
    if (static_cast<SocketHandle>(c) == kInvalid) return nullptr;
    setNonBlocking(c);
    setNoDelay(c);
    auto conn = std::make_unique<Connection>();
    conn->m_socket = static_cast<SocketHandle>(c);
    conn->m_state = Connection::State::Connected;
    conn->m_address = formatAddress(reinterpret_cast<const sockaddr*>(&addr));
    conn->m_lastReceive = nowSeconds();
    return conn;
}

void Listener::close() {
    if (m_socket != kInvalid) {
        closeNative(native(m_socket));
        m_socket = kInvalid;
    }
    m_dualStack = false;
}

bool Listener::isListening() const { return m_socket != kInvalid; }

} // namespace eng::net
