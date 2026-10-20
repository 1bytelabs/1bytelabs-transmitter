#include "ndi_audio.h"
#include "send_gain.h"
#include <windows.h>
#include <avrt.h>
#include <filesystem>
#include <mutex>
#include <cstdio>
#include <cmath>
#include <algorithm>
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
    const NDIlib_v6* acquire(std::wstring& report) {
        std::lock_guard lock(mutex);
        if (api) { ++references; report=L"Runtime NDI ja inicializado por outra instancia."; return api; }
        auto fail=[&](const wchar_t* stage,DWORD code,const std::wstring& path) {
            wchar_t message[1024]{};
            if(code) FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,code,0,message,1024,nullptr);
            report=std::wstring(stage)+L"\r\nCodigo Windows: "+std::to_wstring(code)+
                L"\r\n"+message+L"\r\nDLL: "+path;
        };
        HMODULE own{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&runtimeAnchor), &own)) {
            fail(L"Falha ao localizar o modulo do plugin",GetLastError(),L""); return nullptr;
        }
        wchar_t path[32768]{};
        if (!GetModuleFileNameW(own, path, 32768)) {
            fail(L"Falha ao obter o caminho do plugin",GetLastError(),L""); return nullptr;
        }
        const auto dll = std::filesystem::path(path).parent_path() / L"Processing.NDI.Lib.x64.dll";
        if(GetFileAttributesW(dll.c_str())==INVALID_FILE_ATTRIBUTES) {
            fail(L"DLL NDI ausente ou inacessivel",GetLastError(),dll.wstring()); return nullptr;
        }
        module = LoadLibraryExW(dll.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) {
            const DWORD code=GetLastError();
            fail(code==ERROR_MOD_NOT_FOUND ? L"DLL existe, mas uma dependencia nao foi encontrada" :
                code==ERROR_BAD_EXE_FORMAT ? L"DLL invalida ou arquitetura incompativel (requer x64)" :
                L"Windows recusou carregar a DLL NDI",code,dll.wstring()); return nullptr;
        }
        const auto load = reinterpret_cast<const NDIlib_v6* (*)()>(GetProcAddress(module,"NDIlib_v6_load"));
        if(!load) {
            fail(L"Runtime incompativel: export NDIlib_v6_load ausente",GetLastError(),dll.wstring());
            FreeLibrary(module); module=nullptr; return nullptr;
        }
        api=load();
        if (!api || !api->initialize()) {
            fail(api ? L"NDI initialize() retornou false (sem codigo Windows)" :
                L"NDIlib_v6_load retornou tabela nula",0,dll.wstring());
            api=nullptr; FreeLibrary(module); module=nullptr; return nullptr;
        }
        wchar_t loaded[32768]{}; GetModuleFileNameW(module,loaded,32768);
        report=L"Runtime NDI inicializado.\r\nDLL carregada: "+std::wstring(loaded);
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
std::wstring NdiAudio::diagnostic() const {
    std::lock_guard lock(diagnosticMutex_); return diagnostic_;
}
void NdiAudio::prepare(int rate) {
    stop(); sampleRate_=rate; sampleRate=rate;
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
    SendGain gain;
    gain.reset(sendDb.load(),sampleRate_);
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
            std::wstring report;
            if (!api) {
                api=runtime.acquire(report);
                std::lock_guard lock(diagnosticMutex_); diagnostic_=report;
            }
            if (api) {
                char name[64]{};
                std::snprintf(name,sizeof(name),"1Byte Labs Send %02d",requestedSlot);
                NDIlib_send_create_t create{};
                create.p_ndi_name=name;
                create.clock_audio=false; create.clock_video=false;
                sender=api->send_create(&create);
                currentSlot=requestedSlot;
                if(!sender) {
                    std::lock_guard lock(diagnosticMutex_);
                    if(diagnostic_.find(L"send_create")==std::wstring::npos)
                        diagnostic_=L"Runtime inicializou, mas NDI send_create retornou nulo.\r\n"+diagnostic_;
                }
            }
            if (!sender) { status=3; retryAt=now+2000; continue; }
            gain.reset(sendDb.load(),sampleRate_);
            status=1; lastConnectionCheck=0;
        }
        if (now-lastConnectionCheck>=250) {
            status=api->send_get_no_connections(sender,0)>0 ? 2 : 1;
            lastConnectionCheck=now;
        }
        if (!queue.pop(packet.data(),packet.data()+packetSamples,packetSamples)) {
            Sleep(1); continue;
        }
        gain.setDb(sendDb.load(std::memory_order_relaxed));
        gain.apply(packet.data(),packet.data()+packetSamples,packetSamples);
        // Meter only the already queued packet on the network worker. The host
        // callback is identical to 0.1.0: no peak scans or CAS loops there.
        static_assert(std::atomic<float>::is_always_lock_free);
        for(int channel=0;channel<2;++channel) {
            float peak=0;
            for(int i=0;i<packetSamples;++i) {
                const float sample=std::abs(packet[static_cast<std::size_t>(channel)*packetSamples+i]);
                if(std::isfinite(sample)) peak=std::max(peak,sample);
            }
            auto previous=peaks[channel].load(std::memory_order_relaxed);
            while(previous<peak && !peaks[channel].compare_exchange_weak(previous,peak,std::memory_order_relaxed)) {}
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
