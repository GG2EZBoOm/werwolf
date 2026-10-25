#include "engine/core/Image.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace eng {

namespace {

const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
    const auto& table = crcTable();
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
}

} // namespace

bool writePng(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4) return false;

    // Rohdaten: pro Zeile ein Filterbyte (0 = keiner) + RGB
    const size_t rowBytes = static_cast<size_t>(width) * 3 + 1;
    std::vector<uint8_t> raw;
    raw.reserve(rowBytes * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            raw.push_back(row[x * 4 + 0]);
            raw.push_back(row[x * 4 + 1]);
            raw.push_back(row[x * 4 + 2]);
        }
    }

    // zlib-Strom aus unkomprimierten Blöcken
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size() || raw.empty();) {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len & 0xFF));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len & 0xFF));
        z.push_back(static_cast<uint8_t>((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
        if (last) break;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8 Bit, RGB
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

} // namespace eng
