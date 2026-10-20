#pragma once
#include "audio_queue.h"
#include "receive_queue.h"
#include <vector>
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
    // UI/state only: source strings never cross the realtime callback.
    std::vector<std::string> sources() const;
    std::string selectedSource() const;
    void selectSource(const std::string& name);
    std::size_t queuedSamples() const { return receiveMode ? receiveQueue.available() : queue.available(); }
    ReceiveQueue receiveQueue;
    std::atomic_int receiveBlock{480};
    std::atomic_uint64_t hostCallbacks{0}, hostSamples{0}, receiveFrames{0}, receivePlayed{0}, invalidReceiveFrames{0};
    std::atomic_int hostBlock{0}, receiveConnections{0}, incomingRate{0}, incomingChannels{0};
    std::atomic_bool receiveMode{false};
    std::atomic_uint64_t receiveEpoch{1}, underruns{0};
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
    void runReceiver();
    mutable std::mutex sourcesMutex_;
    std::vector<std::string> sources_;
    std::string selectedSource_;
    mutable std::mutex diagnosticMutex_;
    std::wstring diagnostic_{L"NDI ainda nao foi iniciado."};
    int sampleRate_{48000};
    std::atomic_bool stopping_{false};
    std::thread worker_;
};
}
