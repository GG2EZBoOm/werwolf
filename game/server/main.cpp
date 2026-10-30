// Dedizierter Werwolf-Server (Konsolenprogramm).
//   werwolf_server [--port 27777] [--timescale 1.0] [--quiet]

#include "server/GameServer.h"

#include "engine/core/Log.h"
#include "engine/net/Net.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

void printUsage() {
    std::printf(ww::L("Werwolf-Server\n"
                      "  --port <n>        TCP-Port (Standard %u)\n"
                      "  --timescale <f>   Zeitraffer für Tests (Standard 1.0)\n"
                      "  --quiet           Chat nicht protokollieren\n",
                      "Werewolf server\n"
                      "  --port <n>        TCP port (default %u)\n"
                      "  --timescale <f>   time lapse for tests (default 1.0)\n"
                      "  --quiet           don't log the chat\n"),
                static_cast<unsigned>(ww::proto::kDefaultPort));
    std::puts(ww::L("  --no-ai          Lokales Sprachmodell ausschalten\n"
                    "  --ai-dir <Pfad>  Anderer Ordner des KI-Pakets\n"
                    "  --ai-port <n>    Vorhandenes Sprachmodell auf 127.0.0.1 verwenden",
                    "  --no-ai          Disable local language model\n"
                    "  --ai-dir <path>  Alternative AI pack directory\n"
                    "  --ai-port <n>    Use existing language model on 127.0.0.1"));
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    ww::setLanguage(ww::systemLanguage());
    ww::ServerOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--timescale" && i + 1 < argc) {
            options.timeScale = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--quiet") {
            options.logChat = false;
        } else if (arg == "--no-ai") {
            options.language.enabled = false;
        } else if (arg == "--ai-port" && i + 1 < argc) {
            const int port = std::atoi(argv[++i]);
            if (port <= 0 || port > 65535) return 1;
            options.language.port = static_cast<uint16_t>(port);
        } else if (arg == "--ai-dir" && i + 1 < argc) {
            options.language.directory = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(argv[++i])));
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::printf(ww::L("Unbekanntes Argument: %s\n", "Unknown argument: %s\n"), arg.c_str());
            printUsage();
            return 1;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    ww::GameServer server(options);
    std::string error;
    if (!server.start(error)) {
        eng::log::error("Server konnte nicht gestartet werden: {}", error);
        return 1;
    }
    for (const eng::net::LocalAddress& a : eng::net::localAddresses()) {
        const std::string endpoint = eng::net::formatEndpoint(a.address, options.port);
        if (a.ipv6) eng::log::info("{} {}", ww::L("Erreichbar über IPv6 unter", "Reachable via IPv6 at"), endpoint);
        else if (eng::net::isHomeNetworkIPv4(a.address))
            eng::log::info("{} {}", ww::L("Erreichbar im lokalen Netz unter", "Reachable on the local network at"), endpoint);
        else eng::log::info("{} {}", ww::L("Erreichbar (Internet oder VPN) unter", "Reachable (internet or VPN) at"), endpoint);
    }
    eng::log::info("{}", ww::L("Für Spieler aus dem Internet: TCP-Port im Router freigeben (IPv4) bzw. in der Router-Firewall erlauben (IPv6).",
                              "For players from the internet: forward the TCP port in your router (IPv4) or allow it in the router firewall (IPv6)."));
    eng::log::info("{}", ww::L("Beenden mit Strg+C", "Press Ctrl+C to stop"));

    server.run(g_running);
    return 0;
}
