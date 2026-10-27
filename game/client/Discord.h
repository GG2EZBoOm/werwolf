#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ww {

// Discord-Anbindung ohne SDK: spricht über die lokale IPC-Schnittstelle (Named Pipe) mit dem
// laufenden Discord-Client. Zeigt den Spielstatus an und ermöglicht Einladungen ins eigene Dorf.
class Discord {
public:
    struct Activity {
        std::string details;     // obere Zeile, z. B. "In der Lobby"
        std::string state;       // untere Zeile
        int64_t startTime = 0;   // Unix-Zeit in Sekunden (0 = keine Uhr)
        std::string partyId;
        int partySize = 0;
        int partyMax = 0;
        std::string joinSecret;  // leer = Mitspieler können nicht beitreten
        std::string largeText = "Werwolf";
        bool operator==(const Activity&) const = default;
    };

    struct JoinRequest {
        std::string userId;
        std::string username;
    };

    Discord() = default;
    ~Discord();
    Discord(const Discord&) = delete;
    Discord& operator=(const Discord&) = delete;

    // Registriert discord-<appId>://, damit Discord das Spiel für Einladungen starten kann
    void init(const std::string& appId, const std::string& launchCommand);
    void setEnabled(bool enabled);
    void shutdown();
    void update(double now);
    bool connected() const { return m_ready; }
    const std::string& userName() const { return m_userName; }

    void setActivity(const Activity& activity);
    void clearActivity();

    // Jemand hat in Discord auf "Beitreten" geklickt
    std::optional<std::string> takeJoinSecret();
    // Jemand fragt, ob er beitreten darf (nur beim Host)
    std::vector<JoinRequest> takeJoinRequests();
    void respond(const std::string& userId, bool accept);

    // Einladung kodieren/dekodieren: Adressen und Port des Servers
    static std::string makeSecret(const std::string& partyId, uint16_t port, const std::vector<std::string>& hosts);
    static bool parseSecret(const std::string& secret, std::string& partyId, uint16_t& port, std::vector<std::string>& hosts);

private:
    bool open();
    void closePipe();
    bool writeFrame(uint32_t opcode, const std::string& json);
    void readFrames();
    void handle(uint32_t opcode, const std::string& json);
    void command(const std::string& cmd, const std::string& args);
    void sendActivity();

    std::string m_appId;
    bool m_enabled = true;
    void* m_pipe = nullptr;
    bool m_ready = false;
    double m_nextAttempt = 0.0;
    double m_nextActivity = 0.0;
    uint64_t m_nonce = 0;
    std::vector<uint8_t> m_buffer;
    std::string m_userName;
    std::optional<Activity> m_activity; // gewünschter Status (leer = keiner)
    std::optional<Activity> m_sent;
    bool m_sentAny = false;
    bool m_dirty = false;
    std::optional<std::string> m_joinSecret;
    std::vector<JoinRequest> m_requests;
};

} // namespace ww
