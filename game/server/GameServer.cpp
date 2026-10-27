#include "server/GameServer.h"

#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ww {

using eng::ByteReader;
using eng::ByteWriter;
using eng::net::Connection;

namespace {

constexpr double kHelloTimeout = 5.0;
constexpr double kStateResendInterval = 1.0;
constexpr size_t kHistoryLimit = 150;
constexpr const char* kHiddenWolfName = "Unbekannter Werwolf";

const std::vector<std::string> kBotNames = {"Anton", "Berta", "Clara",  "Dieter", "Emil",  "Frieda",
                                            "Gustav", "Hanna", "Ida",   "Jakob",  "Karl",  "Luise",
                                            "Martha", "Otto",  "Paula", "Wilhelm"};

} // namespace

GameServer::GameServer(ServerOptions options) : m_options(options), m_rng(eng::randomSeed()) {}

GameServer::~GameServer() { shutdown(); }

bool GameServer::start(std::string& error) {
    if (!m_listener.listen(m_options.port, &error)) return false;
    m_lastTick = eng::nowSeconds();
    eng::log::info("Server lauscht auf Port {}", m_options.port);
    return true;
}

void GameServer::run(const std::atomic<bool>& running) {
    while (running) {
        tick();
        eng::sleepSeconds(1.0 / 60.0);
    }
    shutdown();
}

void GameServer::shutdown() {
    if (!m_listener.isListening()) return;
    m_listener.close();
    for (auto& m : m_members) {
        if (!m->conn) continue;
        ByteWriter w = proto::begin(proto::ServerMsg::Reject);
        w.str("Der Server wurde beendet.");
        m->conn->send(w.data());
        m->conn->update();
    }
    m_members.clear();
    m_pending.clear();
    m_closing.clear();
    m_match.reset();
    m_bots.clear();
    eng::log::info("Server beendet");
}

void GameServer::tick() {
    const double now = eng::nowSeconds();
    const double realDt = std::min(0.25, now - m_lastTick);
    const float dt = static_cast<float>(realDt) * m_options.timeScale;
    m_lastTick = now;

    acceptConnections(now);
    updatePending(now);
    updateMembers(now, realDt);
    processRemovals();
    if (m_lobbyRequested) returnToLobby();

    const bool anyHuman =
        std::any_of(m_members.begin(), m_members.end(), [](const auto& m) { return m->online && !m->bot; });
    if (anyHuman || m_members.empty()) {
        m_noHumansSince = -1.0;
    } else if (m_noHumansSince < 0.0) {
        m_noHumansSince = now;
    } else if (now - m_noHumansSince > 90.0) {
        resetIfEmpty();
        m_noHumansSince = -1.0;
    }

    if (m_match) {
        m_match->update(dt);
        const Bot::SayFn say = [this](uint8_t botId, const std::string& text, bool wolves) { chatFrom(botId, text, wolves); };
        for (Bot& bot : m_bots) bot.update(dt, *m_match, say);
        routeMatchNotices();
        sendCorrections();
        if (m_match->takeDirty()) m_dirty = true;
        if (now - m_lastSnapshot >= proto::kSnapshotInterval) {
            broadcastSnapshots();
            m_lastSnapshot = now;
        }
    }

    if (m_dirty || now - m_lastBroadcast > kStateResendInterval) broadcastState(now);

    for (auto& m : m_members)
        if (m->conn) m->conn->update();
    for (auto it = m_closing.begin(); it != m_closing.end();) {
        it->conn->update();
        if (it->conn->state() == Connection::State::Disconnected || now > it->deadline) it = m_closing.erase(it);
        else ++it;
    }
}

// --- Verbindungen ------------------------------------------------------------

void GameServer::acceptConnections(double now) {
    while (auto conn = m_listener.accept()) {
        eng::log::info("Neue Verbindung von {}", conn->address());
        m_pending.push_back(Pending{std::move(conn), now});
    }
}

void GameServer::updatePending(double now) {
    std::vector<uint8_t> data;
    for (Pending& p : m_pending) {
        p.conn->update();
        if (p.conn->receive(data)) {
            ByteReader r(data);
            if (static_cast<proto::ClientMsg>(r.u8()) == proto::ClientMsg::Hello) handleHello(p, r);
            else reject(std::move(p.conn), "Ungültige Anmeldung.");
            continue;
        }
        if (p.conn->state() == Connection::State::Disconnected || now - p.since > kHelloTimeout) p.conn.reset();
    }
    std::erase_if(m_pending, [](const Pending& p) { return !p.conn; });
}

void GameServer::reject(std::unique_ptr<Connection> conn, const std::string& reason) {
    if (!conn) return;
    eng::log::info("Verbindung {} abgelehnt: {}", conn->address(), reason);
    ByteWriter w = proto::begin(proto::ServerMsg::Reject);
    w.str(reason);
    conn->send(w.data());
    conn->closeAfterFlush();
    m_closing.push_back(Closing{std::move(conn), eng::nowSeconds() + 2.0});
}

void GameServer::handleHello(Pending& pending, ByteReader& r) {
    const uint16_t version = r.u16();
    std::string name = eng::utf8::sanitize(r.str(256), proto::kMaxNameLength);
    const uint64_t token = r.u64();
    if (!r.ok()) {
        reject(std::move(pending.conn), "Ungültige Anmeldung.");
        return;
    }
    if (version != proto::kVersion) {
        reject(std::move(pending.conn),
               std::format("Inkompatible Version (Server: {}, Client: {}). Bitte aktualisiere das Spiel.",
                           proto::kVersion, version));
        return;
    }

    if (token != 0) {
        for (auto& m : m_members) {
            if (m->bot || m->online || m->token != token) continue;
            m->conn = std::move(pending.conn);
            m->online = true;
            if (m_match) m_match->setConnected(m->id, true);
            ByteWriter w = proto::begin(proto::ServerMsg::Welcome);
            w.u8(m->id);
            w.u64(m->token);
            m->conn->send(w.data());
            sendHistory(*m);
            broadcastNotice(NoticeKind::Info, m->name + " ist zurück.");
            eng::log::info("{} hat sich wieder verbunden", m->name);
            if (m_match) {
                if (const Match::Player* p = m_match->find(m->id)) {
                    ByteWriter c = proto::begin(proto::ServerMsg::Correction);
                    const Vec3 pos = p->alive ? p->position : p->ghostPosition;
                    c.f32(pos.x);
                    c.f32(pos.z);
                    c.f32(p->alive ? p->yaw : p->ghostYaw);
                    m->conn->send(c.data());
                }
            }
            ensureHost();
            m_dirty = true;
            return;
        }
    }

    if (m_match) {
        reject(std::move(pending.conn), "Im Dorf läuft gerade eine Runde. Bitte warte, bis sie vorbei ist.");
        return;
    }
    if (static_cast<int>(m_members.size()) >= kMaxPlayers) {
        reject(std::move(pending.conn), std::format("Das Dorf ist voll ({} Spieler).", kMaxPlayers));
        return;
    }

    if (name.empty()) name = "Spieler";
    auto m = std::make_unique<Member>();
    m->id = freeId();
    m->name = uniqueName(name);
    m->token = m_rng.next64() | 1;
    m->online = true;
    m->conn = std::move(pending.conn);

    ByteWriter w = proto::begin(proto::ServerMsg::Welcome);
    w.u8(m->id);
    w.u64(m->token);
    m->conn->send(w.data());
    eng::log::info("{} ({}) ist beigetreten", m->name, m->conn->address());

    Member& ref = *m;
    m_members.push_back(std::move(m));
    sendHistory(ref);
    broadcastNotice(NoticeKind::Info, ref.name + " hat das Dorf betreten.");
    ensureHost();
    m_dirty = true;
}

void GameServer::updateMembers(double now, double realDt) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < m_members.size(); ++i) {
        Member& m = *m_members[i];
        m.chatBudget = std::min(6.0, m.chatBudget + realDt * 0.6);
        if (!m.conn) continue;
        m.conn->update();
        while (m.conn && m.conn->receive(data)) handleMessage(m.id, data);
        if (!m.conn) continue;
        if (m.conn->state() == Connection::State::Disconnected) onConnectionLost(m, m.conn->error());
        else if (now - m.conn->lastReceiveTime() > proto::kTimeout) onConnectionLost(m, "Zeitüberschreitung");
    }
}

void GameServer::onConnectionLost(Member& m, const std::string& reason) {
    eng::log::info("{} hat die Verbindung verloren ({})", m.name, reason);
    m.conn.reset();
    m.online = false;
    if (m_match) {
        m_match->setConnected(m.id, false);
        broadcastNotice(NoticeKind::Warning, m.name + " hat die Verbindung verloren.");
    } else {
        m.removeRequested = true;
        broadcastNotice(NoticeKind::Info, m.name + " hat das Dorf verlassen.");
    }
    m_dirty = true;
}

void GameServer::processRemovals() {
    bool removed = false;
    for (auto& m : m_members) {
        if (!m->removeRequested) continue;
        if (m->conn) {
            if (!m->removeReason.empty()) {
                reject(std::move(m->conn), m->removeReason);
            } else {
                m->conn->closeAfterFlush();
                m_closing.push_back(Closing{std::move(m->conn), eng::nowSeconds() + 2.0});
            }
        }
        if (m_match) m_match->playerLeft(m->id);
        std::erase_if(m_bots, [&](const Bot& b) { return b.id() == m->id; });
        removed = true;
    }
    if (!removed) return;
    std::erase_if(m_members, [](const auto& m) { return m->removeRequested; });
    ensureHost();
    resetIfEmpty();
    m_dirty = true;
}

void GameServer::ensureHost() {
    if (Member* h = member(m_host); h && h->online && !h->bot) return;
    const uint8_t previous = m_host;
    m_host = kNoPlayer;
    for (auto& m : m_members) {
        if (m->online && !m->bot) {
            m_host = m->id;
            break;
        }
    }
    if (m_host != previous && m_host != kNoPlayer) broadcastNotice(NoticeKind::Info, member(m_host)->name + " ist jetzt Spielleitung.");
    m_dirty = true;
}

void GameServer::resetIfEmpty() {
    const bool anyHuman =
        std::any_of(m_members.begin(), m_members.end(), [](const auto& m) { return m->online && !m->bot; });
    if (anyHuman) return;
    if (!m_members.empty() || m_match) eng::log::info("Keine Spieler mehr verbunden – Server wird zurückgesetzt");
    m_match.reset();
    m_bots.clear();
    m_members.clear();
    m_history.clear();
    m_host = kNoPlayer;
}

// --- Nachrichten -------------------------------------------------------------

void GameServer::handleMessage(uint8_t memberId, const std::vector<uint8_t>& data) {
    Member* m = member(memberId);
    if (!m || data.empty()) return;
    ByteReader r(data);
    const auto type = static_cast<proto::ClientMsg>(r.u8());
    const bool isHost = memberId == m_host;

    switch (type) {
    case proto::ClientMsg::Chat: {
        std::string text = eng::utf8::sanitize(r.str(2048), proto::kMaxChatLength);
        const bool wolves = r.boolean();
        if (text.empty() || !r.ok()) break;
        if (m->chatBudget < 1.0) {
            sendNotice(*m, NoticeKind::Warning, "Nicht so schnell – das Dorf kommt kaum mit.");
            break;
        }
        m->chatBudget -= 1.0;
        chatFrom(memberId, std::move(text), wolves);
        break;
    }
    case proto::ClientMsg::Move: {
        const float x = r.f32(), z = r.f32(), yaw = r.f32();
        const uint8_t flags = r.u8();
        if (m_match && r.ok() && std::isfinite(x) && std::isfinite(z) && std::isfinite(yaw))
            m_match->movePlayer(memberId, x, z, yaw, flags);
        break;
    }
    case proto::ClientMsg::Interact: {
        const auto kind = static_cast<Interaction>(r.u8());
        const uint8_t target = r.u8();
        if (m_match && r.ok()) {
            const std::string err = m_match->interact(memberId, kind, target);
            if (!err.empty()) sendNotice(*m, NoticeKind::Warning, err);
            m_dirty = true;
        }
        break;
    }
    case proto::ClientMsg::StartGame:
        if (!isHost) sendNotice(*m, NoticeKind::Warning, "Nur die Spielleitung kann das Spiel starten.");
        else if (!m_match) startGame(*m);
        break;
    case proto::ClientMsg::UpdateSettings: {
        Settings s;
        if (isHost && !m_match && s.read(r)) {
            s.sanitize();
            m_settings = s;
            m_dirty = true;
        }
        break;
    }
    case proto::ClientMsg::AddBot:
        if (isHost && !m_match) {
            if (static_cast<int>(m_members.size()) >= kMaxPlayers) sendNotice(*m, NoticeKind::Warning, "Das Dorf ist bereits voll.");
            else addBot();
        }
        break;
    case proto::ClientMsg::Kick: {
        const uint8_t target = r.u8();
        Member* t = member(target);
        if (isHost && !m_match && t && target != memberId) {
            t->removeRequested = true;
            t->removeReason = "Die Spielleitung hat dich aus dem Dorf geworfen.";
            broadcastNotice(NoticeKind::Info, t->name + " wurde aus dem Dorf geworfen.");
        }
        break;
    }
    case proto::ClientMsg::ReturnToLobby:
        if (isHost && m_match) m_lobbyRequested = true;
        break;
    case proto::ClientMsg::Ping: {
        ByteWriter w = proto::begin(proto::ServerMsg::Pong);
        if (m->conn) m->conn->send(w.data());
        break;
    }
    case proto::ClientMsg::Leave:
        m->removeRequested = true;
        eng::log::info("{} hat das Spiel verlassen", m->name);
        broadcastNotice(NoticeKind::Info, m->name + " hat das Dorf verlassen.");
        break;
    default:
        eng::log::warn("Unbekannte Nachricht {} von {}", static_cast<int>(type), m->name);
        break;
    }
}

// --- Spielverwaltung ---------------------------------------------------------

void GameServer::startGame(Member& host) {
    std::vector<Match::Seat> seats;
    for (auto& m : m_members) seats.push_back(Match::Seat{m->id, m->name, m->bot});

    const uint32_t worldSeed = static_cast<uint32_t>(m_rng.next64() & 0x7FFFFFFF);
    const double t0 = eng::nowSeconds();
    m_world.generate(worldSeed);
    auto match = std::make_unique<Match>();
    std::string error;
    if (!match->start(seats, m_settings, m_world, m_rng.next64(), error)) {
        sendNotice(host, NoticeKind::Warning, error);
        return;
    }
    m_history.clear();
    m_match = std::move(match);
    m_bots.clear();
    for (auto& m : m_members)
        if (m->bot) m_bots.emplace_back(m->id, m_rng.next64());
    eng::log::info("Spiel gestartet mit {} Spielern (Welt {} in {:.0f} ms)", seats.size(), worldSeed,
                   (eng::nowSeconds() - t0) * 1000.0);
    routeMatchNotices();
    sendCorrections();
    m_dirty = true;
}

void GameServer::returnToLobby() {
    m_lobbyRequested = false;
    if (!m_match) return;
    m_match.reset();
    m_bots.clear();
    for (auto& m : m_members)
        if (!m->bot && !m->online) m->removeRequested = true;
    processRemovals();
    m_history.clear();
    broadcastNotice(NoticeKind::Info, "Zurück in der Lobby. Bereit für eine neue Runde?");
    eng::log::info("Zurück in der Lobby");
    m_dirty = true;
}

void GameServer::addBot() {
    std::vector<std::string> available;
    for (const std::string& n : kBotNames) {
        const bool used = std::any_of(m_members.begin(), m_members.end(), [&](const auto& m) { return m->name == n; });
        if (!used) available.push_back(n);
    }
    auto m = std::make_unique<Member>();
    m->id = freeId();
    m->name = available.empty() ? uniqueName("Bot") : m_rng.pick(available);
    m->bot = true;
    m->online = true;
    broadcastNotice(NoticeKind::Info, m->name + " (Bot) hat das Dorf betreten.");
    m_members.push_back(std::move(m));
    m_dirty = true;
}

GameServer::Member* GameServer::member(uint8_t id) {
    for (auto& m : m_members)
        if (m->id == id) return m.get();
    return nullptr;
}

uint8_t GameServer::freeId() const {
    for (int id = 0; id < 250; ++id) {
        const bool used = std::any_of(m_members.begin(), m_members.end(), [&](const auto& m) { return m->id == id; });
        if (!used) return static_cast<uint8_t>(id);
    }
    return kNoPlayer;
}

std::string GameServer::uniqueName(const std::string& wanted) const {
    auto taken = [&](const std::string& n) {
        return std::any_of(m_members.begin(), m_members.end(), [&](const auto& m) { return m->name == n; });
    };
    if (!taken(wanted)) return wanted;
    const std::string base = eng::utf8::sanitize(wanted, proto::kMaxNameLength - 3);
    for (int i = 2; i < 100; ++i) {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return base + " ?";
}

// --- Chat & Meldungen --------------------------------------------------------

bool GameServer::noticeVisible(const Notice& n, uint8_t memberId) const {
    switch (n.audience) {
    case Notice::Audience::All: return true;
    case Notice::Audience::Player: return n.player == memberId;
    case Notice::Audience::Wolves: {
        if (!m_match) return false;
        const Match::Player* p = m_match->find(memberId);
        return p && (isWolfRole(p->role) || !p->alive);
    }
    case Notice::Audience::Dead: {
        if (!m_match) return false;
        const Match::Player* p = m_match->find(memberId);
        return p && !p->alive;
    }
    case Notice::Audience::Area: {
        // Geräusche hört, wer in der Nähe ist – Geister hören alles
        if (!m_match) return false;
        const Match::Player* p = m_match->find(memberId);
        if (!p) return false;
        if (!p->alive) return true;
        const float dx = p->position.x - n.x, dz = p->position.z - n.z;
        return dx * dx + dz * dz <= n.radius * n.radius;
    }
    }
    return false;
}

void GameServer::chatFrom(uint8_t memberId, std::string text, bool wolves) {
    Member* m = member(memberId);
    if (!m) return;
    ChatChannel channel = ChatChannel::Global;
    if (m_match) {
        const auto ch = m_match->chatChannel(memberId, wolves);
        if (!ch) return;
        channel = *ch;
        const Match::Player* p = m_match->find(memberId);
        if (channel == ChatChannel::Nearby && p && p->wolfForm()) text = "*knurrt bedrohlich*";
    }

    ByteWriter w = proto::begin(proto::ServerMsg::Chat);
    w.u8(static_cast<uint8_t>(channel));
    w.u8(memberId);
    w.str(m->name);
    w.str(text);
    // Für das kleine Mädchen ohne Namen
    ByteWriter anonymous = proto::begin(proto::ServerMsg::Chat);
    anonymous.u8(static_cast<uint8_t>(channel));
    anonymous.u8(kNoPlayer);
    anonymous.str(kHiddenWolfName);
    anonymous.str(text);
    for (auto& target : m_members) {
        if (!target->conn) continue;
        const bool hears = m_match ? m_match->canHear(target->id, memberId, channel) : true;
        if (!hears) continue;
        const bool hidden = m_match && m_match->hidesSpeaker(target->id, channel);
        target->conn->send(hidden ? anonymous.data() : w.data());
    }
    if (m_match) {
        for (Bot& bot : m_bots)
            if (m_match->canHear(bot.id(), memberId, channel) && !m_match->hidesSpeaker(bot.id(), channel))
                bot.hear(*m_match, memberId, text);
    }

    if (channel != ChatChannel::Nearby && channel != ChatChannel::Family) {
        HistoryEntry e;
        e.isChat = true;
        e.channel = channel;
        e.from = memberId;
        e.name = m->name;
        e.text = text;
        pushHistory(std::move(e));
    }
    if (m_options.logChat) eng::log::info("[{}] {}: {}", channelName(channel), m->name, text);
}

void GameServer::deliverNotice(const Notice& notice) {
    ByteWriter w = proto::begin(proto::ServerMsg::Notice);
    w.u8(static_cast<uint8_t>(notice.kind));
    w.str(notice.text);
    for (auto& m : m_members)
        if (m->conn && noticeVisible(notice, m->id)) m->conn->send(w.data());
    if (notice.audience == Notice::Audience::Area) return; // Geräusche sind flüchtig
    HistoryEntry e;
    e.notice = notice;
    pushHistory(std::move(e));
}

void GameServer::broadcastNotice(NoticeKind kind, const std::string& text) {
    Notice n;
    n.kind = kind;
    n.text = text;
    deliverNotice(n);
}

void GameServer::sendNotice(Member& m, NoticeKind kind, const std::string& text) {
    if (!m.conn) return;
    ByteWriter w = proto::begin(proto::ServerMsg::Notice);
    w.u8(static_cast<uint8_t>(kind));
    w.str(text);
    m.conn->send(w.data());
}

void GameServer::routeMatchNotices() {
    if (!m_match) return;
    for (const Notice& n : m_match->takeNotices()) {
        deliverNotice(n);
        if (n.audience == Notice::Audience::All) eng::log::info("» {}", n.text);
    }
}

void GameServer::pushHistory(HistoryEntry entry) {
    m_history.push_back(std::move(entry));
    while (m_history.size() > kHistoryLimit) m_history.pop_front();
}

void GameServer::sendHistory(Member& m) {
    if (!m.conn) return;
    for (const HistoryEntry& e : m_history) {
        if (e.isChat) {
            if (m_match && !m_match->canReadChannel(m.id, e.channel)) continue;
            const bool hidden = m_match && m_match->hidesSpeaker(m.id, e.channel);
            ByteWriter w = proto::begin(proto::ServerMsg::Chat);
            w.u8(static_cast<uint8_t>(e.channel));
            w.u8(hidden ? kNoPlayer : e.from);
            w.str(hidden ? std::string(kHiddenWolfName) : e.name);
            w.str(e.text);
            m.conn->send(w.data());
        } else {
            if (!noticeVisible(e.notice, m.id)) continue;
            ByteWriter w = proto::begin(proto::ServerMsg::Notice);
            w.u8(static_cast<uint8_t>(e.notice.kind));
            w.str(e.notice.text);
            m.conn->send(w.data());
        }
    }
}

// --- Zustand -----------------------------------------------------------------

GameView GameServer::lobbyView(uint8_t viewer) const {
    GameView v;
    v.phase = Phase::Lobby;
    v.you = viewer;
    v.settings = m_settings;
    for (const auto& m : m_members) {
        PlayerView p;
        p.id = m->id;
        p.name = m->name;
        p.connected = m->online;
        p.bot = m->bot;
        v.players.push_back(std::move(p));
    }
    return v;
}

void GameServer::broadcastState(double now) {
    for (auto& m : m_members) {
        if (!m->conn) continue;
        GameView v = m_match ? m_match->viewFor(m->id) : lobbyView(m->id);
        v.host = m_host;
        ByteWriter w = proto::begin(proto::ServerMsg::State);
        v.write(w);
        m->conn->send(w.data());
    }
    m_dirty = false;
    m_lastBroadcast = now;
}

void GameServer::broadcastSnapshots() {
    for (auto& m : m_members) {
        if (!m->conn) continue;
        ByteWriter w = proto::begin(proto::ServerMsg::Snapshot);
        writeEntities(w, m_match->entitiesFor(m->id));
        writeDoors(w, m_match->doors());
        m->conn->send(w.data());
    }
}

void GameServer::sendCorrections() {
    if (!m_match) return;
    for (uint8_t id : m_match->takeCorrections()) {
        Member* m = member(id);
        const Match::Player* p = m_match->find(id);
        if (!m || !m->conn || !p) continue;
        ByteWriter w = proto::begin(proto::ServerMsg::Correction);
        const Vec3 pos = p->alive ? p->position : p->ghostPosition;
        w.f32(pos.x);
        w.f32(pos.z);
        w.f32(p->alive ? p->yaw : p->ghostYaw);
        m->conn->send(w.data());
    }
}

// --- ServerThread ------------------------------------------------------------

bool ServerThread::start(const ServerOptions& options, std::string& error) {
    stop();
    m_server = std::make_unique<GameServer>(options);
    if (!m_server->start(error)) {
        m_server.reset();
        return false;
    }
    m_running = true;
    m_thread = std::thread([this] {
        eng::log::setThreadTag("server");
        m_server->run(m_running);
    });
    return true;
}

void ServerThread::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    m_server.reset();
}

} // namespace ww
