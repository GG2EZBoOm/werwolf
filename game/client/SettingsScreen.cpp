#include "client/App.h"

#include <cmath>
#include <format>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using namespace palette;

namespace {

constexpr int kShadowSizes[] = {1024, 2048, 4096};

struct KeyBinding {
    const char* key;
    const char* action;
};

constexpr KeyBinding kBindings[] = {
    {"W A S D", "Laufen"},           {"Shift", "Rennen"},
    {"Maus", "Umsehen"},             {"E", "Hauptaktion (Stimme, Vision, …)"},
    {"F", "Zweitaktion (Gift, Feuer, …)"}, {"Q", "Tür öffnen / schließen"},
    {"R", "Verwandeln (Werwolf)"},   {"Linksklick", "Reißen, erstechen, Tür einschlagen"},
    {"Tab", "Geheimer Chat (Rudel, Geister, Geschwister)"}, {"J", "Tagebuch"},
    {"Enter", "Chat schreiben"},     {"Esc", "Pause"},
    {"F11", "Vollbild"},             {"F12", "Bildschirmfoto"},
};

} // namespace

void App::applySettings() {
    m_window.setVSync(m_config.vsync);
    if (m_window.isFullscreen() != m_config.fullscreen) m_window.toggleFullscreen();
    m_renderer3d.setQuality(m_config.antialiasing, kShadowSizes[std::clamp(m_config.shadowQuality, 0, 2)]);
    m_camera.sensitivity = 0.0024f * m_config.mouseSensitivity;
    m_camera.invertY = m_config.invertY;
    m_camera.fieldOfView = m_config.fieldOfView;
}

void App::applyGraphics(eng::Environment& env) const {
    if (!m_config.bloom) env.bloomStrength = 0.0f;
    if (!m_config.filmGrain) env.grain = 0.0f;
}

bool App::drawSettings(const Rect& area) {
    eng::Renderer2D& r = m_renderer;
    // Lesebändchen als Reiter, die oben über das Blatt hängen
    const char* tabs[] = {"Grafik", "Steuerung", "Oberfläche"};
    const Color ribbons[] = {Color::hex(0x7A1F18), Color::hex(0x2F4A35), Color::hex(0x2E3D5C)};
    m_kit.paper(area, 7070u);
    for (int i = 0; i < 3; ++i) {
        const Rect tr{area.x + 40.0f + static_cast<float>(i) * 132.0f, area.y - 8.0f, 124.0f, 40.0f};
        if (m_kit.inkTab(std::string("settings/tab/") + tabs[i], tr, tabs[i], m_settingsTab == i, ribbons[i])) m_settingsTab = i;
    }
    Rect inner = area.shrink(44.0f, 30.0f);
    inner.takeTop(40.0f);
    r.text(m_fonts.scriptLarge, {inner.x, inner.y}, "Wie soll die Nacht sein?", inkRed);
    inner.takeTop(m_fonts.scriptLarge.lineHeight() + 4.0f);
    m_kit.inkLine({inner.x, inner.y}, {inner.right(), inner.y + 1.0f}, 1.0f, inkFaded.withAlpha(0.5f), 1.0f, 41);
    inner.takeTop(12.0f);

    const Rect footer = inner.takeBottom(48.0f);
    const Rect hintArea = inner.takeBottom(50.0f);
    const char* hovered = nullptr;
    bool changed = false;
    int rowIndex = 0;

    // Eine Zeile: Beschriftung links, Bedienelement rechts, Beschreibung beim Überfahren
    auto row = [&](const char* text, const char* description) {
        const Rect rc = inner.takeTop(36.0f);
        inner.takeTop(3.0f);
        const bool hover = m_ui.hovered(rc);
        const float appear = eng::smoothstep(0.0f, 1.0f, m_kit.animFrom(std::string("settings/row/") + std::to_string(m_settingsTab) + "/" +
                                                                            std::to_string(rowIndex++),
                                                                        0.0f, 1.0f, 5.0f - static_cast<float>(rowIndex) * 0.35f));
        if (hover) {
            hovered = description;
            r.gradientH(rc, inkRed.withAlpha(0.07f), inkRed.withAlpha(0.0f));
        }
        r.text(m_fonts.script, {rc.x + 8.0f + (1.0f - appear) * 12.0f, rc.center().y - m_fonts.script.lineHeight() * 0.5f}, text,
               (hover ? inkDark : inkSoft).withAlpha(appear));
        return Rect{rc.right() - 250.0f, rc.y + 2.0f, 246.0f, rc.h - 4.0f};
    };
    auto onOff = [&](const char* id, const char* text, const char* description, bool& value) {
        int index = value ? 1 : 0;
        if (m_kit.inkChoice(id, row(text, description), {"Aus", "An"}, index)) {
            value = index == 1;
            changed = true;
        }
    };

    switch (m_settingsTab) {
    case 0: {
        int mode = m_config.fullscreen ? 1 : 0;
        if (m_kit.inkChoice("settings/fullscreen", row("Anzeige", "Im Fenster oder im Vollbild spielen (F11)."), {"Fenster", "Vollbild"}, mode)) {
            m_config.fullscreen = mode == 1;
            changed = true;
        }
        onOff("settings/vsync", "Bildsynchronisation", "Gleicht die Bildrate an den Monitor an und verhindert Bildrisse.", m_config.vsync);
        std::vector<std::string> aa = {"Aus"};
        std::vector<int> aaValues = {1};
        for (int s : {2, 4, 8})
            if (s <= m_renderer3d.maxSamples()) {
                aa.push_back(std::format("{}-fach", s));
                aaValues.push_back(s);
            }
        int aaIndex = 0;
        for (size_t i = 0; i < aaValues.size(); ++i)
            if (aaValues[i] == m_config.antialiasing) aaIndex = static_cast<int>(i);
        if (m_kit.inkChoice("settings/aa", row("Kantenglättung", "Glättet Kanten (MSAA). Höhere Stufen kosten Leistung."), aa, aaIndex)) {
            m_config.antialiasing = aaValues[static_cast<size_t>(aaIndex)];
            changed = true;
        }
        int shadows = m_config.shadowQuality;
        if (m_kit.inkChoice("settings/shadows", row("Schatten", "Schärfe der Schatten von Sonne und Mond."), {"Grob", "Mittel", "Fein"}, shadows)) {
            m_config.shadowQuality = shadows;
            changed = true;
        }
        onOff("settings/bloom", "Lichtschein", "Laternen, Feuer und Fenster leuchten weich nach.", m_config.bloom);
        onOff("settings/grain", "Filmkorn", "Feines Korn über dem Bild – wie ein alter Film.", m_config.filmGrain);
        float fov = m_config.fieldOfView;
        if (m_kit.inkSlider("settings/fov", row("Sichtfeld", "Wie weit die Kamera seitlich blickt."), fov, 50.0f, 95.0f, std::format("{:.0f}°", fov))) {
            m_config.fieldOfView = std::round(fov);
            changed = true;
        }
        break;
    }
    case 1: {
        float sens = m_config.mouseSensitivity;
        if (m_kit.inkSlider("settings/sens", row("Mausempfindlichkeit", "Wie schnell sich die Kamera mit der Maus dreht."), sens, 0.2f, 3.0f,
                            std::format("{:.1f}×", sens))) {
            m_config.mouseSensitivity = std::round(sens * 10.0f) / 10.0f;
            changed = true;
        }
        onOff("settings/invert", "Maus umkehren", "Maus nach oben schaut nach unten – wie im Flugsimulator.", m_config.invertY);
        inner.takeTop(8.0f);
        r.text(m_fonts.script, {inner.x + 8.0f, inner.y}, "Tastenbelegung", inkRed);
        inner.takeTop(m_fonts.script.lineHeight() + 4.0f);
        const float colW = (inner.w - 24.0f) * 0.5f;
        float y = inner.y;
        for (size_t i = 0; i < std::size(kBindings); ++i) {
            const float x = inner.x + 8.0f + (i % 2 == 0 ? 0.0f : colW + 16.0f);
            const KeyBinding& b = kBindings[i];
            const float kw = std::max(30.0f, m_fonts.smallBold.measure(b.key) + 14.0f);
            const Rect key{x, y, kw, 22.0f};
            m_kit.inkLine({key.x, key.bottom()}, {key.right(), key.bottom()}, 1.2f, inkFaded, 1.0f, static_cast<uint32_t>(i));
            r.textCentered(m_fonts.smallBold, key, b.key, inkDark);
            r.text(m_fonts.paperItalic, {x + kw + 10.0f, y + 2.0f}, m_fonts.paperItalic.ellipsize(b.action, colW - kw - 14.0f), inkSoft);
            if (i % 2 == 1) y += 27.0f;
        }
        break;
    }
    default:
        onOff("settings/subtitles", "Erzähler-Untertitel", "Wichtige Ereignisse erscheinen kurz als Untertitel in der Bildmitte.", m_config.subtitles);
        onOff("settings/fadechat", "Chat ausblenden", "Der Chat tritt in den Hintergrund, solange niemand etwas sagt.", m_config.fadeChat);
        onOff("settings/fps", "Bildrate anzeigen", "Zeigt die aktuelle Bildrate oben links.", m_config.showFps);
        break;
    }

    // Beschreibung am unteren Rand
    if (hovered) {
        float y = hintArea.y + 8.0f;
        for (const std::string& line : m_fonts.paperItalic.wrap(hovered, hintArea.w - 16.0f)) {
            r.text(m_fonts.paperItalic, {hintArea.x + 8.0f, y}, line, inkSoft);
            y += m_fonts.paperItalic.lineHeight();
        }
    }

    bool back = false;
    Rect buttons = footer;
    if (m_kit.inkButton("settings/back", buttons.takeRight(170.0f), "Fertig", true)) back = true;
    buttons.takeRight(20.0f);
    if (m_kit.inkButton("settings/defaults", {buttons.right() - 200.0f, buttons.y + 4.0f, 200.0f, 38.0f}, "Wie früher")) {
        const Config defaults;
        m_config.fullscreen = defaults.fullscreen;
        m_config.vsync = defaults.vsync;
        m_config.antialiasing = defaults.antialiasing;
        m_config.shadowQuality = defaults.shadowQuality;
        m_config.bloom = defaults.bloom;
        m_config.filmGrain = defaults.filmGrain;
        m_config.fieldOfView = defaults.fieldOfView;
        m_config.mouseSensitivity = defaults.mouseSensitivity;
        m_config.invertY = defaults.invertY;
        m_config.fadeChat = defaults.fadeChat;
        m_config.subtitles = defaults.subtitles;
        m_config.showFps = defaults.showFps;
        changed = true;
    }
    if (m_window.input().pressed(eng::Key::Escape) && !m_ui.anyFocused()) back = true;
    if (changed) {
        applySettings();
        m_config.save();
    }
    return back;
}

} // namespace ww