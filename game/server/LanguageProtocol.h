#pragma once
#include "engine/core/Json.h"
#include <format>
#include <string>

namespace ww::languageWire {
inline std::string quote(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) out += std::format("\\u{:04x}", c);
        else out += static_cast<char>(c);
    }
    return out + '"';
}
inline bool content(const std::string& response, eng::json::Value& result) {
    eng::json::Value envelope;
    if (!eng::json::parse(response, envelope)) return false;
    const auto* choices = envelope.get("choices");
    if (!choices || !choices->isArray() || choices->array.size() != 1) return false;
    const auto& choice = choices->array.front();
    const auto* message = choice.get("message");
    return choice.getString("finish_reason") == "stop" && message &&
        eng::json::parse(message->getString("content"), result) && result.isObject();
}
inline std::string payload(const std::string& system, const std::string& context,
                           const std::string& schema, float temperature, int tokens) {
    return "{\"messages\":[{\"role\":\"system\",\"content\":" + quote(system) +
        "},{\"role\":\"user\",\"content\":" + quote(context) +
        std::format("}}],\"temperature\":{:.1f},\"max_tokens\":{},\"stream\":false,", temperature, tokens) +
        "\"response_format\":{\"type\":\"json_object\",\"schema\":" + schema + "}}";
}
}
