#include "ndi_audio.h"
#include <windows.h>
#include <avrt.h>
#include <filesystem>
#include <mutex>
#include <cstdio>
#include "Processing.NDI.Lib.h"

namespace zosma {
namespace {
// One runtime lifetime for all instances in this plug-in module. Only workers
// acquire/release it; no DLL loading, locks or NDI calls in the audio callback.
struct Runtime {
    std::mutex mutex;
    HMODULE module{};
    const NDIlib_v6* api{};
    unsigned references{};
    const NDIlib_v6* acquire() {
        std::lock_guard lock(mutex);
        if (api) { ++references; return api; }
        HMODULE own{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&runtimeAnchor), &own)) return nullptr;
        wchar_t path[32768]{};
        if (!GetModuleFileNameW(own, path, 32768)) return nullptr;
        const auto dll = std::filesystem::path(path).parent_path() / L"Processing.NDI.Lib.x64.dll";
        module = LoadLibraryExW(dll.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) return nullptr;
        const auto load = reinterpret_cast<const NDIlib_v6* (*)()>(GetProcAddress(module,"NDIlib_v6_load"));
        api = load ? load() : nullptr;
        if (!api || !api->initialize()) {
            api=nullptr; FreeLibrary(module); module=nullptr; return nullptr;
        }
        references=1; return api;
    }
    void release() {
        std::lock_guard lock(mutex);
        if (references && --references==0) {
            api->destroy(); api=nullptr; FreeLibrary(module); module=nullptr;
        }
    }
    static void runtimeAnchor() {}
};
Runtime runtime;
}
void NdiAudio::prepare(int rate) {
    stop(); sampleRate_=rate;
    queue.prepare(static_cast<std::size_t>(rate/10)); // bounded 100 ms backlog
    dropped=0;
}
void NdiAudio::start() {
    stop(); stopping_=false;
    worker_=std::thread([this] { run(); });
}
void NdiAudio::stop() {
    stopping_=true;
    if (worker_.joinable()) worker_.join();
    queue.discard(); status=0;
}
void NdiAudio::run() {
    DWORD taskIndex{};
    HANDLE task=AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (task) AvSetMmThreadPriority(task, AVRT_PRIORITY_HIGH);
    const int packetSamples=sampleRate_/100; // 10 ms, independent of host block size
    std::vector<float> packet(static_cast<std::size_t>(packetSamples)*2);
    const NDIlib_v6* api=nullptr;
    NDIlib_send_instance_t sender=nullptr;
    int currentSlot=0;
    ULONGLONG retryAt=0, lastConnectionCheck=0;
    while (!stopping_.load()) {
        const auto now=GetTickCount64();
        const int requestedSlot=slot.load();
        const bool active=enabled.load() && processing.load();
        if (!active || (sender && currentSlot!=requestedSlot)) {
            if (sender) { api->send_destroy(sender); sender=nullptr; }
            queue.discard(); status=0; retryAt=0;
        }
        if (!active) { Sleep(2); continue; }
        if (!sender) {
            queue.discard();
            if (now<retryAt) { Sleep(2); continue; }
            if (!api) api=runtime.acquire();
            if (api) {
                char name[64]{};
                std::snprintf(name,sizeof(name),"Zosma Send %02d",requestedSlot);
                NDIlib_send_create_t create{};
                create.p_ndi_name=name;
                create.clock_audio=false; create.clock_video=false;
                sender=api->send_create(&create);
                currentSlot=requestedSlot;
            }
            if (!sender) { status=3; retryAt=now+2000; continue; }
            status=1; lastConnectionCheck=0;
        }
        if (now-lastConnectionCheck>=250) {
            status=api->send_get_no_connections(sender,0)>0 ? 2 : 1;
            lastConnectionCheck=now;
        }
        if (!queue.pop(packet.data(),packet.data()+packetSamples,packetSamples)) {
            Sleep(1); continue;
        }
        NDIlib_audio_frame_v3_t frame{};
        frame.sample_rate=sampleRate_; frame.no_channels=2; frame.no_samples=packetSamples;
        frame.FourCC=NDIlib_FourCC_audio_type_FLTP;
        frame.timecode=NDIlib_send_timecode_synthesize;
        frame.p_data=reinterpret_cast<std::uint8_t*>(packet.data());
        frame.channel_stride_in_bytes=packetSamples*sizeof(float);
        api->send_send_audio_v3(sender,&frame);
    }
    if (sender) api->send_destroy(sender);
    if (api) runtime.release();
    if (task) AvRevertMmThreadCharacteristics(task);
}
}
