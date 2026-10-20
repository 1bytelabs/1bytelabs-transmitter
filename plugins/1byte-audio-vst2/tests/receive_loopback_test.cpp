#include "ndi_audio.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
int main() {
    zosma::NdiAudio send,receive;
    send.prepare(48000);receive.prepare(48000);
    send.slot=16;send.enabled=true;send.processing=true;
    receive.receiveMode=true;receive.enabled=true;receive.processing=true;
    send.start();receive.start();
    std::array<float,480> left{},right{},a{},b{};
    left.fill(.2f);right.fill(.1f);
    bool selected=false,heard=false;
    for(int i=0;i<2000;++i) {
        send.queue.push(left.data(),right.data(),left.size());
        if(!selected && i%10==0) {
            for(const auto& name:receive.sources()) if(name.find("1Byte Labs Send 16")!=std::string::npos) {
                receive.selectSource(name);selected=true;break;
            }
        }
        if(selected && receive.receiveQueue.pop(a.data(),b.data(),a.size(),receive.receiveEpoch.load())) {
            if(std::abs(a[200]-.2f)<.003f && std::abs(b[200]-.1f)<.003f) { heard=true;break; }
        }
        Sleep(10);
    }
    if(!heard) std::wcerr<<L"Real NDI loopback failed: "<<receive.diagnostic()<<L"\n";
    receive.stop();send.stop();
    if(!heard) return 1;
    std::cout<<"Real NDI Send -> discovery -> Receive/FrameSync -> host queue passed\n";return 0;
}
