#include "client/Config.h"

#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace ww {

std::filesystem::path Config::path() { return eng::paths::userDataDir("Werwolf") / "config.txt"; }

void Config::load() {
    std::ifstream in(path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name") name = value;
        else if (key == "address") address = value;
        else if (key == "update_url") updateUrl = value;
        else if (key == "fullscreen") fullscreen = value == "1";
        else if (key == "vsync") vsync = value == "1";
        else if (key == "antialiasing") antialiasing = std::clamp(std::atoi(value.c_str()), 1, 8);
        else if (key == "shadows") shadowQuality = std::clamp(std::atoi(value.c_str()), 0, 2);
        else if (key == "bloom") bloom = value == "1";
        else if (key == "film_grain") filmGrain = value == "1";
        else if (key == "fov") fieldOfView = std::clamp(static_cast<float>(std::atof(value.c_str())), 50.0f, 95.0f);
        else if (key == "mouse_sensitivity") mouseSensitivity = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.2f, 3.0f);
        else if (key == "invert_y") invertY = value == "1";
        else if (key == "fade_chat") fadeChat = value == "1";
        else if (key == "subtitles") subtitles = value == "1";
        else if (key == "show_fps") showFps = value == "1";
        else if (key == "port") {
            const int p = std::atoi(value.c_str());
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        }
    }
}

void Config::save() const {
    std::ofstream out(path(), std::ios::trunc);
    if (!out) {
        eng::log::warn("Einstellungen konnten nicht gespeichert werden");
        return;
    }
    out << "name=" << name << "\n";
    out << "address=" << address << "\n";
    out << "port=" << port << "\n";
    if (!updateUrl.empty()) out << "update_url=" << updateUrl << "\n";
    out << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
    out << "vsync=" << (vsync ? 1 : 0) << "\n";
    out << "antialiasing=" << antialiasing << "\n";
    out << "shadows=" << shadowQuality << "\n";
    out << "bloom=" << (bloom ? 1 : 0) << "\n";
    out << "film_grain=" << (filmGrain ? 1 : 0) << "\n";
    out << "fov=" << fieldOfView << "\n";
    out << "mouse_sensitivity=" << mouseSensitivity << "\n";
    out << "invert_y=" << (invertY ? 1 : 0) << "\n";
    out << "fade_chat=" << (fadeChat ? 1 : 0) << "\n";
    out << "subtitles=" << (subtitles ? 1 : 0) << "\n";
    out << "show_fps=" << (showFps ? 1 : 0) << "\n";
}

} // namespace ww
