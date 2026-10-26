#include "engine/core/Json.h"

#include "engine/core/Utf8.h"

#include <cmath>
#include <cstdlib>

namespace eng::json {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    bool parseDocument(Value& out) {
        skipWhitespace();
        if (!parseValue(out, 0)) return false;
        skipWhitespace();
        if (m_pos != m_text.size()) return fail("Zusätzliche Zeichen nach dem Ende");
        return true;
    }

    const std::string& error() const { return m_error; }

private:
    bool fail(const char* message) {
        if (m_error.empty()) m_error = std::string(message) + " (Position " + std::to_string(m_pos) + ")";
        return false;
    }

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_pos;
            else break;
        }
    }

    bool literal(std::string_view word) {
        if (m_text.substr(m_pos, word.size()) != word) return fail("Unbekanntes Wort");
        m_pos += word.size();
        return true;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return fail("Zu tief verschachtelt");
        if (m_pos >= m_text.size()) return fail("Unerwartetes Ende");
        const char c = m_text[m_pos];
        switch (c) {
        case '{': return parseObject(out, depth);
        case '[': return parseArray(out, depth);
        case '"':
            out.type = Value::Type::String;
            return parseString(out.string);
        case 't':
            out.type = Value::Type::Bool;
            out.boolean = true;
            return literal("true");
        case 'f':
            out.type = Value::Type::Bool;
            out.boolean = false;
            return literal("false");
        case 'n':
            out.type = Value::Type::Null;
            return literal("null");
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
            return fail("Unerwartetes Zeichen");
        }
    }

    bool parseObject(Value& out, int depth) {
        out.type = Value::Type::Object;
        ++m_pos; // {
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("Schlüssel erwartet");
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail("':' erwartet");
            ++m_pos;
            skipWhitespace();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(v));
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("Unerwartetes Ende im Objekt");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return fail("',' oder '}' erwartet");
        }
    }

    bool parseArray(Value& out, int depth) {
        out.type = Value::Type::Array;
        ++m_pos; // [
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        for (;;) {
            skipWhitespace();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.array.push_back(std::move(v));
            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("Unerwartetes Ende in der Liste");
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return fail("',' oder ']' erwartet");
        }
    }

    bool hex4(uint32_t& out) {
        if (m_pos + 4 > m_text.size()) return fail("Unvollständige Unicode-Sequenz");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("Ungültige Hex-Ziffer");
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++m_pos; // "
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("Steuerzeichen in Zeichenkette");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) break;
            const char e = m_text[m_pos++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low = 0;
                    if (m_text.substr(m_pos, 2) != "\\u") return fail("Ersatzzeichen-Paar unvollständig");
                    m_pos += 2;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) return fail("Ungültiges Ersatzzeichen-Paar");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("Einzelnes Ersatzzeichen");
                }
                utf8::append(out, static_cast<char32_t>(cp));
                break;
            }
            default: return fail("Unbekannte Escape-Sequenz");
            }
        }
        return fail("Zeichenkette nicht abgeschlossen");
    }

    bool parseNumber(Value& out) {
        const size_t start = m_pos;
        if (m_text[m_pos] == '-') ++m_pos;
        auto digits = [&] {
            const size_t s = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
            return m_pos > s;
        };
        if (!digits()) return fail("Zahl erwartet");
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            if (!digits()) return fail("Nachkommastellen erwartet");
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
            if (!digits()) return fail("Exponent erwartet");
        }
        const std::string number(m_text.substr(start, m_pos - start));
        out.type = Value::Type::Number;
        out.number = std::strtod(number.c_str(), nullptr);
        if (!std::isfinite(out.number)) return fail("Zahl außerhalb des Wertebereichs");
        return true;
    }

    std::string_view m_text;
    size_t m_pos = 0;
    std::string m_error;
};

} // namespace

const Value* Value::get(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : object)
        if (k == key) return &v;
    return nullptr;
}

std::string Value::getString(std::string_view key, std::string fallback) const {
    const Value* v = get(key);
    return v && v->isString() ? v->string : fallback;
}

double Value::getNumber(std::string_view key, double fallback) const {
    const Value* v = get(key);
    return v && v->isNumber() ? v->number : fallback;
}

bool parse(std::string_view text, Value& out, std::string* error) {
    out = Value{};
    Parser parser(text);
    if (parser.parseDocument(out)) return true;
    if (error) *error = parser.error();
    out = Value{};
    return false;
}

} // namespace eng::json
