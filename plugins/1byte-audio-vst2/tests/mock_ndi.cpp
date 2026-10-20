// Test fixture only. Never included in the distributable/installer.
#include "Processing.NDI.Lib.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
std::atomic_int failure{0};
std::atomic_int frames{0},lastRate{0},senders{0};
std::atomic<float> leftPeak{0},rightPeak{0};
bool initialize() { return failure!=1; }
void destroy() {}
NDIlib_send_instance_t create(const NDIlib_send_create_t*) {
    if(failure==2) return nullptr;
    ++senders;return reinterpret_cast<NDIlib_send_instance_t>(new int(1));
}
void close(NDIlib_send_instance_t sender) { delete reinterpret_cast<int*>(sender);--senders; }
int connections(NDIlib_send_instance_t,uint32_t) { return 1; }
void capture(NDIlib_send_instance_t,const NDIlib_audio_frame_v3_t* frame) {
    float peaks[2]{};
    for(int ch=0;ch<2;++ch) {
        const auto* data=reinterpret_cast<const float*>(frame->p_data+ch*frame->channel_stride_in_bytes);
        for(int i=0;i<frame->no_samples;++i) peaks[ch]=std::max(peaks[ch],std::abs(data[i]));
    }
    leftPeak=peaks[0];rightPeak=peaks[1];lastRate=frame->sample_rate;++frames;
}
std::atomic_int receivers{0},syncs{0},finders{0},receiveRate{0};
struct Receiver { bool alternate; };
NDIlib_find_instance_t findCreate(const NDIlib_find_create_t*) {
    ++finders; return reinterpret_cast<NDIlib_find_instance_t>(new int(1));
}
void findClose(NDIlib_find_instance_t p) { delete reinterpret_cast<int*>(p); --finders; }
const NDIlib_source_t* findSources(NDIlib_find_instance_t,uint32_t* count) {
    static NDIlib_source_t sources[2]{};
    sources[0].p_ndi_name="Fixture (Audio)"; sources[0].p_url_address="127.0.0.1:5961";
    sources[1].p_ndi_name="Fixture (Alternate)"; sources[1].p_url_address="127.0.0.1:5962";
    *count=2; return sources;
}
NDIlib_recv_instance_t recvCreate(const NDIlib_recv_create_v3_t* settings) {
    if(settings->bandwidth!=NDIlib_recv_bandwidth_audio_only) return nullptr;
    ++receivers;
    return reinterpret_cast<NDIlib_recv_instance_t>(new Receiver{settings->source_to_connect_to.p_ndi_name &&
        std::strstr(settings->source_to_connect_to.p_ndi_name,"Alternate")!=nullptr});
}
void recvClose(NDIlib_recv_instance_t p) { delete reinterpret_cast<Receiver*>(p); --receivers; }
int recvConnections(NDIlib_recv_instance_t) { return failure==3 ? 0 : 1; }
NDIlib_framesync_instance_t syncCreate(NDIlib_recv_instance_t p) {
    ++syncs; return reinterpret_cast<NDIlib_framesync_instance_t>(p);
}
void syncClose(NDIlib_framesync_instance_t) { --syncs; }
void receiveAudio(NDIlib_framesync_instance_t p,NDIlib_audio_frame_v2_t* f,int rate,int channels,int count) {
    const bool alternate=reinterpret_cast<Receiver*>(p)->alternate;
    receiveRate=rate; f->sample_rate=rate; f->no_channels=channels; f->no_samples=count;
    f->channel_stride_in_bytes=(count+8)*sizeof(float);
    f->p_data=new float[static_cast<std::size_t>(count+8)*2]{};
    const float sign=alternate ? -1.f : 1.f;
    for(int i=0;i<count;++i) {
        f->p_data[i]=failure==3 ? 0.f : sign*.375f;
        f->p_data[count+8+i]=failure==3 ? 0.f : sign*.125f;
    }
}
void freeAudio(NDIlib_framesync_instance_t,NDIlib_audio_frame_v2_t* f) { delete[] f->p_data; f->p_data=nullptr; }

}
// Inherit the C linkage declared by the NDI header; exported by linker flag.
const NDIlib_v6* NDIlib_v6_load() {
    static const NDIlib_v6 api=[] {
        NDIlib_v6 table{};table.initialize=initialize;table.destroy=destroy;
        table.send_create=create;table.send_destroy=close;
        table.send_get_no_connections=connections;table.send_send_audio_v3=capture;
        table.find_create_v2=findCreate; table.find_destroy=findClose; table.find_get_current_sources=findSources;
        table.recv_create_v3=recvCreate; table.recv_destroy=recvClose; table.recv_get_no_connections=recvConnections;
        table.framesync_create=syncCreate; table.framesync_destroy=syncClose;
        table.framesync_capture_audio=receiveAudio; table.framesync_free_audio=freeAudio;
        return table;
    }();
    return &api;
}
extern "C" __declspec(dllexport) int TestFrameCount() { return frames.load(); }
extern "C" __declspec(dllexport) int TestSampleRate() { return lastRate.load(); }
extern "C" __declspec(dllexport) int TestSenderCount() { return senders.load(); }
extern "C" __declspec(dllexport) float TestLeftPeak() { return leftPeak.load(); }
extern "C" __declspec(dllexport) float TestRightPeak() { return rightPeak.load(); }

extern "C" __declspec(dllexport) void TestFailure(int mode) { failure=mode; }

extern "C" __declspec(dllexport) int TestReceiveObjects() { return receivers+syncs+finders; }
extern "C" __declspec(dllexport) int TestReceiveRate() { return receiveRate; }
