#pragma once

#include "engine/core/Math.h"
#include "engine/gfx/Font.h"
#include "engine/gfx/Renderer2D.h"
#include "engine/ui/UI.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ww {

struct Fonts {
    eng::Font ui;
    eng::Font uiBold;
    eng::Font chat;
    eng::Font small;
    eng::Font smallBold;
    eng::Font large;
    eng::Font title;
    eng::Font heading;
    eng::Font narration;
    eng::Font icon;
    eng::Font iconLarge;
    eng::Font iconHuge;  // Rollenkarte
    eng::Font display;   // Bildschirmtitel
    eng::Font menu;      // Menüpunkte
    eng::Font label;     // kleine Sperrschrift-Überschriften
    eng::Font subtitle;  // Erzähler-Untertitel
    // Handschrift und Kalligrafie für Papier, Tafeln und Bücher
    eng::Font script;       // Segoe Script
    eng::Font scriptBold;   // Segoe Script fett (Zettel)
    eng::Font scriptLarge;  // große Überschriften auf Papier
    eng::Font hand;         // Ink Free (Namen, Notizen)
    eng::Font handSmall;    // Kreide im Spiel-Chat
    eng::Font handLarge;
    eng::Font calligraphy;  // Gabriola, Titel
    eng::Font calligraphyMid;
    eng::Font paper;        // gedruckter Text auf Papier (Georgia)
    eng::Font paperItalic;
};

// Farbwelt: Tinte, Messing, Pergament und Glut – eine Winternacht im Dorf.
namespace palette {
using eng::Color;
inline constexpr Color ink = Color::hex(0x07080C);
inline constexpr Color inkLight = Color::hex(0x12141B);
inline constexpr Color brass = Color::hex(0xC9A45C);
inline constexpr Color brassLight = Color::hex(0xF0D9A0);
inline constexpr Color brassDark = Color::hex(0x7A6236);
inline constexpr Color parchment = Color::hex(0xE8DFCC);
inline constexpr Color ash = Color::hex(0xA39C8C);
inline constexpr Color smoke = Color::hex(0x6F6A5E);
inline constexpr Color ember = Color::hex(0xE07B3A);
inline constexpr Color blood = Color::hex(0xC4453A);
inline constexpr Color moon = Color::hex(0x9FB2C8);
// Tinte und Wachs auf Pergament
inline constexpr Color inkDark = Color::hex(0x2B1D12);
inline constexpr Color inkSoft = Color::hex(0x4E3B27);
inline constexpr Color inkFaded = Color::hex(0x6A5236);
inline constexpr Color inkRed = Color::hex(0x8E2219);
inline constexpr Color wax = Color::hex(0x9A2418);
inline constexpr Color iron = Color::hex(0x2A2724);
inline constexpr Color chalk = Color::hex(0xE4E2D6);
} // namespace palette

// Großbuchstaben inklusive Umlauten (für Sperrschrift)
std::string upper(std::string_view text);

// Gestaltungsbaukasten für alle Bildschirme: Atmosphäre, Flächen, Schmuck und
// animierte Bedienelemente im Stil des Spiels.
class UiKit {
public:
    void init();
    void shutdown();
    void beginFrame(eng::ui::Context& ui, eng::Renderer2D& r, Fonts& fonts, double time, float dt);

    // Weicher Wert je ID, der dem Ziel folgt (Hover, Einblenden …)
    float anim(std::string_view id, float target, float speed = 10.0f);
    // Wie anim, beginnt beim ersten Aufruf aber bei from (für Einblendungen)
    float animFrom(std::string_view id, float from, float target, float speed = 10.0f);

    // Atmosphäre
    void vignette(float strength);
    void embers(const eng::Rect& area, int count, float alpha);
    void fade(float alpha, eng::Color color = palette::ink);
    void sideShade(float width, float alpha); // dunkler Verlauf vom linken Rand

    // Flächen und Schmuck
    void panel(const eng::Rect& r, float alpha = 1.0f);
    void frame(const eng::Rect& r, eng::Color c, float alpha = 1.0f);
    void divider(eng::Vec2 center, float width, eng::Color c);
    void diamond(eng::Vec2 center, float size, eng::Color c);
    // Kleine Überschrift in Sperrschrift (wird groß geschrieben)
    void label(eng::Vec2 pos, std::string_view text, eng::Color c, float spacing = 2.4f);
    float labelWidth(std::string_view text, float spacing = 2.4f);
    void spaced(eng::Font& font, eng::Vec2 pos, std::string_view text, float spacing, eng::Color c);
    void spacedCentered(eng::Font& font, const eng::Rect& r, std::string_view text, float spacing, eng::Color c);
    void keycap(const eng::Rect& r, std::string_view key, eng::Color c, float alpha = 1.0f);
    float keycapWidth(std::string_view key);
    // Tastenhinweis "[Taste] Text", liefert die Breite
    float hint(eng::Vec2 pos, std::string_view key, std::string_view text, float alpha = 1.0f);

    // Bedienelemente
    bool menuItem(std::string_view id, eng::Vec2 pos, std::string_view text, bool selected, bool enabled = true,
                  float width = 320.0f);
    bool button(std::string_view id, const eng::Rect& r, std::string_view text, bool primary = false, bool enabled = true);
    bool tab(std::string_view id, const eng::Rect& r, std::string_view text, bool active);
    bool choice(std::string_view id, const eng::Rect& r, const std::vector<std::string>& options, int& index);
    bool slider(std::string_view id, const eng::Rect& r, float& value, float lo, float hi, std::string_view valueText);

    // --- Materialien: Holz, Pergament, Eisen, Wachs, Schiefer --------------------
    void board(const eng::Rect& r, float alpha = 1.0f);
    void paper(const eng::Rect& r, uint32_t seed, float alpha = 1.0f, eng::Color tint = eng::Color{1, 1, 1, 1});
    void slate(const eng::Rect& r, float alpha = 1.0f);
    void paperDisc(eng::Vec2 center, float radius, uint32_t seed, float alpha = 1.0f);
    void nail(eng::Vec2 p, float alpha = 1.0f);
    void seal(eng::Vec2 center, float radius, eng::Color wax, std::string_view glyph, float alpha = 1.0f, uint32_t seed = 7);
    // Drei Krallenspuren von a nach b, gezeichnet bis progress (0..1)
    void claws(eng::Vec2 a, eng::Vec2 b, float spread, float progress, eng::Color c);
    // Leicht zittriger Federstrich, gezeichnet bis progress
    void inkLine(eng::Vec2 a, eng::Vec2 b, float thickness, eng::Color c, float progress = 1.0f, uint32_t seed = 1);
    // Unterstreichung mit Schwung (Tinte)
    void swash(eng::Vec2 left, float width, eng::Color c, float progress = 1.0f, uint32_t seed = 3);
    void candleLight(eng::Vec2 p, float radius, float strength, float seed = 0.0f);
    float flicker(float seed) const;
    // Kerzenlicht für die Materialien (Relief, Glanz) an dieser Stelle setzen
    void light(eng::Vec2 p, float strength = 1.0f, float seed = 0.0f);
    // Inhalte, die mit einem Zettel schräg hängen
    void beginTilt(const eng::Rect& r, float angle, float scale = 1.0f);
    void endTilt();

    // --- Tinte auf Papier ------------------------------------------------------------
    // Zettel an der Tafel; liefert true beim Klick
    bool slip(std::string_view id, const eng::Rect& r, std::string_view text, float angle, bool selected, uint32_t seed,
              float appear = 1.0f);
    bool inkButton(std::string_view id, const eng::Rect& r, std::string_view text, bool primary = false, bool enabled = true);
    bool inkTab(std::string_view id, const eng::Rect& r, std::string_view text, bool active, eng::Color ribbon);
    bool inkChoice(std::string_view id, const eng::Rect& r, const std::vector<std::string>& options, int& index);
    bool inkSlider(std::string_view id, const eng::Rect& r, float& value, float lo, float hi, std::string_view valueText);
    bool inkCheck(std::string_view id, const eng::Rect& r, std::string_view text, bool& value, bool enabled = true);
    // Engine-Eingabefelder für Papier umfärben (bis endPaperTheme)
    void beginPaperTheme();
    void endPaperTheme();
    // Text, der wie mit der Feder geschrieben erscheint (progress 0..1)
    void writeText(eng::Font& font, eng::Vec2 pos, std::string_view text, eng::Color c, float progress);

    // --- Zeiger und Übergänge ---------------------------------------------------------
    void cursor(eng::Vec2 p, eng::Cursor kind);
    // Krallenhieb über den ganzen Bildschirm (0..1)
    void clawWipe(float t);
    // Funken, z. B. beim Klick
    void burst(eng::Vec2 p, eng::Color c, int count = 14);
    void drawBursts();

private:
    struct Spark {
        eng::Vec2 pos, vel;
        float life, maxLife, size;
        eng::Color color;
    };
    std::vector<Spark> m_sparks;
    eng::ui::Theme m_savedTheme;
    unsigned m_parchment = 0;
    unsigned m_wood = 0;
    unsigned m_slateTex = 0;
    float m_lift = 0.0f; // angehobener Zettel: weicherer, weiterer Schatten
    void buildMaterials();
    void destroyMaterials();

private:
    eng::ui::Context* m_ui = nullptr;
    eng::Renderer2D* m_r = nullptr;
    Fonts* m_fonts = nullptr;
    double m_time = 0.0;
    float m_dt = 0.0f;
    unsigned m_paper = 0;
    uint64_t m_drag = 0;
    std::unordered_map<uint64_t, float> m_anim;
};

} // namespace ww
