#include "engine/core/ByteBuffer.h"

#include <algorithm>
#include <cstring>

namespace eng {

void ByteWriter::u16(uint16_t v) {
    u8(static_cast<uint8_t>(v >> 8));
    u8(static_cast<uint8_t>(v & 0xFF));
}

void ByteWriter::u32(uint32_t v) {
    u16(static_cast<uint16_t>(v >> 16));
    u16(static_cast<uint16_t>(v & 0xFFFF));
}

void ByteWriter::u64(uint64_t v) {
    u32(static_cast<uint32_t>(v >> 32));
    u32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
}

void ByteWriter::f32(float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    u32(bits);
}

void ByteWriter::str(std::string_view s) {
    const size_t n = std::min<size_t>(s.size(), 0xFFFF);
    u16(static_cast<uint16_t>(n));
    m_data.insert(m_data.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

bool ByteReader::need(size_t n) {
    if (!m_ok || m_size - m_pos < n) {
        m_ok = false;
        return false;
    }
    return true;
}

uint8_t ByteReader::u8() {
    if (!need(1)) return 0;
    return m_data[m_pos++];
}

uint16_t ByteReader::u16() {
    if (!need(2)) return 0;
    uint16_t v = static_cast<uint16_t>((m_data[m_pos] << 8) | m_data[m_pos + 1]);
    m_pos += 2;
    return v;
}

uint32_t ByteReader::u32() {
    uint32_t hi = u16();
    uint32_t lo = u16();
    return (hi << 16) | lo;
}

uint64_t ByteReader::u64() {
    uint64_t hi = u32();
    uint64_t lo = u32();
    return (hi << 32) | lo;
}

float ByteReader::f32() {
    uint32_t bits = u32();
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

std::string ByteReader::str(size_t maxBytes) {
    const size_t n = u16();
    if (!need(n)) return {};
    std::string s(reinterpret_cast<const char*>(m_data + m_pos), std::min(n, maxBytes));
    m_pos += n;
    return s;
}

} // namespace eng
