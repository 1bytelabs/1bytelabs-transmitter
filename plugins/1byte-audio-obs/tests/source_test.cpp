// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs.h>
#include <util/platform.h>
#include "ndi_audio.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
void require(bool okay,const char* text) {
    if(!okay) { std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1); }
}
struct Capture { std::atomic_bool heard{false}; std::atomic_int callbacks{0}; std::atomic<float> expected{.2f}; };
void capture(void* data,obs_source_t*,const audio_data* frame,bool muted) {
    auto& state=*static_cast<Capture*>(data);
    ++state.callbacks;
    if(muted || frame->frames<2 || !frame->data[0] || !frame->data[1]) return;
    const auto* left=reinterpret_cast<const float*>(frame->data[0]);
    const auto* right=reinterpret_cast<const float*>(frame->data[1]);
    const auto i=frame->frames/2;
    const float expected=state.expected.load();
    if(std::abs(left[i]-expected)<.003f && std::abs(right[i]-expected*.5f)<.003f) state.heard=true;
}
}
int main(int argc,char** argv) {
    require(argc==3,"module path and data path");
    require(obs_startup("en-US",nullptr,nullptr),"OBS startup");
    obs_audio_info audio{};audio.samples_per_sec=48000;audio.speakers=SPEAKERS_STEREO;
    require(obs_reset_audio(&audio),"OBS audio initialization");
    obs_module_t* module{};
    require(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS,"load native OBS module");
    require(obs_init_module(module),"initialize native OBS module");
    auto* settings=obs_data_create();
    obs_data_set_bool(settings,"enabled",true);
    auto* source=obs_source_create("onebyte_ndi_audio","1Byte native source test",settings,nullptr);
    require(source!=nullptr,"create source without a microphone or VST filter");
    auto* initialProperties=obs_source_properties(source);
    auto* website=obs_properties_get(initialProperties,"website");
    require(website && obs_property_button_type(website)==OBS_BUTTON_URL,"website is a native OBS URL button");
    require(std::string(obs_property_button_url(website))=="https://1bytelabs.com.br","OBS website URL");
    obs_properties_destroy(initialProperties);
    Capture received;
    obs_source_add_audio_capture_callback(source,capture,&received);
    obs_source_inc_active(source);
    zosma::NdiAudio send;
    send.prepare(48000);send.slot=15;send.enabled=true;send.processing=true;send.start();
    std::array<float,480> left{},right{};left.fill(.2f);right.fill(.1f);
    bool selected=false;
    for(int i=0;i<2000 && !received.heard;++i) {
        send.queue.push(left.data(),right.data(),left.size());
        if(!selected && i%20==0) {
            auto* props=obs_source_properties(source);
            auto* list=obs_properties_get(props,"ndi_source");
            for(std::size_t j=0;j<obs_property_list_item_count(list);++j) {
                const char* value=obs_property_list_item_string(list,j);
                if(value && std::string(value).find("1Byte Labs Send 15")!=std::string::npos) {
                    obs_data_set_string(settings,"ndi_source",value);selected=true;break;
                }
            }
            obs_properties_destroy(props);
            if(selected) obs_source_update(source,settings);
        }
        os_sleep_ms(10);
    }
    require(selected,"native source discovers real NDI sender");
    require(received.heard,"native source delivers real NDI stereo audio to libobs");
    // Verify source settings apply the received-volume fader.
    received.expected=.2f*std::pow(10.f,-6.f/20.f);received.heard=false;
    obs_data_set_double(settings,"gain_db",-6.);obs_source_update(source,settings);
    for(int i=0;i<300 && !received.heard;++i) {
        send.queue.push(left.data(),right.data(),left.size());os_sleep_ms(10);
    }
    require(received.heard,"native source volume applies to received audio");
    require(received.callbacks>10,"continuous native source audio callbacks");
    obs_source_dec_active(source);
    obs_source_remove_audio_capture_callback(source,capture,&received);
    obs_source_release(source);obs_data_release(settings);send.stop();obs_shutdown();
    std::puts("Native OBS module: registration, source discovery, real NDI receive, gain and cleanup passed");
    return 0;
}
