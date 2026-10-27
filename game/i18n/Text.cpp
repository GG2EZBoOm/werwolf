#include "i18n/Text.h"

#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ww {

namespace {
std::atomic<uint8_t> g_language{static_cast<uint8_t>(Lang::German)};
} // namespace

Lang language() { return static_cast<Lang>(g_language.load(std::memory_order_relaxed)); }

void setLanguage(Lang lang) { g_language.store(static_cast<uint8_t>(lang), std::memory_order_relaxed); }

Lang systemLanguage() {
#ifdef _WIN32
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN ? Lang::German : Lang::English;
#else
    return Lang::English;
#endif
}

const char* languageCode(Lang lang) { return lang == Lang::English ? "en" : "de"; }

bool parseLanguage(std::string_view code, Lang& out) {
    if (code == "de") out = Lang::German;
    else if (code == "en") out = Lang::English;
    else return false;
    return true;
}

void Text::write(eng::ByteWriter& w) const {
    w.str(de);
    w.str(en == de ? std::string_view() : std::string_view(en));
}

bool Text::read(eng::ByteReader& r, size_t maxBytes) {
    de = r.str(maxBytes);
    en = r.str(maxBytes);
    if (en.empty()) en = de;
    return r.ok();
}

std::string joinList(const std::vector<std::string>& items, Lang lang) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += i + 1 == items.size() ? (lang == Lang::English ? " and " : " und ") : ", ";
        out += items[i];
    }
    return out;
}

Text joinNames(const std::vector<std::string>& names) { return {joinList(names, Lang::German), joinList(names, Lang::English)}; }

Text joinTexts(const std::vector<Text>& items) {
    std::vector<std::string> de, en;
    for (const Text& t : items) {
        de.push_back(t.de);
        en.push_back(t.en);
    }
    return {joinList(de, Lang::German), joinList(en, Lang::English)};
}

} // namespace ww
