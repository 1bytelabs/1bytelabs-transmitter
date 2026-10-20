#include "editor.h"
#include <vst.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <memory>

namespace {
// x64 ABI checks independent of the compiler's default packing.
static_assert(sizeof(vst_effect_t)==192);
static_assert(offsetof(vst_effect_t,process_float)==120);
static_assert(std::atomic<float>::is_always_lock_free);
constexpr int Version=301; // 0.3.1, stable dropdowns and Receive diagnostics
enum Parameter : std::uint32_t { Send, Source, Status, Drops, Queued, Volume, Mode, Count };
struct Chunk {
    std::uint32_t magic{VST_FOURCC('1','B','A','2')},version{2};
    std::array<float,Count> values{};
    std::array<char,1024> source{};
};
static_assert(sizeof(Chunk)==1060);
template<std::size_t N> void copyText(void* ptr,const char* text) {
    if(ptr) { std::strncpy(static_cast<char*>(ptr),text,N-1); static_cast<char*>(ptr)[N-1]=0; }
}

class Plugin final : public zosma::EditorClient {
public:
    vst_effect_t effect{};
    zosma::NdiAudio audio;
    vst_host_callback_t host;
    explicit Plugin(vst_host_callback_t callback):host(callback) {
        effect.magic_number=VST_FOURCC('V','s','t','P');
        effect.control=dispatch; effect.set_parameter=setParameter; effect.get_parameter=getParameter;
        effect.process=processFloat; effect.process_float=processFloat; effect.process_double=processDouble;
        effect.num_programs=1; effect.num_params=Count; effect.num_inputs=effect.num_outputs=2;
        effect.flags=VST_EFFECT_FLAG_EDITOR|VST_EFFECT_FLAG_SUPPORTS_FLOAT|VST_EFFECT_FLAG_SUPPORTS_DOUBLE|VST_EFFECT_FLAG_CHUNKS;
        effect.input_output_ratio=1.f; effect.effect_internal=this;
        effect.unique_id=VST_FOURCC('1','b','A','u'); effect.version=Version;
        audio.prepare(rate_);
    }
    ~Plugin() override { editor_.reset(); audio.processing=false; audio.stop(); }
    void editParameter(std::uint32_t id,double value) override {
        if(id>=Count) return;
        host(&effect,VST_HOST_OPCODE_PARAM_START_EDIT,static_cast<int>(id),0,nullptr,0);
        setParameter(&effect,id,static_cast<float>(value));
        host(&effect,VST_HOST_OPCODE_AUTOMATE,static_cast<int>(id),0,nullptr,static_cast<float>(value));
        host(&effect,VST_HOST_OPCODE_PARAM_STOP_EDIT,static_cast<int>(id),0,nullptr,0);
    }
private:
    int rate_{48000};
    std::atomic_bool active_{false},bypassed_{false};
    std::unique_ptr<zosma::NativeEditor> editor_;
    vst_rect_t rect_{0,0,604,640};
    Chunk chunk_;
    bool receivePrimed_{false};
    std::uint64_t callbackEpoch_{0};
    char programName_[24]{"Send NDI"};
    static Plugin* self(vst_effect_t* effect) { return static_cast<Plugin*>(effect->effect_internal); }
    static void VST_FUNCTION_INTERFACE setParameter(vst_effect_t* effect,std::uint32_t id,float value) {
        if(!effect || !std::isfinite(value)) return;
        auto* p=self(effect); value=std::clamp(value,0.f,1.f);
        if(id==Send) p->audio.enabled=value>=.5f;
        else if(id==Source) p->audio.slot=1+static_cast<int>(std::lround(value*15));
        else if(id==Volume) p->audio.sendDb=value*66.f-60.f;
        else if(id==Mode && p->audio.receiveMode.exchange(value>=.5f)!=(value>=.5f)) ++p->audio.receiveEpoch;
    }
    static float VST_FUNCTION_INTERFACE getParameter(vst_effect_t* effect,std::uint32_t id) {
        if(!effect) return 0;
        const auto& a=self(effect)->audio;
        switch(id) {
        case Send:return a.enabled.load() ? 1.f : 0.f;
        case Source:return (a.slot.load()-1)/15.f;
        case Mode:return a.receiveMode ? 1.f : 0.f;
        case Volume:return (a.sendDb.load()+60.f)/66.f;
        case Status:return a.status.load()/3.f;
        case Drops:return static_cast<float>(std::min(1.,a.dropped.load()/1000000.));
        case Queued:return static_cast<float>(std::min(1.,a.queuedSamples()*10./a.sampleRate.load()));
        default:return 0;
        }
    }
    bool resume(bool on) {
        if(!on) { active_=false; audio.processing=false; audio.stop(); return true; }
        if(active_.load()) return true;
        audio.prepare(rate_); audio.processing=!bypassed_.load();
        audio.start(); active_=true; return true;
    }
    template<class Sample> void process(const Sample* const* inputs,Sample** outputs,int samples) noexcept {
        if(samples<=0 || !outputs) return;
        audio.hostCallbacks.fetch_add(1,std::memory_order_relaxed);
        audio.hostSamples.fetch_add(static_cast<std::uint64_t>(samples),std::memory_order_relaxed);
        audio.hostBlock.store(samples,std::memory_order_relaxed);
        // The host process-level query is the standard VST2 offline-render hint.
        // Hosts without it return zero; normal live processing remains supported.
        const bool offline=host(&effect,VST_HOST_OPCODE_GET_ACTIVE_THREAD,0,0,nullptr,0)==4;
        const bool sending=active_.load(std::memory_order_relaxed) && !bypassed_.load(std::memory_order_relaxed) && !offline;
        audio.processing=sending;
        const Sample* left=inputs ? inputs[0] : nullptr;
        const Sample* right=inputs ? inputs[1] : nullptr;
        const bool receive=audio.receiveMode.load(std::memory_order_relaxed);
        const auto epoch=audio.receiveEpoch.load();
        if(receive) {
            audio.receiveBlock=std::min(samples,rate_/2);
            if(callbackEpoch_!=epoch) {
                audio.receiveQueue.discard(); receivePrimed_=false; callbackEpoch_=epoch;
            }
            if(sending) {
                const bool on=audio.enabled.load();
                if(!on) { audio.receiveQueue.discard(); receivePrimed_=false; }
                const auto ready=audio.receiveQueue.available();
                if(!receivePrimed_ && ready>=static_cast<std::size_t>(std::max(samples,rate_/33))) receivePrimed_=true;
                const bool copied=on && receivePrimed_ && audio.receiveQueue.pop(outputs[0],outputs[1],static_cast<std::size_t>(samples),epoch);
                if(copied) audio.receivePlayed.fetch_add(static_cast<std::uint64_t>(samples),std::memory_order_relaxed);
                if(!copied) {
                    if(on && receivePrimed_) audio.underruns.fetch_add(1,std::memory_order_relaxed);
                    receivePrimed_=false;
                    for(int ch=0;ch<2;++ch) if(outputs[ch]) std::fill_n(outputs[ch],samples,Sample(0));
                }
                return;
            }
            audio.receiveQueue.discard(); receivePrimed_=false;
        } else {
            audio.receiveQueue.discard(); receivePrimed_=false;
        }
        if(!receive && sending && audio.enabled.load(std::memory_order_relaxed)) {
            if(!audio.queue.push(left,right,static_cast<std::size_t>(samples)))
                audio.dropped.fetch_add(static_cast<std::uint64_t>(samples),std::memory_order_relaxed);
        }
        // Exactly the same pass-through semantics as the working VST3 Send:
        // network gain is applied only by the original NDI worker.
        for(int ch=0;ch<2;++ch) {
            if(!outputs[ch]) continue;
            const Sample* input=inputs ? inputs[ch] : nullptr;
            if(!input) std::fill_n(outputs[ch],samples,Sample(0));
            else if(input!=outputs[ch]) std::memmove(outputs[ch],input,static_cast<std::size_t>(samples)*sizeof(Sample));
        }
    }
    static void VST_FUNCTION_INTERFACE processFloat(vst_effect_t* effect,const float* const* in,float** out,int32_t n) {
        self(effect)->process(in,out,n);
    }
    static void VST_FUNCTION_INTERFACE processDouble(vst_effect_t* effect,const double* const* in,double** out,int32_t n) {
        self(effect)->process(in,out,n);
    }
    intptr_t control(int32_t op,int32_t index,intptr_t value,void* ptr,float opt) {
        switch(op) {
        case VST_EFFECT_OPCODE_INITIALIZE:return 1;
        case VST_EFFECT_OPCODE_SET_SAMPLE_RATE:
            if(active_ || !std::isfinite(opt) || opt<8000 || opt>192000 || std::floor(opt)!=opt) return 0;
            rate_=static_cast<int>(opt); audio.prepare(rate_); return 1;
        case VST_EFFECT_OPCODE_SET_BLOCK_SIZE:return value>0 ? 1 : 0;
        case VST_EFFECT_OPCODE_SUSPEND_RESUME:return resume(value!=0) ? 1 : 0;
        case VST_EFFECT_OPCODE_PROCESS_BEGIN:audio.processing=active_ && !bypassed_;return 1;
        case VST_EFFECT_OPCODE_PROCESS_END:audio.processing=false;return 1;
        case VST_EFFECT_OPCODE_BYPASS:bypassed_=value!=0;audio.processing=active_ && !bypassed_;return 1;
        case VST_EFFECT_OPCODE_VST_VERSION:return 2400;
        case VST_EFFECT_OPCODE_VENDOR_VERSION:return Version;
        case VST_EFFECT_OPCODE_EFFECT_CATEGORY:return VST_EFFECT_CATEGORY_EFFECT;
        case VST_EFFECT_OPCODE_EFFECT_NAME:copyText<32>(ptr,"1Byte Labs Audio");return 1;
        case VST_EFFECT_OPCODE_PRODUCT_NAME:copyText<64>(ptr,"1Byte Labs Audio");return 1;
        case VST_EFFECT_OPCODE_VENDOR_NAME:copyText<64>(ptr,"1Byte Labs");return 1;
        case VST_EFFECT_OPCODE_PROGRAM_SET:return value==0 ? 1 : 0;
        case VST_EFFECT_OPCODE_PROGRAM_GET:return 0;
        case VST_EFFECT_OPCODE_PROGRAM_GET_NAME:copyText<24>(ptr,programName_);return 1;
        case VST_EFFECT_OPCODE_PROGRAM_SET_NAME:if(ptr) { std::strncpy(programName_,static_cast<char*>(ptr),23);programName_[23]=0; }return 1;
        case VST_EFFECT_OPCODE_PARAM_IS_AUTOMATABLE:return index==Send || index==Source || index==Volume || index==Mode;
        case VST_EFFECT_OPCODE_PARAM_GET_NAME: {
            constexpr const char* names[]{"Send","Source","Status","Lost","Queue","Volume","Mode"};
            if(index<0 || index>=Count) return 0;copyText<8>(ptr,names[index]);return 1;
        }
        case VST_EFFECT_OPCODE_PARAM_GET_LABEL:copyText<8>(ptr,index==Volume ? "dB" : index==Queued ? "ms" : "");return 1;
        case VST_EFFECT_OPCODE_PARAM_GET_VALUE: {
            if(index<0 || index>=Count) return 0;char text[32]{};
            if(index==Mode) std::snprintf(text,sizeof(text),"%s",audio.receiveMode ? "Receive" : "Send");
            else if(index==Send) std::snprintf(text,sizeof(text),"%s",audio.enabled ? "On" : "Off");
            else if(index==Source) std::snprintf(text,sizeof(text),"%02d",audio.slot.load());
            else if(index==Volume) std::snprintf(text,sizeof(text),"%+.1f",audio.sendDb.load());
            else if(index==Status) std::snprintf(text,sizeof(text),"%s",audio.status==2 ? "Linked" : audio.status==3 ? "Error" : audio.status==1 ? "Waiting" : "Off");
            else if(index==Drops) std::snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(audio.dropped.load()));
            else std::snprintf(text,sizeof(text),"%.1f",std::min(100.,audio.queuedSamples()*1000./audio.sampleRate.load()));
            copyText<8>(ptr,text);return 1;
        }
        case VST_EFFECT_OPCODE_GET_CHUNK_DATA:
            if(!ptr) return 0;
            for(std::uint32_t i=0;i<Count;++i) chunk_.values[i]=getParameter(&effect,i);
            chunk_.source.fill(0);
            { const auto selected=audio.selectedSource(); std::strncpy(chunk_.source.data(),selected.c_str(),chunk_.source.size()-1); }
            *static_cast<void**>(ptr)=&chunk_;return sizeof(chunk_);
        case VST_EFFECT_OPCODE_SET_CHUNK_DATA: {
            if(!ptr) return 0;
            Chunk data;
            if(value==32) {
                struct Legacy { std::uint32_t magic,version; std::array<float,6> values; } old{};
                std::memcpy(&old,ptr,sizeof(old));
                if(old.magic!=chunk_.magic || old.version!=1) return 0;
                std::copy(old.values.begin(),old.values.end(),data.values.begin());
                data.values[Mode]=0;
            } else if(value==sizeof(Chunk)) {
                std::memcpy(&data,ptr,sizeof(data));
                if(data.magic!=chunk_.magic || data.version!=2 || !std::memchr(data.source.data(),0,data.source.size())) return 0;
            } else return 0;
            for(auto x:data.values) if(!std::isfinite(x) || x<0 || x>1) return 0;
            audio.selectSource(data.source.data());
            setParameter(&effect,Mode,data.values[Mode]);
            setParameter(&effect,Send,data.values[Send]);setParameter(&effect,Source,data.values[Source]);setParameter(&effect,Volume,data.values[Volume]);return 1;
        }
        case VST_EFFECT_OPCODE_EDITOR_GET_RECT:
            if(!ptr) return 0;*static_cast<vst_rect_t**>(ptr)=&rect_;return 1;
        case VST_EFFECT_OPCODE_EDITOR_OPEN:
            if(!ptr || editor_) return 0;
            editor_=std::make_unique<zosma::NativeEditor>(this,audio);
            if(!editor_->open(static_cast<HWND>(ptr))) { editor_.reset();return 0; }
            host(&effect,VST_HOST_OPCODE_EDITOR_RESIZE,rect_.right,rect_.bottom,nullptr,0);return 1;
        case VST_EFFECT_OPCODE_EDITOR_CLOSE:editor_.reset();return 1;
        case VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE:return 1; // native timer refreshes the view
        case VST_EFFECT_OPCODE_SUPPORTS:
            return ptr && std::strcmp(static_cast<const char*>(ptr),"bypass")==0 ? 1 : 0;
        case 77:return value==0 || value==1; // VST2.4 setProcessPrecision
        default:return 0;
        }
    }
    static intptr_t VST_FUNCTION_INTERFACE dispatch(vst_effect_t* effect,int32_t op,int32_t index,intptr_t value,void* ptr,float opt) {
        if(!effect || !effect->effect_internal) return 0;
        auto* p=self(effect);
        if(op==VST_EFFECT_OPCODE_DESTROY) { delete p; return 1; }
        try { return p->control(op,index,value,ptr,opt); }
        catch(...) { p->audio.status=3;return 0; }
    }
};
}
extern "C" __declspec(dllexport) vst_effect_t* VST_FUNCTION_INTERFACE VSTPluginMain(vst_host_callback_t host) {
    if(!host || !host(nullptr,VST_HOST_OPCODE_VST_VERSION,0,0,nullptr,0)) return nullptr;
    try { return &(new Plugin(host))->effect; } catch(...) { return nullptr; }
}
