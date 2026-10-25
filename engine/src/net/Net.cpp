#include "engine/net/Net.h"

#include "engine/core/Log.h"
#include "engine/core/Time.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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

std::string formatAddress(const sockaddr_in& addr) {
    char buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof buffer);
    return std::string(buffer) + ":" + std::to_string(ntohs(addr.sin_port));
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

std::vector<std::string> localIPv4Addresses() {
    initialize();
    std::vector<std::string> result;
#ifdef _WIN32
    char hostname[256] = {};
    if (gethostname(hostname, sizeof hostname) != 0) return result;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* list = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &list) != 0) return result;
    for (addrinfo* it = list; it; it = it->ai_next) {
        const auto* addr = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
        char buffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof buffer);
        std::string ip = buffer;
        if (ip.rfind("127.", 0) != 0 && std::find(result.begin(), result.end(), ip) == result.end())
            result.push_back(ip);
    }
    freeaddrinfo(list);
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        const auto* addr = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
        char buffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof buffer);
        std::string ip = buffer;
        if (ip.rfind("127.", 0) != 0) result.push_back(ip);
    }
    freeifaddrs(list);
#endif
    return result;
}

// --- Connection --------------------------------------------------------------

Connection::Connection() : m_socket(kInvalid) {}

Connection::~Connection() { close(); }

bool Connection::connect(const std::string& host, uint16_t port, double timeoutSeconds) {
    close();
    m_error.clear();
    if (!initialize()) {
        m_error = "Netzwerk konnte nicht initialisiert werden";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0 || !res) {
        m_error = "Adresse \"" + host + "\" wurde nicht gefunden";
        return false;
    }

    NativeSocket s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (static_cast<SocketHandle>(s) == kInvalid) {
        freeaddrinfo(res);
        m_error = "Socket konnte nicht erstellt werden";
        return false;
    }
    setNonBlocking(s);
    setNoDelay(s);
    m_address = formatAddress(*reinterpret_cast<const sockaddr_in*>(res->ai_addr));
    const int rc = ::connect(s, res->ai_addr, static_cast<socklen_type>(res->ai_addrlen));
    const int err = rc == 0 ? 0 : lastError();
    freeaddrinfo(res);

    m_socket = static_cast<SocketHandle>(s);
    if (rc == 0) {
        m_state = State::Connected;
    } else if (isInProgress(err)) {
        m_state = State::Connecting;
        m_connectDeadline = nowSeconds() + timeoutSeconds;
    } else {
        fail("Verbindung fehlgeschlagen (Fehler " + std::to_string(err) + ")");
        return false;
    }
    m_lastReceive = nowSeconds();
    return true;
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
        if (r > 0) {
            int soError = 0;
            socklen_type len = sizeof soError;
            getsockopt(native(m_socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
            if (soError == 0 && FD_ISSET(native(m_socket), &writeSet)) {
                m_state = State::Connected;
                m_lastReceive = nowSeconds();
            } else {
                fail("Server nicht erreichbar");
                return;
            }
        } else if (r < 0) {
            fail("Verbindung fehlgeschlagen");
            return;
        } else if (nowSeconds() > m_connectDeadline) {
            fail("Zeitüberschreitung beim Verbinden");
            return;
        } else {
            return;
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

    NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (static_cast<SocketHandle>(s) == kInvalid) return fail("Socket konnte nicht erstellt werden");
    m_socket = static_cast<SocketHandle>(s);

#ifdef _WIN32
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
#else
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0)
        return fail("Port " + std::to_string(port) + " ist bereits belegt oder nicht verfügbar");
    if (::listen(s, 16) != 0) return fail("listen() fehlgeschlagen");
    setNonBlocking(s);
    return true;
}

std::unique_ptr<Connection> Listener::accept() {
    if (m_socket == kInvalid) return nullptr;
    sockaddr_in addr{};
    socklen_type len = sizeof addr;
    NativeSocket c = ::accept(native(m_socket), reinterpret_cast<sockaddr*>(&addr), &len);
    if (static_cast<SocketHandle>(c) == kInvalid) return nullptr;
    setNonBlocking(c);
    setNoDelay(c);
    auto conn = std::make_unique<Connection>();
    conn->m_socket = static_cast<SocketHandle>(c);
    conn->m_state = Connection::State::Connected;
    conn->m_address = formatAddress(addr);
    conn->m_lastReceive = nowSeconds();
    return conn;
}

void Listener::close() {
    if (m_socket != kInvalid) {
        closeNative(native(m_socket));
        m_socket = kInvalid;
    }
}

bool Listener::isListening() const { return m_socket != kInvalid; }

} // namespace eng::net
