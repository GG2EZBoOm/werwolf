#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Plattformunabhängiger Kern des Auto-Updaters: Versionen, Manifest, Installation.
namespace ww::update {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static bool parse(std::string_view text, Version& out); // "1.2.3" oder "v1.2.3"
    std::string str() const;
    auto operator<=>(const Version&) const = default;
};

struct FileEntry {
    std::string path;   // relativ zum Spielordner, z. B. "werwolf.exe"
    std::string url;    // HTTPS-Quelle
    std::string sha256; // 64 Hex-Zeichen (klein)
    uint64_t size = 0;
};

// Inhalt von update.json
struct Manifest {
    Version version;
    std::string versionText;
    std::string notes;
    std::vector<FileEntry> files;
};

bool parseManifest(std::string_view json, Manifest& out, std::string& error);
bool isSafeRelativePath(std::string_view path);
bool isAllowedUrl(std::string_view url);

// Tauscht die bereitgestellten Dateien gegen die installierten aus.
// Bestehende Dateien werden zu "<name>.old" umbenannt (funktioniert auch für die laufende exe).
// Schlägt ein Schritt fehl, wird alles zurückgerollt.
bool installFiles(const std::filesystem::path& installDir, const std::filesystem::path& stagingDir,
                  const std::vector<FileEntry>& files, std::string& error);

// Entfernt Reste früherer Updates (*.old, Zwischenordner).
void cleanupOldFiles(const std::filesystem::path& installDir);

constexpr const char* kStagingFolder = ".update";
constexpr const char* kCleanupList = ".update-cleanup.txt";
constexpr uint64_t kMaxFileSize = 256ull * 1024 * 1024;

} // namespace ww::update
