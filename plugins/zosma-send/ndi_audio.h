#pragma once
#include "audio_queue.h"
#include <thread>
#include <mutex>
#include <string>

namespace zosma {
class NdiAudio {
public:
    ~NdiAudio() { stop(); }
    void prepare(int sampleRate);
    void start();
    void stop();
    // Called only by UI/tests, never by the host audio callback.
    std::wstring diagnostic() const;
    AudioQueue queue;
    std::atomic_bool enabled{false};
    std::atomic_bool processing{false};
    std::atomic_int slot{1};
    // 0 disabled, 1 awaiting receiver, 2 connected, 3 runtime/sender error.
    std::atomic_int status{0};
    std::atomic_uint64_t dropped{0};
    std::atomic_int sampleRate{48000};
    std::atomic<float> peaks[2]{};
    std::atomic<float> sendDb{0.f};
private:
    void run();
    mutable std::mutex diagnosticMutex_;
    std::wstring diagnostic_{L"NDI ainda nao foi iniciado."};
    int sampleRate_{48000};
    std::atomic_bool stopping_{false};
    std::thread worker_;
};
}
