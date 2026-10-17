#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zosma {
// Exactly one producer (host audio callback) and one consumer (network worker).
// Storage and counters are reset only when neither thread is running.
class AudioQueue {
public:
    void prepare(std::size_t capacity) {
        capacity_ = capacity;
        samples_.assign(capacity * 2, 0.f);
        read_.store(0); write_.store(0);
    }
    template<class Sample>
    bool push(const Sample* left, const Sample* right, std::size_t count,
              bool silentLeft = false, bool silentRight = false) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (count > capacity_ - static_cast<std::size_t>(w-r)) return false;
        for (std::size_t i=0; i<count; ++i) {
            const auto p = static_cast<std::size_t>((w+i) % capacity_);
            samples_[p*2] = silentLeft || !left ? 0.f : static_cast<float>(left[i]);
            samples_[p*2+1] = silentRight || !right ? 0.f : static_cast<float>(right[i]);
        }
        write_.store(w+count, std::memory_order_release);
        return true;
    }
    bool pop(float* left, float* right, std::size_t count) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        if (w-r < count) return false;
        for (std::size_t i=0; i<count; ++i) {
            const auto p = static_cast<std::size_t>((r+i) % capacity_);
            left[i] = samples_[p*2]; right[i] = samples_[p*2+1];
        }
        read_.store(r+count, std::memory_order_release);
        return true;
    }
    // Consumer only. Never modifies the producer cursor.
    void discard() noexcept { read_.store(write_.load(std::memory_order_acquire), std::memory_order_release); }
    std::size_t available() const noexcept {
        const auto r = read_.load(std::memory_order_acquire);
        const auto w = write_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(w-r);
    }
private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::vector<float> samples_;
    std::size_t capacity_{};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    alignas(64) std::atomic<std::uint64_t> write_{0};
};
}
