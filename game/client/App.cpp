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
#include <cstdio>
#include <cstdlib>

namespace ww {

using eng::Color;
using eng::Rect;

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
    eng::log::info("Werwolf startet");
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
    if (!m_renderer.init()) {
        MessageBoxW(nullptr, L"Der Renderer konnte nicht initialisiert werden.", L"Werwolf", MB_ICONERROR);
        return false;
    }
    loadFonts(m_window.dpiScale());
    m_scenery.init(0xC0FFEE);
    eng::net::initialize();
    m_lastFrame = eng::nowSeconds();
    m_startTime = m_lastFrame;
    m_lastShot = m_lastFrame;
    return true;
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
        else if (a == "--timescale" && hasValue) m_dev.timeScale = static_cast<float>(std::atof(arg(++i).c_str()));
        else if (a == "--screenshots" && hasValue) m_dev.shotDir = std::filesystem::path(argv[++i]);
        else if (a == "--shot-every" && hasValue) m_dev.shotInterval = std::atof(arg(++i).c_str());
        else if (a == "--quit-after" && hasValue) m_dev.quitAfter = std::atof(arg(++i).c_str());
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
    if (m_dev.autoPlay && m_screen == Screen::Session && m_net.hasView() && m_time > m_devNextAction) {
        const GameView& v = m_net.view();
        const PlayerView* me = v.me();
        m_devNextAction = m_time + 1.5;
        if (v.action == ActionKind::SkipDiscussion) {
            if (me && !me->skip) m_net.sendSkip(true);
        } else if (v.action != ActionKind::None) {
            const bool needsPick = v.action == ActionKind::CupidPick ? v.selection.size() < 2 : v.selection.empty();
            if (needsPick && !v.confirmed) {
                std::vector<uint8_t> options;
                for (const PlayerView& p : v.players)
                    if (isTargetable(v, p.id) && std::find(v.selection.begin(), v.selection.end(), p.id) == v.selection.end())
                        options.push_back(p.id);
                if (!options.empty()) clickPlayer(v, m_devRng.pick(options));
            } else if (!v.confirmed && v.action != ActionKind::DayVote) {
                m_net.sendConfirm();
            }
        }
    }
    if (m_dev.quitAfter > 0.0 && m_time - m_startTime > m_dev.quitAfter) m_quit = true;
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
    m_net.disconnect();
    m_server.stop();
    m_config.name = eng::utf8::sanitize(m_nameInput, proto::kMaxNameLength);
    m_config.save();
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

    // Zeilenumbrüche im Chat neu berechnen
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

    // Tag/Nacht-Übergang
    float targetNight = 1.0f;
    if (m_screen == Screen::Session && m_net.hasView()) {
        switch (m_net.view().phase) {
        case Phase::Day:
        case Phase::Vote:
        case Phase::Execution: targetNight = 0.0f; break;
        case Phase::Hunter: targetNight = 0.2f; break;
        case Phase::Morning: targetNight = 0.45f; break;
        case Phase::GameOver: targetNight = 0.6f; break;
        default: targetNight = 1.0f; break;
        }
    }
    m_night = eng::approach(m_night, targetNight, m_dt * 0.5f);

    if (m_window.isMinimized()) {
        in.endFrame();
        eng::sleepSeconds(0.02);
        return;
    }

    m_renderer.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), m_window.dpiScale());
    m_ui.beginFrame(m_window, m_renderer, now);
    switch (m_screen) {
    case Screen::Menu: drawMenu(); break;
    case Screen::Connecting: drawConnecting(); break;
    case Screen::Lost: drawLost(); break;
    case Screen::Session:
        if (!m_net.hasView()) drawConnecting();
        else if (m_net.view().phase == Phase::Lobby) drawLobby();
        else drawGame();
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
            m_lastPhase = m_net.view().phase;
            m_lastDay = m_net.view().day;
            m_night = m_net.view().phase == Phase::Lobby ? 1.0f : m_night;
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
        return;
    }

    for (const LogEntry& e : fresh)
        if (e.isChat && e.from != kNoPlayer) m_bubbles[e.from] = Bubble{e.text, m_time, e.channel};

    if (m_net.hasView()) {
        const GameView& v = m_net.view();
        if (v.phase != m_lastPhase || v.day != m_lastDay) {
            onPhaseChanged(v);
            m_lastPhase = v.phase;
            m_lastDay = v.day;
        }
    }
}

void App::onPhaseChanged(const GameView& v) {
    if (m_lastPhase == Phase::Lobby && v.phase != Phase::Lobby) {
        m_bubbles.clear();
        m_chatScroll = 0.0f;
    }
    if (v.phase == Phase::Lobby || v.phase == Phase::GameOver) {
        m_bannerTime = -100.0;
        return;
    }
    m_bannerTitle = headerTitle(v);
    switch (v.phase) {
    case Phase::Night: m_bannerSubtitle = "Das Dorf schläft ein …"; break;
    case Phase::Witch: m_bannerSubtitle = "Die Hexe erwacht …"; break;
    case Phase::Morning: m_bannerSubtitle = "Die Sonne geht auf."; break;
    case Phase::Hunter: m_bannerSubtitle = "Ein letzter Schuss …"; break;
    case Phase::Day: m_bannerSubtitle = "Das Dorf versammelt sich am Feuer."; break;
    case Phase::Vote: m_bannerSubtitle = "Wer soll hingerichtet werden?"; break;
    case Phase::Execution: m_bannerSubtitle = "Das Dorf hat entschieden."; break;
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
    m_net.connect(host, port, name, token);
    m_screen = Screen::Connecting;
}

void App::leaveSession() {
    m_net.disconnect();
    if (m_hosting) {
        m_server.stop();
        m_hosting = false;
    }
    m_bubbles.clear();
    m_chatInput.clear();
    m_screen = Screen::Menu;
}

void App::drawPanel(const Rect& r, float radius) {
    m_renderer.roundRect(r.offset(0, 4), radius, Color::hex(0x000000, 0.25f));
    m_renderer.roundRect(r, radius, theme::panel);
    m_renderer.roundRectOutline(r, radius, 1.0f, theme::panelBorder);
}

} // namespace ww
