#include "client/Config.h"

#include "engine/core/Log.h"
#include "engine/core/Paths.h"

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
}

} // namespace ww
