#pragma once

#include "logic/Types.h"

#include "engine/core/Math.h"
#include "engine/core/Utf8.h"

#include <cwctype>
#include <string>

namespace ww::theme {

using eng::Color;

inline constexpr Color text = Color::hex(0xECE7DF);
inline constexpr Color muted = Color::hex(0xA39C8C);
inline constexpr Color faint = Color::hex(0x6F6A5E);
inline constexpr Color gold = Color::hex(0xC9A45C);
inline constexpr Color goldLight = Color::hex(0xF0D9A0);
inline constexpr Color blood = Color::hex(0xD64B3F);
inline constexpr Color panel = Color::hex(0x0B0C11, 0.86f);
inline constexpr Color panelLight = Color::hex(0x171920, 0.9f);
inline constexpr Color panelBorder = Color::hex(0xC9A45C, 0.16f);
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
    case Role::Elder: return Color::hex(0xB8A27A);
    case Role::LittleGirl: return Color::hex(0xF0A8C8);
    case Role::Medium: return Color::hex(0x8FB3E8);
    case Role::Tracker: return Color::hex(0x8DC26F);
    case Role::Blacksmith: return Color::hex(0x9AA6B4);
    case Role::Priest: return Color::hex(0xEDE3C4);
    case Role::Mayor: return Color::hex(0xE8C547);
    case Role::Idiot: return Color::hex(0xC7B98C);
    case Role::Cursed: return Color::hex(0x9E7AA8);
    case Role::AlphaWolf: return Color::hex(0xB8322A);
    case Role::WolfCub: return Color::hex(0xE07A6E);
    case Role::Jester: return Color::hex(0xC77DDB);
    case Role::SerialKiller: return Color::hex(0x8A9099);
    case Role::Arsonist: return Color::hex(0xF07A2A);
    case Role::Thief: return Color::hex(0x8F8A6E);
    case Role::Scapegoat: return Color::hex(0xB5A27E);
    case Role::Fox: return Color::hex(0xE0894A);
    case Role::BearTamer: return Color::hex(0xA87C5A);
    case Role::Knight: return Color::hex(0xB0B8C2);
    case Role::Servant: return Color::hex(0xC9A0B8);
    case Role::Judge: return Color::hex(0x8FA7C9);
    case Role::Actor: return Color::hex(0xD9B86A);
    case Role::WildChild: return Color::hex(0x9CCB7A);
    case Role::WolfHound: return Color::hex(0xB98C6E);
    case Role::Sister: return Color::hex(0xE4A6C9);
    case Role::Brother: return Color::hex(0x7FA6D9);
    case Role::HonestVillager: return Color::hex(0xBFD9A6);
    case Role::Raven: return Color::hex(0x7A7F9A);
    case Role::BigBadWolf: return Color::hex(0xA8281F);
    case Role::WhiteWolf: return Color::hex(0xE8EEF5);
    case Role::Piper: return Color::hex(0x6FD0C4);
    case Role::Angel: return Color::hex(0xF5E7A1);
    case Role::Sectarian: return Color::hex(0xA98BD6);
    default: return Color::hex(0x6B7385);
    }
}

inline Color teamColor(Team team) {
    switch (team) {
    case Team::Village: return Color::hex(0x7FC4A3);
    case Team::Wolves: return Color::hex(0xD64B3F);
    case Team::Lovers: return Color::hex(0xE87FA2);
    case Team::Jester: return Color::hex(0xC77DDB);
    case Team::Killer: return Color::hex(0x8A9099);
    case Team::Arsonist: return Color::hex(0xF07A2A);
    case Team::Piper: return Color::hex(0x6FD0C4);
    case Team::Angel: return Color::hex(0xF5E7A1);
    case Team::WhiteWolf: return Color::hex(0xE8EEF5);
    case Team::Sectarian: return Color::hex(0xA98BD6);
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
