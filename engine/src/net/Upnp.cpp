#include "engine/net/Upnp.h"

#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/net/Net.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace eng::net {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
const NativeSocket kBad = INVALID_SOCKET;
void closeNative(NativeSocket s) { closesocket(s); }
#else
using NativeSocket = int;
const NativeSocket kBad = -1;
void closeNative(NativeSocket s) { ::close(s); }
#endif

struct SocketGuard {
    NativeSocket s;
    ~SocketGuard() {
        if (s != kBad) closeNative(s);
    }
};

bool waitFor(NativeSocket s, bool write, double seconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{static_cast<long>(seconds), static_cast<long>((seconds - static_cast<long>(seconds)) * 1e6)};
    const int r = ::select(static_cast<int>(s) + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
    return r > 0;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "http://192.168.178.1:49000/igddesc.xml" zerlegen
bool parseHttpUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path) {
    const std::string prefix = "http://";
    if (lower(url.substr(0, prefix.size())) != prefix) return false;
    const size_t slash = url.find('/', prefix.size());
    const std::string hostPort = url.substr(prefix.size(), slash == std::string::npos ? std::string::npos : slash - prefix.size());
    path = slash == std::string::npos ? "/" : url.substr(slash);
    port = 80;
    return parseEndpoint(hostPort, host, port);
}

// Einfaches HTTP/1.1 über TCP (UPnP spricht im Heimnetz kein HTTPS)
bool httpRequest(const std::string& host, uint16_t port, const std::string& request, std::string& response, double timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
    SocketGuard g{::socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP)};
    if (g.s == kBad) {
        freeaddrinfo(res);
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(g.s, FIONBIO, &nb);
#endif
    ::connect(g.s, res->ai_addr, static_cast<socklen_type>(res->ai_addrlen));
    freeaddrinfo(res);
    if (!waitFor(g.s, true, timeout)) return false;
    int soError = 0;
    socklen_type len = sizeof soError;
    getsockopt(g.s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
    if (soError != 0) return false;
    size_t sent = 0;
    const double deadline = nowSeconds() + timeout;
    while (sent < request.size()) {
        if (!waitFor(g.s, true, std::max(0.05, deadline - nowSeconds()))) return false;
        const auto n = ::send(g.s, request.data() + sent, static_cast<int>(request.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    response.clear();
    char buffer[8192];
    while (nowSeconds() < deadline && response.size() < 512 * 1024) {
        if (!waitFor(g.s, false, std::max(0.05, deadline - nowSeconds()))) break;
        const auto n = ::recv(g.s, buffer, sizeof buffer, 0);
        if (n <= 0) break;
        response.append(buffer, static_cast<size_t>(n));
    }
    return !response.empty();
}

int statusCode(const std::string& response) {
    // "HTTP/1.1 200 OK"
    const size_t space = response.find(' ');
    return space == std::string::npos ? 0 : std::atoi(response.c_str() + space + 1);
}

std::string body(const std::string& response) {
    const size_t pos = response.find("\r\n\r\n");
    return pos == std::string::npos ? std::string() : response.substr(pos + 4);
}

// Inhalt des ersten Elements <tag>…</tag> (Namensraum-Präfixe werden ignoriert)
std::string xmlValue(const std::string& xml, const std::string& tag, size_t from = 0, size_t* end = nullptr) {
    size_t pos = from;
    while ((pos = xml.find(tag, pos)) != std::string::npos) {
        const size_t open = xml.rfind('<', pos);
        const size_t close = xml.find('>', pos);
        if (open == std::string::npos || close == std::string::npos || xml[open + 1] == '/') {
            pos += tag.size();
            continue;
        }
        const size_t stop = xml.find("</", close);
        if (stop == std::string::npos) return {};
        if (end) *end = stop;
        std::string value = xml.substr(close + 1, stop - close - 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
        return value;
    }
    return {};
}

std::string soap(const UpnpGateway& gw, const std::string& path, const std::string& service, const std::string& action,
                 const std::string& args, int& status) {
    const std::string envelope =
        "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
        action + " xmlns:u=\"" + service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    const std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + formatEndpoint(gw.host, gw.port) +
                                "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + service + "#" + action +
                                "\"\r\nContent-Length: " + std::to_string(envelope.size()) + "\r\nConnection: close\r\n\r\n" +
                                envelope;
    std::string response;
    status = 0;
    if (!httpRequest(gw.host, gw.port, request, response, 4.0)) return {};
    status = statusCode(response);
    return body(response);
}

std::string soapError(int status, const std::string& reply) {
    const std::string code = xmlValue(reply, "errorCode");
    const std::string text = xmlValue(reply, "errorDescription");
    if (status == 0) return "keine Antwort vom Router";
    return "HTTP " + std::to_string(status) + (code.empty() ? "" : ", UPnP " + code) + (text.empty() ? "" : " (" + text + ")");
}

// Gerätebeschreibung laden und die Dienste suchen
bool readDescription(const std::string& location, UpnpGateway& gw) {
    std::string host, path;
    uint16_t port = 80;
    if (!parseHttpUrl(location, host, port, path)) return false;
    std::string response;
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + formatEndpoint(host, port) + "\r\nConnection: close\r\n\r\n";
    if (!httpRequest(host, port, request, response, 3.0) || statusCode(response) != 200) return false;
    const std::string xml = body(response);
    gw.host = host;
    gw.port = port;
    gw.controlPath.clear();
    gw.firewallPath.clear();
    int bestRank = 0;
    size_t pos = 0;
    while ((pos = xml.find("<serviceType>", pos)) != std::string::npos) {
        size_t end = pos;
        const std::string type = xmlValue(xml, "serviceType", pos, &end);
        std::string control = xmlValue(xml, "controlURL", end);
        pos = end;
        if (control.empty()) continue;
        // absolute Adressen auf den Pfad kürzen
        if (control.rfind("http://", 0) == 0) {
            std::string h, p;
            uint16_t pt = 80;
            if (parseHttpUrl(control, h, pt, p)) control = p;
        }
        if (control.front() != '/') control = "/" + control;
        int rank = 0;
        if (type.find("WANIPConnection:2") != std::string::npos) rank = 3;
        else if (type.find("WANIPConnection:1") != std::string::npos) rank = 2;
        else if (type.find("WANPPPConnection:1") != std::string::npos) rank = 1;
        else if (type.find("WANIPv6FirewallControl:1") != std::string::npos) gw.firewallPath = control;
        if (rank > bestRank) {
            bestRank = rank;
            gw.controlPath = control;
            gw.serviceType = type;
        }
    }
    return bestRank > 0;
}

// Eigene IPv4-Adresse, mit der wir den Router erreichen
std::string localAddressTowards(const std::string& host, uint16_t port) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &to.sin_addr) != 1) return {};
    SocketGuard g{::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    if (g.s == kBad || ::connect(g.s, reinterpret_cast<const sockaddr*>(&to), sizeof to) != 0) return {};
    sockaddr_in me{};
    socklen_type len = sizeof me;
    if (getsockname(g.s, reinterpret_cast<sockaddr*>(&me), &len) != 0) return {};
    char buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &me.sin_addr, buffer, sizeof buffer);
    return buffer;
}

} // namespace

bool upnpDiscover(UpnpGateway& gateway, double timeoutSeconds, std::string& error) {
    if (!initialize()) {
        error = "Netzwerk nicht verfügbar";
        return false;
    }
    SocketGuard g{::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    if (g.s == kBad) {
        error = "kein UDP-Socket";
        return false;
    }
    sockaddr_in any{};
    any.sin_family = AF_INET;
    ::bind(g.s, reinterpret_cast<const sockaddr*>(&any), sizeof any);
    sockaddr_in multicast{};
    multicast.sin_family = AF_INET;
    multicast.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &multicast.sin_addr);
    // Über jeden Adapter suchen: sonst landet die Anfrage bei mehreren Netzen (VPN!) leicht im falschen
    std::vector<std::string> interfaces = localIPv4Addresses();
    interfaces.push_back(""); // zuletzt über die Standardroute
    for (const std::string& local : interfaces) {
        in_addr ifAddr{};
        if (!local.empty() && inet_pton(AF_INET, local.c_str(), &ifAddr) == 1)
            setsockopt(g.s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&ifAddr), sizeof ifAddr);
        for (const char* target : {"urn:schemas-upnp-org:device:InternetGatewayDevice:2", "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                   "urn:schemas-upnp-org:service:WANIPConnection:1"}) {
            const std::string search = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") +
                                       target + "\r\n\r\n";
            ::sendto(g.s, search.data(), static_cast<int>(search.size()), 0, reinterpret_cast<const sockaddr*>(&multicast), sizeof multicast);
        }
    }
    std::vector<std::string> locations;
    const double deadline = nowSeconds() + timeoutSeconds;
    char buffer[2048];
    while (nowSeconds() < deadline) {
        if (!waitFor(g.s, false, std::max(0.05, deadline - nowSeconds()))) break;
        const auto n = ::recv(g.s, buffer, sizeof buffer - 1, 0);
        if (n <= 0) continue;
        buffer[n] = 0;
        const std::string reply(buffer, static_cast<size_t>(n));
        const std::string low = lower(reply);
        const size_t pos = low.find("\nlocation:");
        if (pos == std::string::npos) continue;
        size_t start = pos + 10;
        while (start < reply.size() && reply[start] == ' ') ++start;
        const size_t stop = reply.find_first_of("\r\n", start);
        const std::string location = reply.substr(start, stop - start);
        if (std::find(locations.begin(), locations.end(), location) != locations.end()) continue;
        locations.push_back(location);
        if (readDescription(location, gateway)) {
            gateway.localAddress = localAddressTowards(gateway.host, gateway.port);
            log::info("UPnP-Router gefunden: {} ({})", location, gateway.serviceType);
            return true;
        }
    }
    // Manche Router antworten nicht auf die Suche: bekannte Beschreibungen direkt am Gateway versuchen
    for (const std::string& host : defaultGateways()) {
        for (const char* path : {":49000/igd2desc.xml", ":49000/igddesc.xml", ":5000/rootDesc.xml", ":1900/rootDesc.xml", ":2189/rootDesc.xml"}) {
            const std::string location = "http://" + host + path;
            if (std::find(locations.begin(), locations.end(), location) != locations.end()) continue;
            locations.push_back(location);
            if (readDescription(location, gateway)) {
                gateway.localAddress = localAddressTowards(gateway.host, gateway.port);
                log::info("UPnP-Router direkt gefunden: {} ({})", location, gateway.serviceType);
                return true;
            }
        }
    }
    error = locations.empty() ? "kein UPnP-Router gefunden" : "Router bietet keine Portfreigabe an (UPnP aus?)";
    return false;
}

bool upnpAddPortMapping(const UpnpGateway& gw, uint16_t port, const std::string& description, std::string& error) {
    if (gw.localAddress.empty()) {
        error = "eigene Adresse unbekannt";
        return false;
    }
    // Erst dauerhaft versuchen, manche Router verlangen eine begrenzte Dauer
    for (const char* lease : {"0", "86400"}) {
        const std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
                                 "</NewExternalPort><NewProtocol>TCP</NewProtocol><NewInternalPort>" + std::to_string(port) +
                                 "</NewInternalPort><NewInternalClient>" + gw.localAddress +
                                 "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" + description +
                                 "</NewPortMappingDescription><NewLeaseDuration>" + lease + "</NewLeaseDuration>";
        int status = 0;
        const std::string reply = soap(gw, gw.controlPath, gw.serviceType, "AddPortMapping", args, status);
        if (status == 200) return true;
        error = soapError(status, reply);
    }
    return false;
}

bool upnpDeletePortMapping(const UpnpGateway& gw, uint16_t port) {
    const std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
                             "</NewExternalPort><NewProtocol>TCP</NewProtocol>";
    int status = 0;
    soap(gw, gw.controlPath, gw.serviceType, "DeletePortMapping", args, status);
    return status == 200;
}

bool upnpExternalAddress(const UpnpGateway& gw, std::string& address) {
    int status = 0;
    const std::string reply = soap(gw, gw.controlPath, gw.serviceType, "GetExternalIPAddress", "", status);
    address = status == 200 ? xmlValue(reply, "NewExternalIPAddress") : std::string();
    return !address.empty();
}

bool upnpOpenPinhole(const UpnpGateway& gw, const std::string& ipv6, uint16_t port, int leaseSeconds, std::string& id,
                     std::string& error) {
    if (gw.firewallPath.empty()) {
        error = "Router bietet keine IPv6-Freigabe an";
        return false;
    }
    const std::string service = "urn:schemas-upnp-org:service:WANIPv6FirewallControl:1";
    const std::string args = "<RemoteHost></RemoteHost><RemotePort>0</RemotePort><InternalClient>" + ipv6 +
                             "</InternalClient><InternalPort>" + std::to_string(port) + "</InternalPort><Protocol>6</Protocol><LeaseTime>" +
                             std::to_string(leaseSeconds) + "</LeaseTime>";
    int status = 0;
    const std::string reply = soap(gw, gw.firewallPath, service, "AddPinhole", args, status);
    if (status != 200) {
        error = soapError(status, reply);
        return false;
    }
    id = xmlValue(reply, "UniqueID");
    return true;
}

bool upnpClosePinhole(const UpnpGateway& gw, const std::string& id) {
    if (gw.firewallPath.empty() || id.empty()) return false;
    int status = 0;
    soap(gw, gw.firewallPath, "urn:schemas-upnp-org:service:WANIPv6FirewallControl:1", "DeletePinhole", "<UniqueID>" + id + "</UniqueID>",
         status);
    return status == 200;
}

} // namespace eng::net
