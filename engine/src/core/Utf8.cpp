#include "engine/core/Utf8.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace eng::utf8 {

char32_t next(std::string_view s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }

    size_t len = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        cp = c & 0x07;
    } else {
        ++i;
        return kReplacement;
    }

    if (i + len > s.size()) {
        ++i;
        return kReplacement;
    }
    for (size_t k = 1; k < len; ++k) {
        const auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return kReplacement;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }

    const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
    if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        i += len;
        return kReplacement;
    }
    i += len;
    return cp;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

size_t prevBoundary(std::string_view s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

size_t nextBoundary(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

size_t length(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        next(s, i);
        ++n;
    }
    return n;
}

size_t byteOffset(std::string_view s, size_t count) {
    size_t i = 0;
    while (count > 0 && i < s.size()) {
        next(s, i);
        --count;
    }
    return i;
}

std::string sanitize(std::string_view s, size_t maxCodepoints) {
    std::string out;
    out.reserve(s.size());
    size_t count = 0;
    for (size_t i = 0; i < s.size() && count < maxCodepoints;) {
        char32_t cp = next(s, i);
        if (cp == kReplacement) continue;
        if (cp == '\t' || cp == '\n' || cp == '\r') cp = ' ';
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) continue;
        if (cp == ' ' && (out.empty() || out.back() == ' ')) continue; // führende/doppelte Leerzeichen
        append(out, cp);
        ++count;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

#ifdef _WIN32
std::wstring toWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string fromWide(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
#endif

} // namespace eng::utf8
