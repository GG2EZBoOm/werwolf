#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace eng {

// SHA-256 nach FIPS 180-4 (eigene Implementierung, ohne externe Bibliothek).
class Sha256 {
public:
    using Digest = std::array<uint8_t, 32>;

    Sha256();
    void update(const void* data, size_t size);
    Digest finish();

    static std::string toHex(const Digest& digest);
    static std::string hashHex(const void* data, size_t size);
    static std::string hashHex(std::string_view text) { return hashHex(text.data(), text.size()); }
    // Hash einer Datei; leerer String, wenn sie nicht gelesen werden kann
    static std::string hashFileHex(const std::filesystem::path& path);

private:
    void block(const uint8_t* data);

    uint32_t m_state[8];
    uint8_t m_buffer[64];
    size_t m_bufferSize = 0;
    uint64_t m_totalBytes = 0;
};

} // namespace eng
