#include "client/App.h"

#include <windows.h>
#include <shellapi.h>

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
    const char* actionEn;
};

constexpr KeyBinding kBindings[] = {
    {"W A S D", "Laufen", "Walk"},
    {"Shift", "Rennen", "Run"},
    {"Maus", "Umsehen", "Look around"},
    {"E", "Hauptaktion (Stimme, Vision, …)", "Main action (vote, vision, …)"},
    {"F", "Zweitaktion (Gift, Feuer, …)", "Second action (poison, fire, …)"},
    {"Q", "Tür öffnen / schließen", "Open / close door"},
    {"R", "Verwandeln (Werwolf)", "Transform (werewolf)"},
    {"Linksklick", "Reißen, erstechen, Tür einschlagen", "Maul, stab, break a door"},
    {"Tab", "Geheimer Chat (Rudel, Geister, Geschwister)", "Secret chat (pack, ghosts, siblings)"},
    {"J", "Tagebuch", "Journal"},
    {"Enter", "Chat schreiben", "Write in chat"},
    {"Esc", "Pause", "Pause"},
    {"F11", "Vollbild", "Fullscreen"},
    {"F12", "Bildschirmfoto", "Screenshot"},
};

} // namespace

void App::applySettings() {
    applyAudioSettings();
    applyLanguage();
    m_window.setVSync(m_config.vsync);
    if (m_window.isFullscreen() != m_config.fullscreen) m_window.toggleFullscreen();
    m_renderer3d.setQuality(m_config.antialiasing, kShadowSizes[std::clamp(m_config.shadowQuality, 0, 2)]);
    m_camera.sensitivity = 0.0024f * m_config.mouseSensitivity;
    m_camera.invertY = m_config.invertY;
    m_camera.fieldOfView = m_config.fieldOfView;
}

void App::applyGraphics(eng::Environment& env) const {
    if (!m_config.bloom) env.bloomStrength = 0.0f;
}

bool App::drawSettings(const Rect& area) {
    eng::Renderer2D& r = m_renderer;
    const bool english = language() == Lang::English;
    // Lesebändchen als Reiter, die oben über das Blatt hängen
    const char* tabs[] = {L("Grafik", "Graphics"), L("Steuerung", "Controls"), L("Oberfläche", "Interface"), L("Ton", "Sound"), "Bots"};
    const Color ribbons[] = {Color::hex(0x7A1F18), Color::hex(0x2F4A35), Color::hex(0x2E3D5C), Color::hex(0x69502F), Color::hex(0x503854)};
    m_kit.paper(area, 7070u);
    const float tabWidth = std::min(132.0f, (area.w - 60.0f) / 5.0f);
    for (int i = 0; i < 5; ++i) {
        const Rect tr{area.x + 30.0f + static_cast<float>(i) * tabWidth, area.y - 8.0f, tabWidth - 4.0f, 40.0f};
        if (m_kit.inkTab(std::string("settings/tab/") + std::to_string(i), tr, tabs[i], m_settingsTab == i, ribbons[i])) m_settingsTab = i;
    }
    Rect inner = area.shrink(44.0f, 30.0f);
    inner.takeTop(40.0f);
    r.text(m_fonts.scriptLarge, {inner.x, inner.y}, L("Wie soll die Nacht sein?", "How should the night be?"), inkRed);
    inner.takeTop(m_fonts.scriptLarge.lineHeight() + 4.0f);
    m_kit.inkLine({inner.x, inner.y}, {inner.right(), inner.y + 1.0f}, 1.0f, inkFaded.withAlpha(0.5f), 1.0f, 41);
    inner.takeTop(12.0f);

    const Rect footer = inner.takeBottom(48.0f);
    const Rect hintArea = inner.takeBottom(50.0f);
    const char* hovered = nullptr;
    bool changed = false;
    int rowIndex = 0;
    const std::vector<std::string> offOn = {L("Aus", "Off"), L("An", "On")};

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
        if (m_kit.inkChoice(id, row(text, description), offOn, index)) {
            value = index == 1;
            changed = true;
        }
    };

    switch (m_settingsTab) {
    case 0: {
        int mode = m_config.fullscreen ? 1 : 0;
        if (m_kit.inkChoice("settings/fullscreen", row(L("Anzeige", "Display"), L("Im Fenster oder im Vollbild spielen (F11).", "Play in a window or fullscreen (F11).")),
                            {L("Fenster", "Window"), L("Vollbild", "Fullscreen")}, mode)) {
            m_config.fullscreen = mode == 1;
            changed = true;
        }
        onOff("settings/vsync", L("Bildsynchronisation", "Vertical sync"),
              L("Gleicht die Bildrate an den Monitor an und verhindert Bildrisse.", "Matches the frame rate to the monitor and prevents tearing."),
              m_config.vsync);
        std::vector<std::string> aa = {L("Aus", "Off")};
        std::vector<int> aaValues = {1};
        for (int s : {2, 4, 8})
            if (s <= m_renderer3d.maxSamples()) {
                aa.push_back(Lf("{}-fach", "{}×", s));
                aaValues.push_back(s);
            }
        int aaIndex = 0;
        for (size_t i = 0; i < aaValues.size(); ++i)
            if (aaValues[i] == m_config.antialiasing) aaIndex = static_cast<int>(i);
        if (m_kit.inkChoice("settings/aa", row(L("Kantenglättung", "Anti-aliasing"), L("Glättet Kanten (MSAA). Höhere Stufen kosten Leistung.", "Smooths edges (MSAA). Higher levels cost performance.")),
                            aa, aaIndex)) {
            m_config.antialiasing = aaValues[static_cast<size_t>(aaIndex)];
            changed = true;
        }
        int shadows = m_config.shadowQuality;
        if (m_kit.inkChoice("settings/shadows", row(L("Schatten", "Shadows"), L("Schärfe der Schatten von Sonne und Mond.", "Sharpness of the sun and moon shadows.")),
                            {L("Grob", "Coarse"), L("Mittel", "Medium"), L("Fein", "Fine")}, shadows)) {
            m_config.shadowQuality = shadows;
            changed = true;
        }
        onOff("settings/bloom", L("Lichtschein", "Bloom"), L("Laternen, Feuer und Fenster leuchten weich nach.", "Lanterns, fires and windows glow softly."),
              m_config.bloom);
        float fov = m_config.fieldOfView;
        if (m_kit.inkSlider("settings/fov", row(L("Sichtfeld", "Field of view"), L("Wie weit die Kamera seitlich blickt.", "How wide the camera sees.")), fov,
                            50.0f, 95.0f, std::format("{:.0f}°", fov))) {
            m_config.fieldOfView = std::round(fov);
            changed = true;
        }
        break;
    }
    case 1: {
        float sens = m_config.mouseSensitivity;
        if (m_kit.inkSlider("settings/sens", row(L("Mausempfindlichkeit", "Mouse sensitivity"), L("Wie schnell sich die Kamera mit der Maus dreht.", "How fast the camera turns with the mouse.")),
                            sens, 0.2f, 3.0f, std::format("{:.1f}×", sens))) {
            m_config.mouseSensitivity = std::round(sens * 10.0f) / 10.0f;
            changed = true;
        }
        onOff("settings/invert", L("Maus umkehren", "Invert mouse"),
              L("Maus nach oben schaut nach unten – wie im Flugsimulator.", "Moving the mouse up looks down – like in a flight simulator."), m_config.invertY);
        inner.takeTop(8.0f);
        r.text(m_fonts.script, {inner.x + 8.0f, inner.y}, L("Tastenbelegung", "Key bindings"), inkRed);
        inner.takeTop(m_fonts.script.lineHeight() + 4.0f);
        const float colW = (inner.w - 24.0f) * 0.5f;
        float y = inner.y;
        for (size_t i = 0; i < std::size(kBindings); ++i) {
            const float x = inner.x + 8.0f + (i % 2 == 0 ? 0.0f : colW + 16.0f);
            const KeyBinding& b = kBindings[i];
            const char* key = english && std::string_view(b.key) == "Maus" ? "Mouse" : english && std::string_view(b.key) == "Linksklick" ? "Left click" : b.key;
            const float kw = std::max(30.0f, m_fonts.smallBold.measure(key) + 14.0f);
            const Rect keyRect{x, y, kw, 22.0f};
            m_kit.inkLine({keyRect.x, keyRect.bottom()}, {keyRect.right(), keyRect.bottom()}, 1.2f, inkFaded, 1.0f, static_cast<uint32_t>(i));
            r.textCentered(m_fonts.smallBold, keyRect, key, inkDark);
            r.text(m_fonts.paperItalic, {x + kw + 10.0f, y + 2.0f}, m_fonts.paperItalic.ellipsize(english ? b.actionEn : b.action, colW - kw - 14.0f),
                   inkSoft);
            if (i % 2 == 1) y += 27.0f;
        }
        break;
    }
    case 2: {
        // Sprache: wie Windows, Deutsch oder Englisch
        int lang = m_config.language.empty() ? 0 : m_config.language == "de" ? 1 : 2;
        if (m_kit.inkChoice("settings/language", row(L("Sprache", "Language"), L("Sprache des Spiels. Mitspieler können eine andere Sprache nutzen.", "The game's language. Other players may use a different one.")),
                            {L("Wie Windows", "Like Windows"), "Deutsch", "English"}, lang)) {
            m_config.language = lang == 0 ? "" : lang == 1 ? "de" : "en";
            changed = true;
        }
        onOff("settings/subtitles", L("Erzähler-Untertitel", "Narrator subtitles"),
              L("Wichtige Ereignisse erscheinen kurz als Untertitel in der Bildmitte.", "Important events briefly appear as subtitles in the middle of the screen."),
              m_config.subtitles);
        onOff("settings/fadechat", L("Chat ausblenden", "Fade chat"),
              L("Der Chat tritt in den Hintergrund, solange niemand etwas sagt.", "The chat fades into the background while nobody is talking."), m_config.fadeChat);
        onOff("settings/fps", L("Bildrate anzeigen", "Show frame rate"), L("Zeigt die aktuelle Bildrate oben links.", "Shows the current frame rate at the top left."),
              m_config.showFps);
        onOff("settings/discord", L("Discord-Status", "Discord status"),
              L("Zeigt in Discord, dass du Werwolf spielst, und erlaubt Freunden, per Einladung beizutreten.",
                "Shows in Discord that you are playing Werewolf and lets friends join via an invite."),
              m_config.discord);
        onOff("settings/upnp", L("Router-Freigabe (UPnP)", "Router port (UPnP)"),
              L("Gibt beim Hosten den Port automatisch im Router frei, damit Freunde über das Internet beitreten können.",
                "Opens the port in your router automatically when hosting, so friends can join over the internet."),
              m_config.upnp);
        break;
    }
    case 3: {
        auto volume = [&](const char* id, const char* label, const char* hint, int& setting) {
            float value = static_cast<float>(setting);
            if (m_kit.inkSlider(id, row(label, hint), value, 0.0f, 100.0f, std::format("{} %", setting))) {
                setting = static_cast<int>(std::round(value));
                changed = true;
            }
        };
        volume("settings/master", L("Gesamtlautstärke", "Master volume"), L("Lautstärke aller Spielgeräusche. Null ist stumm.", "Volume of all game sounds. Zero is silent."),
               m_config.masterVolume);
        volume("settings/effects", L("Effekte", "Effects"), L("Schritte, Türen, Verwandlungen, Spielereignisse und Menüs.", "Footsteps, doors, transformations, game events and menus."),
               m_config.effectsVolume);
        volume("settings/ambience", L("Umgebung", "Ambience"), L("Wind, Vögel, Grillen und das Knistern der Feuer.", "Wind, birds, crickets and the crackling of fires."),
               m_config.ambienceVolume);
        volume("settings/music", L("Musik", "Music"), L("Ruhige Musik am Tag und Horror-Klänge in der Nacht.", "Calm music by day and horror sounds at night."),
               m_config.musicVolume);
        onOff("settings/muted", L("Stummschalten", "Mute"), L("Schaltet alle Spielgeräusche aus, ohne die Regler zu verändern.", "Turns off all game sounds without changing the sliders."),
              m_config.muted);
        onOff("settings/background", L("Im Hintergrund stumm", "Mute in background"),
              L("Blendet den Ton aus, sobald du zu einem anderen Fenster wechselst.", "Fades out the sound as soon as you switch to another window."), m_config.muteUnfocused);
        inner.takeTop(14.0f);
        if (m_kit.inkButton("settings/testsound", inner.takeTop(38.0f), L("Glocke probehören", "Test the bell"), false, m_audioReady)) {
            applyAudioSettings();
            playSound(Sound::Bell, 0.8f);
        }
        if (!m_audioReady) r.text(m_fonts.paperItalic, {inner.x, inner.y + 8.0f}, L("Kein Audiogerät verfügbar.", "No audio device available."), inkRed);
        break;
    }
    case 4: {
        onOff("settings/localai", L("Lokale Bot-KI", "Local bot AI"),
              L("Gilt ab dem nächsten Hosten. Nur der Host braucht das Sprachpaket.", "Applies next time you host. Only the host needs the language pack."),
              m_config.localAI);
        inner.takeTop(12.0f);
        auto paragraph = [&](const char* text) {
            for (const auto& line : m_fonts.paperItalic.wrap(text, inner.w - 16.0f)) {
                r.text(m_fonts.paperItalic, {inner.x + 8.0f, inner.y}, line, inkSoft);
                inner.takeTop(m_fonts.paperItalic.lineHeight());
            }
            inner.takeTop(12.0f);
        };
        paragraph(LocalLanguage::installed() ? L("Sprachpaket installiert. Die KI startet automatisch beim Hosten.",
                                                                  "Language pack installed. AI starts automatically when hosting.")
                                             : L("Sprachpaket noch nicht installiert. Einmaliger Download: etwa 2,6 GB.",
                                                 "Language pack not installed yet. One-time download: about 2.6 GB."));
        paragraph(L("Freie Antworten auf deinen Chat, passend zu Stimmung und Beziehungen. Läuft auf deinem Rechner, ohne Konto oder laufende Kosten.",
                    "Free-form replies to your chat, shaped by mood and relationships. Runs on your computer without an account or ongoing fees."));
        paragraph(L("Wenn die KI nicht verfügbar ist, antworten die Bots wie bisher. Rollen und Spielaktionen bestimmt weiterhin die Spiellogik.",
                    "When AI is unavailable, bots use their usual replies. Roles and game actions remain controlled by the game rules."));
        if (m_kit.inkButton("settings/aipack", inner.takeTop(38.0f), L("KI-Paket auf GitHub", "AI pack on GitHub")))
            ShellExecuteW(nullptr, L"open", L"https://github.com/GG2EZBoOm/werwolf/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    default: break;
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
    if (m_kit.inkButton("settings/back", buttons.takeRight(170.0f), L("Fertig", "Done"), true)) back = true;
    buttons.takeRight(20.0f);
    if (m_kit.inkButton("settings/defaults", {buttons.right() - 200.0f, buttons.y + 4.0f, 200.0f, 38.0f}, L("Wie früher", "Defaults"))) {
        const Config defaults;
        m_config.fullscreen = defaults.fullscreen;
        m_config.vsync = defaults.vsync;
        m_config.antialiasing = defaults.antialiasing;
        m_config.shadowQuality = defaults.shadowQuality;
        m_config.bloom = defaults.bloom;
        m_config.fieldOfView = defaults.fieldOfView;
        m_config.mouseSensitivity = defaults.mouseSensitivity;
        m_config.invertY = defaults.invertY;
        m_config.fadeChat = defaults.fadeChat;
        m_config.subtitles = defaults.subtitles;
        m_config.showFps = defaults.showFps;
        m_config.discord = defaults.discord;
        m_config.upnp = defaults.upnp;
        m_config.localAI = defaults.localAI;
        m_config.masterVolume = defaults.masterVolume;
        m_config.effectsVolume = defaults.effectsVolume;
        m_config.ambienceVolume = defaults.ambienceVolume;
        m_config.musicVolume = defaults.musicVolume;
        m_config.muted = defaults.muted;
        m_config.muteUnfocused = defaults.muteUnfocused;
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
