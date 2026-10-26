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

// Läuft das Spiel direkt aus einem CMake-Build-Ordner (Entwicklung)?
bool isDeveloperBuild(const std::filesystem::path& exeDir) {
    std::error_code ec;
    return std::filesystem::exists(exeDir.parent_path() / "CMakeCache.txt", ec);
}
} // namespace

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
    if (m_config.name.empty()) m_config.name = "Gast " + std::to_string(eng::Rng(eng::randomSeed()).range(10, 99));
    m_nameInput = m_config.name;
    m_addressInput = m_config.address;
    m_portInput = std::to_string(m_config.port);
    parseCommandLine();

    std::string error;
    if (!m_window.create("Werwolf", 1280, 800, &error)) {
        MessageBoxW(nullptr, eng::utf8::toWide("Das Fenster konnte nicht erstellt werden:\n" + error).c_str(),
                    L"Werwolf", MB_ICONERROR);
        return false;
    }
    if (!m_renderer.init() || !m_renderer3d.init()) {
        MessageBoxW(nullptr, L"Der Renderer konnte nicht initialisiert werden.", L"Werwolf", MB_ICONERROR);
        return false;
    }
    m_characters.init();
    ensureWorld(kBackdropSeed);
    loadFonts(m_window.dpiScale());
    eng::net::initialize();
    m_lastFrame = eng::nowSeconds();
    m_startTime = m_lastFrame;
    m_lastShot = m_lastFrame;
    if (!m_dev.updatedTo.empty()) m_menuNotice = "Das Spiel wurde auf Version " + m_dev.updatedTo + " aktualisiert.";

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
                m_menuNotice = "Version " + version + " ist installiert – bitte starte das Spiel neu.";
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
    r.textCentered(m_fonts.title, {0, H * 0.18f, W, 92.0f}, "WERWOLF", theme::goldLight);

    const std::string notes = m_updater.notes();
    const bool downloading = state == Updater::State::Downloading || state == Updater::State::ReadyToInstall ||
                             state == Updater::State::Installed;
    const std::vector<std::string> noteLines = downloading && !notes.empty() ? m_fonts.small.wrap(notes, 400.0f) : std::vector<std::string>{};
    const size_t shownNotes = std::min<size_t>(noteLines.size(), 6);
    const float pw = 460.0f;
    const float ph = 150.0f + static_cast<float>(shownNotes) * m_fonts.small.lineHeight() + (shownNotes ? 16.0f : 0.0f);
    const Rect panel{std::round(W * 0.5f - pw * 0.5f), std::round(H * 0.42f), pw, ph};
    drawPanel(panel);

    const std::string title = downloading ? "Update auf Version " + m_updater.newVersion() : "Update";
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
    if (skippable && m_ui.button("update/skip", {panel.center().x - 80.0f, panel.bottom() - 46.0f, 160.0f, 34.0f}, "Überspringen",
                                 eng::ui::ButtonKind::Ghost)) {
        m_updater.cancel();
        m_menuNotice = "Update übersprungen – mit einer anderen Version kannst du evtl. nicht mitspielen.";
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
    eng::Camera cam;
    cam.position = {std::cos(a) * 30.0f, 7.0f + std::sin(t * 0.05f) * 1.5f, std::sin(a) * 30.0f};
    cam.target = {0.0f, 2.5f, 0.0f};
    const eng::Environment env = m_worldRenderer.environment(0.93f, cam.target, t);
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), cam, env);
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
    const eng::Environment env = m_worldRenderer.environment(m_previewTimeOfDay, cam.target, t);
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), cam, env);
    m_worldRenderer.draw(m_renderer3d, env, t);
    m_renderer3d.endFrame();
}

void App::drawPreviewOverlay() {
    const int hours = static_cast<int>(m_previewTimeOfDay * 24.0f);
    const int minutes = static_cast<int>(std::fmod(m_previewTimeOfDay * 24.0f * 60.0f, 60.0f));
    const std::string info = std::format("{:02}:{:02} Uhr · {} Draw-Calls · {} Dreiecke · {:.0f} FPS", hours, minutes,
                                         m_renderer3d.drawCalls(), m_renderer3d.triangles(), 1.0f / std::max(m_dt, 0.001f));
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
    m_updater.cancel();
    m_net.disconnect();
    m_server.stop();
    m_config.name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    m_config.save();
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
    m_fontScale = scale;

    for (LogEntry& e : m_net.log()) e.wrapWidth = -1.0f;
    m_ui.theme().font = &m_fonts.ui;
    m_ui.theme().fontSmall = &m_fonts.small;
}

void App::frame() {
    const double now = eng::nowSeconds();
    m_dt = static_cast<float>(std::clamp(now - m_lastFrame, 0.0, 0.1));
    m_lastFrame = now;
    m_time = now;

    if (m_window.dpiScale() != m_fontScale) loadFonts(m_window.dpiScale());
    m_net.update(now);
    updateSession();
    runDevAutomation();

    eng::Input& in = m_window.input();
    if (in.pressed(eng::Key::F11)) m_window.toggleFullscreen();

    if (m_window.isMinimized()) {
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

    // 2D-Oberfläche
    m_renderer.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), m_window.dpiScale());
    m_ui.beginFrame(m_window, m_renderer, now);
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

    for (const LogEntry& e : fresh)
        if (e.isChat && e.from != kNoPlayer) m_bubbles[e.from] = Bubble{e.text, m_time, e.channel};

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
    if (v.phase == Phase::Lobby || v.phase == Phase::GameOver) {
        m_bannerTime = -100.0;
        if (v.phase == Phase::GameOver) m_window.setMouseCaptured(false);
        return;
    }
    m_bannerTitle = headerTitle(v);
    switch (v.phase) {
    case Phase::Day: m_bannerSubtitle = v.day <= 1 ? "Erkundet das Dorf." : "Sucht nach Spuren – wer war es?"; break;
    case Phase::Assembly: m_bannerSubtitle = "Das Dorf versammelt sich am Feuer."; break;
    case Phase::Vote: m_bannerSubtitle = "Schaut den Schuldigen an und drückt E."; break;
    case Phase::Execution: m_bannerSubtitle = "Das Dorf hat entschieden."; break;
    case Phase::Night: m_bannerSubtitle = "Bleibt im Licht …"; break;
    case Phase::Morning: m_bannerSubtitle = "Wer hat die Nacht überlebt?"; break;
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
        m_menuError = "Bitte gib zuerst einen Namen ein.";
        return;
    }
    if (!parsePort(m_portInput, port)) {
        m_menuError = "Der Port muss eine Zahl zwischen 1 und 65535 sein.";
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
        m_menuError = "Der Server konnte nicht gestartet werden: " + error;
        return;
    }
    m_hosting = true;
    m_localIps = eng::net::localIPv4Addresses();
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
        m_menuError = "Bitte gib zuerst einen Namen ein.";
        return;
    }
    if (address.empty()) {
        m_menuError = "Bitte gib die Adresse des Servers ein.";
        return;
    }
    if (!parsePort(m_portInput, port)) {
        m_menuError = "Der Port muss eine Zahl zwischen 1 und 65535 sein.";
        return;
    }
    m_nameInput = name;
    m_config.name = name;
    m_config.address = address;
    m_config.port = port;
    m_config.save();

    m_hosting = false;
    m_net.clearLog();
    m_menuError.clear();
    if (!m_net.connect(address, port, name)) {
        m_menuError = m_net.error();
        return;
    }
    m_screen = Screen::Connecting;
}

void App::reconnect() {
    const std::string host = m_net.host();
    const uint16_t port = m_net.port();
    const std::string name = m_net.name();
    const uint64_t token = m_net.token();
    m_net.clearLog();
    m_inMatch = false;
    m_net.connect(host, port, name, token);
    m_screen = Screen::Connecting;
}

void App::leaveSession() {
    m_net.disconnect();
    if (m_hosting) {
        m_server.stop();
        m_hosting = false;
    }
    m_inMatch = false;
    m_bubbles.clear();
    m_chatInput.clear();
    m_window.setMouseCaptured(false);
    m_screen = Screen::Menu;
}

void App::drawPanel(const Rect& r, float radius) {
    m_renderer.roundRect(r.offset(0, 4), radius, Color::hex(0x000000, 0.25f));
    m_renderer.roundRect(r, radius, theme::panel);
    m_renderer.roundRectOutline(r, radius, 1.0f, theme::panelBorder);
}

} // namespace ww
