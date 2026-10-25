#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

// Binäre Serialisierung in Netzwerk-Byte-Reihenfolge (Big Endian).
class ByteWriter {
public:
    void u8(uint8_t v) { m_data.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v);
    void boolean(bool v) { u8(v ? 1 : 0); }
    // Länge (u16) + UTF-8-Bytes
    void str(std::string_view s);

    const std::vector<uint8_t>& data() const { return m_data; }
    size_t size() const { return m_data.size(); }

private:
    std::vector<uint8_t> m_data;
};

// Liest defensiv: Bei zu wenig Daten wird ok() false und es kommen Nullwerte.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : m_data(v.data()), m_size(v.size()) {}

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32();
    bool boolean() { return u8() != 0; }
    std::string str(size_t maxBytes = 4096);

    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos >= m_size; }
    size_t remaining() const { return m_size - m_pos; }

private:
    bool need(size_t n);

    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
    bool m_ok = true;
};

} // namespace eng
