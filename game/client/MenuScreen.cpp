#include "client/App.h"
#include "client/Theme.h"

#include <cmath>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using eng::ui::ButtonKind;

namespace {
void keepDigits(std::string& s) {
    std::string out;
    for (char c : s)
        if (c >= '0' && c <= '9') out += c;
    s = out;
}
} // namespace

void App::drawMenu() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();

    // 3D-Dorf im Hintergrund, oben abgedunkelt für den Titel
    r.gradientV({0, 0, W, H * 0.45f}, Color::hex(0x02030A, 0.55f), Color::hex(0x02030A, 0.0f));
    r.rect({0, 0, W, H}, Color::hex(0x02030A, 0.18f));

    // Titel
    const float titleY = H * 0.1f;
    r.glow({W * 0.5f, titleY + 46.0f}, 340.0f, theme::gold.withAlpha(0.10f));
    r.textCentered(m_fonts.title, {0, titleY + 3.0f, W, 92}, "WERWOLF", Color::hex(0x000000, 0.5f));
    r.textCentered(m_fonts.title, {0, titleY, W, 92}, "WERWOLF", theme::goldLight);
    r.textCentered(m_fonts.narration, {0, titleY + 96.0f, W, 24}, "Das Dorf schläft. Die Wölfe nicht.", theme::muted);

    // Anmeldefeld
    const float pw = 440.0f;
    const float ph = 296.0f;
    const Rect panel{std::round(W * 0.5f - pw * 0.5f), std::round(std::max(titleY + 150.0f, H * 0.36f)), pw, ph};
    drawPanel(panel);
    Rect inner = panel.shrink(28.0f, 24.0f);

    r.text(m_fonts.smallBold, {inner.x, inner.y}, "DEIN NAME", theme::muted);
    inner.takeTop(20.0f);
    const Rect nameRect = inner.takeTop(42.0f);
    m_ui.textInput("menu/name", nameRect, m_nameInput, "Wie heißt du?", proto::kMaxNameLength);
    inner.takeTop(18.0f);

    const float portW = 96.0f;
    r.text(m_fonts.smallBold, {inner.x, inner.y}, "SERVER-ADRESSE", theme::muted);
    r.text(m_fonts.smallBold, {inner.right() - portW, inner.y}, "PORT", theme::muted);
    inner.takeTop(20.0f);
    Rect row = inner.takeTop(42.0f);
    const Rect portRect = row.takeRight(portW);
    row.takeRight(10.0f);
    const bool submitAddress =
        m_ui.textInput("menu/address", row, m_addressInput, "z. B. 192.168.0.12", 128);
    const bool submitPort = m_ui.textInput("menu/port", portRect, m_portInput, "27777", 5);
    keepDigits(m_portInput);
    inner.takeTop(22.0f);

    Rect buttons = inner.takeTop(46.0f);
    const float bw = (buttons.w - 12.0f) * 0.5f;
    const Rect hostRect = buttons.takeLeft(bw);
    const Rect joinRect = buttons.takeRight(bw);
    if (m_ui.button("menu/host", hostRect, "Spiel hosten", ButtonKind::Primary)) hostGame();
    if (m_ui.button("menu/join", joinRect, "Beitreten") || submitAddress || submitPort) joinGame();

    inner.takeTop(14.0f);
    if (!m_menuError.empty()) {
        float y = inner.y;
        for (const std::string& line : m_fonts.small.wrap(m_menuError, inner.w)) {
            r.text(m_fonts.small, {inner.x, y}, line, Color::hex(0xF08A7E));
            y += m_fonts.small.lineHeight();
        }
    } else {
        r.text(m_fonts.small, {inner.x, inner.y},
               "Hosten startet einen Server auf diesem PC – die anderen treten per IP bei.", theme::faint);
    }

    const Rect exploreRect{panel.center().x - 150.0f, panel.bottom() + 16.0f, 180.0f, 38.0f};
    if (m_ui.button("menu/explore", exploreRect, "Dorf erkunden", ButtonKind::Ghost)) enterSandbox();
    const Rect quitRect{panel.center().x + 40.0f, panel.bottom() + 16.0f, 110.0f, 38.0f};
    if (m_ui.button("menu/quit", quitRect, "Beenden", ButtonKind::Ghost)) m_quit = true;

    if (!m_menuNotice.empty()) {
        float y = quitRect.bottom() + 14.0f;
        for (const std::string& line : m_fonts.small.wrap(m_menuNotice, 520.0f)) {
            r.textCentered(m_fonts.small, {0, y, W, 18.0f}, line, theme::gold.fade(0.85f));
            y += m_fonts.small.lineHeight();
        }
    }

    r.textRight(m_fonts.small, {W - 16.0f, H - 26.0f},
                std::string("Eigene C++-Engine · F11 Vollbild · Version ") + versionString(), theme::faint);
}

void App::drawConnecting() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();
    r.rect({0, 0, W, H}, Color::hex(0x02030A, 0.4f));

    const Rect panel{std::round(W * 0.5f - 210.0f), std::round(H * 0.5f - 110.0f), 420.0f, 200.0f};
    drawPanel(panel);
    const std::string target = m_net.host() + ":" + std::to_string(m_net.port());
    r.textCentered(m_fonts.large, {panel.x, panel.y + 26.0f, panel.w, 30.0f},
                   m_hosting ? "Dorf wird gegründet …" : "Verbinde …", theme::text);
    r.textCentered(m_fonts.small, {panel.x, panel.y + 60.0f, panel.w, 20.0f}, target, theme::muted);

    // Lade-Animation: kreisende Punkte
    const Vec2 c{panel.center().x, panel.y + 112.0f};
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) / 8.0f * 2.0f * eng::kPi;
        const float phase = std::fmod(static_cast<float>(m_time) * 1.2f - static_cast<float>(i) / 8.0f + 10.0f, 1.0f);
        r.circle(c + Vec2{std::cos(a) * 16.0f, std::sin(a) * 16.0f}, 3.2f, theme::gold.withAlpha(0.25f + 0.75f * phase));
    }

    if (m_ui.button("connect/cancel", {panel.center().x - 70.0f, panel.bottom() - 52.0f, 140.0f, 36.0f}, "Abbrechen",
                    ButtonKind::Ghost))
        leaveSession();
}

void App::drawLost() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();
    r.rect({0, 0, W, H}, Color::hex(0x02030A, 0.45f));

    const Rect panel{std::round(W * 0.5f - 230.0f), std::round(H * 0.5f - 120.0f), 460.0f, 220.0f};
    drawPanel(panel);
    r.textCentered(m_fonts.large, {panel.x, panel.y + 26.0f, panel.w, 30.0f}, "Verbindung verloren", theme::text);
    float y = panel.y + 66.0f;
    const std::string reason = m_net.error().empty() ? "Unbekannter Fehler" : m_net.error();
    for (const std::string& line : m_fonts.small.wrap(reason, panel.w - 56.0f)) {
        r.textCentered(m_fonts.small, {panel.x, y, panel.w, 18.0f}, line, theme::muted);
        y += m_fonts.small.lineHeight();
    }

    Rect buttons{panel.x + 28.0f, panel.bottom() - 70.0f, panel.w - 56.0f, 44.0f};
    const float bw = (buttons.w - 12.0f) * 0.5f;
    const Rect retry = buttons.takeLeft(bw);
    const Rect menu = buttons.takeRight(bw);
    if (m_ui.button("lost/retry", retry, "Erneut verbinden", ButtonKind::Primary)) reconnect();
    if (m_ui.button("lost/menu", menu, "Hauptmenü")) leaveSession();
}

} // namespace ww
