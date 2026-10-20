#include "ndi_audio.h"
#include <windows.h>
#include <iostream>
#include <string>
int main(int argc,char**) {
    if(argc>1) {
        zosma::NdiAudio audio; audio.prepare(48000); audio.enabled=true; audio.processing=true; audio.start();
        for(int i=0;i<200 && audio.status!=3;++i) Sleep(10);
        const bool okay=audio.status==3 && audio.diagnostic().find(L"ausente")!=std::wstring::npos;
        audio.stop(); return okay ? 0 : 1;
    }
    HMODULE mock=LoadLibraryW(L"Processing.NDI.Lib.x64.dll");
    if(!mock) return 2;
    auto failure=reinterpret_cast<void(*)(int)>(GetProcAddress(mock,"TestFailure"));
    if(!failure) return 3;
    for(int mode=1;mode<=2;++mode) {
        failure(mode);
        zosma::NdiAudio audio; audio.prepare(48000); audio.enabled=true; audio.processing=true; audio.start();
        for(int i=0;i<200 && audio.status!=3;++i) Sleep(10);
        const auto report=audio.diagnostic();
        const bool okay=audio.status==3 && report.find(mode==1 ? L"initialize()" : L"send_create")!=std::wstring::npos;
        audio.stop();
        if(!okay) return 4+mode;
    }
    failure(0); FreeLibrary(mock);
    std::cout<<"NDI initialization and sender failure diagnostics passed\n";
    return 0;
}
