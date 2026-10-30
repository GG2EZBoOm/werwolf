#pragma once

#include "audio/SoundEvents.h"
#include "client/Characters.h"
#include "client/Config.h"
#include "client/Controller.h"
#include "client/Discord.h"
#include "client/HostNetwork.h"
#include "client/NetClient.h"
#include "client/Style.h"
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

// Der grafische Client: Hauptmenü, Lobby und die 3D-Spielwelt.
class App {
public:
    int run();

private:
    enum class Screen { Updating, Menu, Connecting, Session, Lost, Preview, Sandbox };
    enum class MenuPage { None, Host, Join, Settings };

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
        Text text;
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
        bool indoors = false; // Sandbox im ersten Haus beginnen (für Screenshots)
        int chapel = 0;       // Sandbox an der Kapelle beginnen: 1 = drinnen, 2 = davor (für Screenshots)
        bool journal = false; // Tagebuch nach Spielbeginn öffnen (für Screenshots)
        bool pause = false;   // Pausenmenü nach Spielbeginn öffnen (für Screenshots)
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
        bool fromDiscord = false; // von Discord für eine Einladung gestartet
        std::string language;     // --lang de|en (nur für diesen Start)
        bool noAI = false;
        uint16_t aiPort = 0;
        std::filesystem::path aiDirectory;
        int settingsTab = -1;     // --settings-tab for rendering QA
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
    float uiScale() const;
    void updateSession();
    void onPhaseChanged(const GameView& v);
    void initAudio();
    void applyAudioSettings();
    void updateAudio();
    void updateMusic(bool inWorld, bool night);
    void playSound(Sound sound, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f);

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
    void drawMenuPage(const eng::Rect& area);
    void drawFooterHints(bool back);
    // Einstellungen (Hauptmenü und Pause); liefert true, wenn "Zurück" gewählt wurde
    bool drawSettings(const eng::Rect& area);
    void applySettings();
    void applyGraphics(eng::Environment& env) const;

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
    void drawRoleReveal(const GameView& v);
    void drawJournal(const GameView& v);
    void drawSubtitles();
    void drawRoleCardFace(const eng::Rect& r, Role role, float alpha);
    std::vector<std::pair<std::string, eng::Color>> roleStatusLines(const GameView& v);
    std::string headerTitle(const GameView& v) const;
    // Phase mit Tageszahl, ohne sie doppelt zu nennen ("Tag 2", "Versammlung · Tag 2")
    std::string phaseWithDay(const GameView& v) const;
    Vec3 myPosition() const { return m_player.position; }
    bool imAlive() const;
    bool canMoveFreely() const;

    // Aktionen
    void hostGame();
    void joinGame();
    void connectTo(const std::vector<std::string>& hosts, uint16_t port, const std::string& name);
    void joinFromSecret(const std::string& secret);
    void reconnect();
    void leaveSession();
    void applyLanguage();
    // Discord: Status, Einladungen, Beitrittsanfragen
    void updateDiscord(double now);
    std::vector<std::string> inviteHosts() const;
    void drawJoinRequests(const eng::Rect& area);
    // Adressen für Mitspieler (Lobby des Hosts)
    float drawAddresses(eng::Rect area);

    eng::Window m_window;
    eng::audio::Mixer m_audio;
    std::unique_ptr<SoundBank> m_sounds;
    eng::audio::Device m_audioDevice; // destroyed before the mixer
    bool m_audioReady = false;
    SoundEvents m_soundEvents;
    std::array<eng::audio::VoiceId, 4> m_ambienceVoices{};
    int m_audioScene = -1;
    float m_fireGain = 0.0f;
    float m_firePan = 0.0f;
    double m_lastNoticeSound = -100.0;
    eng::Rng m_audioRng{eng::randomSeed()};
    std::array<eng::audio::ClipPtr, 5> m_music{};
    eng::audio::VoiceId m_dayMusicVoice = 0, m_previousDayVoice = 0, m_nightMusicVoice = 0;
    size_t m_dayTrack = 0, m_nightTrack = 0;
    double m_dayTrackStarted = 0.0, m_nextNightMusicAt = 0.0, m_musicDuckUntil = 0.0;
    float m_dayMusicGain = 0.0f, m_nightMusicGain = 0.0f;
    bool m_musicWasNight = false;
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
    std::vector<DoorState> m_sandboxDoors;
    eng::Camera m_lastCamera;
    eng::ui::Context m_ui;
    UiKit m_kit;
    Fonts m_fonts;
    float m_fontScale = 0.0f;

    Config m_config;
    bool m_languageApplied = false;
    NetClient m_net;
    ServerThread m_server;
    bool m_hosting = false;
    HostNetwork m_hostNet;
    Discord m_discord;
    std::string m_partyId;        // Discord-Gruppe dieser Sitzung
    std::string m_discordSecret;  // Einladung, über die wir beigetreten sind
    std::string m_pendingDiscordSecret;
    std::vector<Discord::JoinRequest> m_joinRequests;
    int64_t m_sessionStart = 0;
    std::string m_copiedText;     // zuletzt kopierte Adresse (kurze Bestätigung)
    double m_copiedAt = -100.0;

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
    double m_lastChatActivity = 0.0;

    // Menüs
    MenuPage m_menuPage = MenuPage::None;
    Screen m_transitionScreen = Screen::Updating;
    bool m_transitionMatch = false;
    double m_transitionStart = -100.0;
    // Filmische Kamera im Menü (folgt weich den Einstellungen je Untermenü)
    eng::Vec3 m_menuCamPos{38.0f, 9.5f, 0.0f};
    eng::Vec3 m_menuCamTarget{0.0f, 2.5f, 0.0f};
    int m_menuFocus = 0;
    double m_menuEnteredAt = 0.0;
    int m_settingsTab = 0;
    const char* m_settingsHint = nullptr;
    bool m_pauseSettings = false;
    int m_pauseFocus = 0;
    bool m_journalOpen = false;
    double m_revealStart = -100.0;
    bool m_revealDone = true;
    Text m_subtitle;
    double m_subtitleTime = -100.0;

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
