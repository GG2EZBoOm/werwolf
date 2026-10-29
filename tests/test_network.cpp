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
    s.set(Role::Cupid, true);
    s.set(Role::Protector, true);
    s.daySeconds = 40;
    s.discussionSeconds = 20;
    s.voteSeconds = 15;
    s.nightSeconds = 40;
    host.sendSettings(s);
    CHECK(pump(host, [&] { return host.view().players.size() == 7 && host.view().settings.has(Role::Cupid); }, 5.0));

    host.sendStartGame();
    CHECK(pump(host, [&] { return host.view().phase == Phase::Day && host.hasSnapshot(); }, 5.0));
    CHECK(host.view().yourRole != Role::Unknown);
    CHECK(host.view().worldSeed != 0);
    const auto start = host.takeCorrection();
    CHECK(start.has_value());
    const size_t entityCount = host.entities(eng::nowSeconds()).size();
    CHECK_EQ(entityCount, 7u);
    CHECK_EQ(host.doors().size(), 13u); // Türzustände (zwölf Häuser und die Kapelle) kommen mit dem Snapshot

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

TEST(AdressenMitPortUndIPv6) {
    std::string host;
    uint16_t port = 1;
    CHECK(eng::net::parseEndpoint("192.168.178.20", host, port) && host == "192.168.178.20" && port == 1);
    CHECK(eng::net::parseEndpoint(" 84.12.3.4:27778 ", host, port) && host == "84.12.3.4" && port == 27778);
    CHECK(eng::net::parseEndpoint("[2001:db8::1]:27779", host, port) && host == "2001:db8::1" && port == 27779);
    port = 5;
    CHECK(eng::net::parseEndpoint("2001:db8::7", host, port) && host == "2001:db8::7" && port == 5);
    CHECK(eng::net::parseEndpoint("dorf.example.org:3000", host, port) && host == "dorf.example.org" && port == 3000);
    CHECK(!eng::net::parseEndpoint("dorf.example.org:99999", host, port));
    CHECK(!eng::net::parseEndpoint("  ", host, port));
    CHECK(eng::net::formatEndpoint("2001:db8::1", 27777) == "[2001:db8::1]:27777");
    CHECK(eng::net::formatEndpoint("10.0.0.2", 27777) == "10.0.0.2:27777");
    CHECK(eng::net::isPrivateIPv4("192.168.0.4") && eng::net::isPrivateIPv4("100.72.1.1") && !eng::net::isPrivateIPv4("84.12.3.4"));
}

TEST(ServerLauschtAufIPv6UndIPv4) {
    ServerThread server;
    ServerOptions options;
    options.port = kTestPort + 2;
    options.logChat = false;
    std::string error;
    CHECK(server.start(options, error));
    // Über IPv4 und – falls der Rechner IPv6 kann – über IPv6-Loopback beitreten
    for (const char* address : {"127.0.0.1", "::1"}) {
        NetClient client;
        if (!client.connect(address, options.port, "Netz")) {
            CHECK(std::string(address) == "::1"); // IPv6 nicht verfügbar: nur dann darf es scheitern
            continue;
        }
        const bool joined = pump(client, [&] { return client.status() == NetClient::Status::Connected && client.hasView(); }, 5.0);
        if (std::string(address) == "127.0.0.1") CHECK(joined);
        if (joined) std::printf("    verbunden über %s\n", client.address().c_str());
        client.disconnect();
    }
    // Mehrere Adressen: die erste ist nicht erreichbar (TEST-NET-1), die zweite funktioniert
    NetClient fallback;
    CHECK(fallback.connect(std::vector<std::string>{"192.0.2.1", "127.0.0.1"}, options.port, "Ausweich"));
    CHECK(pump(fallback, [&] { return fallback.status() == NetClient::Status::Connected && fallback.hasView(); }, 10.0));
    fallback.disconnect();
    server.stop();
}

TEST(TexteInBeidenSprachen) {
    const Text t = tx("{} ist {}.", "{} is {}.", std::string("Anna"), roleText(Role::Seer));
    CHECK(t.de == "Anna ist Seherin.");
    CHECK(t.en == "Anna is Seer.");
    CHECK(joinNames({"A", "B", "C"}).en == "A, B and C");
    eng::ByteWriter w;
    Text{"gleich", "gleich"}.write(w);
    t.write(w);
    eng::ByteReader r(w.data());
    Text a, b;
    CHECK(a.read(r) && b.read(r));
    CHECK(a.en == "gleich" && b == t);
    setLanguage(Lang::English);
    CHECK(std::string(roleName(Role::Witch)) == "Witch");
    CHECK(std::string(L("Hallo", "Hello")) == "Hello");
    setLanguage(Lang::German);
    CHECK(Lf("{} Spieler", "{} players", 3) == "3 Spieler");
}

TEST(SpielerChatErreichtGenauEinenPassendenBot) {
    ServerThread server;
    ServerOptions options;
    options.port = kTestPort + 3;
    options.timeScale = 5.0f;
    options.logChat = false;
    std::string error;
    CHECK(server.start(options, error));
    if (!server.isRunning()) return;
    NetClient client;
    CHECK(client.connect("127.0.0.1", options.port, "Testerin"));
    CHECK(pump(client, [&] { return client.hasView(); }, 5.0));
    for (int i = 0; i < 4; ++i) client.sendAddBot();
    Settings settings;
    settings.captain = true; // first-day assembly before any night deaths
    settings.daySeconds = 40;
    settings.discussionSeconds = 120;
    settings.voteSeconds = 30;
    client.sendSettings(settings);
    CHECK(pump(client, [&] { return client.view().players.size() == 5 && client.view().settings.discussionSeconds == 120; }, 5.0));
    client.sendStartGame();
    CHECK(pump(client, [&] { return client.view().phase == Phase::Assembly; }, 12.0));
    uint8_t bot = kNoPlayer;
    std::string name;
    for (const auto& p : client.view().players) if (p.bot) { bot = p.id; name = p.name; break; }
    CHECK(bot != kNoPlayer);
    client.clearLog();
    // A greeting, not a role claim: the real seer among the bots would rightly object to that as well.
    client.sendChat(name + ", hallo", false);
    // Only answers to the greeting: a bot's own small talk may also start with "Testerin, ".
    auto isReply = [&](const LogEntry& entry) {
        return entry.isChat && entry.from != client.myId() && entry.text.de.starts_with("Testerin, ") &&
               entry.text.de.find("Verdächtiges") != std::string::npos;
    };
    auto replies = [&] {
        int count = 0;
        for (const auto& entry : client.log())
            if (isReply(entry)) ++count;
        return count;
    };
    CHECK(pump(client, [&] { return replies() > 0; }, 3.0));
    pump(client, [] { return false; }, 1.2); // allow enough time for accidental bot echoes
    CHECK_EQ(replies(), 1);
    for (const auto& entry : client.log()) {
        if (!isReply(entry)) continue;
        CHECK_EQ(entry.from, bot);
        CHECK_EQ(entry.channel, ChatChannel::Global);
        CHECK(entry.text.en.find("suspicious") != std::string::npos);
        std::printf("    %s: %s\n", entry.name.c_str(), entry.text.de.c_str());
    }
    client.disconnect();
    server.stop();
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    eng::log::setMinLevel(eng::log::Level::Warn);
    return test::runAll();
}
