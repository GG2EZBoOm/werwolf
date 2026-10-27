#pragma once

#include "logic/Types.h"

#include "engine/core/ByteBuffer.h"

#include <cstdint>

// Netzwerkprotokoll zwischen Client und Server.
// Jede Nachricht beginnt mit einem Typ-Byte, danach folgen die Felder.
namespace ww::proto {

constexpr uint16_t kVersion = 6;
constexpr uint16_t kDefaultPort = 27777;
constexpr size_t kMaxNameLength = 16;  // Codepoints
constexpr size_t kMaxChatLength = 240; // Codepoints
constexpr double kPingInterval = 2.0;
constexpr double kTimeout = 15.0;
constexpr double kMoveInterval = 1.0 / 20.0;     // Client -> Server
constexpr double kSnapshotInterval = 1.0 / 20.0; // Server -> Client

enum class ClientMsg : uint8_t {
    Hello = 1,      // u16 version, str name, u64 token (0 = neu)
    Chat,           // str text, bool wolves (Rudel-Kanal gewünscht)
    Move,           // f32 x, f32 z, f32 yaw, u8 flags
    Interact,       // u8 Interaction, u8 target
    StartGame,      // – (nur Spielleitung)
    UpdateSettings, // Settings (nur Spielleitung)
    AddBot,         // – (nur Spielleitung)
    Kick,           // u8 player (nur Spielleitung)
    ReturnToLobby,  // – (nur Spielleitung)
    Ping,           // –
    Leave,          // –
};

// Text = str Deutsch + str Englisch (leer, wenn gleich); jeder Client zeigt seine Sprache
enum class ServerMsg : uint8_t {
    Welcome = 1, // u8 yourId, u64 token
    Reject,      // Text reason
    State,       // GameView
    Chat,        // u8 channel, u8 from, str name (leer = verborgener Werwolf), Text text
    Notice,      // u8 kind, Text text
    Pong,        // –
    Snapshot,    // Entities, Türen
    Correction,  // f32 x, f32 z, f32 yaw – Server setzt die eigene Position
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
