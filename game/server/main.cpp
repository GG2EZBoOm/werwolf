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
    std::printf("Werwolf-Server\n"
                "  --port <n>        TCP-Port (Standard %u)\n"
                "  --timescale <f>   Zeitraffer für Tests (Standard 1.0)\n"
                "  --quiet           Chat nicht protokollieren\n",
                static_cast<unsigned>(ww::proto::kDefaultPort));
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    ww::ServerOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--timescale" && i + 1 < argc) {
            options.timeScale = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--quiet") {
            options.logChat = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::printf("Unbekanntes Argument: %s\n", arg.c_str());
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
    for (const std::string& ip : eng::net::localIPv4Addresses())
        eng::log::info("Erreichbar im lokalen Netz unter {}:{}", ip, options.port);
    eng::log::info("Beenden mit Strg+C");

    server.run(g_running);
    return 0;
}
