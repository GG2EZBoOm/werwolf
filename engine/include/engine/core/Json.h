#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng::json {

// Kleiner JSON-Leser (RFC 8259) für Konfigurations- und Update-Dateien.
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool isNull() const { return type == Type::Null; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // Feld eines Objekts oder nullptr
    const Value* get(std::string_view key) const;
    std::string getString(std::string_view key, std::string fallback = {}) const;
    double getNumber(std::string_view key, double fallback = 0.0) const;
};

bool parse(std::string_view text, Value& out, std::string* error = nullptr);

} // namespace eng::json
