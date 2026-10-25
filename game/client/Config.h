#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ww {

// Kleine Einstellungsdatei (Schlüssel=Wert) im Benutzerordner.
struct Config {
    std::string name;
    std::string address = "127.0.0.1";
    uint16_t port = 27777;

    static std::filesystem::path path();
    void load();
    void save() const;
};

} // namespace ww
