#include "client/App.h"

#include "client/Theme.h"

#include "engine/core/Image.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/core/Time.h"
#include "engine/core/Utf8.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>

namespace ww {

using eng::Color;
using eng::Rect;

#ifndef WERWOLF_VERSION
#define WERWOLF_VERSION "0.0.0"
#endif
#ifndef WERWOLF_UPDATE_URL
#define WERWOLF_UPDATE_URL ""
#endif

namespace {
constexpr uint32_t kBackdropSeed = 1337u;
constexpr const char* kDiscordAppId = "1564687671161524365";

// Läuft das Spiel direkt aus einem CMake-Build-Ordner (Entwicklung)?
bool isDeveloperBuild(const std::filesystem::path& exeDir) {
    std::error_code ec;
    return std::filesystem::exists(exeDir.parent_path() / "CMakeCache.txt", ec);
}

std::string executablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return eng::utf8::fromWide(buffer);
}
} // namespace

void App::applyLanguage() {
    Lang lang = systemLanguage();
    if (!m_config.language.empty()) parseLanguage(m_config.language, lang);
    if (!m_dev.language.empty()) parseLanguage(m_dev.language, lang); // --lang gilt nur für diesen Start
    if (lang == language() && m_languageApplied) return;
    setLanguage(lang);
    m_languageApplied = true;
    // Chatverlauf neu umbrechen
    for (LogEntry& e : m_net.log()) e.wrapWidth = -1.0f;
}

int App::run() {
    if (!init()) return 1;
    // Während das Fenster gezogen oder vergrößert wird, weiterzeichnen
    m_window.onModalFrame = [this] { frame(); };
    while (!m_quit && m_window.processEvents()) frame();
    shutdown();
    return 0;
}

bool App::init() {
    eng::log::setOutputFile(eng::paths::userDataDir("Werwolf") / "werwolf.log");
    eng::log::info("Werwolf {} startet", WERWOLF_VERSION);
    update::cleanupOldFiles(eng::paths::executableDir());
    m_config.load();
    applyLanguage();
    if (m_config.name.empty()) m_config.name = std::string(L("Gast ", "Guest ")) + std::to_string(eng::Rng(eng::randomSeed()).range(10, 99));
    m_nameInput = m_config.name;
    m_addressInput = m_config.address;
    m_portInput = std::to_string(m_config.port);
    parseCommandLine();

    std::string error;
    if (!m_window.create(L("Werwolf", "Werewolf"), 1280, 800, &error)) {
        MessageBoxW(nullptr, eng::utf8::toWide(L("Das Fenster konnte nicht erstellt werden:\n", "The window could not be created:\n") + error).c_str(),
                    L"Werwolf", MB_ICONERROR);
        return false;
    }
    if (!m_renderer.init() || !m_renderer3d.init()) {
        MessageBoxW(nullptr, eng::utf8::toWide(L("Der Renderer konnte nicht initialisiert werden.", "The renderer could not be initialised.")).c_str(),
                    L"Werwolf", MB_ICONERROR);
        return false;
    }
    m_characters.init();
    m_kit.init();
    ensureWorld(kBackdropSeed);
    loadFonts(uiScale());
    initAudio();
    applySettings();
    m_menuEnteredAt = eng::nowSeconds();
    eng::net::initialize();
    m_lastFrame = eng::nowSeconds();
    m_startTime = m_lastFrame;
    m_lastShot = m_lastFrame;
    if (!m_dev.updatedTo.empty())
        m_menuNotice = Lf("Das Spiel wurde auf Version {} aktualisiert.", "The game has been updated to version {}.", m_dev.updatedTo);
    // Discord darf das Spiel für Einladungen starten
    m_discord.init(kDiscordAppId, "\"" + executablePath() + "\" --discord");
    m_discord.setEnabled(m_config.discord);

    if (m_dev.preview) m_screen = Screen::Preview;
    else if (m_dev.sandbox) enterSandbox();
    else startUpdateCheck();
    return true;
}

const char* App::versionString() { return WERWOLF_VERSION; }

void App::startUpdateCheck() {
    const bool devAutomation = m_dev.autoHost || !m_dev.join.empty() || m_dev.autoPlay;
    std::string url = !m_dev.updateUrl.empty() ? m_dev.updateUrl : !m_config.updateUrl.empty() ? m_config.updateUrl : WERWOLF_UPDATE_URL;
    const std::filesystem::path exeDir = eng::paths::executableDir();
    if (m_dev.noUpdate || devAutomation || url.empty()) {
        eng::log::info("Update-Prüfung übersprungen");
        return;
    }
    if (!m_dev.updatedTo.empty()) {
        // gerade erst aktualisiert: nicht erneut prüfen (verhindert Endlosschleifen bei falscher Versionsnummer)
        if (m_dev.updatedTo != WERWOLF_VERSION)
            eng::log::warn("Update meldete Version {}, das Spiel meldet {} – bitte Versionsnummer im Release prüfen",
                           m_dev.updatedTo, WERWOLF_VERSION);
        return;
    }
    if (isDeveloperBuild(exeDir) && !m_dev.forceUpdate) {
        eng::log::info("Entwickler-Build erkannt – keine automatischen Updates (--force-update erzwingt sie)");
        return;
    }
    update::Version current;
    update::Version::parse(WERWOLF_VERSION, current);
    m_updater.start(url, current, exeDir);
    m_updateShownSince = eng::nowSeconds();
    m_updateDoneAt = -1.0;
    m_screen = Screen::Updating;
}

void App::drawUpdater() {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width(), H = r.height();
    const Updater::State state = m_updater.state();

    // Abschluss: installieren, neu starten oder weiter zum Menü
    if (state == Updater::State::ReadyToInstall) {
        std::string error;
        if (m_updater.install(error)) {
            const std::string version = m_updater.newVersion();
            if (restartApplication("--updated " + version)) {
                m_quit = true;
            } else {
                m_menuNotice = Lf("Version {} ist installiert – bitte starte das Spiel neu.", "Version {} is installed – please restart the game.", version);
                m_screen = Screen::Menu;
            }
            return;
        }
    }
    if (m_updater.finished() && m_updateDoneAt < 0.0) m_updateDoneAt = m_time;
    if (m_updateDoneAt >= 0.0) {
        const double hold = state == Updater::State::Failed ? 2.2 : 0.5;
        if (m_time - m_updateDoneAt > hold) {
            if (state == Updater::State::Failed) m_menuNotice = m_updater.status();
            m_screen = Screen::Menu;
            return;
        }
    }

    r.rect({0, 0, W, H}, Color::hex(0x02030A, 0.45f));
    m_kit.vignette(1.0f);
    m_kit.spacedCentered(m_fonts.title, {0, H * 0.18f, W, 92.0f}, L("WERWOLF", "WEREWOLF"), 10.0f, palette::brassLight);

    const std::string notes = m_updater.notes();
    const bool downloading = state == Updater::State::Downloading || state == Updater::State::ReadyToInstall ||
                             state == Updater::State::Installed;
    const std::vector<std::string> noteLines = downloading && !notes.empty() ? m_fonts.small.wrap(notes, 400.0f) : std::vector<std::string>{};
    const size_t shownNotes = std::min<size_t>(noteLines.size(), 6);
    const float pw = 460.0f;
    const float ph = 150.0f + static_cast<float>(shownNotes) * m_fonts.small.lineHeight() + (shownNotes ? 16.0f : 0.0f);
    const Rect panel{std::round(W * 0.5f - pw * 0.5f), std::round(H * 0.42f), pw, ph};
    drawPanel(panel);

    const std::string title = downloading ? Lf("Update auf Version {}", "Updating to version {}", m_updater.newVersion()) : std::string("Update");
    r.textCentered(m_fonts.large, {panel.x, panel.y + 18.0f, panel.w, 28.0f}, title, theme::text);
    const Color statusColor = state == Updater::State::Failed ? Color::hex(0xF0A35E) : theme::muted;
    r.textCentered(m_fonts.small, {panel.x + 16.0f, panel.y + 52.0f, panel.w - 32.0f, 20.0f},
                   m_fonts.small.ellipsize(m_updater.status(), panel.w - 40.0f), statusColor);

    // Fortschritt (oder pulsierender Balken während der Prüfung)
    const Rect bar{panel.x + 30.0f, panel.y + 84.0f, panel.w - 60.0f, 8.0f};
    r.roundRect(bar, 4.0f, Color::hex(0xFFFFFF, 0.08f));
    if (state == Updater::State::Checking) {
        const float t = static_cast<float>(std::fmod(m_time * 0.8, 1.0));
        const float w = bar.w * 0.25f;
        r.roundRect({bar.x + (bar.w - w) * t, bar.y, w, bar.h}, 4.0f, theme::gold.withAlpha(0.8f));
    } else {
        const float p = state == Updater::State::Downloading ? m_updater.progress() : (downloading || state == Updater::State::UpToDate ? 1.0f : 0.0f);
        r.roundRect({bar.x, bar.y, bar.w * eng::clamp01(p), bar.h}, 4.0f, theme::gold);
        if (state == Updater::State::Downloading)
            r.textRight(m_fonts.smallBold, {bar.right(), bar.y + 12.0f}, std::format("{:.0f} %", p * 100.0f), theme::text);
    }

    float y = panel.y + 112.0f;
    for (size_t i = 0; i < shownNotes; ++i) {
        r.text(m_fonts.small, {panel.x + 30.0f, y}, noteLines[i], theme::text);
        y += m_fonts.small.lineHeight();
    }

    const bool skippable = (state == Updater::State::Checking || state == Updater::State::Downloading) &&
                           m_time - m_updateShownSince > 1.5;
    if (skippable && m_ui.button("update/skip", {panel.center().x - 80.0f, panel.bottom() - 46.0f, 160.0f, 34.0f}, L("Überspringen", "Skip"),
                                 eng::ui::ButtonKind::Ghost)) {
        m_updater.cancel();
        m_menuNotice = L("Update übersprungen – mit einer anderen Version kannst du evtl. nicht mitspielen.",
                         "Update skipped – you may not be able to play with people on a different version.");
        m_screen = Screen::Menu;
    }
    r.textRight(m_fonts.small, {W - 16.0f, H - 26.0f}, std::string("Version ") + WERWOLF_VERSION, theme::faint);
}

void App::parseCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    auto arg = [&](int i) { return eng::utf8::fromWide(argv[i]); };
    for (int i = 1; i < argc; ++i) {
        const std::string a = arg(i);
        const bool hasValue = i + 1 < argc;
        if (a == "--host") m_dev.autoHost = true;
        else if (a == "--join" && hasValue) m_dev.join = arg(++i);
        else if (a == "--name" && hasValue) m_nameInput = arg(++i);
        else if (a == "--port" && hasValue) m_portInput = arg(++i);
        else if (a == "--bots" && hasValue) m_dev.bots = std::atoi(arg(++i).c_str());
        else if (a == "--start") m_dev.autoStart = true;
        else if (a == "--autoplay") m_dev.autoPlay = true;
        else if (a == "--preview") m_dev.preview = true;
        else if (a == "--sandbox") m_dev.sandbox = true;
        else if (a == "--indoors") m_dev.indoors = true;
        else if (a == "--chapel") m_dev.chapel = 1;
        else if (a == "--chapel-outside") m_dev.chapel = 2;
        else if (a == "--journal") m_dev.journal = true;
        else if (a == "--pause") m_dev.pause = true;
        else if (a == "--page" && hasValue) {
            const std::string page = arg(++i);
            m_menuPage = page == "host" ? MenuPage::Host : page == "join" ? MenuPage::Join : page == "settings" ? MenuPage::Settings : MenuPage::None;
        }
        else if (a == "--wolf") m_playerWolfTarget = true;
        else if (a == "--orbit") m_dev.cameraOrbit = "orbit";
        else if (a == "--camera" && hasValue) m_dev.previewCamera = std::atoi(arg(++i).c_str());
        else if (a == "--tod" && hasValue) m_dev.timeOfDay = static_cast<float>(std::atof(arg(++i).c_str()));
        else if (a == "--timescale" && hasValue) m_dev.timeScale = static_cast<float>(std::atof(arg(++i).c_str()));
        else if (a == "--screenshots" && hasValue) m_dev.shotDir = std::filesystem::path(argv[++i]);
        else if (a == "--shot-every" && hasValue) m_dev.shotInterval = std::atof(arg(++i).c_str());
        else if (a == "--quit-after" && hasValue) m_dev.quitAfter = std::atof(arg(++i).c_str());
        else if (a == "--no-update") m_dev.noUpdate = true;
        else if (a == "--force-update") m_dev.forceUpdate = true;
        else if (a == "--update-url" && hasValue) m_dev.updateUrl = arg(++i);
        else if (a == "--updated" && hasValue) m_dev.updatedTo = arg(++i);
        else if (a == "--discord") m_dev.fromDiscord = true;
        else if (a == "--lang" && hasValue) {
            m_dev.language = arg(++i);
            applyLanguage();
        }
        else if (a.rfind("discord-", 0) == 0) m_dev.fromDiscord = true; // von Discord über das Protokoll gestartet
        else eng::log::warn("Unbekannte Option: {}", a);
    }
    LocalFree(argv);
}

void App::runDevAutomation() {
    if (m_screen == Screen::Menu && !m_devConnectTried) {
        m_devConnectTried = true;
        if (m_dev.autoHost) {
            hostGame();
        } else if (!m_dev.join.empty()) {
            m_addressInput = m_dev.join;
            joinGame();
        }
    }
    if (m_screen == Screen::Session && m_net.hasView() && m_net.isHost() && m_net.view().phase == Phase::Lobby) {
        if (!m_devBotsSent && m_dev.bots > 0) {
            for (int i = 0; i < m_dev.bots; ++i) m_net.sendAddBot();
            m_devBotsSent = true;
        }
        if (m_dev.autoStart && !m_devStarted && static_cast<int>(m_net.view().players.size()) > m_dev.bots) {
            m_net.sendStartGame();
            m_devStarted = true;
        }
    }
    // Autopilot: bereite Aktionen gelegentlich auslösen
    if (m_dev.autoPlay && m_inMatch && m_time > m_devNextAction) {
        m_devNextAction = m_time + 1.2;
        for (const Prompt& p : m_prompts) {
            if (!p.ready || !m_devRng.chance(0.5f)) continue;
            m_net.sendInteract(p.kind, p.kind == Interaction::Transform ? p.param : p.target);
            break;
        }
    }
    if (m_dev.quitAfter > 0.0 && m_time - m_startTime > m_dev.quitAfter) m_quit = true;
}

void App::ensureWorld(uint32_t seed) {
    if (m_worldReady && m_world.seed() == seed) return;
    const double start = eng::nowSeconds();
    m_world.generate(seed);
    m_worldRenderer.build(m_world);
    m_worldReady = true;
    eng::log::info("Welt erzeugt (Seed {}): {} Häuser, {} Bäume, {} Objekte in {:.0f} ms", seed, m_world.houses().size(),
                   m_world.trees().size(), m_world.props().size(), (eng::nowSeconds() - start) * 1000.0);
}

void App::renderBackdrop3D() {
    ensureWorld(m_inMatch ? m_world.seed() : kBackdropSeed);
    const float t = static_cast<float>(m_time - m_startTime);
    const float a = t * 0.025f + 2.2f;
    // Je Untermenü eine eigene Einstellung, zwischen denen die Kamera weich fährt
    eng::Vec3 wantPos{std::cos(a) * 38.0f, 9.5f + std::sin(t * 0.05f) * 1.5f, std::sin(a) * 38.0f};
    eng::Vec3 wantTarget{0.0f, 2.5f, 0.0f};
    if (m_screen == Screen::Menu) {
        switch (m_menuPage) {
        case MenuPage::Host: // dicht am Lagerfeuer
            wantPos = {5.5f + std::sin(t * 0.2f) * 0.4f, 1.9f, 8.5f};
            wantTarget = {-1.0f, 1.2f, 0.0f};
            break;
        case MenuPage::Join: // der Weg ins Dorf
            wantPos = {-1.0f + std::sin(t * 0.15f) * 0.5f, 2.4f, 36.0f};
            wantTarget = {1.0f, 2.2f, 4.0f};
            break;
        case MenuPage::Settings: // hoch über den Dächern
            wantPos = {-26.0f, 24.0f + std::sin(t * 0.1f), 30.0f};
            wantTarget = {4.0f, 0.0f, -4.0f};
            break;
        default: break;
        }
        // Leichte Parallaxe mit der Maus
        const eng::Vec2 m = m_window.input().mouse;
        const float mx = std::clamp(m.x / std::max(1.0f, m_renderer.width()) - 0.5f, -0.5f, 0.5f);
        const float my = std::clamp(m.y / std::max(1.0f, m_renderer.height()) - 0.5f, -0.5f, 0.5f);
        wantTarget = wantTarget + eng::Vec3{mx * 2.4f, -my * 1.2f, 0.0f};
    }
    const float follow = 1.0f - std::exp(-m_dt * 1.4f);
    m_menuCamPos = m_menuCamPos + (wantPos - m_menuCamPos) * follow;
    m_menuCamTarget = m_menuCamTarget + (wantTarget - m_menuCamTarget) * follow;
    eng::Camera cam;
    cam.position = m_menuCamPos;
    cam.target = m_menuCamTarget;
    eng::Environment env = m_worldRenderer.environment(0.93f, cam.target, t);
    applyGraphics(env);
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), cam, env);
    m_worldRenderer.setHiddenRoof(-1);
    m_worldRenderer.draw(m_renderer3d, env, t);
    m_renderer3d.endFrame();
}

void App::renderPreview3D() {
    const float t = static_cast<float>(m_time - m_startTime);
    eng::Input& in = m_window.input();
    if (in.pressed(eng::Key::Num1)) m_dev.previewCamera = 0;
    if (in.pressed(eng::Key::Num2)) m_dev.previewCamera = 1;
    if (in.pressed(eng::Key::Num3)) m_dev.previewCamera = 2;
    if (in.pressed(eng::Key::Num4)) m_dev.previewCamera = 3;
    if (m_dev.timeOfDay >= 0.0f) {
        m_previewTimeOfDay = m_dev.timeOfDay;
        if (in.down(eng::Key::Right)) m_dev.timeOfDay = std::fmod(m_dev.timeOfDay + m_dt * 0.08f, 1.0f);
        if (in.down(eng::Key::Left)) m_dev.timeOfDay = std::fmod(m_dev.timeOfDay - m_dt * 0.08f + 1.0f, 1.0f);
    } else {
        m_previewTimeOfDay = std::fmod(0.3f + t * 0.012f, 1.0f);
    }

    eng::Camera cam;
    switch (m_dev.previewCamera) {
    case 1:
        cam.position = m_world.ground(13.0f, 9.0f) + eng::Vec3{0, 1.8f, 0};
        cam.target = {0.0f, 1.6f, -2.0f};
        break;
    case 2:
        cam.position = {-46.0f, 22.0f, 52.0f};
        cam.target = {0.0f, 0.0f, -4.0f};
        break;
    case 3:
        cam.position = m_world.ground(9.0f, -34.0f) + eng::Vec3{0, 2.2f, 0};
        cam.target = m_world.chapel() + eng::Vec3{0, 6.0f, 0};
        break;
    default: {
        const float a = t * 0.06f + 0.6f;
        cam.position = {std::cos(a) * 34.0f, 10.0f, std::sin(a) * 34.0f};
        cam.target = {0.0f, 2.0f, 0.0f};
        break;
    }
    }
    eng::Environment env = m_worldRenderer.environment(m_previewTimeOfDay, cam.target, t);
    applyGraphics(env);
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), cam, env);
    m_worldRenderer.setHiddenRoof(-1);
    m_worldRenderer.draw(m_renderer3d, env, t);
    m_renderer3d.endFrame();
}

void App::drawPreviewOverlay() {
    const int hours = static_cast<int>(m_previewTimeOfDay * 24.0f);
    const int minutes = static_cast<int>(std::fmod(m_previewTimeOfDay * 24.0f * 60.0f, 60.0f));
    const std::string info = Lf("{:02}:{:02} Uhr · {} Draw-Calls · {} Dreiecke · {:.0f} FPS", "{:02}:{:02} · {} draw calls · {} triangles · {:.0f} FPS",
                                hours, minutes, m_renderer3d.drawCalls(), m_renderer3d.triangles(), 1.0f / std::max(m_dt, 0.001f));
    m_renderer.text(m_fonts.small, {14.0f, 12.0f}, info, eng::Color::hex(0xFFFFFF, 0.7f));
    if (m_window.input().pressed(eng::Key::Escape)) m_screen = Screen::Menu;
}

void App::takeScreenshot(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::vector<uint8_t> pixels = m_renderer.readPixels();
    if (eng::writePng(path, m_renderer.framebufferWidth(), m_renderer.framebufferHeight(), pixels))
        eng::log::info("Screenshot gespeichert: {}", eng::utf8::fromWide(path.wstring()));
    else
        eng::log::warn("Screenshot konnte nicht gespeichert werden");
}

void App::shutdown() {
    m_ui.onActivate = {};
    m_audioDevice.stop();
    m_updater.cancel();
    m_net.disconnect();
    m_server.stop();
    m_hostNet.stop();
    m_discord.shutdown();
    m_config.name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    m_config.save();
    m_kit.shutdown();
    m_renderer3d.shutdown();
    m_renderer.shutdown();
    m_window.destroy();
}

void App::loadFonts(float scale) {
    auto load = [scale](eng::Font& font, const char* family, float size, int weight, bool italic = false) {
        eng::FontDesc d;
        d.family = family;
        d.size = size;
        d.weight = weight;
        d.italic = italic;
        font.load(d, scale);
    };
    load(m_fonts.ui, "Segoe UI", 16, 400);
    load(m_fonts.uiBold, "Segoe UI Semibold", 16, 400);
    load(m_fonts.small, "Segoe UI", 13, 400);
    load(m_fonts.smallBold, "Segoe UI Semibold", 13, 400);
    load(m_fonts.large, "Segoe UI Semibold", 22, 400);
    load(m_fonts.title, "Georgia", 76, 700);
    load(m_fonts.heading, "Georgia", 30, 400);
    load(m_fonts.narration, "Georgia", 15, 400, true);
    load(m_fonts.icon, "Segoe UI Symbol", 15, 400);
    load(m_fonts.iconLarge, "Segoe UI Symbol", 40, 400);
    load(m_fonts.iconHuge, "Segoe UI Symbol", 92, 400);
    load(m_fonts.display, "Georgia", 44, 700);
    load(m_fonts.menu, "Georgia", 23, 400);
    load(m_fonts.label, "Segoe UI Semibold", 11, 400);
    load(m_fonts.subtitle, "Georgia", 19, 400, true);
    load(m_fonts.script, "Segoe Script", 17, 400);
    load(m_fonts.scriptBold, "Segoe Script", 21, 700);
    load(m_fonts.scriptLarge, "Segoe Script", 30, 700);
    load(m_fonts.hand, "Ink Free", 19, 400);
    load(m_fonts.handLarge, "Ink Free", 28, 400);
    load(m_fonts.handSmall, "Ink Free", 16, 400);
    load(m_fonts.calligraphy, "Gabriola", 124, 400);
    load(m_fonts.calligraphyMid, "Gabriola", 58, 400);
    load(m_fonts.paper, "Georgia", 16, 400);
    load(m_fonts.paperItalic, "Georgia", 15, 400, true);
    m_fontScale = scale;

    for (LogEntry& e : m_net.log()) e.wrapWidth = -1.0f;
    eng::ui::Theme& t = m_ui.theme();
    t.font = &m_fonts.ui;
    t.fontSmall = &m_fonts.small;
    // Bedienelemente der Engine an die Farbwelt des Spiels anpassen
    t.text = palette::parchment;
    t.textMuted = palette::ash;
    t.panel = Color::hex(0x0B0C11, 0.9f);
    t.panelBorder = palette::brass.withAlpha(0.2f);
    t.button = Color::hex(0x16181F);
    t.buttonHover = Color::hex(0x22252E);
    t.buttonPressed = Color::hex(0x101217);
    t.primary = Color::hex(0xB8914A);
    t.primaryHover = Color::hex(0xD2AE62);
    t.primaryText = Color::hex(0x1A1308);
    t.danger = Color::hex(0x9C3A30);
    t.dangerHover = Color::hex(0xB8473B);
    t.inputBg = Color::hex(0x06070A, 0.85f);
    t.inputBorder = palette::brass.withAlpha(0.28f);
    t.focus = palette::brassLight;
    t.disabled = Color::hex(0x1C1D21);
    t.disabledText = palette::smoke;
    t.radius = 3.0f;
}

float App::uiScale() const {
    // Gestaltet für 820 logische Pixel Höhe; größere Fenster vergrößern die Oberfläche
    const float dpi = m_window.dpiScale();
    const float logicalH = static_cast<float>(m_window.framebufferHeight()) / dpi;
    return dpi * std::clamp(logicalH / 820.0f, 1.0f, 2.4f);
}

void App::frame() {
    const double now = eng::nowSeconds();
    m_dt = static_cast<float>(std::clamp(now - m_lastFrame, 0.0, 0.1));
    m_lastFrame = now;
    m_time = now;
    applyAudioSettings();

    // Die Oberfläche wächst mit der Fenstergröße; Schriften werden passend neu gerastert
    const float ui = uiScale();
    if (std::fabs(ui - m_fontScale) > 0.01f) loadFonts(ui);
    m_net.update(now);
    updateSession();
    updateDiscord(now);
    runDevAutomation();

    eng::Input& in = m_window.input();
    const eng::Vec2 rawMouse = in.mouse;
    in.mouse = rawMouse * (m_window.dpiScale() / ui); // in Oberflächen-Pixel umrechnen
    if (in.pressed(eng::Key::F11)) {
        m_window.toggleFullscreen();
        m_config.fullscreen = m_window.isFullscreen();
        m_config.save();
    }

    if (m_window.isMinimized()) {
        m_soundEvents.reset();
        in.mouse = rawMouse;
        in.endFrame();
        eng::sleepSeconds(0.02);
        return;
    }

    const bool match = m_screen == Screen::Session && m_inMatch;
    if (m_screen != Screen::Sandbox && !match && m_window.isMouseCaptured()) m_window.setMouseCaptured(false);

    // 3D-Szene
    switch (m_screen) {
    case Screen::Preview: renderPreview3D(); break;
    case Screen::Sandbox:
        updateSandbox();
        renderSandbox3D();
        break;
    default:
        if (match) {
            updateMatch();
            renderMatch3D();
        } else {
            renderBackdrop3D();
        }
        break;
    }

    updateAudio();

    // 2D-Oberfläche
    m_renderer.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), ui);
    m_ui.beginFrame(m_window, m_renderer, now);
    m_kit.beginFrame(m_ui, m_renderer, m_fonts, now, m_dt);
    switch (m_screen) {
    case Screen::Updating: drawUpdater(); break;
    case Screen::Preview: drawPreviewOverlay(); break;
    case Screen::Sandbox: drawSandboxOverlay(); break;
    case Screen::Menu: drawMenu(); break;
    case Screen::Connecting: drawConnecting(); break;
    case Screen::Lost: drawLost(); break;
    case Screen::Session:
        if (!m_net.hasView()) drawConnecting();
        else if (match) drawMatchHud();
        else drawLobby();
        break;
    }
    // Funken, Bildschirmübergang und eigener Zeiger liegen über allem
    m_kit.drawBursts();
    const bool matchNow = m_screen == Screen::Session && m_inMatch;
    if (m_screen != m_transitionScreen || matchNow != m_transitionMatch) {
        if (m_transitionScreen != Screen::Updating || m_screen != Screen::Menu) m_transitionStart = now;
        m_transitionScreen = m_screen;
        m_transitionMatch = matchNow;
    }
    m_kit.clawWipe(static_cast<float>((now - m_transitionStart) / 1.1));
    m_ui.setCustomCursor(true);
    if (!m_window.isMouseCaptured() && m_window.hasFocus()) m_kit.cursor(in.mouse, m_ui.cursor());
    m_ui.endFrame();
    m_renderer.endFrame();

    if (in.pressed(eng::Key::F12)) {
        const auto dir = eng::paths::userDataDir("Werwolf") / "screenshots";
        takeScreenshot(dir / ("werwolf_" + std::to_string(static_cast<long long>(now * 1000.0)) + ".png"));
    }
    if (!m_dev.shotDir.empty() && m_dev.shotInterval > 0.0 && now - m_lastShot >= m_dev.shotInterval) {
        m_lastShot = now;
        char name[32];
        std::snprintf(name, sizeof name, "shot_%03d.png", m_shotIndex++);
        takeScreenshot(m_dev.shotDir / name);
    }

    m_window.swapBuffers();
    in.mouse = rawMouse;
    in.endFrame();
}

void App::updateSession() {
    const NetClient::Status status = m_net.status();
    std::vector<LogEntry> fresh = m_net.takeNewEntries();

    if (m_screen == Screen::Connecting) {
        if (status == NetClient::Status::Connected && m_net.hasView()) {
            m_screen = Screen::Session;
            m_chatScroll = 0.0f;
            m_lastPhase = Phase::Lobby;
            m_lastDay = -1;
        } else if (status == NetClient::Status::Failed) {
            m_menuError = m_net.error();
            m_screen = Screen::Menu;
            if (m_hosting) {
                m_server.stop();
                m_hosting = false;
            }
        }
        return;
    }
    if (m_screen != Screen::Session) return;
    if (status == NetClient::Status::Failed) {
        m_screen = Screen::Lost;
        m_inMatch = false;
        return;
    }

    for (const LogEntry& e : fresh) {
        if (m_time - m_lastNoticeSound > 0.3) {
            if (e.isChat && e.from != m_net.myId()) {
                playSound(Sound::Chat, 0.38f);
                m_lastNoticeSound = m_time;
            } else if (!e.isChat && (e.kind == NoticeKind::Warning || e.kind == NoticeKind::Private)) {
                playSound(e.kind == NoticeKind::Warning ? Sound::Warning : Sound::Magic, 0.45f);
                m_lastNoticeSound = m_time;
            }
        }
        if (e.isChat && e.from != kNoPlayer) m_bubbles[e.from] = Bubble{e.text, m_time, e.channel};
        if (!e.isChat && e.kind == NoticeKind::Narration) {
            m_subtitle = e.text;
            m_subtitleTime = m_time;
        }
        m_lastChatActivity = m_time;
    }

    if (!m_net.hasView()) return;
    const GameView& v = m_net.view();
    if (v.phase != Phase::Lobby && !m_inMatch) enterMatch();
    if (v.phase == Phase::Lobby && m_inMatch) {
        m_inMatch = false;
        m_window.setMouseCaptured(false);
    }
    if (v.phase != m_lastPhase || v.day != m_lastDay) {
        onPhaseChanged(v);
        m_lastPhase = v.phase;
        m_lastDay = v.day;
    }
}

void App::onPhaseChanged(const GameView& v) {
    switch (v.phase) {
    case Phase::Assembly: playSound(Sound::Bell, 0.8f); break;
    case Phase::Vote: playSound(Sound::Vote, 0.75f); break;
    case Phase::Execution: playSound(Sound::Death, 0.65f); break;
    case Phase::Night: if (!m_music[2]) playSound(Sound::Night, 0.7f); break;
    case Phase::Morning: playSound(Sound::Dawn, 0.65f); break;
    case Phase::GameOver: {
        const PlayerView* me = v.me();
        playSound(me && me->winner ? Sound::Victory : Sound::Night, 0.75f);
        break;
    }
    default: break;
    }
    if (v.phase == Phase::Lobby || v.phase == Phase::GameOver) {
        m_bannerTime = -100.0;
        if (v.phase == Phase::GameOver) m_window.setMouseCaptured(false);
        return;
    }
    m_bannerTitle = headerTitle(v);
    switch (v.phase) {
    case Phase::Day:
        m_bannerSubtitle = v.day <= 1 ? L("Erkundet das Dorf.", "Explore the village.") : L("Sucht nach Spuren – wer war es?", "Look for clues – who was it?");
        break;
    case Phase::Assembly: m_bannerSubtitle = L("Das Dorf versammelt sich am Feuer.", "The village gathers at the fire."); break;
    case Phase::Vote: m_bannerSubtitle = L("Schaut den Schuldigen an und drückt E.", "Look at the guilty one and press E."); break;
    case Phase::Execution: m_bannerSubtitle = L("Das Dorf hat entschieden.", "The village has decided."); break;
    case Phase::Night: m_bannerSubtitle = L("Bleibt im Licht …", "Stay in the light …"); break;
    case Phase::Morning: m_bannerSubtitle = L("Wer hat die Nacht überlebt?", "Who survived the night?"); break;
    default: m_bannerSubtitle.clear(); break;
    }
    m_bannerTime = m_time;
}

// --- Aktionen ----------------------------------------------------------------

namespace {
bool parsePort(const std::string& text, uint16_t& port) {
    if (text.empty() || text.size() > 5) return false;
    for (char c : text)
        if (c < '0' || c > '9') return false;
    const int p = std::atoi(text.c_str());
    if (p <= 0 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
    return true;
}
} // namespace

void App::hostGame() {
    const std::string name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    uint16_t port = 0;
    if (name.empty()) {
        m_menuError = L("Bitte gib zuerst einen Namen ein.", "Please enter a name first.");
        return;
    }
    if (!parsePort(m_portInput, port)) {
        m_menuError = L("Der Port muss eine Zahl zwischen 1 und 65535 sein.", "The port must be a number between 1 and 65535.");
        return;
    }
    m_nameInput = name;
    m_config.name = name;
    m_config.port = port;
    m_config.save();

    ServerOptions options;
    options.port = port;
    options.timeScale = m_dev.timeScale;
    std::string error;
    if (!m_server.start(options, error)) {
        m_menuError = Lf("Der Server konnte nicht gestartet werden: {}", "The server could not be started: {}", netErrorText(error).str());
        return;
    }
    m_hosting = true;
    // Adressen ermitteln und den Port im Router freigeben (im Hintergrund)
    m_hostNet.start(port, m_config.upnp && !m_dev.autoHost);
    m_partyId = std::format("ww-{:016x}", eng::Rng(eng::randomSeed()).next64());
    m_sessionStart = static_cast<int64_t>(std::time(nullptr));
    m_discordSecret.clear();
    m_net.clearLog();
    m_net.connect("127.0.0.1", port, name);
    m_menuError.clear();
    m_screen = Screen::Connecting;
}

void App::joinGame() {
    const std::string name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    const std::string address = eng::utf8::sanitize(m_addressInput, 128);
    uint16_t port = 0;
    if (name.empty()) {
        m_menuError = L("Bitte gib zuerst einen Namen ein.", "Please enter a name first.");
        return;
    }
    if (address.empty()) {
        m_menuError = L("Bitte gib die Adresse des Servers ein.", "Please enter the server's address.");
        return;
    }
    if (!parsePort(m_portInput, port)) {
        m_menuError = L("Der Port muss eine Zahl zwischen 1 und 65535 sein.", "The port must be a number between 1 and 65535.");
        return;
    }
    // Die Adresse darf den Port enthalten: 1.2.3.4:27777 oder [2001:db8::1]:27777
    std::string host;
    if (!eng::net::parseEndpoint(address, host, port)) {
        m_menuError = L("Die Adresse ist ungültig. Beispiele: 192.168.178.20, 84.12.3.4:27777 oder [2001:db8::1]:27777",
                        "The address is invalid. Examples: 192.168.178.20, 84.12.3.4:27777 or [2001:db8::1]:27777");
        return;
    }
    m_nameInput = name;
    m_config.name = name;
    m_config.address = address;
    m_config.port = port;
    m_config.save();
    connectTo({host}, port, name);
}

void App::connectTo(const std::vector<std::string>& hosts, uint16_t port, const std::string& name) {
    m_hosting = false;
    m_net.clearLog();
    m_menuError.clear();
    m_sessionStart = static_cast<int64_t>(std::time(nullptr));
    if (!m_net.connect(hosts, port, name)) {
        m_menuError = m_net.error();
        return;
    }
    m_screen = Screen::Connecting;
}

void App::joinFromSecret(const std::string& secret) {
    std::string party;
    uint16_t port = 0;
    std::vector<std::string> hosts;
    if (!Discord::parseSecret(secret, party, port, hosts)) {
        m_menuError = L("Die Discord-Einladung ist ungültig.", "The Discord invite is invalid.");
        return;
    }
    if (m_screen == Screen::Session || m_screen == Screen::Connecting) leaveSession();
    const std::string name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    m_partyId = party;
    m_discordSecret = secret;
    eng::log::info("Trete über Discord bei: {} Adressen, Port {}", hosts.size(), port);
    connectTo(hosts, port, name.empty() ? std::string(L("Gast", "Guest")) : name);
}

void App::reconnect() {
    const std::vector<std::string> hosts = m_net.hosts();
    const uint16_t port = m_net.port();
    const std::string name = m_net.name();
    const uint64_t token = m_net.token();
    m_net.clearLog();
    m_inMatch = false;
    m_net.connect(hosts, port, name, token);
    m_screen = Screen::Connecting;
}

void App::leaveSession() {
    m_net.disconnect();
    if (m_hosting) {
        m_server.stop();
        m_hosting = false;
    }
    m_hostNet.stop();
    m_partyId.clear();
    m_discordSecret.clear();
    m_joinRequests.clear();
    m_inMatch = false;
    m_bubbles.clear();
    m_chatInput.clear();
    m_window.setMouseCaptured(false);
    m_screen = Screen::Menu;
    m_menuPage = MenuPage::None;
    m_menuEnteredAt = m_time;
    m_journalOpen = false;
    m_pauseOpen = false;
}

void App::drawPanel(const Rect& r, float) { m_kit.panel(r); }

// --- Discord -----------------------------------------------------------------

std::vector<std::string> App::inviteHosts() const {
    // Reihenfolge für Mitspieler: öffentliche IPv6, öffentliche IPv4, dann das Heimnetz
    std::vector<std::string> hosts;
    if (!m_hosting) return hosts;
    const HostNetwork::Status s = m_hostNet.status();
    if (!s.ipv6.empty()) hosts.push_back(s.ipv6.front());
    if (!s.publicIPv4.empty() && !s.cgnat()) hosts.push_back(s.publicIPv4);
    if (!s.lan.empty()) hosts.push_back(s.lan.front());
    if (!s.vpn.empty()) hosts.push_back(s.vpn.front());
    return hosts;
}

void App::updateDiscord(double now) {
    m_discord.setEnabled(m_config.discord);
    m_discord.update(now);
    if (auto secret = m_discord.takeJoinSecret()) {
        if (m_screen == Screen::Session && m_inMatch) m_menuNotice = L("Discord-Einladung erst nach der Runde möglich.", "Discord invites only work after the round.");
        else if (m_screen == Screen::Updating) m_pendingDiscordSecret = *secret;
        else joinFromSecret(*secret);
    }
    if (!m_pendingDiscordSecret.empty() && m_screen == Screen::Menu) {
        const std::string secret = std::move(m_pendingDiscordSecret);
        m_pendingDiscordSecret.clear();
        joinFromSecret(secret);
    }
    for (Discord::JoinRequest& r : m_discord.takeJoinRequests()) {
        if (m_hosting && m_screen == Screen::Session) m_joinRequests.push_back(std::move(r));
        else m_discord.respond(r.userId, false);
    }

    Discord::Activity a;
    a.largeText = L("Werwolf", "Werewolf");
    const bool session = m_screen == Screen::Session && m_net.hasView();
    if (!session) {
        a.details = m_screen == Screen::Sandbox ? L("Erkundet das Dorf", "Exploring the village") : L("Im Hauptmenü", "In the main menu");
        m_discord.setActivity(a);
        return;
    }
    const GameView& v = m_net.view();
    a.startTime = m_sessionStart;
    a.partyMax = kMaxPlayers;
    a.partySize = static_cast<int>(v.players.size());
    if (v.phase == Phase::Lobby) {
        a.details = m_hosting ? L("Hostet ein Dorf", "Hosting a village") : L("In der Lobby", "In the lobby");
        a.state = Lf("{} von {} Bewohnern", "{} of {} villagers", v.players.size(), kMaxPlayers);
        a.partyId = m_partyId;
        // Beitreten geht nur in der Lobby; Gäste geben ihre Einladung weiter
        if (m_hosting) {
            const std::vector<std::string> hosts = inviteHosts();
            if (!hosts.empty() && !m_partyId.empty()) a.joinSecret = Discord::makeSecret(m_partyId, m_net.port(), hosts);
        } else {
            a.joinSecret = m_discordSecret;
        }
        if (static_cast<int>(v.players.size()) >= kMaxPlayers) a.joinSecret.clear();
    } else {
        // Keine Rolle verraten – nur Phase und Lebende
        int alive = 0;
        for (const PlayerView& p : v.players) alive += p.alive ? 1 : 0;
        a.details = v.phase == Phase::GameOver ? std::string(L("Spielende", "Game over")) : phaseWithDay(v);
        a.state = Lf("{} von {} leben noch", "{} of {} still alive", alive, v.players.size());
        a.partyId = m_partyId;
    }
    m_discord.setActivity(a);
}

} // namespace ww
