#include "engine/gfx/Font.h"

#include "engine/core/Log.h"
#include "engine/core/Utf8.h"
#include "engine/gfx/GL.h"

#include <cmath>
#include <unordered_map>

namespace eng {

namespace {
constexpr int kAtlasSize = 1024;
constexpr int kPadding = 1;
} // namespace

struct Font::Impl {
    FontDesc desc;
    float scale = 1.0f;
    HDC dc = nullptr;
    HFONT font = nullptr;
    HFONT fallback = nullptr;
    HGDIOBJ previousObject = nullptr;
    GLuint texture = 0;
    float ascent = 0, descent = 0, lineHeight = 0, capHeight = 0;
    std::unordered_map<char32_t, Glyph> glyphs;
    int penX = kPadding, penY = kPadding, rowHeight = 0;
    bool atlasFullReported = false;

    static HFONT createFont(const std::string& family, float pixelSize, int weight, bool italic) {
        const std::wstring wfamily = utf8::toWide(family);
        return CreateFontW(-static_cast<int>(std::lround(pixelSize)), 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE,
                           FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, wfamily.c_str());
    }

    static bool hasGlyph(HDC dc, char32_t cp) {
        if (cp > 0xFFFF) return false;
        const wchar_t wc = static_cast<wchar_t>(cp);
        WORD index = 0;
        if (GetGlyphIndicesW(dc, &wc, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) return false;
        return index != 0xFFFF;
    }

    bool place(int w, int h, int& outX, int& outY) {
        if (penX + w + kPadding > kAtlasSize) {
            penX = kPadding;
            penY += rowHeight + kPadding;
            rowHeight = 0;
        }
        if (penY + h + kPadding > kAtlasSize) return false;
        outX = penX;
        outY = penY;
        penX += w + kPadding;
        rowHeight = std::max(rowHeight, h);
        return true;
    }

    Glyph rasterize(char32_t cp) {
        Glyph g;
        bool useFallback = false;
        if (!hasGlyph(dc, cp)) {
            SelectObject(dc, fallback);
            useFallback = hasGlyph(dc, cp);
            SelectObject(dc, font);
            if (!useFallback) cp = (cp == '?') ? ' ' : '?';
        }
        if (useFallback) SelectObject(dc, fallback);

        MAT2 identity{};
        identity.eM11.value = 1;
        identity.eM22.value = 1;
        GLYPHMETRICS gm{};
        const UINT ch = static_cast<UINT>(cp);
        // Ohne Hinting: gleichmäßigere Strichstärken (sonst wirken z. B. Ziffern fett)
        const UINT format = GGO_GRAY8_BITMAP | GGO_UNHINTED;
        DWORD size = GetGlyphOutlineW(dc, ch, format, &gm, 0, nullptr, &identity);
        std::vector<uint8_t> bitmap;
        if (size != GDI_ERROR && size > 0) {
            bitmap.resize(size);
            GetGlyphOutlineW(dc, ch, format, &gm, size, bitmap.data(), &identity);
        } else {
            GetGlyphOutlineW(dc, ch, GGO_METRICS, &gm, 0, nullptr, &identity);
            size = 0;
        }
        if (useFallback) SelectObject(dc, font);

        g.advance = static_cast<float>(gm.gmCellIncX);
        if (size == 0) return g; // Leerzeichen o. Ä.

        const int w = static_cast<int>(gm.gmBlackBoxX);
        const int pitch = (w + 3) & ~3;
        const int h = std::min(static_cast<int>(gm.gmBlackBoxY), static_cast<int>(size) / std::max(1, pitch));
        int x = 0, y = 0;
        if (!place(w, h, x, y)) {
            if (!atlasFullReported) log::warn("Schriftatlas voll ({} {})", desc.family, desc.size);
            atlasFullReported = true;
            return g;
        }

        // GDI liefert 65 Graustufen (0..64)
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                const int v = bitmap[static_cast<size_t>(row * pitch + col)];
                pixels[static_cast<size_t>(row * w + col)] = static_cast<uint8_t>(std::min(255, v * 255 / 64));
            }
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels.data());

        g.width = w;
        g.height = h;
        g.bearingX = gm.gmptGlyphOrigin.x;
        g.bearingY = gm.gmptGlyphOrigin.y;
        g.u0 = static_cast<float>(x) / kAtlasSize;
        g.v0 = static_cast<float>(y) / kAtlasSize;
        g.u1 = static_cast<float>(x + w) / kAtlasSize;
        g.v1 = static_cast<float>(y + h) / kAtlasSize;
        return g;
    }
};

Font::Font() : m(std::make_unique<Impl>()) {}

Font::~Font() { unload(); }

bool Font::load(const FontDesc& desc, float dpiScale) {
    unload();
    m->desc = desc;
    m->scale = dpiScale;
    const float pixelSize = desc.size * dpiScale;

    m->dc = CreateCompatibleDC(nullptr);
    m->font = Impl::createFont(desc.family, pixelSize, desc.weight, desc.italic);
    m->fallback = Impl::createFont(desc.fallbackFamily, pixelSize, desc.weight, desc.italic);
    if (!m->dc || !m->font || !m->fallback) {
        log::error("Schrift '{}' konnte nicht geladen werden", desc.family);
        unload();
        return false;
    }
    m->previousObject = SelectObject(m->dc, m->font);

    TEXTMETRICW tm{};
    GetTextMetricsW(m->dc, &tm);
    m->ascent = static_cast<float>(tm.tmAscent);
    m->descent = static_cast<float>(tm.tmDescent);
    m->lineHeight = static_cast<float>(tm.tmHeight + tm.tmExternalLeading);

    glGenTextures(1, &m->texture);
    glBindTexture(GL_TEXTURE_2D, m->texture);
    std::vector<uint8_t> empty(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, empty.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Einkanalige Textur als (1, 1, 1, Deckkraft) lesen
    const GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    const Glyph* h = glyph('H');
    m->capHeight = h ? static_cast<float>(h->bearingY) : m->ascent * 0.7f;
    return true;
}

void Font::unload() {
    if (m->texture) {
        glDeleteTextures(1, &m->texture);
        m->texture = 0;
    }
    if (m->dc && m->previousObject) SelectObject(m->dc, m->previousObject);
    if (m->font) DeleteObject(m->font);
    if (m->fallback) DeleteObject(m->fallback);
    if (m->dc) DeleteDC(m->dc);
    m->dc = nullptr;
    m->font = m->fallback = nullptr;
    m->previousObject = nullptr;
    m->glyphs.clear();
    m->penX = m->penY = kPadding;
    m->rowHeight = 0;
    m->atlasFullReported = false;
}

bool Font::isLoaded() const { return m->texture != 0; }

const Glyph* Font::glyph(char32_t cp) {
    if (!m->dc) return nullptr;
    auto it = m->glyphs.find(cp);
    if (it != m->glyphs.end()) return &it->second;
    Glyph g = m->rasterize(cp);
    return &m->glyphs.emplace(cp, g).first->second;
}

unsigned Font::texture() const { return m->texture; }
float Font::dpiScale() const { return m->scale; }
const FontDesc& Font::desc() const { return m->desc; }
float Font::lineHeight() const { return m->lineHeight / m->scale; }
float Font::ascent() const { return m->ascent / m->scale; }
float Font::descent() const { return m->descent / m->scale; }
float Font::capHeight() const { return m->capHeight / m->scale; }

float Font::measure(std::string_view text) {
    float width = 0.0f;
    float best = 0.0f;
    for (size_t i = 0; i < text.size();) {
        const char32_t cp = utf8::next(text, i);
        if (cp == '\n') {
            best = std::max(best, width);
            width = 0.0f;
            continue;
        }
        if (const Glyph* g = glyph(cp)) width += g->advance;
    }
    return std::max(best, width) / m->scale;
}

std::vector<std::string> Font::wrap(std::string_view text, float maxWidth) {
    std::vector<std::string> lines;
    const float limit = maxWidth * m->scale;

    size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        size_t paragraphEnd = text.find('\n', paragraphStart);
        if (paragraphEnd == std::string_view::npos) paragraphEnd = text.size();
        const std::string_view para = text.substr(paragraphStart, paragraphEnd - paragraphStart);

        std::string line;
        float lineWidth = 0.0f;
        size_t i = 0;
        while (i < para.size()) {
            // nächstes Wort inkl. folgender Leerzeichen
            size_t wordEnd = i;
            while (wordEnd < para.size() && para[wordEnd] != ' ') ++wordEnd;
            size_t spaceEnd = wordEnd;
            while (spaceEnd < para.size() && para[spaceEnd] == ' ') ++spaceEnd;
            const std::string_view word = para.substr(i, wordEnd - i);
            const std::string_view spaces = para.substr(wordEnd, spaceEnd - wordEnd);

            float wordWidth = 0.0f;
            for (size_t k = 0; k < word.size();) {
                const Glyph* g = glyph(utf8::next(word, k));
                if (g) wordWidth += g->advance;
            }
            float spaceWidth = 0.0f;
            if (const Glyph* g = glyph(' ')) spaceWidth = g->advance * static_cast<float>(spaces.size());

            if (lineWidth + wordWidth > limit && !line.empty()) {
                while (!line.empty() && line.back() == ' ') line.pop_back();
                lines.push_back(std::move(line));
                line.clear();
                lineWidth = 0.0f;
            }
            if (wordWidth > limit) {
                // Wort zeichenweise aufteilen
                for (size_t k = 0; k < word.size();) {
                    const size_t start = k;
                    const char32_t cp = utf8::next(word, k);
                    const Glyph* g = glyph(cp);
                    const float adv = g ? g->advance : 0.0f;
                    if (lineWidth + adv > limit && !line.empty()) {
                        lines.push_back(std::move(line));
                        line.clear();
                        lineWidth = 0.0f;
                    }
                    line.append(word.substr(start, k - start));
                    lineWidth += adv;
                }
            } else {
                line.append(word);
                lineWidth += wordWidth;
            }
            line.append(spaces);
            lineWidth += spaceWidth;
            i = spaceEnd;
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(std::move(line));
        if (paragraphEnd >= text.size()) break;
        paragraphStart = paragraphEnd + 1;
    }
    return lines;
}

std::string Font::ellipsize(std::string_view text, float maxWidth) {
    if (measure(text) <= maxWidth) return std::string(text);
    const std::string dots = "…";
    const float limit = maxWidth - measure(dots);
    std::string out;
    float width = 0.0f;
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        const Glyph* g = glyph(utf8::next(text, i));
        const float adv = g ? g->advance / m->scale : 0.0f;
        if (width + adv > limit) break;
        out.append(text.substr(start, i - start));
        width += adv;
    }
    return out + dots;
}

} // namespace eng
