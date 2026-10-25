#pragma once

#include "logic/Types.h"

#include "engine/core/Math.h"
#include "engine/core/Utf8.h"

#include <cwctype>
#include <string>

namespace ww::theme {

using eng::Color;

inline constexpr Color text = Color::hex(0xECE7DF);
inline constexpr Color muted = Color::hex(0x9AA3B5);
inline constexpr Color faint = Color::hex(0x6B7385);
inline constexpr Color gold = Color::hex(0xD9A441);
inline constexpr Color goldLight = Color::hex(0xF2CC6B);
inline constexpr Color blood = Color::hex(0xD64B3F);
inline constexpr Color panel = Color::hex(0x10152A, 0.86f);
inline constexpr Color panelLight = Color::hex(0x1A2140, 0.9f);
inline constexpr Color panelBorder = Color::hex(0xFFFFFF, 0.07f);
inline constexpr Color ghost = Color::hex(0x8FA3C7);

inline Color roleColor(Role role) {
    switch (role) {
    case Role::Villager: return Color::hex(0xA7AEBD);
    case Role::Werewolf: return Color::hex(0xD64B3F);
    case Role::Seer: return Color::hex(0x9B87D6);
    case Role::Witch: return Color::hex(0x5DBE8F);
    case Role::Hunter: return Color::hex(0xD09452);
    case Role::Cupid: return Color::hex(0xE87FA2);
    case Role::Protector: return Color::hex(0x5FA8D3);
    default: return Color::hex(0x6B7385);
    }
}

inline Color teamColor(Team team) {
    switch (team) {
    case Team::Village: return Color::hex(0x7FC4A3);
    case Team::Wolves: return Color::hex(0xD64B3F);
    case Team::Lovers: return Color::hex(0xE87FA2);
    default: return Color::hex(0xA7AEBD);
    }
}

inline Color noticeColor(NoticeKind kind) {
    switch (kind) {
    case NoticeKind::Narration: return Color::hex(0xE3C58A);
    case NoticeKind::Death: return Color::hex(0xE58A7F);
    case NoticeKind::Private: return Color::hex(0xBBA9EC);
    case NoticeKind::Warning: return Color::hex(0xF0A35E);
    case NoticeKind::Victory: return Color::hex(0xF2CC6B);
    default: return Color::hex(0x9AA3B5);
    }
}

inline Color playerColor(uint8_t id) {
    static constexpr uint32_t palette[] = {0xE07A5F, 0x81B29A, 0xF2CC8F, 0x6D9DC5, 0xC08497, 0x9C89B8,
                                           0xF4A261, 0x2A9D8F, 0xE9C46A, 0x8AB17D, 0xB56576, 0x5FA8D3,
                                           0xD4A5A5, 0x7FB7BE, 0xC9ADA7, 0xA3C4BC};
    return Color::hex(palette[id % (sizeof palette / sizeof palette[0])]);
}

// Erster Buchstabe eines Namens in Großschreibung (für Avatare)
inline std::string initial(const std::string& name) {
    if (name.empty()) return "?";
    size_t i = 0;
    char32_t cp = eng::utf8::next(name, i);
    if (cp < 0x10000) cp = static_cast<char32_t>(std::towupper(static_cast<wint_t>(cp)));
    std::string out;
    eng::utf8::append(out, cp);
    return out;
}

} // namespace ww::theme
