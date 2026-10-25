#include "engine/core/Paths.h"

#include "engine/core/Utf8.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace eng::paths {

std::filesystem::path userDataDir(std::string_view appName) {
    std::filesystem::path base;
#ifdef _WIN32
    if (const wchar_t* appData = _wgetenv(L"APPDATA")) base = appData;
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = xdg;
    else if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / ".local" / "share";
#endif
    if (base.empty()) base = std::filesystem::current_path();

#ifdef _WIN32
    std::filesystem::path dir = base / utf8::toWide(appName);
#else
    std::filesystem::path dir = base / std::string(appName);
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
#else
    char buffer[4096];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(n))).parent_path();
#endif
}

} // namespace eng::paths
