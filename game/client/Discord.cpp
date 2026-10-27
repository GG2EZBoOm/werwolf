#include "client/Discord.h"

#include "engine/core/Json.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/core/Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ww {

namespace {

enum Opcode : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

constexpr double kRetrySeconds = 6.0;
constexpr double kActivityInterval = 4.5; // Discord erlaubt fünf Statusänderungen in 20 Sekunden

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) out += ' ';
            else out += c;
        }
    }
    return out;
}

std::string jsonString(const std::string& s) { return "\"" + escape(s) + "\""; }

// Discord verlangt für Texte 2 bis 128 Zeichen
std::string clampText(std::string s) {
    s = eng::utf8::sanitize(s, 120);
    while (s.size() < 2) s += ' ';
    return s;
}

#ifdef _WIN32
bool setRegistryString(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG rc = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
#endif

} // namespace

Discord::~Discord() { shutdown(); }

void Discord::init(const std::string& appId, const std::string& launchCommand) {
    m_appId = appId;
#ifdef _WIN32
    // discord-<appId>:// startet das Spiel, wenn jemand eine Einladung annimmt und es noch nicht läuft
    const std::wstring base = L"Software\\Classes\\discord-" + eng::utf8::toWide(appId);
    const std::wstring command = eng::utf8::toWide(launchCommand);
    bool ok = setRegistryString(HKEY_CURRENT_USER, base, nullptr, L"URL:Run game " + eng::utf8::toWide(appId) + L" protocol");
    ok &= setRegistryString(HKEY_CURRENT_USER, base, L"URL Protocol", L"");
    ok &= setRegistryString(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", nullptr, command);
    if (!ok) eng::log::warn("Discord: Startbefehl konnte nicht registriert werden");
#else
    (void)launchCommand;
#endif
}

void Discord::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        if (m_ready) writeFrame(Frame, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                                           ",\"activity\":null},\"nonce\":\"off\"}");
        closePipe();
    }
    m_nextAttempt = 0.0;
}

void Discord::shutdown() { closePipe(); }

void Discord::closePipe() {
#ifdef _WIN32
    if (m_pipe) {
        CloseHandle(static_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
#endif
    m_ready = false;
    m_buffer.clear();
    m_sent.reset();
    m_sentAny = false;
}

bool Discord::open() {
#ifdef _WIN32
    for (int i = 0; i < 10; ++i) {
        const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        m_pipe = h;
        if (!writeFrame(Handshake, "{\"v\":1,\"client_id\":" + jsonString(m_appId) + "}")) {
            closePipe();
            continue;
        }
        return true;
    }
#endif
    return false;
}

bool Discord::writeFrame(uint32_t opcode, const std::string& json) {
#ifdef _WIN32
    if (!m_pipe) return false;
    std::vector<uint8_t> frame(8 + json.size());
    const uint32_t length = static_cast<uint32_t>(json.size());
    std::memcpy(frame.data(), &opcode, 4);
    std::memcpy(frame.data() + 4, &length, 4);
    std::memcpy(frame.data() + 8, json.data(), json.size());
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_pipe), frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) ||
        written != frame.size()) {
        closePipe();
        return false;
    }
    return true;
#else
    (void)opcode;
    (void)json;
    return false;
#endif
}

void Discord::readFrames() {
#ifdef _WIN32
    while (m_pipe) {
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(m_pipe), nullptr, 0, nullptr, &available, nullptr)) {
            eng::log::info("Discord: Verbindung getrennt");
            closePipe();
            return;
        }
        if (available == 0) break;
        const size_t old = m_buffer.size();
        m_buffer.resize(old + available);
        DWORD read = 0;
        if (!ReadFile(static_cast<HANDLE>(m_pipe), m_buffer.data() + old, available, &read, nullptr)) {
            closePipe();
            return;
        }
        m_buffer.resize(old + read);
    }
    while (m_buffer.size() >= 8) {
        uint32_t opcode = 0, length = 0;
        std::memcpy(&opcode, m_buffer.data(), 4);
        std::memcpy(&length, m_buffer.data() + 4, 4);
        if (length > 64 * 1024) {
            closePipe();
            return;
        }
        if (m_buffer.size() < 8 + length) break;
        const std::string json(reinterpret_cast<const char*>(m_buffer.data()) + 8, length);
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + 8 + length);
        handle(opcode, json);
        if (!m_pipe) return;
    }
#endif
}

void Discord::handle(uint32_t opcode, const std::string& json) {
    if (opcode == Ping) {
        writeFrame(Pong, json);
        return;
    }
    eng::json::Value msg;
    if (!eng::json::parse(json, msg)) return;
    if (opcode == Close) {
        eng::log::warn("Discord hat die Verbindung geschlossen: {}", msg.getString("message"));
        closePipe();
        m_nextAttempt = eng::nowSeconds() + 60.0;
        return;
    }
    const std::string cmd = msg.getString("cmd");
    const std::string evt = msg.getString("evt");
    const eng::json::Value* data = msg.get("data");
    if (cmd == "DISPATCH" && evt == "READY") {
        m_ready = true;
        if (data)
            if (const eng::json::Value* user = data->get("user")) m_userName = user->getString("global_name", user->getString("username"));
        eng::log::info("Discord verbunden ({})", m_userName);
        for (const char* event : {"ACTIVITY_JOIN", "ACTIVITY_JOIN_REQUEST"})
            writeFrame(Frame, "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"" + std::string(event) + "\",\"nonce\":\"" + std::to_string(++m_nonce) + "\"}");
        m_dirty = true;
        return;
    }
    if (cmd == "DISPATCH" && evt == "ACTIVITY_JOIN" && data) {
        m_joinSecret = data->getString("secret");
        eng::log::info("Discord: Einladung angenommen");
        return;
    }
    if (cmd == "DISPATCH" && evt == "ACTIVITY_JOIN_REQUEST" && data) {
        if (const eng::json::Value* user = data->get("user")) {
            JoinRequest r;
            r.userId = user->getString("id");
            r.username = user->getString("global_name", user->getString("username"));
            if (!r.userId.empty()) m_requests.push_back(std::move(r));
        }
        return;
    }
    if (evt == "ERROR" && data) eng::log::warn("Discord meldet: {}", data->getString("message"));
}

void Discord::command(const std::string& cmd, const std::string& args) {
    writeFrame(Frame, "{\"cmd\":\"" + cmd + "\",\"args\":" + args + ",\"nonce\":\"" + std::to_string(++m_nonce) + "\"}");
}

void Discord::sendActivity() {
#ifdef _WIN32
    const std::string pid = std::to_string(GetCurrentProcessId());
#else
    const std::string pid = "0";
#endif
    if (!m_activity) {
        command("SET_ACTIVITY", "{\"pid\":" + pid + ",\"activity\":null}");
        m_sent.reset();
        m_sentAny = true;
        return;
    }
    const Activity& a = *m_activity;
    std::ostringstream o;
    o << "{\"pid\":" << pid << ",\"activity\":{";
    o << "\"details\":" << jsonString(clampText(a.details));
    if (!a.state.empty()) o << ",\"state\":" << jsonString(clampText(a.state));
    if (a.startTime > 0) o << ",\"timestamps\":{\"start\":" << a.startTime << "}";
    o << ",\"assets\":{\"large_image\":\"logo\",\"large_text\":" << jsonString(clampText(a.largeText)) << "}";
    if (!a.partyId.empty() && a.partyMax > 0)
        o << ",\"party\":{\"id\":" << jsonString(a.partyId) << ",\"size\":[" << std::max(1, a.partySize) << "," << a.partyMax << "]}";
    if (!a.joinSecret.empty()) o << ",\"secrets\":{\"join\":" << jsonString(a.joinSecret) << "}";
    o << ",\"instance\":false}}";
    command("SET_ACTIVITY", o.str());
    m_sent = m_activity;
    m_sentAny = true;
}

void Discord::update(double now) {
    if (!m_enabled || m_appId.empty()) return;
#ifdef _WIN32
    if (!m_pipe) {
        if (now < m_nextAttempt) return;
        m_nextAttempt = now + kRetrySeconds;
        if (!open()) return;
    }
    readFrames();
    if (!m_ready) return;
    const bool changed = !m_sentAny || m_activity != m_sent;
    if ((changed || m_dirty) && now >= m_nextActivity) {
        sendActivity();
        m_dirty = false;
        m_nextActivity = now + kActivityInterval;
    }
#else
    (void)now;
#endif
}

void Discord::setActivity(const Activity& activity) { m_activity = activity; }

void Discord::clearActivity() { m_activity.reset(); }

std::optional<std::string> Discord::takeJoinSecret() {
    std::optional<std::string> s;
    s.swap(m_joinSecret);
    return s;
}

std::vector<Discord::JoinRequest> Discord::takeJoinRequests() {
    std::vector<JoinRequest> out;
    out.swap(m_requests);
    return out;
}

void Discord::respond(const std::string& userId, bool accept) {
    if (!m_ready) return;
    command(accept ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_REQUEST", "{\"user_id\":" + jsonString(userId) + "}");
}

std::string Discord::makeSecret(const std::string& partyId, uint16_t port, const std::vector<std::string>& hosts) {
    std::string secret = "ww1;" + partyId + ";" + std::to_string(port) + ";";
    for (size_t i = 0; i < hosts.size(); ++i) {
        const std::string next = (i ? "," : "") + hosts[i];
        if (secret.size() + next.size() > 127) break; // Discord erlaubt höchstens 128 Zeichen
        secret += next;
    }
    return secret;
}

bool Discord::parseSecret(const std::string& secret, std::string& partyId, uint16_t& port, std::vector<std::string>& hosts) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t semi = secret.find(';', start);
        parts.push_back(secret.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    if (parts.size() != 4 || parts[0] != "ww1") return false;
    partyId = parts[1];
    const int p = std::atoi(parts[2].c_str());
    if (p <= 0 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
    hosts.clear();
    size_t pos = 0;
    const std::string& list = parts[3];
    while (pos <= list.size()) {
        const size_t comma = list.find(',', pos);
        std::string host = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!host.empty()) hosts.push_back(std::move(host));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return !hosts.empty();
}

} // namespace ww
