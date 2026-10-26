#include "client/Updater.h"

#include "engine/core/Log.h"
#include "engine/core/Sha256.h"
#include "engine/core/Utf8.h"
#include "engine/net/Http.h"

#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ww {

namespace fs = std::filesystem;

Updater::~Updater() {
    cancel();
    if (m_thread.joinable()) m_thread.join();
}

void Updater::start(const std::string& manifestUrl, const update::Version& current, const fs::path& installDir) {
    if (m_thread.joinable()) m_thread.join();
    m_url = manifestUrl;
    m_current = current;
    m_installDir = installDir;
    m_cancel = false;
    m_progress = 0.0f;
    m_state = State::Checking;
    setStatus("Suche nach Updates …");
    m_thread = std::thread([this] { run(); });
}

void Updater::cancel() {
    m_cancel = true;
    const State s = m_state.load();
    if (s == State::Checking || s == State::Downloading) {
        m_state = State::Cancelled;
        setStatus("Update übersprungen.");
    }
}

std::string Updater::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

std::string Updater::newVersion() const {
    std::lock_guard lock(m_mutex);
    return m_manifest.versionText;
}

std::string Updater::notes() const {
    std::lock_guard lock(m_mutex);
    return m_manifest.notes;
}

bool Updater::finished() const {
    const State s = m_state.load();
    return s == State::UpToDate || s == State::Failed || s == State::Cancelled || s == State::Installed;
}

void Updater::setStatus(std::string text) {
    std::lock_guard lock(m_mutex);
    m_status = std::move(text);
}

void Updater::fail(const std::string& text) {
    if (m_state.load() == State::Cancelled) return;
    eng::log::warn("Update: {}", text);
    setStatus(text);
    m_state = State::Failed;
}

void Updater::run() {
    eng::log::setThreadTag("update");
    eng::net::HttpOptions options;
    options.userAgent = "Werwolf-Updater/" + m_current.str();
    options.cancel = &m_cancel;
    options.timeoutMs = 8000;
    options.maxBytes = 1024 * 1024;

    std::vector<uint8_t> data;
    std::string error;
    if (!eng::net::httpGet(m_url, data, error, options)) {
        fail("Update-Prüfung fehlgeschlagen: " + error);
        return;
    }
    update::Manifest manifest;
    if (!update::parseManifest(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), manifest, error)) {
        fail(error);
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        m_manifest = manifest;
    }
    if (m_cancel) return;
    if (!(manifest.version > m_current)) {
        eng::log::info("Update: Version {} ist aktuell (Server: {})", m_current.str(), manifest.versionText);
        setStatus("Du spielst die neueste Version (" + m_current.str() + ").");
        State expected = State::Checking;
        m_state.compare_exchange_strong(expected, State::UpToDate);
        return;
    }

    eng::log::info("Update: lade Version {} herunter", manifest.versionText);
    State expected = State::Checking;
    if (!m_state.compare_exchange_strong(expected, State::Downloading)) return; // inzwischen übersprungen
    const fs::path staging = m_installDir / update::kStagingFolder;
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);

    uint64_t total = 0, done = 0;
    for (const update::FileEntry& f : manifest.files) total += f.size;
    for (const update::FileEntry& f : manifest.files) {
        setStatus("Lade Version " + manifest.versionText + " herunter … (" + f.path + ")");
        eng::net::HttpOptions fileOptions = options;
        fileOptions.timeoutMs = 30000;
        fileOptions.maxBytes = f.size;
        fileOptions.progress = [&](uint64_t received, uint64_t) {
            m_progress = total > 0 ? static_cast<float>(static_cast<double>(done + received) / static_cast<double>(total)) : 0.0f;
        };
        std::vector<uint8_t> file;
        if (!eng::net::httpGet(f.url, file, error, fileOptions)) {
            fail("Download fehlgeschlagen (" + f.path + "): " + error);
            return;
        }
        if (file.size() != f.size || eng::Sha256::hashHex(file.data(), file.size()) != f.sha256) {
            fail("Die heruntergeladene Datei " + f.path + " ist beschädigt (Prüfsumme stimmt nicht).");
            return;
        }
        const fs::path out = staging / fs::path(f.path);
        fs::create_directories(out.parent_path(), ec);
        std::ofstream stream(out, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!stream) {
            fail("Datei konnte nicht gespeichert werden: " + f.path);
            return;
        }
        done += f.size;
        m_progress = total > 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 1.0f;
    }
    if (m_cancel) return;
    setStatus("Version " + manifest.versionText + " wird installiert …");
    State downloading = State::Downloading;
    m_state.compare_exchange_strong(downloading, State::ReadyToInstall);
}

bool Updater::install(std::string& error) {
    if (m_thread.joinable()) m_thread.join();
    update::Manifest manifest;
    {
        std::lock_guard lock(m_mutex);
        manifest = m_manifest;
    }
    if (!update::installFiles(m_installDir, m_installDir / update::kStagingFolder, manifest.files, error)) {
        fail("Installation fehlgeschlagen: " + error);
        return false;
    }
    eng::log::info("Update: Version {} installiert", manifest.versionText);
    setStatus("Version " + manifest.versionText + " installiert – Neustart …");
    m_state = State::Installed;
    return true;
}

bool restartApplication(const std::string& extraArgs) {
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    // ursprüngliche Argumente übernehmen (ohne den Programmnamen)
    std::wstring original = GetCommandLineW();
    size_t pos = 0;
    if (!original.empty() && original[0] == L'"') {
        pos = original.find(L'"', 1);
        pos = pos == std::wstring::npos ? original.size() : pos + 1;
    } else {
        pos = original.find(L' ');
        if (pos == std::wstring::npos) pos = original.size();
    }
    const std::wstring rest = original.substr(pos);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"" + rest + L" " + eng::utf8::toWide(extraArgs);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)extraArgs;
    return false;
#endif
}

} // namespace ww
