#pragma once

#include "update/UpdateCore.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace ww {

// Prüft beim Start auf eine neue Version, lädt sie im Hintergrund und installiert sie.
class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Downloading, ReadyToInstall, Installed, Failed, Cancelled };

    ~Updater();
    void start(const std::string& manifestUrl, const update::Version& current, const std::filesystem::path& installDir,
               const std::string& exclusiveExecutable = {});
    void cancel();
    // Nur aufrufen, wenn state() == ReadyToInstall (im Hauptthread)
    bool install(std::string& error);

    State state() const { return m_state.load(); }
    float progress() const { return m_progress.load(); }
    std::string status() const;
    std::string newVersion() const;
    std::string notes() const;
    bool finished() const;

private:
    void run();
    void setStatus(std::string text);
    void fail(const std::string& text);

    std::thread m_thread;
    std::atomic<State> m_state{State::Idle};
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_mutex;
    std::string m_status;
    std::string m_url;
    std::string m_exclusiveExecutable;
    update::Version m_current;
    update::Manifest m_manifest;
    std::filesystem::path m_installDir;
};

// Startet das Programm neu (mit zusätzlichem Argument) – das aktuelle muss sich danach beenden.
bool restartApplication(const std::string& extraArgs);

} // namespace ww
