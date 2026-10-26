#include "client/NetClient.h"

#include "engine/core/Log.h"
#include "engine/core/Math.h"
#include "engine/core/Time.h"

#include <algorithm>
#include <cmath>

namespace ww {

namespace {
constexpr size_t kLogLimit = 400;
constexpr double kInterpolationDelay = 0.11;

float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, 2.0f * eng::kPi);
    if (d > eng::kPi) d -= 2.0f * eng::kPi;
    if (d < -eng::kPi) d += 2.0f * eng::kPi;
    return a + d * t;
}
} // namespace

bool NetClient::connect(const std::string& host, uint16_t port, const std::string& name, uint64_t token) {
    disconnect();
    m_host = host;
    m_port = port;
    m_name = name;
    m_token = token;
    m_error.clear();
    m_hasView = false;
    m_myId = kNoPlayer;
    m_snapshots.clear();
    m_correction.reset();
    if (!m_conn.connect(host, port)) {
        m_error = m_conn.error();
        m_status = Status::Failed;
        return false;
    }
    m_status = Status::Connecting;
    return true;
}

void NetClient::disconnect() {
    if (m_conn.isConnected()) {
        send(proto::begin(proto::ClientMsg::Leave));
        m_conn.update();
    }
    m_conn.close();
    m_status = Status::Idle;
    m_hasView = false;
    m_snapshots.clear();
}

void NetClient::send(const eng::ByteWriter& w) { m_conn.send(w.data()); }

void NetClient::update(double now) {
    if (m_status == Status::Idle || m_status == Status::Failed) return;
    m_conn.update();

    if (m_status == Status::Connecting && m_conn.isConnected()) {
        eng::ByteWriter w = proto::begin(proto::ClientMsg::Hello);
        w.u16(proto::kVersion);
        w.str(m_name);
        w.u64(m_token);
        send(w);
        m_status = Status::Handshake;
        m_lastPing = now;
    }

    std::vector<uint8_t> data;
    while (m_conn.receive(data)) {
        handle(data, now);
        if (m_status == Status::Failed) return;
    }

    if (m_conn.state() == eng::net::Connection::State::Disconnected) {
        m_status = Status::Failed;
        if (m_error.empty()) m_error = m_conn.error().empty() ? "Verbindung verloren" : m_conn.error();
        return;
    }
    if (m_status == Status::Connected && now - m_lastPing > proto::kPingInterval) {
        send(proto::begin(proto::ClientMsg::Ping));
        m_lastPing = now;
    }
    if (m_conn.isConnected() && now - m_conn.lastReceiveTime() > proto::kTimeout) {
        m_error = "Der Server antwortet nicht mehr";
        m_conn.close();
        m_status = Status::Failed;
    }
}

void NetClient::handle(const std::vector<uint8_t>& data, double now) {
    eng::ByteReader r(data);
    const auto type = static_cast<proto::ServerMsg>(r.u8());
    switch (type) {
    case proto::ServerMsg::Welcome:
        m_myId = r.u8();
        m_token = r.u64();
        m_status = Status::Connected;
        eng::log::info("Verbunden mit {}:{} als Spieler {}", m_host, m_port, m_myId);
        break;
    case proto::ServerMsg::Reject:
        m_error = r.str();
        m_status = Status::Failed;
        m_conn.close();
        eng::log::info("Server hat die Verbindung abgelehnt: {}", m_error);
        break;
    case proto::ServerMsg::State: {
        GameView v;
        if (v.read(r)) {
            if (v.phase == Phase::Lobby) m_snapshots.clear();
            m_view = std::move(v);
            m_hasView = true;
            m_viewTime = now;
        } else {
            eng::log::warn("Fehlerhafter Spielzustand empfangen");
        }
        break;
    }
    case proto::ServerMsg::Chat: {
        LogEntry e;
        e.isChat = true;
        e.channel = static_cast<ChatChannel>(std::min<uint8_t>(r.u8(), 3));
        e.from = r.u8();
        e.name = r.str();
        e.text = r.str();
        e.time = now;
        if (r.ok()) addEntry(std::move(e));
        break;
    }
    case proto::ServerMsg::Notice: {
        LogEntry e;
        e.kind = static_cast<NoticeKind>(std::min<uint8_t>(r.u8(), static_cast<uint8_t>(NoticeKind::Victory)));
        e.text = r.str();
        e.time = now;
        if (r.ok()) addEntry(std::move(e));
        break;
    }
    case proto::ServerMsg::Snapshot: {
        Snapshot s;
        s.time = now;
        if (readEntities(r, s.entities)) {
            m_snapshots.push_back(std::move(s));
            while (m_snapshots.size() > 30) m_snapshots.pop_front();
        }
        break;
    }
    case proto::ServerMsg::Correction: {
        Correction c;
        c.x = r.f32();
        c.z = r.f32();
        c.yaw = r.f32();
        if (r.ok()) m_correction = c;
        break;
    }
    case proto::ServerMsg::Pong:
        break;
    default:
        eng::log::warn("Unbekannte Servernachricht {}", static_cast<int>(type));
        break;
    }
}

std::vector<EntityState> NetClient::entities(double now) const {
    if (m_snapshots.empty()) return {};
    const double renderTime = now - kInterpolationDelay;
    // zwei Snapshots suchen, die den Zeitpunkt einschließen
    const Snapshot* a = &m_snapshots.front();
    const Snapshot* b = &m_snapshots.front();
    for (size_t i = 0; i < m_snapshots.size(); ++i) {
        if (m_snapshots[i].time <= renderTime) a = &m_snapshots[i];
        if (m_snapshots[i].time >= renderTime) {
            b = &m_snapshots[i];
            break;
        }
        b = &m_snapshots[i];
    }
    const double span = b->time - a->time;
    const float t = span > 1e-6 ? static_cast<float>(std::clamp((renderTime - a->time) / span, 0.0, 1.0)) : 1.0f;

    std::vector<EntityState> out = b->entities;
    for (EntityState& e : out) {
        for (const EntityState& old : a->entities) {
            if (old.id != e.id || old.kind != e.kind) continue;
            e.x = eng::lerp(old.x, e.x, t);
            e.z = eng::lerp(old.z, e.z, t);
            e.yaw = lerpAngle(old.yaw, e.yaw, t);
            e.transform = eng::lerp(old.transform, e.transform, t);
            break;
        }
    }
    return out;
}

std::optional<Correction> NetClient::takeCorrection() {
    std::optional<Correction> c = m_correction;
    m_correction.reset();
    return c;
}

void NetClient::addEntry(LogEntry entry) {
    m_newEntries.push_back(entry);
    m_log.push_back(std::move(entry));
    while (m_log.size() > kLogLimit) m_log.pop_front();
}

std::vector<LogEntry> NetClient::takeNewEntries() {
    std::vector<LogEntry> out;
    out.swap(m_newEntries);
    return out;
}

float NetClient::timeLeft(double now) const {
    if (!m_hasView) return 0.0f;
    return std::max(0.0f, m_view.timeLeft - static_cast<float>(now - m_viewTime));
}

void NetClient::sendChat(const std::string& text, bool wolves) {
    eng::ByteWriter w = proto::begin(proto::ClientMsg::Chat);
    w.str(text);
    w.boolean(wolves);
    send(w);
}

void NetClient::sendMove(float x, float z, float yaw, uint8_t flags) {
    eng::ByteWriter w = proto::begin(proto::ClientMsg::Move);
    w.f32(x);
    w.f32(z);
    w.f32(yaw);
    w.u8(flags);
    send(w);
}

void NetClient::sendInteract(Interaction kind, uint8_t target) {
    eng::ByteWriter w = proto::begin(proto::ClientMsg::Interact);
    w.u8(static_cast<uint8_t>(kind));
    w.u8(target);
    send(w);
}

void NetClient::sendStartGame() { send(proto::begin(proto::ClientMsg::StartGame)); }

void NetClient::sendSettings(const Settings& settings) {
    eng::ByteWriter w = proto::begin(proto::ClientMsg::UpdateSettings);
    settings.write(w);
    send(w);
}

void NetClient::sendAddBot() { send(proto::begin(proto::ClientMsg::AddBot)); }

void NetClient::sendKick(uint8_t player) {
    eng::ByteWriter w = proto::begin(proto::ClientMsg::Kick);
    w.u8(player);
    send(w);
}

void NetClient::sendReturnToLobby() { send(proto::begin(proto::ClientMsg::ReturnToLobby)); }

} // namespace ww
