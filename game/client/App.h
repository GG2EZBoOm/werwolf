#pragma once

#include "client/Characters.h"
#include "client/Config.h"
#include "client/Controller.h"
#include "client/NetClient.h"
#include "client/Updater.h"
#include "client/WorldRenderer.h"
#include "server/GameServer.h"
#include "world/World.h"

#include "engine/gfx/Font.h"
#include "engine/gfx/Renderer2D.h"
#include "engine/gfx/Renderer3D.h"
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

// Der grafische Client: Hauptmenü, Lobby und die 3D-Spielwelt.
class App {
public:
    int run();

private:
    enum class Screen { Updating, Menu, Connecting, Session, Lost, Preview, Sandbox };

    // Offline-Testumgebung: selbst durchs Dorf laufen, Dorfbewohner wandern umher
    struct SandboxNpc {
        uint8_t id = 0;
        Mover mover;
        Vec2 target{0, 0};
        float idle = 0.0f;
        float stuck = 0.0f;
        Vec2 lastPos{0, 0};
    };

    struct Bubble {
        std::string text;
        double time = 0.0;
        ChatChannel channel = ChatChannel::Global;
    };

    // Eine fremde Figur in der Spielwelt (aus Snapshots interpoliert)
    struct Figure {
        Mover mover;
        float transform = 0.0f;
        uint8_t flags = 0;
        uint8_t kind = EntityLiving;
        bool present = false;
    };

    // Eine mögliche Aktion mit Taste, Ziel und Beschriftung (für HUD und Eingabe)
    struct Prompt {
        Interaction kind;
        const char* key;   // z. B. "E"
        std::string label;
        uint8_t target = kNoPlayer;
        bool ready = false;
        uint8_t param = 0; // z. B. Verwandeln: 1 = Werwolf
    };

    // Entwickler-Optionen über die Kommandozeile (automatisch hosten, Bots, Screenshots)
    struct DevOptions {
        bool autoHost = false;
        std::string join;
        int bots = 0;
        bool autoStart = false;
        bool autoPlay = false;
        bool preview = false;
        bool sandbox = false;
        int previewCamera = 0;
        float timeOfDay = -1.0f;
        float timeScale = 1.0f;
        std::filesystem::path shotDir;
        double shotInterval = 0.0;
        double quitAfter = 0.0;
        std::string cameraOrbit; // "": Spieler folgen, "orbit": Übersicht für Screenshots
        bool noUpdate = false;    // Update-Prüfung überspringen
        bool forceUpdate = false; // auch im Entwickler-Build aktualisieren
        std::string updateUrl;    // andere update.json verwenden
        std::string updatedTo;    // nach einem Update neu gestartet
    };

    bool init();
    void shutdown();
    void frame();
    void startUpdateCheck();
    void drawUpdater();
    static const char* versionString();
    void parseCommandLine();
    void runDevAutomation();
    void ensureWorld(uint32_t seed);
    void takeScreenshot(const std::filesystem::path& path);
    void loadFonts(float scale);
    void updateSession();
    void onPhaseChanged(const GameView& v);

    // Vorschau und Sandbox
    void renderPreview3D();
    void drawPreviewOverlay();
    void enterSandbox();
    void updateSandbox();
    void renderSandbox3D();
    void drawSandboxOverlay();
    void drawCharacters3D(float night);
    // 3D-Kulisse für Menü und Lobby: langsame Kamerafahrt durch das nächtliche Dorf
    void renderBackdrop3D();

    // Bildschirme
    void drawMenu();
    void drawConnecting();
    void drawLost();
    void drawLobby();
    void drawChat(const eng::Rect& r, bool compact);
    void drawPanel(const eng::Rect& r, float radius = 14.0f);

    // Echtzeit-Runde
    void enterMatch();
    void updateMatch();
    void renderMatch3D();
    void drawMatchHud();
    void buildPrompts(const GameView& v);
    void drawNameTags(const GameView& v);
    void drawRoleCard(eng::Rect r, const GameView& v);
    void drawPhaseBox(const GameView& v);
    void drawPrompts(const GameView& v);
    void drawVotePanel(const GameView& v);
    void drawMinimap(eng::Rect r, const GameView& v);
    void drawPauseMenu(const GameView& v);
    void drawGameOver(const GameView& v);
    void drawBanner();
    std::string headerTitle(const GameView& v) const;
    Vec3 myPosition() const { return m_player.position; }
    bool imAlive() const;
    bool canMoveFreely() const;

    // Aktionen
    void hostGame();
    void joinGame();
    void reconnect();
    void leaveSession();

    eng::Window m_window;
    eng::Renderer2D m_renderer;
    eng::Renderer3D m_renderer3d;
    World m_world;
    WorldRenderer m_worldRenderer;
    bool m_worldReady = false;
    float m_previewTimeOfDay = 0.3f;
    CharacterRenderer m_characters;
    ThirdPersonCamera m_camera;
    Mover m_player;
    float m_playerWolf = 0.0f;
    bool m_playerWolfTarget = false;
    float m_playerAttack = 0.0f;
    std::vector<SandboxNpc> m_npcs;
    eng::Camera m_lastCamera;
    eng::ui::Context m_ui;
    Fonts m_fonts;
    float m_fontScale = 0.0f;

    Config m_config;
    NetClient m_net;
    ServerThread m_server;
    bool m_hosting = false;
    std::vector<std::string> m_localIps;

    Screen m_screen = Screen::Menu;
    Updater m_updater;
    double m_updateShownSince = 0.0;
    double m_updateDoneAt = -1.0;
    std::string m_menuNotice; // dezenter Hinweis im Hauptmenü (z. B. Update-Ergebnis)
    std::string m_menuError;
    std::string m_nameInput;
    std::string m_addressInput;
    std::string m_portInput;
    std::string m_chatInput;
    float m_chatScroll = 0.0f;
    bool m_chatWolves = false;

    double m_time = 0.0;
    double m_lastFrame = 0.0;
    float m_dt = 0.0f;
    float m_night = 1.0f;
    bool m_quit = false;

    // Runde
    bool m_inMatch = false;
    std::unordered_map<uint16_t, Figure> m_figures; // Schlüssel: id * 4 + EntityKind
    std::vector<EntityState> m_entities;
    std::vector<eng::Rect> m_hudBlockers; // HUD-Flächen, auf die geklickt werden kann
    bool m_chatWasFocused = false;
    double m_chatFocusTime = -1.0; // Frame, in dem der Chat per Enter geöffnet wurde
    Vec2 m_autoTarget{0, 0};
    double m_lastMoveSent = 0.0;
    float m_timeOfDay = 0.3f;
    std::vector<Prompt> m_prompts;
    uint8_t m_aimTarget = kNoPlayer;
    bool m_pauseOpen = false;
    double m_deathTime = -100.0;
    bool m_wasAlive = true;
    float m_myTransform = 0.0f;

    Phase m_lastPhase = Phase::Lobby;
    int m_lastDay = -1;
    double m_leaveArmedTime = -100.0;
    double m_abortArmedTime = -100.0;
    double m_bannerTime = -100.0;
    std::string m_bannerTitle;
    std::string m_bannerSubtitle;
    std::unordered_map<uint8_t, Bubble> m_bubbles;

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
