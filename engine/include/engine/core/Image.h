#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace eng {

// Schreibt ein RGBA-Bild (8 Bit pro Kanal, Zeilen von oben nach unten) als PNG.
// Kommt ohne zlib aus: die Daten werden unkomprimiert ("stored") abgelegt.
bool writePng(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& rgba);

} // namespace eng
