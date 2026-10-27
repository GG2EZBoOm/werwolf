#pragma once

#include <cstdint>
#include <string>

// UPnP-IGD: Portfreigaben im Heimrouter anlegen, damit Mitspieler aus dem Internet einen
// gehosteten Server erreichen. Alle Funktionen blockieren (für einen Hintergrund-Thread gedacht).
namespace eng::net {

struct UpnpGateway {
    std::string host;          // Router im Heimnetz
    uint16_t port = 0;         // Port des UPnP-Dienstes
    std::string controlPath;   // WANIPConnection bzw. WANPPPConnection (IPv4)
    std::string serviceType;
    std::string firewallPath;  // WANIPv6FirewallControl (leer, falls nicht angeboten)
    std::string localAddress;  // eigene IPv4-Adresse aus Sicht des Routers
};

// Sucht einen Router per SSDP. Liefert false, wenn keiner antwortet.
bool upnpDiscover(UpnpGateway& gateway, double timeoutSeconds, std::string& error);
// TCP-Port vom Internet auf diesen Rechner weiterleiten
bool upnpAddPortMapping(const UpnpGateway& gateway, uint16_t port, const std::string& description, std::string& error);
bool upnpDeletePortMapping(const UpnpGateway& gateway, uint16_t port);
bool upnpExternalAddress(const UpnpGateway& gateway, std::string& address);
// IPv6: eingehende TCP-Verbindungen zu dieser Adresse in der Router-Firewall erlauben
bool upnpOpenPinhole(const UpnpGateway& gateway, const std::string& ipv6, uint16_t port, int leaseSeconds, std::string& id,
                     std::string& error);
bool upnpClosePinhole(const UpnpGateway& gateway, const std::string& id);

} // namespace eng::net
