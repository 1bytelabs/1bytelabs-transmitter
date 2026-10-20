#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>
namespace zosma {
// Worker is the sole producer; host callback is the sole consumer.
// Epoch tags prevent audio from a previous source/mode reaching the output.
class ReceiveQueue {
    struct Sample { float left{},right{}; std::uint64_t epoch{}; };
    std::vector<Sample> data_;
    alignas(64) std::atomic<std::uint64_t> read_{0};
    alignas(64) std::atomic<std::uint64_t> write_{0};
public:
    void prepare(std::size_t capacity) {
        data_.assign(capacity,{}); read_=0; write_=0;
    }
    std::size_t available() const noexcept {
        const auto r=read_.load(std::memory_order_acquire);
        const auto w=write_.load(std::memory_order_acquire);
        return w>=r ? static_cast<std::size_t>(w-r) : 0;
    }
    bool push(const float* l,const float* r,std::size_t n,std::uint64_t epoch) noexcept {
        const auto w=write_.load(std::memory_order_relaxed);
        const auto read=read_.load(std::memory_order_acquire);
        if(n>data_.size()-static_cast<std::size_t>(w-read)) return false;
        for(std::size_t i=0;i<n;++i) data_[(w+i)%data_.size()]={l[i],r[i],epoch};
        write_.store(w+n,std::memory_order_release); return true;
    }
    void discard() noexcept { read_.store(write_.load(std::memory_order_acquire),std::memory_order_release); }
    template<class T> bool pop(T* l,T* right,std::size_t n,std::uint64_t epoch) noexcept {
        const auto r=read_.load(std::memory_order_relaxed);
        const auto w=write_.load(std::memory_order_acquire);
        if(w-r<n) return false;
        for(std::size_t i=0;i<n;++i) {
            const auto& s=data_[(r+i)%data_.size()];
            if(l) l[i]=s.epoch==epoch ? static_cast<T>(s.left) : T(0);
            if(right) right[i]=s.epoch==epoch ? static_cast<T>(s.right) : T(0);
        }
        read_.store(r+n,std::memory_order_release); return true;
    }
};
}
