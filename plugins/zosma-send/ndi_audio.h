#pragma once
#include "audio_queue.h"
#include <thread>

namespace zosma {
class NdiAudio {
public:
    ~NdiAudio() { stop(); }
    void prepare(int sampleRate);
    void start();
    void stop();
    AudioQueue queue;
    std::atomic_bool enabled{false};
    std::atomic_bool processing{false};
    std::atomic_int slot{1};
    // 0 disabled, 1 awaiting receiver, 2 connected, 3 runtime/sender error.
    std::atomic_int status{0};
    std::atomic_uint64_t dropped{0};
    std::atomic_int sampleRate{48000};
    std::atomic<float> peaks[2]{};
private:
    void run();
    int sampleRate_{48000};
    std::atomic_bool stopping_{false};
    std::thread worker_;
};
}
