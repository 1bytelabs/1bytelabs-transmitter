#include "ndi_audio.h"
#include "editor.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace zosma {
const FUID pluginId(0x69F84583,0x68334CD9,0xA39A9A47,0x3B3FA714);
enum : ParamID { Send=0, Source=1, Status=2, Drops=3, Queued=4 };

// Combined component deliberately shares only atomic controls with its worker.
// Native editor polls only atomics; host audio processing never paints UI.
class SendPlugin final : public SingleComponentEffect {
public:
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new SendPlugin); }
    IPlugView* PLUGIN_API createView(FIDString name) override {
        return name && std::strcmp(name,ViewType::kEditor)==0 ? new SendEditor(this,audio) : nullptr;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override {
        if(id==Send) return audio.enabled.load() ? 1. : 0.;
        if(id==Source) return (audio.slot.load()-1)/15.;
        if(id==Status) return audio.status.load()/3.;
        if(id==Drops) return std::min(1.,audio.dropped.load()/1000000.);
        if(id==Queued) return std::min(1.,audio.queue.available()*10./audio.sampleRate.load());
        return SingleComponentEffect::getParamNormalized(id);
    }
    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto result=SingleComponentEffect::initialize(context);
        if (result!=kResultOk) return result;
        addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo);
        parameters.addParameter(STR16("Send NDI"),nullptr,1,0,ParameterInfo::kCanAutomate,Send);
        parameters.addParameter(new RangeParameter(STR16("Source number"),Source,nullptr,1,16,1,15));
        auto* status=new StringListParameter(STR16("Network status"),Status,nullptr,ParameterInfo::kIsReadOnly);
        status->appendString(STR16("Off")); status->appendString(STR16("Waiting for receiver"));
        status->appendString(STR16("Connected")); status->appendString(STR16("Runtime or sender error"));
        parameters.addParameter(status);
        parameters.addParameter(new RangeParameter(STR16("Dropped samples (max 1000000)"),Drops,nullptr,0,1000000,0,0,ParameterInfo::kIsReadOnly));
        parameters.addParameter(new RangeParameter(STR16("Queued audio"),Queued,STR16("ms"),0,100,0,0,ParameterInfo::kIsReadOnly));
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { audio.stop(); return SingleComponentEffect::terminate(); }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,int32 nIn,SpeakerArrangement* out,int32 nOut) override {
        if(nIn!=1 || nOut!=1 || !in || !out || in[0]!=SpeakerArr::kStereo || out[0]!=SpeakerArr::kStereo)
            return kResultFalse;
        return SingleComponentEffect::setBusArrangements(in,nIn,out,nOut);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override {
        return size==kSample32 || size==kSample64 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (!std::isfinite(setup.sampleRate) || setup.sampleRate<8000 || setup.sampleRate>192000 ||
            std::floor(setup.sampleRate)!=setup.sampleRate || setup.maxSamplesPerBlock<1 ||
            canProcessSampleSize(setup.symbolicSampleSize)!=kResultTrue) return kResultFalse;
        audio.prepare(static_cast<int>(setup.sampleRate));
        return SingleComponentEffect::setupProcessing(setup);
    }
    tresult PLUGIN_API setActive(TBool active) override {
        if (active) {
            try { audio.start(); } catch (...) { audio.status=3; return kResultFalse; }
        } else { audio.processing=false; audio.stop(); }
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool processing) override {
        // Hosts may call this on their real-time thread. Never start/join here.
        audio.processing=processing!=0; return kResultOk;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue value) override {
        if (!std::isfinite(value)) return kInvalidArgument;
        apply(id,std::clamp(value,0.,1.));
        return SingleComponentEffect::setParamNormalized(id,value);
    }
    tresult PLUGIN_API setState(IBStream* stream) override {
        if(!stream) return kInvalidArgument;
        IBStreamer state(stream,kLittleEndian);
        int32 version{},enabled{},slot{};
        if(!state.readInt32(version) || version!=1 || !state.readInt32(enabled) || !state.readInt32(slot)) return kResultFalse;
        setParamNormalized(Send,enabled ? 1. : 0.);
        setParamNormalized(Source,(std::clamp(slot,1,16)-1)/15.);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        if(!stream) return kInvalidArgument;
        IBStreamer state(stream,kLittleEndian);
        return state.writeInt32(1) && state.writeInt32(audio.enabled ? 1 : 0) && state.writeInt32(audio.slot.load())
            ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API process(ProcessData& data) override {
        if (data.inputParameterChanges) {
            for (int32 i=0;i<data.inputParameterChanges->getParameterCount();++i) {
                auto* changes=data.inputParameterChanges->getParameterData(i);
                if(!changes || !changes->getPointCount()) continue;
                int32 offset{}; ParamValue value{};
                if(changes->getPoint(changes->getPointCount()-1,offset,value)==kResultTrue && std::isfinite(value))
                    apply(changes->getParameterId(),std::clamp(value,0.,1.));
            }
        }
        publish(data,Status,audio.status.load()/3.);
        publish(data,Drops,std::min(1.,audio.dropped.load()/1000000.));
        publish(data,Queued,std::min(1.,audio.queue.available()*10./processSetup.sampleRate));
        if(data.numSamples<=0) return kResultOk; // parameter-only flush
        if(data.numInputs!=1 || data.numOutputs!=1 || !data.inputs || !data.outputs ||
           data.inputs[0].numChannels!=2 || data.outputs[0].numChannels!=2) return kResultFalse;
        if(data.symbolicSampleSize==kSample32) copyAndSend<float>(data);
        else if(data.symbolicSampleSize==kSample64) copyAndSend<double>(data);
        else return kResultFalse;
        return kResultOk;
    }
private:
    void apply(ParamID id,ParamValue value) noexcept {
        if(id==Send) audio.enabled=value>=.5;
        else if(id==Source) audio.slot=1+static_cast<int>(std::lround(value*15.));
    }
    static void publish(ProcessData& data,ParamID id,ParamValue value) {
        if(!data.outputParameterChanges) return;
        int32 index{};
        if(auto* changes=data.outputParameterChanges->addParameterData(id,index)) changes->addPoint(0,value,index);
    }
    template<class Sample> void copyAndSend(ProcessData& data) noexcept {
        auto& in=data.inputs[0]; auto& out=data.outputs[0];
        Sample** inputs; Sample** outputs;
        if constexpr(sizeof(Sample)==sizeof(float)) { inputs=in.channelBuffers32; outputs=out.channelBuffers32; }
        else { inputs=in.channelBuffers64; outputs=out.channelBuffers64; }
        if(!inputs || !outputs) return;
        const bool leftSilent=(in.silenceFlags & 1)!=0;
        const bool rightSilent=(in.silenceFlags & 2)!=0;
        static_assert(std::atomic<float>::is_always_lock_free);
        if(data.processMode==kRealtime) {
            for(int ch=0;ch<2;++ch) {
                float peak=0;
                if(inputs[ch] && !(in.silenceFlags & (uint64(1)<<ch))) {
                    for(int32 i=0;i<data.numSamples;++i) {
                        const float sample=static_cast<float>(std::abs(inputs[ch][i]));
                        if(std::isfinite(sample)) peak=std::max(peak,sample);
                    }
                }
                auto previous=audio.peaks[ch].load(std::memory_order_relaxed);
                while(previous<peak && !audio.peaks[ch].compare_exchange_weak(previous,peak,std::memory_order_relaxed)) {}
            }
        }
        // Queue first: host may alias input and output buffers.
        if(audio.enabled.load() && audio.processing.load() && data.processMode==kRealtime) {
            if(!audio.queue.push(inputs[0],inputs[1],static_cast<std::size_t>(data.numSamples),leftSilent,rightSilent))
                audio.dropped.fetch_add(static_cast<std::uint64_t>(data.numSamples),std::memory_order_relaxed);
        }
        for(int ch=0;ch<2;++ch) {
            if(!outputs[ch]) continue;
            if((in.silenceFlags & (uint64(1)<<ch)) || !inputs[ch])
                std::fill_n(outputs[ch],data.numSamples,Sample(0));
            else if(outputs[ch]!=inputs[ch]) std::memmove(outputs[ch],inputs[ch],data.numSamples*sizeof(Sample));
        }
        out.silenceFlags=in.silenceFlags;
    }
    NdiAudio audio;
};
}

BEGIN_FACTORY_DEF("1Byte Labs","https://1bytelabs.com.br","")
DEF_CLASS2(INLINE_UID_FROM_FUID(zosma::pluginId),PClassInfo::kManyInstances,
    kVstAudioEffectClass,"1Byte Labs Send",0,"Fx|Tools","0.1.2",kVstVersionString,zosma::SendPlugin::create)
END_FACTORY
