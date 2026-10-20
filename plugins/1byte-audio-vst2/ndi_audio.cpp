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
    std::wstring report;
    { std::lock_guard lock(diagnosticMutex_); report=diagnostic_; }
    report+=L"\r\n\r\nModo: "+std::wstring(receiveMode ? L"Receber" : L"Enviar")+
        L"\r\nLigado: "+std::to_wstring(enabled.load())+L" / Processamento: "+std::to_wstring(processing.load())+
        L"\r\nStatus NDI (0 desligado, 1 aguardando, 2 conectado, 3 erro): "+std::to_wstring(status.load())+
        L"\r\nChamadas de audio do host: "+std::to_wstring(hostCallbacks.load())+
        L"\r\nSamples do host: "+std::to_wstring(hostSamples.load())+
        L"\r\nUltimo bloco do host: "+std::to_wstring(hostBlock.load())+
        L"\r\nTaxa do host: "+std::to_wstring(sampleRate.load())+
        L"\r\nConexoes de recepcao: "+std::to_wstring(receiveConnections.load())+
        L"\r\nFormato de entrada NDI: "+std::to_wstring(incomingRate.load())+L" Hz / "+
        std::to_wstring(incomingChannels.load())+L" canais"+
        L"\r\nFrames FrameSync validos: "+std::to_wstring(receiveFrames.load())+
        L"\r\nFrames FrameSync invalidos: "+std::to_wstring(invalidReceiveFrames.load())+
        L"\r\nSamples recebidos entregues ao host: "+std::to_wstring(receivePlayed.load())+
        L"\r\nFila de recepcao (samples): "+std::to_wstring(receiveQueue.available())+
        L"\r\nFaltas de buffer: "+std::to_wstring(underruns.load())+
        L"\r\n\r\nSe Chamadas de audio nao aumentar entre dois relatorios, o host nao esta chamando o filtro. "+
        L"No OBS, use uma fonte de audio ativa, com o filtro habilitado. NDI FrameSync pode gerar silencio quando nao ha sinal.";
    return report;
}
std::vector<std::string> NdiAudio::sources() const {
    std::lock_guard lock(sourcesMutex_); return sources_;
}
std::string NdiAudio::selectedSource() const {
    std::lock_guard lock(sourcesMutex_); return selectedSource_;
}
void NdiAudio::selectSource(const std::string& name) {
    std::lock_guard lock(sourcesMutex_);
    if(selectedSource_!=name) { selectedSource_=name; ++receiveEpoch; }
}
void NdiAudio::prepare(int rate) {
    stop(); sampleRate_=rate; sampleRate=rate;
    queue.prepare(static_cast<std::size_t>(rate/10)); // bounded 100 ms backlog
    receiveQueue.prepare(static_cast<std::size_t>(rate));
    dropped=0; underruns=0; ++receiveEpoch;
    hostCallbacks=0;hostSamples=0;receiveFrames=0;receivePlayed=0;invalidReceiveFrames=0;
    hostBlock=0;receiveConnections=0;incomingRate=0;incomingChannels=0;
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
        if(receiveMode.load()) {
            if(sender) { api->send_destroy(sender); sender=nullptr; }
            if(api) { runtime.release(); api=nullptr; }
            queue.discard(); runReceiver(); retryAt=0; continue;
        }
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
void NdiAudio::runReceiver() {
    std::wstring report;
    const auto* api=runtime.acquire(report);
    { std::lock_guard lock(diagnosticMutex_); diagnostic_=report; }
    if(!api) { status=3; for(int i=0;i<200 && !stopping_ && receiveMode;++i) Sleep(10); return; }
    if(!api->find_create_v2 || !api->find_get_current_sources || !api->recv_create_v3 ||
       !api->framesync_create || !api->framesync_capture_audio || !api->framesync_free_audio ||
       !api->framesync_destroy || !api->recv_destroy || !api->find_destroy || !api->recv_get_no_connections) {
        { std::lock_guard lock(diagnosticMutex_); diagnostic_=L"Runtime sem as funcoes NDI Receive/FrameSync."; }
        status=3; runtime.release();
        for(int i=0;i<200 && !stopping_ && receiveMode;++i) Sleep(10);
        return;
    }
    NDIlib_find_create_t findSettings{}; findSettings.show_local_sources=true;
    auto finder=api->find_create_v2(&findSettings);
    if(!finder) {
        { std::lock_guard lock(diagnosticMutex_); diagnostic_=L"NDI find_create_v2 retornou nulo."; }
        status=3; runtime.release();
        for(int i=0;i<200 && !stopping_ && receiveMode;++i) Sleep(10);
        return;
    }
    NDIlib_recv_instance_t receiver=nullptr;
    NDIlib_framesync_instance_t sync=nullptr;
    std::uint64_t connectedEpoch=0;
    ULONGLONG scanAt=0,retryAt=0,formatAt=0;
    std::vector<std::pair<std::string,std::string>> found;
    SendGain gain; gain.reset(sendDb.load(),sampleRate_);
    const int count=sampleRate_/100;
    // At least 50 ms plus one host block is filled on demand, paced by consumption.
    while(!stopping_ && receiveMode.load()) {
        const auto now=GetTickCount64();
        if(now>=scanAt) {
            uint32_t n=0; const auto* list=api->find_get_current_sources(finder,&n);
            found.clear(); std::vector<std::string> names;
            for(uint32_t i=0;list && i<n;++i) if(list[i].p_ndi_name) {
                names.emplace_back(list[i].p_ndi_name);
                found.emplace_back(list[i].p_ndi_name,list[i].p_url_address ? list[i].p_url_address : "");
            }
            std::sort(names.begin(),names.end());
            { std::lock_guard lock(sourcesMutex_); sources_=std::move(names); }
            scanAt=now+1000;
        }
        const auto epoch=receiveEpoch.load();
        const bool active=enabled.load() && processing.load();
        if(receiver && (!active || epoch!=connectedEpoch)) {
            api->framesync_destroy(sync); sync=nullptr;
            api->recv_destroy(receiver); receiver=nullptr; retryAt=0;
        }
        if(!active) { status=0; Sleep(5); continue; }
        const auto selected=selectedSource();
        if(selected.empty()) { status=1; Sleep(5); continue; }
        if(!receiver) {
            if(now<retryAt) { Sleep(5); continue; }
            NDIlib_recv_create_v3_t settings{};
            std::string url;
            for(const auto& item:found) if(item.first==selected) { url=item.second; break; }
            settings.source_to_connect_to.p_ndi_name=selected.c_str();
            settings.source_to_connect_to.p_url_address=url.empty() ? nullptr : url.c_str();
            settings.color_format=NDIlib_recv_color_format_fastest;
            settings.bandwidth=NDIlib_recv_bandwidth_audio_only;
            settings.allow_video_fields=false;
            settings.p_ndi_recv_name="1Byte Labs Audio Receive";
            receiver=api->recv_create_v3(&settings);
            if(receiver) sync=api->framesync_create(receiver);
            if(!receiver || !sync) {
                if(receiver) api->recv_destroy(receiver);
                receiver=nullptr;
                { std::lock_guard lock(diagnosticMutex_); diagnostic_=L"NDI recv_create_v3 ou framesync_create retornou nulo."; }
                status=3; retryAt=now+2000; Sleep(5); continue;
            }
            connectedEpoch=epoch; gain.reset(sendDb.load(),sampleRate_);
        }
        receiveConnections=api->recv_get_no_connections(receiver);
        status=receiveConnections>0 ? 2 : 1;
        if(now>=formatAt) {
            NDIlib_audio_frame_v2_t probe{};
            api->framesync_capture_audio(sync,&probe,0,0,0);
            incomingRate=probe.sample_rate;incomingChannels=probe.no_channels;
            api->framesync_free_audio(sync,&probe);formatAt=now+250;
        }
        // Backpressure follows the host clock. FrameSync converts rate/channels and
        // continuously corrects the sender/receiver clock difference.
        if(receiveQueue.available()>=static_cast<std::size_t>(std::min(sampleRate_/2,std::max(sampleRate_/20,receiveBlock.load()+sampleRate_/50)))) { Sleep(1); continue; }
        NDIlib_audio_frame_v2_t frame{};
        api->framesync_capture_audio(sync,&frame,sampleRate_,2,count);
        if(frame.p_data && frame.no_channels>=2 && frame.no_samples==count && frame.channel_stride_in_bytes>=count*static_cast<int>(sizeof(float))) {
            ++receiveFrames;
            float* left=frame.p_data;
            float* right=reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(frame.p_data)+frame.channel_stride_in_bytes);
            gain.setDb(sendDb.load()); gain.apply(left,right,count);
            for(int ch=0;ch<2;++ch) {
                float peak=0; const auto* samples=ch ? right : left;
                for(int i=0;i<count;++i) if(std::isfinite(samples[i])) peak=std::max(peak,std::abs(samples[i]));
                auto old=peaks[ch].load();
                while(old<peak && !peaks[ch].compare_exchange_weak(old,peak)) {}
            }
            if(!receiveQueue.push(left,right,count,connectedEpoch)) dropped.fetch_add(count);
        }
        if(!frame.p_data || frame.no_channels<2 || frame.no_samples!=count || frame.channel_stride_in_bytes<count*static_cast<int>(sizeof(float))) ++invalidReceiveFrames;
        api->framesync_free_audio(sync,&frame);
        Sleep(1);
    }
    if(sync) api->framesync_destroy(sync);
    if(receiver) api->recv_destroy(receiver);
    api->find_destroy(finder); runtime.release();
    receiveConnections=0;incomingRate=0;incomingChannels=0;
}

}
