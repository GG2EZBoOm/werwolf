// End-to-End-Test: echter Server im eigenen Thread, Clients über TCP auf localhost.

#include "TestFramework.h"

#include "client/NetClient.h"
#include "server/GameServer.h"

#include "engine/core/Log.h"
#include "engine/core/Random.h"
#include "engine/core/Time.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace ww;

namespace {

constexpr uint16_t kTestPort = 27991;

template <class Pred>
bool pump(NetClient& client, Pred done, double timeout) {
    const double end = eng::nowSeconds() + timeout;
    while (eng::nowSeconds() < end) {
        client.update(eng::nowSeconds());
        if (done()) return true;
        eng::sleepSeconds(0.005);
    }
    return false;
}

// Rohverbindung für Protokolltests (simuliert einen abstürzenden Client).
struct RawClient {
    eng::net::Connection conn;
    uint8_t id = kNoPlayer;
    uint64_t token = 0;
    std::string rejectReason;

    bool hello(const std::string& name, uint64_t useToken) {
        if (!conn.connect("127.0.0.1", kTestPort)) return false;
        const double end = eng::nowSeconds() + 5.0;
        bool sent = false;
        std::vector<uint8_t> data;
        while (eng::nowSeconds() < end) {
            conn.update();
            if (!sent && conn.isConnected()) {
                eng::ByteWriter w = proto::begin(proto::ClientMsg::Hello);
                w.u16(proto::kVersion);
                w.str(name);
                w.u64(useToken);
                conn.send(w.data());
                sent = true;
            }
            while (conn.receive(data)) {
                eng::ByteReader r(data);
                const auto type = static_cast<proto::ServerMsg>(r.u8());
                if (type == proto::ServerMsg::Welcome) {
                    id = r.u8();
                    token = r.u64();
                    return true;
                }
                if (type == proto::ServerMsg::Reject) {
                    rejectReason = r.str();
                    return false;
                }
            }
            eng::sleepSeconds(0.005);
        }
        return false;
    }
};

} // namespace

TEST(KomplettePartieUeberNetzwerk) {
    ServerThread server;
    ServerOptions options;
    options.port = kTestPort;
    options.timeScale = 40.0f;
    options.logChat = false;
    std::string error;
    CHECK(server.start(options, error));
    if (!server.isRunning()) {
        std::printf("    %s\n", error.c_str());
        return;
    }

    NetClient host;
    CHECK(host.connect("127.0.0.1", kTestPort, "Testerin"));
    CHECK(pump(host, [&] { return host.status() == NetClient::Status::Connected && host.hasView(); }, 5.0));
    CHECK(host.isHost());

    // Zweiter Mensch, der später abstürzt und wiederkommt
    RawClient guest;
    CHECK(guest.hello("Gast", 0));
    const uint8_t guestId = guest.id;
    CHECK(guestId != kNoPlayer);

    for (int i = 0; i < 5; ++i) host.sendAddBot();
    Settings s;
    s.cupid = true;
    s.protector = true;
    s.daySeconds = 40;
    s.discussionSeconds = 20;
    s.voteSeconds = 15;
    s.nightSeconds = 40;
    host.sendSettings(s);
    CHECK(pump(host, [&] { return host.view().players.size() == 7 && host.view().settings.cupid; }, 5.0));

    host.sendStartGame();
    CHECK(pump(host, [&] { return host.view().phase == Phase::Day && host.hasSnapshot(); }, 5.0));
    CHECK(host.view().yourRole != Role::Unknown);
    CHECK(host.view().worldSeed != 0);
    const auto start = host.takeCorrection();
    CHECK(start.has_value());
    const size_t entityCount = host.entities(eng::nowSeconds()).size();
    CHECK_EQ(entityCount, 7u);

    // Gast "stürzt ab" und verbindet sich mit seinem Token neu
    guest.conn.close();
    CHECK(pump(host, [&] {
        const PlayerView* g = host.view().player(guestId);
        return g && !g->connected;
    }, 5.0));
    RawClient rejoin;
    CHECK(rejoin.hello("Egal", guest.token));
    CHECK_EQ(rejoin.id, guestId);

    // Fremde dürfen während einer Runde nicht beitreten
    RawClient stranger;
    CHECK(!stranger.hello("Fremd", 0));
    CHECK(!stranger.rejectReason.empty());

    // Die Testerin läuft ein wenig herum und stimmt mit, bis die Runde vorbei ist
    eng::Rng rng(7);
    float x = start ? start->x : 0.0f, z = start ? start->z : 8.0f;
    const bool finished = pump(host, [&] {
        const GameView& v = host.view();
        if (v.phase == Phase::GameOver) return true;
        if (auto c = host.takeCorrection()) {
            x = c->x;
            z = c->z;
        }
        if (rng.chance(0.05f)) {
            x += rng.uniform(-0.3f, 0.3f);
            z += rng.uniform(-0.3f, 0.3f);
            host.sendMove(x, z, 0.0f, 0);
        }
        if (v.can(AbilityVote) && v.vote == kNoPlayer && rng.chance(0.05f)) {
            std::vector<uint8_t> targets;
            for (const PlayerView& p : v.players)
                if (p.alive && p.id != v.you) targets.push_back(p.id);
            if (!targets.empty()) host.sendInteract(Interaction::Vote, rng.pick(targets));
        }
        if (rng.chance(0.002f)) host.sendChat("Ich bin unschuldig!", false);
        // Gast-Verbindung am Leben halten
        rejoin.conn.update();
        std::vector<uint8_t> sink;
        while (rejoin.conn.receive(sink)) {}
        return false;
    }, 60.0);
    CHECK(finished);
    CHECK(host.view().winner != Team::None);

    size_t notices = 0, deaths = 0;
    for (const LogEntry& e : host.log()) {
        if (!e.isChat) ++notices;
        if (!e.isChat && e.kind == NoticeKind::Death) ++deaths;
    }
    CHECK(notices > 5);
    std::printf("    Partie beendet: %s gewinnt, %zu Meldungen, %zu Todesfälle\n", teamName(host.view().winner),
                notices, deaths);

    // Zurück in die Lobby
    host.sendReturnToLobby();
    CHECK(pump(host, [&] { return host.view().phase == Phase::Lobby; }, 5.0));

    host.disconnect();
    server.stop();
}

TEST(VersionWirdGeprueft) {
    ServerThread server;
    ServerOptions options;
    options.port = kTestPort + 1;
    options.logChat = false;
    std::string error;
    CHECK(server.start(options, error));

    eng::net::Connection conn;
    CHECK(conn.connect("127.0.0.1", options.port));
    std::vector<uint8_t> data;
    std::string reason;
    bool sent = false;
    const double end = eng::nowSeconds() + 5.0;
    while (eng::nowSeconds() < end && reason.empty()) {
        conn.update();
        if (!sent && conn.isConnected()) {
            eng::ByteWriter w = proto::begin(proto::ClientMsg::Hello);
            w.u16(proto::kVersion + 100);
            w.str("Alt");
            w.u64(0);
            conn.send(w.data());
            sent = true;
        }
        while (conn.receive(data)) {
            eng::ByteReader r(data);
            if (static_cast<proto::ServerMsg>(r.u8()) == proto::ServerMsg::Reject) reason = r.str();
        }
        eng::sleepSeconds(0.005);
    }
    CHECK(reason.find("Version") != std::string::npos);
    server.stop();
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    eng::log::setMinLevel(eng::log::Level::Warn);
    return test::runAll();
}
