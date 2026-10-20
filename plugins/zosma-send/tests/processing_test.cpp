// Exercise the actual processor implementation, including its host parameters.
#include "../plugin.cpp"
#include <array>
#include <cstdlib>
#include <iostream>

void require(bool value) { if(!value) { std::cerr << "Audio transparency test failed\n"; std::abort(); } }
template<class Sample> void test(int32 size,bool inPlace,bool send,bool offline) {
    auto* plugin=new zosma::SendPlugin;
    require(plugin->initialize(nullptr)==kResultOk);
    ProcessSetup setup{};
    setup.sampleRate=48000; setup.maxSamplesPerBlock=256;
    setup.symbolicSampleSize=size; setup.processMode=offline ? kOffline : kRealtime;
    require(plugin->setupProcessing(setup)==kResultOk);
    require(plugin->setProcessing(true)==kResultOk);
    require(plugin->setParamNormalized(zosma::Send,send ? 1. : 0.)==kResultOk);
    // The controller must report what the host set, as in the 0.1.0 SDK getter.
    require(plugin->getParamNormalized(zosma::Send)==(send ? 1. : 0.));
    std::array<Sample,256> left{},right{},outL{},outR{};
    for(int block=0;block<2000;++block) {
        for(int i=0;i<256;++i) {
            left[i]=static_cast<Sample>((i%41-20)/13.0); // includes > 0 dBFS
            right[i]=static_cast<Sample>((i%23-11)/17.0);
        }
        const auto expectedL=left, expectedR=right;
        Sample* inputs[]{left.data(),right.data()};
        Sample* outputs[]{inPlace ? left.data() : outL.data(),inPlace ? right.data() : outR.data()};
        AudioBusBuffers input{},output{}; input.numChannels=output.numChannels=2;
        if constexpr(sizeof(Sample)==sizeof(float)) {
            input.channelBuffers32=inputs; output.channelBuffers32=outputs;
        } else { input.channelBuffers64=inputs; output.channelBuffers64=outputs; }
        ProcessData data{}; data.numSamples=256; data.numInputs=data.numOutputs=1;
        data.inputs=&input; data.outputs=&output; data.symbolicSampleSize=size;
        data.processMode=setup.processMode;
        require(plugin->process(data)==kResultOk);
        require(std::memcmp(outputs[0],expectedL.data(),sizeof(left))==0);
        require(std::memcmp(outputs[1],expectedR.data(),sizeof(right))==0);
        // One channel flagged silent must be zero-filled; the other unchanged.
        input.silenceFlags=1;
        require(plugin->process(data)==kResultOk);
        for(int i=0;i<256;++i) require(outputs[0][i]==Sample(0));
        require(std::memcmp(outputs[1],expectedR.data(),sizeof(right))==0);
    }
    require(plugin->setProcessing(false)==kResultOk);
    plugin->terminate(); plugin->release();
}
int main() {
    for(bool inPlace:{false,true}) for(bool send:{false,true}) for(bool offline:{false,true}) {
        test<float>(kSample32,inPlace,send,offline);
        test<double>(kSample64,inPlace,send,offline);
    }
    std::cout << "Processor: 32/64-bit, in-place/out-of-place, send on/off, real-time/offline and silence passed; samples bit-exact, no gain changes\n";
}
