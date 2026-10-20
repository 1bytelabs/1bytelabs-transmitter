// Test fixture only. Never included in the distributable/installer.
#include "Processing.NDI.Lib.h"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace {
std::atomic_int frames{0},lastRate{0},senders{0};
std::atomic<float> leftPeak{0},rightPeak{0};
bool initialize() { return true; }
void destroy() {}
NDIlib_send_instance_t create(const NDIlib_send_create_t*) {
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
}
// Inherit the C linkage declared by the NDI header; exported by linker flag.
const NDIlib_v6* NDIlib_v6_load() {
    static const NDIlib_v6 api=[] {
        NDIlib_v6 table{};table.initialize=initialize;table.destroy=destroy;
        table.send_create=create;table.send_destroy=close;
        table.send_get_no_connections=connections;table.send_send_audio_v3=capture;
        return table;
    }();
    return &api;
}
extern "C" __declspec(dllexport) int TestFrameCount() { return frames.load(); }
extern "C" __declspec(dllexport) int TestSampleRate() { return lastRate.load(); }
extern "C" __declspec(dllexport) int TestSenderCount() { return senders.load(); }
extern "C" __declspec(dllexport) float TestLeftPeak() { return leftPeak.load(); }
extern "C" __declspec(dllexport) float TestRightPeak() { return rightPeak.load(); }
