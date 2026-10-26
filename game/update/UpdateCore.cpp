#include "update/UpdateCore.h"

#include "engine/core/Json.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace ww::update {

namespace fs = std::filesystem;

bool Version::parse(std::string_view text, Version& out) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text.remove_prefix(1);
    int parts[3] = {0, 0, 0};
    int index = 0;
    bool digit = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            parts[index] = parts[index] * 10 + (c - '0');
            if (parts[index] > 100000) return false;
            digit = true;
        } else if (c == '.' && digit && index < 2) {
            ++index;
            digit = false;
        } else {
            return false;
        }
    }
    if (!digit || index != 2) return false;
    out = {parts[0], parts[1], parts[2]};
    return true;
}

std::string Version::str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool isSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 200 || path.front() == '/' || path.front() == '\\') return false;
    for (char c : path) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-' || c == '/';
        if (!ok) return false;
    }
    // keine Ausbrüche aus dem Spielordner, keine leeren Pfadteile
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find('/', start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    // keine Reste alter Updates überschreiben
    return !(path.size() >= 4 && path.substr(path.size() - 4) == ".old");
}

bool isAllowedUrl(std::string_view url) {
    if (url.size() > 2000) return false;
    if (url.rfind("https://", 0) == 0) return url.size() > 8;
    return url.rfind("http://localhost:", 0) == 0 || url.rfind("http://127.0.0.1:", 0) == 0;
}

bool parseManifest(std::string_view json, Manifest& out, std::string& error) {
    out = Manifest{};
    eng::json::Value root;
    if (!eng::json::parse(json, root, &error)) {
        error = "update.json ist fehlerhaft: " + error;
        return false;
    }
    if (!root.isObject()) {
        error = "update.json: Objekt erwartet";
        return false;
    }
    out.versionText = root.getString("version");
    if (!Version::parse(out.versionText, out.version)) {
        error = "update.json: ungültige Version \"" + out.versionText + "\"";
        return false;
    }
    out.notes = root.getString("notes");
    if (out.notes.size() > 4000) out.notes.resize(4000);

    const eng::json::Value* files = root.get("files");
    if (!files || !files->isArray() || files->array.empty() || files->array.size() > 64) {
        error = "update.json: Dateiliste fehlt";
        return false;
    }
    for (const eng::json::Value& f : files->array) {
        FileEntry e;
        e.path = f.getString("path");
        e.url = f.getString("url");
        e.sha256 = f.getString("sha256");
        const double size = f.getNumber("size", -1.0);
        if (!isSafeRelativePath(e.path)) {
            error = "update.json: unsicherer Dateipfad \"" + e.path + "\"";
            return false;
        }
        if (!isAllowedUrl(e.url)) {
            error = "update.json: unsichere Adresse für " + e.path;
            return false;
        }
        if (e.sha256.size() != 64 || e.sha256.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            error = "update.json: ungültige Prüfsumme für " + e.path;
            return false;
        }
        for (char& c : e.sha256)
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (size <= 0.0 || size > static_cast<double>(kMaxFileSize)) {
            error = "update.json: ungültige Größe für " + e.path;
            return false;
        }
        e.size = static_cast<uint64_t>(size);
        for (const FileEntry& other : out.files) {
            if (other.path == e.path) {
                error = "update.json: Datei doppelt aufgeführt: " + e.path;
                return false;
            }
        }
        out.files.push_back(std::move(e));
    }
    return true;
}

bool installFiles(const fs::path& installDir, const fs::path& stagingDir, const std::vector<FileEntry>& files,
                  std::string& error) {
    struct Step {
        fs::path target;
        fs::path backup;
        bool hadOriginal;
    };
    std::vector<Step> done;
    std::error_code ec;

    auto rollback = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code e;
            fs::remove(it->target, e);
            if (it->hadOriginal) fs::rename(it->backup, it->target, e);
        }
    };

    for (const FileEntry& f : files) {
        // Pfade sind geprüft und bestehen nur aus ASCII-Zeichen
        const fs::path staged = stagingDir / fs::path(f.path);
        const fs::path target = installDir / fs::path(f.path);
        const fs::path backup = fs::path(target).concat(".old");
        if (!fs::exists(staged, ec)) {
            error = "Heruntergeladene Datei fehlt: " + f.path;
            rollback();
            return false;
        }
        fs::create_directories(target.parent_path(), ec);
        const bool hadOriginal = fs::exists(target, ec);
        if (hadOriginal) {
            fs::remove(backup, ec); // Rest eines früheren Updates
            fs::rename(target, backup, ec);
            if (ec) {
                error = "Keine Schreibrechte im Spielordner (" + f.path + "): " + ec.message();
                rollback();
                return false;
            }
        }
        fs::rename(staged, target, ec);
        if (ec) {
            // Umbenennen über Laufwerksgrenzen hinweg nicht möglich: kopieren
            ec.clear();
            fs::copy_file(staged, target, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            error = "Datei konnte nicht ersetzt werden (" + f.path + "): " + ec.message();
            if (hadOriginal) {
                std::error_code e;
                fs::rename(backup, target, e);
            }
            rollback();
            return false;
        }
        done.push_back({target, backup, hadOriginal});
    }
    fs::remove_all(stagingDir, ec);

    // Nur die eigenen Sicherungen später aufräumen – nie fremde *.old-Dateien
    std::ofstream list(installDir / kCleanupList, std::ios::app);
    for (const Step& s : done)
        if (s.hadOriginal) list << fs::relative(s.backup, installDir, ec).generic_string() << "\n";
    return true;
}

void cleanupOldFiles(const fs::path& installDir) {
    std::error_code ec;
    const fs::path listPath = installDir / kCleanupList;
    std::vector<std::string> remaining;
    if (std::ifstream list(listPath); list) {
        std::string line;
        while (std::getline(list, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string original = line.size() > 4 ? line.substr(0, line.size() - 4) : std::string();
            if (line.size() < 5 || line.substr(line.size() - 4) != ".old" || !isSafeRelativePath(original)) continue;
            fs::remove(installDir / fs::path(line), ec);
            // gelingt nicht, solange die alte Version noch läuft – dann beim nächsten Start
            if (ec && fs::exists(installDir / fs::path(line))) remaining.push_back(line);
            ec.clear();
        }
    }
    if (remaining.empty()) {
        fs::remove(listPath, ec);
    } else {
        std::ofstream list(listPath, std::ios::trunc);
        for (const std::string& r : remaining) list << r << "\n";
    }
    fs::remove_all(installDir / kStagingFolder, ec);
}

} // namespace ww::update
