#include "engine/core/Log.h"

#include "engine/core/Time.h"

#include <cstdio>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eng::log {
namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
Level g_minLevel = Level::Debug;
thread_local std::string t_tag;

const char* levelName(Level level) {
    switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    }
    return "?";
}

} // namespace

void setMinLevel(Level level) {
    std::lock_guard lock(g_mutex);
    g_minLevel = level;
}

void setOutputFile(const std::filesystem::path& path) {
    std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
#ifdef _WIN32
    g_file = _wfopen(path.c_str(), L"w");
#else
    g_file = std::fopen(path.c_str(), "w");
#endif
}

void setThreadTag(std::string_view tag) { t_tag = tag; }

void write(Level level, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (level < g_minLevel) return;

    std::string line = std::format("[{:9.3f}] {} ", processSeconds(), levelName(level));
    if (!t_tag.empty()) line += "<" + t_tag + "> ";
    line += message;
    line += '\n';

    std::FILE* console = level >= Level::Warn ? stderr : stdout;
    std::fputs(line.c_str(), console);
    std::fflush(console);
    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fflush(g_file);
    }
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif
}

} // namespace eng::log
