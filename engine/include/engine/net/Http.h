#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eng::net {

// Fortschritt: empfangene Bytes, Gesamtgröße (0 = unbekannt)
using HttpProgress = std::function<void(uint64_t received, uint64_t total)>;

struct HttpOptions {
    uint64_t maxBytes = 512ull * 1024 * 1024;
    int timeoutMs = 15000;
    std::string userAgent = "Werwolf";
    const std::atomic<bool>* cancel = nullptr; // auf true setzen, um abzubrechen
    HttpProgress progress;
};

// Lädt eine Datei per HTTP(S) GET. Weiterleitungen werden befolgt.
// Aus Sicherheitsgründen nur HTTPS – unverschlüsseltes HTTP nur für localhost (Tests).
bool httpGet(const std::string& url, std::vector<uint8_t>& body, std::string& error, const HttpOptions& options = {});

// JSON to an IPv4 loopback service only, without proxy or redirects (local bot inference).
bool httpPostLocal(const std::string& url, const std::string& json, std::vector<uint8_t>& body,
                   std::string& error, const HttpOptions& options = {});

bool isLocalHttpUrl(const std::string& url);

} // namespace eng::net
