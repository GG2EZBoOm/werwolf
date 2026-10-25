#pragma once

#include <filesystem>
#include <string_view>

namespace eng::paths {

// Benutzerspezifischer Datenordner (z. B. %APPDATA%\<app>), wird bei Bedarf angelegt.
std::filesystem::path userDataDir(std::string_view appName);
std::filesystem::path executableDir();

} // namespace eng::paths
