#pragma once

#include "client/Config.h"
#include "client/NetClient.h"
#include "client/Scenery.h"
#include "server/GameServer.h"

#include "engine/gfx/Font.h"
#include "engine/gfx/Renderer2D.h"
#include "engine/platform/Window.h"
#include "engine/ui/UI.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ww {

struct Fonts {
    eng::Font ui;
    eng::Font uiBold;
    eng::Font small;
    eng::Font smallBold;
    eng::Font large;
    eng::Font title;
    eng::Font heading;
    eng::Font narration;
    eng::Font icon;
    eng::Font iconLarge;
};

// Der grafische Client: Hauptmenü, Lobby und Spieltisch.
class App {
public:
    int run();

private:
    enum class Screen { Menu, Connecting, Session, Lost };

    struct Bubble {
        std::string text;
        double time = 0.0;
        ChatChannel channel = ChatChannel::Public;
    };

    struct Token {
        uint8_t id;
        eng::Vec2 pos;
        float radius;
    };

    // Entwickler-Optionen über die Kommandozeile (automatisch hosten, Bots, Screenshots)
    struct DevOptions {
        bool autoHost = false;
        std::string join;
        int bots = 0;
        bool autoStart = false;
        bool autoPlay = false; // eigener Spieler handelt zufällig (zum Testen der Oberfläche)
        float timeScale = 1.0f;
        std::filesystem::path shotDir;
        double shotInterval = 0.0;
        double quitAfter = 0.0;
    };

    bool init();
    void shutdown();
    void frame();
    void parseCommandLine();
    void runDevAutomation();
    void takeScreenshot(const std::filesystem::path& path);
    void loadFonts(float scale);
    void updateSession();
    void onPhaseChanged(const GameView& v);

    // Bildschirme
    void drawMenu();
    void drawConnecting();
    void drawLost();
    void drawLobby();
    void drawGame();
    void drawChat(const eng::Rect& r);

    // Spieltisch
    void drawHeader(const eng::Rect& r, const GameView& v);
    void drawRoleCard(eng::Rect r, const GameView& v);
    void drawVillage(const eng::Rect& area, const GameView& v);
    void drawToken(const PlayerView& p, const Token& t, const GameView& v, bool targetable, bool hovered);
    void drawBubbles(const std::vector<Token>& tokens, const GameView& v);
    void drawActionBar(const eng::Rect& r, const GameView& v);
    void drawGameOver(const eng::Rect& area, const GameView& v);
    void drawBanner(const eng::Rect& area);
    void drawPanel(const eng::Rect& r, float radius = 14.0f);

    // Aktionen
    void hostGame();
    void joinGame();
    void reconnect();
    void leaveSession();
    bool isTargetable(const GameView& v, uint8_t id) const;
    void clickPlayer(const GameView& v, uint8_t id);
    std::string headerTitle(const GameView& v) const;
    std::string actionHint(const GameView& v) const;

    eng::Window m_window;
    eng::Renderer2D m_renderer;
    eng::ui::Context m_ui;
    Fonts m_fonts;
    float m_fontScale = 0.0f;
    Scenery m_scenery;

    Config m_config;
    NetClient m_net;
    ServerThread m_server;
    bool m_hosting = false;
    std::vector<std::string> m_localIps;

    Screen m_screen = Screen::Menu;
    std::string m_menuError;
    std::string m_nameInput;
    std::string m_addressInput;
    std::string m_portInput;
    std::string m_chatInput;
    float m_chatScroll = 0.0f;

    double m_time = 0.0;
    double m_lastFrame = 0.0;
    float m_dt = 0.0f;
    float m_night = 1.0f;
    bool m_quit = false;

    Phase m_lastPhase = Phase::Lobby;
    int m_lastDay = -1;
    double m_leaveArmedTime = -100.0; // Sicherheitsabfrage "Wirklich verlassen?"
    double m_abortArmedTime = -100.0; // Sicherheitsabfrage "Runde wirklich abbrechen?"
    double m_bannerTime = -100.0;
    std::string m_bannerTitle;
    std::string m_bannerSubtitle;
    std::unordered_map<uint8_t, Bubble> m_bubbles;
    std::unordered_map<uint8_t, float> m_hover;

    DevOptions m_dev;
    bool m_devConnectTried = false;
    bool m_devBotsSent = false;
    bool m_devStarted = false;
    double m_devNextAction = 0.0;
    eng::Rng m_devRng{eng::randomSeed()};
    double m_startTime = 0.0;
    double m_lastShot = 0.0;
    int m_shotIndex = 0;
};

} // namespace ww
