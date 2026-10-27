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
    std::string updateUrl; // leer = Standardquelle

    // Grafik
    bool fullscreen = false;
    bool vsync = true;
    int antialiasing = 4;    // MSAA-Samples (1 = aus)
    int shadowQuality = 1;   // 0 niedrig, 1 mittel, 2 hoch
    bool bloom = true;
    bool filmGrain = true;
    float fieldOfView = 62.0f; // Grad
    // Steuerung
    float mouseSensitivity = 1.0f; // Faktor
    bool invertY = false;
    // Oberfläche
    bool fadeChat = true;   // Chat blendet sich aus, wenn nichts passiert
    bool subtitles = true;  // Erzähler als Untertitel
    bool showFps = false;
    // Ton (Prozent)
    int masterVolume = 75;
    int effectsVolume = 85;
    int ambienceVolume = 60;
    int musicVolume = 65;
    bool muted = false;
    bool muteUnfocused = true;

    static std::filesystem::path path();
    void load();
    void save() const;
};

} // namespace ww
