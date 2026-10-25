#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace eng::utf8 {

constexpr char32_t kReplacement = 0xFFFD;

// Dekodiert den Codepoint ab Byte-Position `i` und rückt `i` dahinter vor.
char32_t next(std::string_view s, size_t& i);
void append(std::string& out, char32_t cp);
size_t prevBoundary(std::string_view s, size_t i);
size_t nextBoundary(std::string_view s, size_t i);
size_t length(std::string_view s);
// Byte-Länge der ersten `count` Codepoints.
size_t byteOffset(std::string_view s, size_t count);

// Entfernt Steuerzeichen und ungültige Sequenzen, trimmt Leerraum und
// kürzt auf höchstens `maxCodepoints` Zeichen.
std::string sanitize(std::string_view s, size_t maxCodepoints);

#ifdef _WIN32
std::wstring toWide(std::string_view s);
std::string fromWide(std::wstring_view s);
#endif

} // namespace eng::utf8
