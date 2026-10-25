#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace eng::log {

enum class Level { Debug, Info, Warn, Error };

void setMinLevel(Level level);
void setOutputFile(const std::filesystem::path& path);
// Kennzeichnet Meldungen des aktuellen Threads (z. B. "server").
void setThreadTag(std::string_view tag);
void write(Level level, std::string_view message);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace eng::log
