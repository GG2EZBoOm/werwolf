#pragma once

#include "logic/Types.h"

#include "engine/core/ByteBuffer.h"

#include <cstdint>

// Netzwerkprotokoll zwischen Client und Server.
// Jede Nachricht beginnt mit einem Typ-Byte, danach folgen die Felder.
namespace ww::proto {

constexpr uint16_t kVersion = 1;
constexpr uint16_t kDefaultPort = 27777;
constexpr size_t kMaxNameLength = 16;  // Codepoints
constexpr size_t kMaxChatLength = 240; // Codepoints
constexpr double kPingInterval = 2.0;
constexpr double kTimeout = 15.0;

enum class ClientMsg : uint8_t {
    Hello = 1,      // u16 version, str name, u64 token (0 = neu)
    Chat,           // str text
    Select,         // u8 target (255 = Auswahl aufheben)
    Confirm,        // –
    WitchHeal,      // bool use
    Skip,           // bool skip
    StartGame,      // – (nur Spielleitung)
    UpdateSettings, // Settings (nur Spielleitung)
    AddBot,         // – (nur Spielleitung)
    Kick,           // u8 player (nur Spielleitung)
    ReturnToLobby,  // – (nur Spielleitung)
    Ping,           // –
    Leave,          // –
};

enum class ServerMsg : uint8_t {
    Welcome = 1, // u8 yourId, u64 token
    Reject,      // str reason
    State,       // GameView
    Chat,        // u8 channel, u8 from, str name, str text
    Notice,      // u8 kind, str text
    Pong,        // –
};

inline eng::ByteWriter begin(ClientMsg type) {
    eng::ByteWriter w;
    w.u8(static_cast<uint8_t>(type));
    return w;
}

inline eng::ByteWriter begin(ServerMsg type) {
    eng::ByteWriter w;
    w.u8(static_cast<uint8_t>(type));
    return w;
}

} // namespace ww::proto
