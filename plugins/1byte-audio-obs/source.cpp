// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <util/platform.h>
#include "ndi_audio.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char* obs_module_name(void) { return "1Byte Labs Audio"; }
MODULE_EXPORT const char* obs_module_description(void) { return "Fonte de audio NDI / 1Byte Labs Audio 0.4.0"; }
MODULE_EXPORT const char* obs_module_author(void) { return "1Byte Labs"; }

namespace {
struct Source {
    obs_source_t* source;
    zosma::NdiAudio audio;
    std::atomic_bool stopping{false};
    std::thread output;
    int rate=48000;
    explicit Source(obs_source_t* value):source(value) {
        obs_audio_info info{};
        if(obs_get_audio_info(&info) && info.samples_per_sec>=8000 && info.samples_per_sec<=192000)
            rate=static_cast<int>(info.samples_per_sec);
        audio.prepare(rate); audio.receiveMode=true; audio.receiveBlock=rate/100;
    }
    ~Source() {
        stopping=true;
        if(output.joinable()) output.join();
        audio.processing=false; audio.stop();
    }
    void update(obs_data_t* settings) {
        audio.selectSource(obs_data_get_string(settings,"ndi_source"));
        const double db=obs_data_get_double(settings,"gain_db");
        audio.sendDb=static_cast<float>(std::isfinite(db) ? std::clamp(db,-60.,6.) : 0.);
        audio.enabled=obs_data_get_bool(settings,"enabled");
    }
    void start() {
        audio.start();
        output=std::thread([this] {
            const int count=rate/100;
            std::vector<float> left(count),right(count);
            std::uint64_t epoch=audio.receiveEpoch.load();
            bool primed=false;
            uint64_t deadline=os_gettime_ns();
            int lastStatus=-1;
            while(!stopping.load()) {
                const bool active=obs_source_active(source);
                audio.processing=active;
                const auto current=audio.receiveEpoch.load();
                if(current!=epoch || !active || !audio.enabled.load()) {
                    audio.receiveQueue.discard(); primed=false; epoch=current;
                }
                if(active) {
                    if(!primed && audio.receiveQueue.available()>=static_cast<std::size_t>(rate/33)) primed=true;
                    const bool copied=audio.enabled && primed &&
                        audio.receiveQueue.pop(left.data(),right.data(),count,epoch);
                    if(!copied) {
                        if(primed && audio.enabled) ++audio.underruns;
                        primed=false;std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);
                    } else audio.receivePlayed.fetch_add(count);
                    obs_source_audio frame{};
                    frame.data[0]=reinterpret_cast<const uint8_t*>(left.data());
                    frame.data[1]=reinterpret_cast<const uint8_t*>(right.data());
                    frame.frames=count;frame.speakers=SPEAKERS_STEREO;
                    frame.format=AUDIO_FORMAT_FLOAT_PLANAR;frame.samples_per_sec=rate;
                    frame.timestamp=os_gettime_ns();
                    obs_source_output_audio(source,&frame);
                }
                const int status=audio.status.load();
                if(status==3 && lastStatus!=3) blog(LOG_WARNING,"[1Byte Labs Audio] Falha ao iniciar a recepcao NDI.");
                lastStatus=status;
                deadline+=10000000ULL;
                const auto now=os_gettime_ns();
                if(now>deadline+100000000ULL) { deadline=now;audio.receiveQueue.discard();primed=false; }
                os_sleepto_ns(deadline);
            }
        });
    }
};
const char* name(void*) { return "1Byte Labs Audio"; }
void defaults(obs_data_t* settings) {
    obs_data_set_default_bool(settings,"enabled",true);
    obs_data_set_default_double(settings,"gain_db",0.);
    obs_data_set_default_string(settings,"ndi_source","");
}
void update(void* data,obs_data_t* settings) { static_cast<Source*>(data)->update(settings); }
void* create(obs_data_t* settings,obs_source_t* source) {
    try {
        auto state=std::make_unique<Source>(source);state->update(settings);state->start();return state.release();
    } catch(...) { blog(LOG_ERROR,"[1Byte Labs Audio] Falha ao criar a fonte.");return nullptr; }
}
void destroy(void* data) { delete static_cast<Source*>(data); }
bool refresh(obs_properties_t*,obs_property_t*,void* data) {
    auto* state=static_cast<Source*>(data);
    if(state) obs_source_update_properties(state->source);
    return true;
}
obs_properties_t* properties(void* data) {
    auto* state=static_cast<Source*>(data);
    auto* props=obs_properties_create();
    auto* list=obs_properties_add_list(props,"ndi_source","Fonte NDI",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list,"Selecione uma fonte NDI","");
    if(state) {
        auto names=state->audio.sources();
        const auto selected=state->audio.selectedSource();
        if(!selected.empty() && std::find(names.begin(),names.end(),selected)==names.end()) names.push_back(selected);
        for(const auto& value:names) obs_property_list_add_string(list,value.c_str(),value.c_str());
    }
    obs_properties_add_button2(props,"refresh","Atualizar fontes",refresh,state);
    obs_properties_add_bool(props,"enabled","Receber audio");
    obs_properties_add_float_slider(props,"gain_db","Volume recebido (dB)",-60.,6.,.1);
    std::string status="Aguardando fonte. Use Atualizar fontes se a lista estiver vazia.";
    if(state) {
        const int value=state->audio.status.load();
        status=value==3 ? "Erro NDI: confira a DLL do runtime e o log do OBS." :
            value==2 ? "NDI conectado. O medidor da fonte mostra o audio recebido." :
            !state->audio.enabled ? "Recepcao desligada." : "Aguardando fonte/conexao NDI.";
    }
    obs_properties_add_text(props,"connection_status",status.c_str(),OBS_TEXT_INFO);
    return props;
}
}
bool obs_module_load(void) {
    obs_source_info info{};
    info.id="onebyte_ndi_audio";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_AUDIO;
    info.get_name=name;info.create=create;info.destroy=destroy;info.update=update;
    info.get_defaults=defaults;info.get_properties=properties;
    obs_register_source(&info);
    blog(LOG_INFO,"[1Byte Labs Audio] Fonte NDI 0.4.0 registrada.");
    return true;
}
