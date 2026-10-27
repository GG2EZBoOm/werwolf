#include "engine/audio/Audio.h"
#include "engine/core/Log.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <thread>

namespace eng::audio {
struct Device::Impl {
    static constexpr size_t kFrames = 441; // Four 10 ms buffers, independent of rendering.
    struct Buffer {
        WAVEHDR header{};
        std::array<int16_t, kFrames * 2> pcm{};
        bool prepared = false;
    };
    HWAVEOUT output = nullptr;
    HANDLE event = nullptr;
    std::array<Buffer, 4> buffers{};
    std::atomic<bool> running{false};
    std::thread thread;
};

Device::Device() = default;
Device::~Device() { stop(); }

bool Device::start(Mixer& mixer) {
    stop();
    m_impl = std::make_unique<Impl>();
    Impl& impl = *m_impl;
    impl.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = kSampleRate * format.nBlockAlign;
    if (!impl.event || waveOutOpen(&impl.output, WAVE_MAPPER, &format,
            reinterpret_cast<DWORD_PTR>(impl.event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log::warn("Audio-Ausgabe nicht verfügbar; das Spiel läuft ohne Ton weiter");
        stop();
        return false;
    }
    for (auto& buffer : impl.buffers) {
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(sizeof(buffer.pcm));
        if (waveOutPrepareHeader(impl.output, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            log::warn("Audio-Puffer konnte nicht vorbereitet werden");
            stop();
            return false;
        }
        buffer.prepared = true;
    }
    impl.running = true;
    impl.thread = std::thread([&impl, &mixer] {
        while (impl.running) {
            ResetEvent(impl.event);
            for (auto& buffer : impl.buffers) {
                if (!(buffer.header.dwFlags & WHDR_INQUEUE)) {
                    mixer.render(buffer.pcm);
                    if (waveOutWrite(impl.output, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                        log::warn("Audio-Gerät hat die Wiedergabe unterbrochen");
                        impl.running = false;
                        break;
                    }
                }
            }
            WaitForSingleObject(impl.event, 20);
        }
    });
    log::info("Audio gestartet: 44,1 kHz, Stereo");
    return true;
}

void Device::stop() {
    if (!m_impl) return;
    Impl& impl = *m_impl;
    impl.running = false;
    if (impl.event) SetEvent(impl.event);
    if (impl.thread.joinable()) impl.thread.join();
    if (impl.output) {
        waveOutReset(impl.output);
        for (auto& buffer : impl.buffers)
            if (buffer.prepared) waveOutUnprepareHeader(impl.output, &buffer.header, sizeof(WAVEHDR));
        waveOutClose(impl.output);
    }
    if (impl.event) CloseHandle(impl.event);
    m_impl.reset();
}
} // namespace eng::audio
