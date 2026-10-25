#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // Texturkoordinaten im Atlas
    int width = 0;                        // Bitmapgröße in physischen Pixeln
    int height = 0;
    int bearingX = 0; // Versatz von der Stiftposition
    int bearingY = 0; // Abstand Grundlinie -> Oberkante (nach oben positiv)
    float advance = 0; // physische Pixel
};

struct FontDesc {
    std::string family = "Segoe UI";
    float size = 16.0f; // logische Pixel (Em-Höhe)
    int weight = 400;   // 400 normal, 600 halbfett, 700 fett
    bool italic = false;
    std::string fallbackFamily = "Segoe UI Symbol"; // für Symbole, die der Hauptschrift fehlen
};

// Schrift mit dynamischem Glyphen-Atlas. Glyphen werden beim ersten Gebrauch
// über das Betriebssystem gerastert und in eine Textur hochgeladen.
class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    bool load(const FontDesc& desc, float dpiScale);
    void unload();
    bool isLoaded() const;

    const Glyph* glyph(char32_t cp);
    unsigned texture() const;
    float dpiScale() const;
    const FontDesc& desc() const;

    // Alle Maße in logischen Pixeln
    float lineHeight() const;
    float ascent() const;
    float descent() const;
    float capHeight() const;
    float measure(std::string_view text);
    // Bricht Text wortweise um; sehr lange Wörter werden hart getrennt.
    std::vector<std::string> wrap(std::string_view text, float maxWidth);
    // Kürzt Text mit "…", damit er in maxWidth passt.
    std::string ellipsize(std::string_view text, float maxWidth);

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace eng
