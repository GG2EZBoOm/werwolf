#pragma once

#include "engine/core/ByteBuffer.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// Zweisprachigkeit (Deutsch und Englisch).
//
// Die Oberfläche wählt mit L("Spiel hosten", "Host game") bzw. Lf(...) für formatierte Texte die
// eingestellte Sprache. Der Server erzeugt Meldungen dagegen immer in beiden Sprachen (Text), weil
// jeder Mitspieler eine andere Sprache eingestellt haben kann; der Client zeigt dann seine.
namespace ww {

enum class Lang : uint8_t { German = 0, English = 1 };

// Sprache der Oberfläche in diesem Prozess
Lang language();
void setLanguage(Lang lang);
// Deutsch, wenn Windows deutsch eingestellt ist, sonst Englisch
Lang systemLanguage();
const char* languageCode(Lang lang); // "de" / "en"
bool parseLanguage(std::string_view code, Lang& out);

inline const char* L(const char* de, const char* en) { return language() == Lang::English ? en : de; }

template <class... Args>
std::string Lf(std::string_view de, std::string_view en, const Args&... args) {
    return std::vformat(language() == Lang::English ? en : de, std::make_format_args(args...));
}

// Ein Text in beiden Sprachen
struct Text {
    std::string de;
    std::string en;

    Text() = default;
    Text(std::string german, std::string english) : de(std::move(german)), en(std::move(english)) {}
    // Gleich in beiden Sprachen (z. B. Namen oder Chat)
    static Text same(std::string s) { return {s, s}; }

    const std::string& in(Lang lang) const { return lang == Lang::English ? en : de; }
    const std::string& str() const { return in(language()); }
    bool empty() const { return de.empty() && en.empty(); }
    bool operator==(const Text&) const = default;

    // Englisch wird nur übertragen, wenn es sich vom Deutschen unterscheidet
    void write(eng::ByteWriter& w) const;
    bool read(eng::ByteReader& r, size_t maxBytes = 4096);
};

namespace detail {
template <class T>
const T& pick(const T& value, Lang) { return value; }
inline const std::string& pick(const Text& text, Lang lang) { return text.in(lang); }
} // namespace detail

// Formatiert beide Sprachen; Text-Argumente erscheinen jeweils in der passenden Sprache
template <class... Args>
Text tx(std::string_view de, std::string_view en, const Args&... args) {
    Text t;
    t.de = std::vformat(de, std::make_format_args(detail::pick(args, Lang::German)...));
    t.en = std::vformat(en, std::make_format_args(detail::pick(args, Lang::English)...));
    return t;
}

// Aufzählung: "A, B und C" bzw. "A, B and C"
std::string joinList(const std::vector<std::string>& items, Lang lang);
Text joinNames(const std::vector<std::string>& names);
Text joinTexts(const std::vector<Text>& items);

} // namespace ww
