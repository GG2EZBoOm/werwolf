// Tests des Auto-Updaters: Prüfsummen, JSON, Manifest, Installation und ein kompletter
// Download über einen lokalen HTTP-Server.

#include "TestFramework.h"

#include "client/Updater.h"
#include "update/UpdateCore.h"

#include "engine/core/Json.h"
#include "engine/core/Sha256.h"
#include "engine/core/Time.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

using namespace ww;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / ("werwolf_test_" + std::string(name));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Winziger HTTP-Server für Tests (nur GET, eine Anfrage pro Verbindung)
class TestHttpServer {
public:
    std::map<std::string, std::string> files;

    bool start() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        int len = sizeof addr;
        getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        listen(m_socket, 8);
        m_running = true;
        m_thread = std::thread([this] { loop(); });
        return true;
    }

    ~TestHttpServer() {
        m_running = false;
        closesocket(m_socket);
        if (m_thread.joinable()) m_thread.join();
    }

    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(m_port) + path; }

private:
    void loop() {
        while (m_running) {
            SOCKET c = accept(m_socket, nullptr, nullptr);
            if (c == INVALID_SOCKET) break;
            std::string request;
            char buf[2048];
            while (request.find("\r\n\r\n") == std::string::npos) {
                const int n = recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                request.append(buf, static_cast<size_t>(n));
            }
            const size_t a = request.find(' ');
            const size_t b = request.find(' ', a + 1);
            const std::string path = a != std::string::npos && b != std::string::npos ? request.substr(a + 1, b - a - 1) : "/";
            auto it = files.find(path);
            std::string response;
            if (it == files.end()) {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else {
                response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(it->second.size()) +
                           "\r\nConnection: close\r\n\r\n" + it->second;
            }
            send(c, response.data(), static_cast<int>(response.size()), 0);
            shutdown(c, SD_SEND);
            closesocket(c);
        }
    }

    SOCKET m_socket = INVALID_SOCKET;
    uint16_t m_port = 0;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

bool waitFor(const Updater& u, double seconds) {
    const double end = eng::nowSeconds() + seconds;
    while (eng::nowSeconds() < end) {
        const Updater::State s = u.state();
        if (s != Updater::State::Checking && s != Updater::State::Downloading) return true;
        eng::sleepSeconds(0.01);
    }
    return false;
}

std::string manifestFor(const std::string& version, const std::string& path, const std::string& url, const std::string& content,
                        const std::string& shaOverride = {}) {
    const std::string sha = shaOverride.empty() ? eng::Sha256::hashHex(content) : shaOverride;
    return "{\"version\": \"" + version + "\", \"notes\": \"Neue Rollen und \\u00c4nderungen\", \"files\": [{\"path\": \"" +
           path + "\", \"url\": \"" + url + "\", \"sha256\": \"" + sha + "\", \"size\": " + std::to_string(content.size()) +
           "}]}";
}

} // namespace

TEST(Sha256Testvektoren) {
    CHECK(eng::Sha256::hashHex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(eng::Sha256::hashHex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(eng::Sha256::hashHex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    eng::Sha256 h;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) h.update(chunk.data(), chunk.size());
    CHECK(eng::Sha256::toHex(h.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(JsonLesen) {
    eng::json::Value v;
    std::string err;
    CHECK(eng::json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": "x\"y\u00e4\ud83d\ude00"}, "d": true, "e": null})", v, &err));
    CHECK(v.get("a")->array.size() == 3);
    CHECK(v.get("a")->array[2].number == -300.0);
    CHECK(v.get("b")->getString("c") == "x\"yä😀");
    CHECK(v.get("d")->boolean);
    CHECK(v.get("e")->isNull());
    CHECK(!eng::json::parse("{\"a\": }", v, &err));
    CHECK(!err.empty());
    CHECK(!eng::json::parse("[1, 2", v, &err));
    CHECK(!eng::json::parse("{} x", v, &err));
    CHECK(!eng::json::parse(std::string(200, '['), v, &err)); // zu tief verschachtelt
}

TEST(VersionenVergleichen) {
    update::Version a, b;
    CHECK(update::Version::parse("0.2.0", a));
    CHECK(update::Version::parse("v0.10.0", b));
    CHECK(b > a);
    CHECK(a.str() == "0.2.0");
    CHECK(!update::Version::parse("1.2", a));
    CHECK(!update::Version::parse("1.2.3-beta", a));
    CHECK(!update::Version::parse("", a));
}

TEST(ManifestPruefung) {
    update::Manifest m;
    std::string err;
    const std::string sha(64, 'a');
    CHECK(update::parseManifest(manifestFor("1.0.0", "werwolf.exe", "https://example.org/w.exe", "abc"), m, err));
    CHECK(m.files.size() == 1);
    CHECK(m.notes == "Neue Rollen und Änderungen");
    CHECK(!update::parseManifest(manifestFor("1.0.0", "../evil.exe", "https://example.org/w.exe", "abc"), m, err));
    CHECK(!update::parseManifest(manifestFor("1.0.0", "C:/evil.exe", "https://example.org/w.exe", "abc"), m, err));
    CHECK(!update::parseManifest(manifestFor("1.0.0", "werwolf.exe", "http://example.org/w.exe", "abc"), m, err));
    CHECK(!update::parseManifest(manifestFor("1.0.0", "werwolf.exe", "https://example.org/w.exe", "abc", "xyz"), m, err));
    CHECK(!update::parseManifest(manifestFor("eins", "werwolf.exe", "https://example.org/w.exe", "abc"), m, err));
    CHECK(update::isSafeRelativePath("data/fonts/a.ttf"));
    CHECK(!update::isSafeRelativePath("data//a"));
    CHECK(!update::isSafeRelativePath("werwolf.exe.old"));
}

TEST(InstallierenMitSicherungUndAufraeumen) {
    const fs::path dir = tempDir("install");
    writeFile(dir / "werwolf.exe", "alt");
    writeFile(dir / "fremd.txt.old", "gehört nicht uns");
    writeFile(dir / update::kStagingFolder / "werwolf.exe", "neu");
    writeFile(dir / update::kStagingFolder / "daten" / "info.txt", "neu dabei");
    std::vector<update::FileEntry> files = {{"werwolf.exe", "", "", 3}, {"daten/info.txt", "", "", 9}};
    std::string err;
    CHECK(update::installFiles(dir, dir / update::kStagingFolder, files, err));
    CHECK(readFile(dir / "werwolf.exe") == "neu");
    CHECK(readFile(dir / "werwolf.exe.old") == "alt");
    CHECK(readFile(dir / "daten" / "info.txt") == "neu dabei");
    update::cleanupOldFiles(dir);
    CHECK(!fs::exists(dir / "werwolf.exe.old"));
    CHECK(fs::exists(dir / "fremd.txt.old")); // fremde Dateien bleiben unangetastet
    CHECK(!fs::exists(dir / update::kStagingFolder));

    // Fehlende Datei: nichts wird verändert
    writeFile(dir / update::kStagingFolder / "werwolf.exe", "neuer");
    files.push_back({"fehlt.exe", "", "", 1});
    CHECK(!update::installFiles(dir, dir / update::kStagingFolder, files, err));
    CHECK(readFile(dir / "werwolf.exe") == "neu");
}

TEST(KompletterUpdateDurchlauf) {
    TestHttpServer server;
    CHECK(server.start());
    const std::string payload(200000, 'W');
    server.files["/v2/werwolf.exe"] = payload;
    server.files["/update.json"] = manifestFor("2.0.0", "werwolf.exe", server.url("/v2/werwolf.exe"), payload);

    const fs::path dir = tempDir("flow");
    writeFile(dir / "werwolf.exe", "alte Version");
    update::Version current;
    update::Version::parse("1.0.0", current);

    Updater updater;
    updater.start(server.url("/update.json"), current, dir);
    CHECK(waitFor(updater, 10.0));
    CHECK(updater.state() == Updater::State::ReadyToInstall);
    CHECK(updater.newVersion() == "2.0.0");
    std::string err;
    CHECK(updater.install(err));
    CHECK(readFile(dir / "werwolf.exe") == payload);

    // Schon aktuell
    Updater upToDate;
    update::Version newer;
    update::Version::parse("2.0.0", newer);
    upToDate.start(server.url("/update.json"), newer, dir);
    CHECK(waitFor(upToDate, 10.0));
    CHECK(upToDate.state() == Updater::State::UpToDate);

    // Manipulierte Datei wird abgelehnt
    server.files["/bad.json"] = manifestFor("3.0.0", "werwolf.exe", server.url("/v2/werwolf.exe"), payload, std::string(64, '0'));
    Updater tampered;
    tampered.start(server.url("/bad.json"), current, dir);
    CHECK(waitFor(tampered, 10.0));
    CHECK(tampered.state() == Updater::State::Failed);
    CHECK(readFile(dir / "werwolf.exe") == payload);

    // Nicht erreichbar
    Updater missing;
    missing.start(server.url("/gibtsnicht.json"), current, dir);
    CHECK(waitFor(missing, 10.0));
    CHECK(missing.state() == Updater::State::Failed);
    std::printf("    Meldung bei 404: %s\n", missing.status().c_str());
}

TEST(ClientUpdateBleibtClientUndLehntVollversionAb) {
    TestHttpServer server;
    CHECK(server.start());
    const std::string payload(1000, 'C');
    server.files["/client.exe"] = payload;
    server.files["/client.json"] = manifestFor("2.0.0", "werwolf_client.exe", server.url("/client.exe"), payload);
    server.files["/full.json"] = manifestFor("2.0.0", "werwolf.exe", server.url("/client.exe"), payload);
    const fs::path dir = tempDir("client-edition");
    writeFile(dir / "werwolf_client.exe", "alter Client");
    update::Version current; update::Version::parse("1.0.0", current);
    Updater wrong;
    wrong.start(server.url("/full.json"), current, dir, "werwolf_client.exe");
    CHECK(waitFor(wrong, 5)); CHECK(wrong.state() == Updater::State::Failed);
    CHECK(!fs::exists(dir / "werwolf.exe"));
    CHECK_EQ(readFile(dir / "werwolf_client.exe"), "alter Client");
    Updater client;
    client.start(server.url("/client.json"), current, dir, "werwolf_client.exe");
    CHECK(waitFor(client, 5)); CHECK(client.state() == Updater::State::ReadyToInstall);
    std::string error;
    CHECK(client.install(error));
    CHECK_EQ(readFile(dir / "werwolf_client.exe"), payload);
    CHECK(!fs::exists(dir / "werwolf.exe"));
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    return test::runAll();
}
