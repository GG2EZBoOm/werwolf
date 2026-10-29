#include "client/App.h"
#include "client/Theme.h"

#include <cmath>

namespace ww {

using eng::Affine2;
using eng::Color;
using eng::Rect;
using eng::Vec2;
using namespace palette;

namespace {

void keepDigits(std::string& s) {
    std::string out;
    for (char c : s)
        if (c >= '0' && c <= '9') out += c;
    s = out;
}

struct MenuEntry {
    const char* label;
    const char* labelEn;
    const char* description;
    const char* descriptionEn;
    float angle; // wie schief der Zettel hängt (Bogenmaß)
};

constexpr MenuEntry kMenu[] = {
    {"Ein Dorf gründen", "Found a village",
     "Eröffne ein Spiel auf diesem PC. Freunde kommen über das Heimnetz, das Internet (IPv4 oder IPv6) oder eine Discord-Einladung.",
     "Start a game on this PC. Friends join over the home network, the internet (IPv4 or IPv6) or a Discord invite.", -0.035f},
    {"Einem Dorf beitreten", "Join a village", "Verbinde dich mit einem Dorf, das jemand anderes gegründet hat.",
     "Connect to a village someone else has founded.", 0.028f},
    {"Durchs Dorf streifen", "Roam the village", "Allein durch das Dorf – ohne Regeln, ohne Wölfe. Nur du und die Nacht.",
     "Alone through the village – no rules, no wolves. Just you and the night.", -0.018f},
    {"Einstellungen", "Settings", "Grafik, Ton, Steuerung, Sprache und Oberfläche.", "Graphics, sound, controls, language and interface.", 0.032f},
    {"Das Feuer löschen", "Put out the fire", "Das Spiel beenden.", "Quit the game.", -0.026f},
};

// Ein paar Zeilen Dorfgeflüster für Lade- und Wartebildschirme
constexpr std::pair<const char*, const char*> kLore[] = {
    {"„Schließ die Tür von innen, Kind. Dann ist sie verriegelt – und bete, dass sie hält.“",
     "“Close the door from the inside, child. Then it's bolted – and pray that it holds.”"},
    {"„Der Bär brummt nicht ohne Grund. Wer neben dem Bärenführer sitzt, sollte sich gut erklären können.“",
     "“The bear doesn't growl for nothing. Whoever sits next to the bear tamer had better explain themselves.”"},
    {"„Die Seherin sieht weit – aber nur, wen sie auch anschauen kann.“", "“The seer sees far – but only those she can look at.”"},
    {"„Ein Schrei in der Nacht ist schlimm. Eine Tür, die kracht, ist schlimmer.“",
     "“A scream in the night is bad. A door that cracks is worse.”"},
    {"„Wer am Feuer am lautesten anklagt, hat oft am meisten zu verbergen.“",
     "“Whoever accuses loudest at the fire often has the most to hide.”"},
    {"„Der Narr lacht noch am Galgen. Hängt ihn nicht – genau das will er.“",
     "“The jester still laughs at the gallows. Don't hang him – that's exactly what he wants.”"},
};

// Ein Blatt, das sich von oben nach unten entrollt (open 0..1)
Affine2 unroll(const Rect& r, float open) {
    const float sy = 0.04f + 0.96f * open;
    return Affine2{1.0f, 0.0f, 0.0f, sy, 0.0f, r.y * (1.0f - sy)};
}

} // namespace

void App::drawFooterHints(bool back) {
    eng::Renderer2D& r = m_renderer;
    const float y = r.height() - 40.0f;
    float x = r.width() * 0.5f;
    const char* select = L("Auswählen", "Select");
    const char* confirm = L("Bestätigen", "Confirm");
    const char* backText = L("Zurück", "Back");
    const float total = m_kit.keycapWidth("↑↓") + m_fonts.small.measure(select) + m_kit.keycapWidth("Enter") +
                        m_fonts.small.measure(confirm) + (back ? m_kit.keycapWidth("Esc") + m_fonts.small.measure(backText) + 60.0f : 30.0f) + 16.0f;
    x = std::round(r.width() - 40.0f - total);
    x += m_kit.hint({x, y}, "↑↓", select) + 26.0f;
    x += m_kit.hint({x, y}, "Enter", confirm) + 26.0f;
    if (back) m_kit.hint({x, y}, "Esc", backText);
}

void App::drawMenu() {
    eng::Renderer2D& r = m_renderer;
    eng::Input& in = m_window.input();
    const float W = r.width();
    const float H = r.height();
    const float since = static_cast<float>(m_time - m_menuEnteredAt);
    const bool english = language() == Lang::English;

    // Nacht und Glut über dem Dorf
    m_kit.vignette(1.0f);
    m_kit.embers({0, H * 0.3f, W, H * 0.7f}, 30, 0.8f);

    // --- Die Dorftafel ---------------------------------------------------------------
    const float boardIn = eng::smoothstep(0.0f, 0.9f, since);
    const Rect board{std::round(std::max(32.0f, W * 0.045f)) - (1.0f - boardIn) * 60.0f, 36.0f, 430.0f, H - 96.0f};
    m_kit.light({board.right() - 10.0f, board.y + 60.0f}, boardIn);
    m_kit.candleLight({board.right() + 10.0f, board.y + 40.0f}, 520.0f, boardIn);
    m_kit.board(board, boardIn);
    m_kit.candleLight({board.x + board.w * 0.65f, board.y + 30.0f}, 330.0f, 0.7f * boardIn, 2.0f);
    // Kratzspuren im Holz – hier war schon jemand
    m_kit.claws({board.right() - 120.0f, board.bottom() - 150.0f}, {board.right() - 40.0f, board.bottom() - 40.0f}, 16.0f,
                eng::smoothstep(1.4f, 2.0f, since), Color::hex(0x0E0805, 0.85f));

    // Kopfzettel mit Titel
    const float headIn = eng::smoothstep(0.35f, 1.1f, since);
    const Rect head{board.x + 26.0f, board.y + 30.0f - (1.0f - headIn) * 30.0f, board.w - 52.0f, 168.0f};
    m_kit.beginTilt(head, -0.022f + (1.0f - headIn) * 0.08f);
    m_kit.paper(head, 1234u, headIn);
    m_kit.nail({head.center().x, head.y + 14.0f}, headIn);
    const char* title = L("Werwolf", "Werewolf");
    const float rawW = m_fonts.calligraphy.measure(title);
    const float fit = std::min(1.0f, (head.w - 30.0f) / std::max(1.0f, rawW)); // "Werewolf" ist etwas breiter
    const float titleW = rawW * fit;
    const float titleX = head.center().x - titleW * 0.5f;
    const float baseline = head.y + 102.0f;
    r.pushTransform(Affine2{fit, 0.0f, 0.0f, fit, titleX * (1.0f - fit), baseline * (1.0f - fit)});
    m_kit.writeText(m_fonts.calligraphy, {titleX, baseline - m_fonts.calligraphy.ascent()}, title, inkDark.withAlpha(headIn),
                    eng::smoothstep(0.7f, 1.9f, since));
    r.popTransform();
    m_kit.swash({titleX + 8.0f, baseline + 9.0f}, titleW - 16.0f, inkRed.withAlpha(0.9f * headIn), eng::smoothstep(1.6f, 2.3f, since), 77);
    const char* tagline = L("Das Dorf schläft. Die Wölfe nicht.", "The village sleeps. The wolves don't.");
    m_kit.writeText(m_fonts.script, {head.center().x - m_fonts.script.measure(tagline) * 0.5f, baseline + 20.0f}, tagline,
                    inkSoft.withAlpha(headIn), eng::smoothstep(2.0f, 3.0f, since));
    m_kit.endTilt();

    // --- Zettel als Menüpunkte -----------------------------------------------------------
    const bool typing = m_ui.anyFocused();
    const int count = static_cast<int>(std::size(kMenu));
    if (!typing && m_menuPage == MenuPage::None) {
        if (in.pressed(eng::Key::Down)) m_menuFocus = (m_menuFocus + 1) % count;
        if (in.pressed(eng::Key::Up)) m_menuFocus = (m_menuFocus + count - 1) % count;
    }
    if (in.pressed(eng::Key::Escape) && !typing) m_menuPage = MenuPage::None;

    int activated = -1;
    const float slipY = head.bottom() + 34.0f;
    const float slipH = 56.0f, slipGap = std::min(14.0f, (board.bottom() - 110.0f - slipY - slipH * count) / (count - 1));
    for (int i = 0; i < count; ++i) {
        const float delay = 0.8f + static_cast<float>(i) * 0.12f;
        const float appear = eng::smoothstep(delay, delay + 0.55f, since);
        if (appear <= 0.01f) continue;
        const Rect rc{board.x + 34.0f + (i % 2 == 0 ? 0.0f : 14.0f), slipY + static_cast<float>(i) * (slipH + slipGap), board.w - 82.0f, slipH};
        const bool selected = m_menuFocus == i || (m_menuPage == MenuPage::Host && i == 0) || (m_menuPage == MenuPage::Join && i == 1) ||
                              (m_menuPage == MenuPage::Settings && i == 3);
        if (m_kit.slip(std::string("menu/slip/") + std::to_string(i), rc, english ? kMenu[i].labelEn : kMenu[i].label, kMenu[i].angle, selected,
                       300u + static_cast<uint32_t>(i) * 17u, appear))
            activated = i;
        if (m_ui.hovered(rc)) m_menuFocus = i;
    }
    if (!typing && m_menuPage == MenuPage::None && in.pressed(eng::Key::Enter)) {
        activated = m_menuFocus;
        playSound(Sound::Click, 0.65f);
    }

    switch (activated) {
    case 0: m_menuPage = m_menuPage == MenuPage::Host ? MenuPage::None : MenuPage::Host; break;
    case 1: m_menuPage = m_menuPage == MenuPage::Join ? MenuPage::None : MenuPage::Join; break;
    case 2: enterSandbox(); break;
    case 3: m_menuPage = m_menuPage == MenuPage::Settings ? MenuPage::None : MenuPage::Settings; break;
    case 4: m_quit = true; break;
    default: break;
    }
    if (activated == 0 || activated == 1) {
        m_menuError.clear();
        m_kit.anim("menu/page/open", 0.0f, 1000.0f); // neu entrollen
    }
    if (activated == 3) m_kit.anim("menu/page/open", 0.0f, 1000.0f);

    // Beschreibung unten auf der Tafel, mit Kreide geschrieben
    {
        const float a = eng::smoothstep(1.5f, 2.2f, since);
        float y = board.bottom() - 84.0f;
        const char* description = english ? kMenu[m_menuFocus].descriptionEn : kMenu[m_menuFocus].description;
        for (const std::string& line : m_fonts.hand.wrap(description, board.w - 80.0f)) {
            r.text(m_fonts.hand, {board.x + 40.0f, y}, line, chalk.withAlpha(0.75f * a));
            y += m_fonts.hand.lineHeight();
        }
    }

    // --- Aushang rechts: Untermenü --------------------------------------------------------
    if (m_menuPage != MenuPage::None) {
        const float open = eng::smoothstep(0.0f, 1.0f, m_kit.anim("menu/page/open", 1.0f, 3.2f));
        const float px = board.right() + std::max(36.0f, W * 0.04f);
        const bool settings = m_menuPage == MenuPage::Settings;
        const float pw = std::min(settings ? 680.0f : 520.0f, W - px - 36.0f);
        const float top = settings ? 36.0f : head.bottom() + 10.0f;
        const float ph = settings ? std::min(H - 96.0f, 680.0f) : 400.0f;
        const Rect area{px, top, pw, ph};
        r.pushTransform(unroll(area, open));
        drawMenuPage(area);
        r.popTransform();
    }

    // Mitteilung (z. B. nach einem Update) als Zettel oben rechts
    if (!m_menuNotice.empty()) {
        const float nw = 360.0f;
        const std::vector<std::string> lines = m_fonts.paper.wrap(m_menuNotice, nw - 50.0f);
        const Rect note{W - nw - 40.0f, 40.0f, nw, 60.0f + static_cast<float>(lines.size()) * m_fonts.paper.lineHeight()};
        const float a = eng::smoothstep(1.2f, 1.8f, since);
        m_kit.beginTilt(note, 0.03f);
        m_kit.paper(note, 888u, a);
        m_kit.nail({note.center().x, note.y + 12.0f}, a);
        r.text(m_fonts.scriptBold, {note.x + 24.0f, note.y + 18.0f}, L("Kunde aus der Stadt", "News from town"), inkRed.withAlpha(a));
        float y = note.y + 48.0f;
        for (const std::string& line : lines) {
            r.text(m_fonts.paper, {note.x + 24.0f, y}, line, inkDark.withAlpha(a));
            y += m_fonts.paper.lineHeight();
        }
        m_kit.endTilt();
    }

    drawFooterHints(m_menuPage != MenuPage::None);
    r.text(m_fonts.small, {board.x + 6.0f, H - 40.0f}, Lf("Version {} · eigene C++-Engine", "Version {} · custom C++ engine", versionString()), smoke);
    m_kit.fade(1.0f - eng::smoothstep(0.0f, 0.9f, since));
}

void App::drawMenuPage(const Rect& area) {
    eng::Renderer2D& r = m_renderer;
    if (m_menuPage == MenuPage::Settings) {
        if (drawSettings(area)) m_menuPage = MenuPage::None;
        return;
    }
    const bool host = m_menuPage == MenuPage::Host;
    m_kit.paper(area, host ? 4242u : 4343u);
    m_kit.nail({area.x + 20.0f, area.y + 18.0f});
    m_kit.nail({area.right() - 20.0f, area.y + 18.0f});
    Rect inner = area.shrink(42.0f, 34.0f);
    r.text(m_fonts.scriptLarge, {inner.x, inner.y - 6.0f}, host ? L("Aushang", "Notice") : L("Ein Brief", "A letter"), inkRed);
    inner.takeTop(m_fonts.scriptLarge.lineHeight() - 4.0f);
    r.text(m_fonts.paperItalic, {inner.x, inner.y},
           host ? L("Ein neues Dorf wird gegründet. Wer mitspielen will, trage sich ein.", "A new village is being founded. Whoever wants to play, sign up.")
                : L("Ich bitte um Einlass in euer Dorf.", "I humbly ask to be let into your village."),
           inkSoft);
    inner.takeTop(34.0f);

    m_kit.beginPaperTheme();
    r.text(m_fonts.script, {inner.x, inner.y}, L("Dein Name", "Your name"), inkFaded);
    inner.takeTop(m_fonts.script.lineHeight());
    m_ui.textInput("menu/name", inner.takeTop(38.0f), m_nameInput, L("Wie heißt du?", "What is your name?"), proto::kMaxNameLength);
    inner.takeTop(14.0f);

    bool submit = false;
    const float portW = 96.0f;
    if (host) {
        r.text(m_fonts.script, {inner.x, inner.y}, L("Tor (Port)", "Gate (port)"), inkFaded);
        inner.takeTop(m_fonts.script.lineHeight());
        Rect row = inner.takeTop(38.0f);
        submit |= m_ui.textInput("menu/port", row.takeLeft(portW), m_portInput, "27777", 5);
        row.takeLeft(16.0f);
        r.text(m_fonts.paperItalic, {row.x, row.y + 2.0f}, L("Andere kommen über deine IP-Adresse", "Others arrive via your IP address"), inkSoft);
        r.text(m_fonts.paperItalic, {row.x, row.y + 19.0f}, L("(IPv4 oder IPv6) und dieses Tor.", "(IPv4 or IPv6) and this gate."), inkSoft);
        inner.takeTop(10.0f);
        m_kit.endPaperTheme();
        // Router-Freigabe automatisch per UPnP
        bool upnp = m_config.upnp;
        if (m_kit.inkCheck("menu/upnp", inner.takeTop(28.0f), L("Port im Router automatisch freigeben (UPnP)", "Open the port in the router automatically (UPnP)"), upnp)) {
            m_config.upnp = upnp;
            m_config.save();
        }
        m_kit.beginPaperTheme();
    } else {
        r.text(m_fonts.script, {inner.x, inner.y}, L("Adresse des Dorfes", "Village address"), inkFaded);
        r.text(m_fonts.script, {inner.right() - portW, inner.y}, L("Tor", "Gate"), inkFaded);
        inner.takeTop(m_fonts.script.lineHeight());
        Rect row = inner.takeTop(38.0f);
        const Rect portRect = row.takeRight(portW);
        row.takeRight(10.0f);
        submit |= m_ui.textInput("menu/address", row, m_addressInput, L("z. B. 84.12.3.4 oder 2001:db8::1", "e.g. 84.12.3.4 or 2001:db8::1"), 128);
        submit |= m_ui.textInput("menu/port", portRect, m_portInput, "27777", 5);
        inner.takeTop(4.0f);
        r.text(m_fonts.paperItalic, {inner.x, inner.y},
               L("IPv4, IPv6 oder Name; der Port darf dabeistehen ([…]:27777).", "IPv4, IPv6 or a name; the port may be included ([…]:27777)."), inkSoft);
        inner.takeTop(m_fonts.paperItalic.lineHeight());
    }
    m_kit.endPaperTheme();
    keepDigits(m_portInput);
    inner.takeTop(20.0f);

    Rect buttons = inner.takeTop(46.0f);
    if (m_kit.inkButton("menu/go", buttons.takeLeft(230.0f), host ? L("Dorf gründen", "Found village") : L("Anklopfen", "Knock"), true) || submit) {
        if (host) hostGame();
        else joinGame();
    }
    buttons.takeLeft(16.0f);
    if (m_kit.inkButton("menu/back", {buttons.x, buttons.y + 4.0f, 120.0f, 38.0f}, L("Zurück", "Back"))) m_menuPage = MenuPage::None;

    inner.takeTop(12.0f);
    if (!m_menuError.empty()) {
        float y = inner.y;
        for (const std::string& line : m_fonts.paperItalic.wrap(m_menuError, inner.w)) {
            r.text(m_fonts.paperItalic, {inner.x, y}, line, inkRed);
            y += m_fonts.paperItalic.lineHeight();
        }
    } else if (!host) {
        // Hinweis auf Discord-Einladungen
        const char* hint = m_discord.connected()
                               ? L("Oder: In Discord auf die Einladung eines Freundes klicken.", "Or: click a friend's invite in Discord.")
                               : L("Tipp: Mit Discord geht es auch per Einladung.", "Tip: with Discord you can also join via an invite.");
        r.text(m_fonts.paperItalic, {inner.x, inner.y}, hint, inkFaded);
    }
}

void App::drawConnecting() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();
    m_kit.fade(0.62f);
    m_kit.vignette(1.0f);
    m_kit.embers({0, H * 0.4f, W, H * 0.6f}, 22, 0.7f);

    // Ein Brief im Kerzenschein
    const Rect letter{std::round(W * 0.5f - 260.0f), std::round(H * 0.5f - 170.0f), 520.0f, 320.0f};
    m_kit.light({letter.right() - 40.0f, letter.y - 10.0f});
    m_kit.candleLight({letter.right() - 30.0f, letter.y - 20.0f}, 420.0f, 1.0f);
    m_kit.beginTilt(letter, -0.015f);
    m_kit.paper(letter, 5150u);
    const char* title = m_hosting ? L("Das Dorf erwacht …", "The village awakens …") : L("Auf dem Weg ins Dorf …", "On the way to the village …");
    r.text(m_fonts.scriptLarge, {letter.center().x - m_fonts.scriptLarge.measure(title) * 0.5f, letter.y + 34.0f}, title, inkRed);
    const std::string target = m_net.address().empty() ? eng::net::formatEndpoint(m_net.host(), m_net.port()) : m_net.address();
    r.text(m_fonts.paperItalic, {letter.center().x - m_fonts.paperItalic.measure(target) * 0.5f, letter.y + 92.0f}, target, inkFaded);
    // Das Siegel dreht sich langsam, während wir warten
    const float t = static_cast<float>(m_time);
    const Vec2 sc{letter.center().x, letter.y + 168.0f};
    r.pushTransform(Affine2::around(sc, t * 1.6f));
    m_kit.seal(sc, 30.0f, wax, "☾", 1.0f, 17);
    r.popTransform();
    const size_t lore = static_cast<size_t>(m_time / 6.0) % std::size(kLore);
    const float loreA = std::min(1.0f, static_cast<float>(std::fmod(m_time, 6.0)) * 0.8f);
    float y = letter.y + 222.0f;
    for (const std::string& line : m_fonts.script.wrap(L(kLore[lore].first, kLore[lore].second), letter.w - 80.0f)) {
        m_kit.writeText(m_fonts.script, {letter.center().x - m_fonts.script.measure(line) * 0.5f, y}, line, inkSoft, loreA);
        y += m_fonts.script.lineHeight();
    }
    m_kit.endTilt();

    if (m_kit.inkButton("connect/cancel", {W * 0.5f - 70.0f, letter.bottom() + 26.0f, 140.0f, 38.0f}, L("Umkehren", "Turn back"))) leaveSession();
}

void App::drawLost() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();
    m_kit.fade(0.65f);
    m_kit.vignette(1.0f);

    const Rect note{std::round(W * 0.5f - 270.0f), std::round(H * 0.5f - 150.0f), 540.0f, 280.0f};
    m_kit.light({note.x + 60.0f, note.y - 20.0f});
    m_kit.beginTilt(note, 0.02f);
    m_kit.paper(note, 6160u);
    m_kit.nail({note.center().x, note.y + 14.0f});
    m_kit.claws({note.x + 40.0f, note.y + 20.0f}, {note.right() - 60.0f, note.bottom() - 30.0f}, 22.0f, 1.0f, inkRed.withAlpha(0.18f));
    r.text(m_fonts.scriptLarge, {note.x + 40.0f, note.y + 36.0f}, L("Der Weg ist abgeschnitten", "The way is cut off"), inkRed);
    float y = note.y + 100.0f;
    const std::string reason = m_net.error().empty() ? std::string(L("Unbekannter Fehler", "Unknown error")) : m_net.error();
    for (const std::string& line : m_fonts.paper.wrap(reason, note.w - 80.0f)) {
        r.text(m_fonts.paper, {note.x + 40.0f, y}, line, inkDark);
        y += m_fonts.paper.lineHeight();
    }
    const float by = note.bottom() - 74.0f;
    if (m_kit.inkButton("lost/retry", {note.x + 40.0f, by, 240.0f, 44.0f}, L("Erneut anklopfen", "Knock again"), true)) reconnect();
    if (m_kit.inkButton("lost/menu", {note.x + 300.0f, by + 4.0f, 170.0f, 38.0f}, L("Zur Tafel", "To the board"))) leaveSession();
    m_kit.endTilt();
}

} // namespace ww
