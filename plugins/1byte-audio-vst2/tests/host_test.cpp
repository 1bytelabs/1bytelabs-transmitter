#include <cstdint>
#include <windows.h>
#include <commctrl.h>
#include <vst.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
std::atomic_int processLevel{2},edits{0};
void require(bool value,const char* label) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1); }
}
intptr_t VST_FUNCTION_INTERFACE host(vst_effect_t*,int32_t op,int32_t,int64_t,const char*,float) {
    if(op==VST_HOST_OPCODE_VST_VERSION) return 2400;
    if(op==VST_HOST_OPCODE_GET_ACTIVE_THREAD) return processLevel.load();
    if(op==VST_HOST_OPCODE_GET_SAMPLE_RATE) return 48000;
    if(op==VST_HOST_OPCODE_EDITOR_RESIZE) return 1;
    if(op==VST_HOST_OPCODE_AUTOMATE) { ++edits;return 1; }
    return 0;
}
template<class T> void audioCheck(vst_effect_t* p,bool inPlace) {
    std::array<T,256> l{},r{},outL{},outR{};
    for(int block=0;block<50;++block) {
        for(int i=0;i<256;++i) { l[i]=T((i%39-19)/11.);r[i]=T((i%23-11)/17.); }
        const auto expectedL=l,expectedR=r;
        const T* inputs[]{l.data(),r.data()};T* outputs[]{inPlace ? l.data() : outL.data(),inPlace ? r.data() : outR.data()};
        if constexpr(sizeof(T)==sizeof(float)) p->process_float(p,inputs,outputs,256);
        else p->process_double(p,inputs,outputs,256);
        require(std::memcmp(outputs[0],expectedL.data(),sizeof(l))==0,"left pass-through unchanged");
        require(std::memcmp(outputs[1],expectedR.data(),sizeof(r))==0,"right pass-through unchanged");
    }
    T* outputs[]{outL.data(),outR.data()};
    if constexpr(sizeof(T)==sizeof(float)) p->process_float(p,nullptr,outputs,256);
    else p->process_double(p,nullptr,outputs,256);
    for(int i=0;i<256;++i) require(outL[i]==T(0) && outR[i]==T(0),"null input is silence");
}
void capture(HWND window) {
    RECT r{};GetClientRect(window,&r);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);void* pixels{};
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    require(bitmap!=nullptr,"screenshot bitmap");auto old=SelectObject(memory,bitmap);
    PrintWindow(window,memory,PW_CLIENTONLY);
    const DWORD count=r.right*r.bottom*4;
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+count;
    FILE* out{};fopen_s(&out,"vst2-editor-preview.bmp","wb");require(out!=nullptr,"screenshot file");
    std::fwrite(&file,sizeof(file),1,out);std::fwrite(&info.bmiHeader,sizeof(BITMAPINFOHEADER),1,out);std::fwrite(pixels,count,1,out);std::fclose(out);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);
}
}
int main(int argc,char** argv) {
    require(argc==3,"DLL arguments");SetProcessDPIAware();
    if(std::strcmp(argv[2],"--real")==0) {
        auto library=LoadLibraryA(argv[1]);require(library!=nullptr,"packaged plugin load");
        using Entry=vst_effect_t* (VST_FUNCTION_INTERFACE *)(vst_host_callback_t);
        auto create=reinterpret_cast<Entry>(GetProcAddress(library,"VSTPluginMain"));require(create!=nullptr,"packaged plugin entry");
        auto* effect=create(host);require(effect!=nullptr,"packaged plugin instance");
        effect->control(effect,VST_EFFECT_OPCODE_INITIALIZE,0,0,nullptr,0);
        effect->control(effect,VST_EFFECT_OPCODE_SET_SAMPLE_RATE,0,0,nullptr,48000);
        effect->set_parameter(effect,0,1);
        require(effect->control(effect,VST_EFFECT_OPCODE_SUSPEND_RESUME,0,1,nullptr,0)==1,"real runtime resume");
        std::array<float,480> l{},r{},a{},b{};l.fill(.1f);r.fill(.05f);
        const float* inputs[]{l.data(),r.data()};float* outputs[]{a.data(),b.data()};
        for(int i=0;i<150;++i) { effect->process_float(effect,inputs,outputs,480);Sleep(10); }
        const float status=effect->get_parameter(effect,2);
        require(status>0 && status<1,"real NDI runtime started successfully");
        require(a==l && b==r,"real NDI path preserves local audio");
        effect->control(effect,VST_EFFECT_OPCODE_SUSPEND_RESUME,0,0,nullptr,0);
        effect->control(effect,VST_EFFECT_OPCODE_DESTROY,0,0,nullptr,0);FreeLibrary(library);
        std::puts("Packaged real-runtime smoke test passed");return 0;
    }
    auto runtime=LoadLibraryA(argv[2]);require(runtime!=nullptr,"load mock NDI");
    auto count=reinterpret_cast<int(*)()>(GetProcAddress(runtime,"TestFrameCount"));
    auto senders=reinterpret_cast<int(*)()>(GetProcAddress(runtime,"TestSenderCount"));
    auto sampleRate=reinterpret_cast<int(*)()>(GetProcAddress(runtime,"TestSampleRate"));
    auto peak=reinterpret_cast<float(*)()>(GetProcAddress(runtime,"TestLeftPeak"));
    require(count && peak && senders && sampleRate,"fixture entry points");
    auto dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"load VST2 DLL");
    using Entry=vst_effect_t* (VST_FUNCTION_INTERFACE *)(vst_host_callback_t);
    auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"VSTPluginMain"));require(entry!=nullptr,"VSTPluginMain export");
    require(entry(nullptr)==nullptr,"reject null host");
    auto* p=entry(host);require(p!=nullptr,"instantiate plugin");
    require(p->magic_number==VST_FOURCC('V','s','t','P') && p->num_inputs==2 && p->num_outputs==2,"ABI and stereo buses");
    require(p->flags&VST_EFFECT_FLAG_EDITOR,"OBS editor flag");
    require(p->flags&VST_EFFECT_FLAG_SUPPORTS_FLOAT,"OBS replacing flag");
    require(!(p->flags&VST_EFFECT_FLAG_INSTRUMENT),"not an instrument");
    require(p->control(p,VST_EFFECT_OPCODE_INITIALIZE,0,0,nullptr,0)==1,"effOpen");
    require(p->control(p,VST_EFFECT_OPCODE_VST_VERSION,0,0,nullptr,0)==2400,"API version");
    require(p->control(p,VST_EFFECT_OPCODE_SET_SAMPLE_RATE,0,0,nullptr,48000)==1,"sample rate");
    require(p->control(p,VST_EFFECT_OPCODE_SET_BLOCK_SIZE,0,1024,nullptr,0)==1,"block size");
    p->set_parameter(p,5,54.f/66.f); // -6 dB: must not change local output.
    audioCheck<float>(p,false);audioCheck<float>(p,true);audioCheck<double>(p,false);audioCheck<double>(p,true);
    p->set_parameter(p,1,3.f/15.f);p->set_parameter(p,0,1);
    void* state{};const auto size=p->control(p,VST_EFFECT_OPCODE_GET_CHUNK_DATA,1,0,&state,0);
    require(size==1060 && state,"chunk size");
    std::vector<unsigned char> saved(static_cast<unsigned char*>(state),static_cast<unsigned char*>(state)+size);
    auto* other=entry(host);require(other!=nullptr,"second instance");
    require(other->control(other,VST_EFFECT_OPCODE_SET_CHUNK_DATA,1,size,saved.data(),0)==1,"restore chunk");
    require(std::abs(other->get_parameter(other,5)-54.f/66.f)<.00001,"gain persists");
    require(std::abs(other->get_parameter(other,1)-3.f/15.f)<.00001,"source persists");
    // A 0.2.x project must load with Send selected and its original gain/source.
    struct Legacy { std::uint32_t magic,version; std::array<float,6> values; } old{};
    old.magic=VST_FOURCC('1','B','A','2');old.version=1;old.values={1,3.f/15.f,0,0,0,54.f/66.f};
    other->set_parameter(other,6,1);
    require(other->control(other,VST_EFFECT_OPCODE_SET_CHUNK_DATA,1,sizeof(old),&old,0)==1,"restore legacy 0.2 state");
    require(other->get_parameter(other,6)==0,"legacy state defaults to Send");
    saved[0]^=0xFF;
    require(other->control(other,VST_EFFECT_OPCODE_SET_CHUNK_DATA,1,size,saved.data(),0)==0,"reject corrupt state");
    other->control(other,VST_EFFECT_OPCODE_DESTROY,0,0,nullptr,0);
    // Same initialization order used by OBS: open, rate, block size, mains on.
    require(p->control(p,VST_EFFECT_OPCODE_SUSPEND_RESUME,0,1,nullptr,0)==1,"resume");
    std::array<float,480> l{},r{},outL{},outR{};l.fill(.75f);r.fill(.25f);
    const float* inputs[]{l.data(),r.data()};float* outputs[]{outL.data(),outR.data()};
    for(int i=0;i<35;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(count()>10,"NDI receives frames from plugin");
    require(sampleRate()==48000,"NDI sample rate");
    require(std::abs(peak()-.75f*std::pow(10.f,-6.f/20.f))<.0001f,"NDI gain applied by reused worker");
    require(outL==l && outR==r,"NDI gain leaves local output unchanged");
    processLevel=4;p->process_float(p,inputs,outputs,480);Sleep(80);
    const int beforeOffline=count();
    for(int i=0;i<20;++i) p->process_float(p,inputs,outputs,480);
    Sleep(30);require(count()==beforeOffline,"offline render is not transmitted");
    processLevel=2;
    p->control(p,VST_EFFECT_OPCODE_BYPASS,0,1,nullptr,0);
    p->process_float(p,inputs,outputs,480);Sleep(30);
    require(count()==beforeOffline,"bypass stops transmission");
    p->control(p,VST_EFFECT_OPCODE_BYPASS,0,0,nullptr,0);
    auto parent=CreateWindowExW(0,L"STATIC",L"1Byte Labs Audio test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        0,0,720,740,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr,"parent window");
    for(int cycle=0;cycle<5;++cycle) {
        vst_rect_t* rect{};require(p->control(p,VST_EFFECT_OPCODE_EDITOR_GET_RECT,0,0,&rect,0)==1,"editor rectangle");
        require(rect && rect->right==640 && rect->bottom==604,"editor dimensions");
        require(p->control(p,VST_EFFECT_OPCODE_EDITOR_OPEN,0,0,parent,0)==1,"attach native editor");
        auto child=FindWindowExW(parent,nullptr,L"OneByteAudioVst2Editor_0200",nullptr);require(child!=nullptr,"editor child");
        SendMessageW(GetDlgItem(child,102),TBM_SETPOS,TRUE,480);
        SendMessageW(child,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(GetDlgItem(child,102)));
        require(std::abs(p->get_parameter(p,5)-48.f/66.f)<.00001,"UI gain and host automation");
        if(cycle==0) {
            for(int i=0;i<15;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
            SendMessageW(child,WM_TIMER,1,0);
            RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);capture(child);
        }
        p->control(p,VST_EFFECT_OPCODE_EDITOR_CLOSE,0,0,nullptr,0);
    }
    require(edits>=5,"host receives automation callbacks");
    // Receive source is selected in the same editor that OBS opens.
    require(p->control(p,VST_EFFECT_OPCODE_EDITOR_OPEN,0,0,parent,0)==1,"open receive editor");
    auto child=FindWindowExW(parent,nullptr,L"OneByteAudioVst2Editor_0200",nullptr);
    SendMessageW(GetDlgItem(child,104),CB_SETCURSEL,1,0);
    SendMessageW(child,WM_COMMAND,MAKEWPARAM(104,CBN_SELCHANGE),0);
    p->set_parameter(p,5,60.f/66.f); // 0 dB
    for(int i=0;i<30;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    SendMessageW(child,WM_TIMER,1,0);
    require(SendMessageW(GetDlgItem(child,105),CB_GETCOUNT,0,0)==3,"discovered source list");
    SendMessageW(GetDlgItem(child,105),CB_SETCURSEL,2,0); // sorted list: Alternate, Audio
    SendMessageW(child,WM_COMMAND,MAKEWPARAM(105,CBN_SELCHANGE),0);
    for(int i=0;i<45;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(std::abs(outL[100]-.375f)<.0001f && std::abs(outR[100]-.125f)<.0001f,"Receive replaces host input, planar stride correct");
    const int sentBefore=count();
    for(int i=0;i<10;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(count()==sentBefore,"Receive never retransmits host input");
    std::array<double,480> dl{},dr{}; double* doubles[]{dl.data(),dr.data()};
    for(int i=0;i<10;++i) { p->process_double(p,nullptr,doubles,480);Sleep(10); }
    require(std::abs(dl[100]-.375)<.0001 && std::abs(dr[100]-.125)<.0001,"double Receive and null host input");
    // Persist the selected name together with the Receive mode.
    state=nullptr;const auto receiveSize=p->control(p,VST_EFFECT_OPCODE_GET_CHUNK_DATA,1,0,&state,0);
    std::vector<unsigned char> receiveSaved(static_cast<unsigned char*>(state),static_cast<unsigned char*>(state)+receiveSize);
    auto restored=entry(host);
    require(restored->control(restored,VST_EFFECT_OPCODE_SET_CHUNK_DATA,1,receiveSize,receiveSaved.data(),0)==1,"receive state restore");
    require(restored->get_parameter(restored,6)==1,"receive mode persists");
    void* restoredState{};restored->control(restored,VST_EFFECT_OPCODE_GET_CHUNK_DATA,1,0,&restoredState,0);
    require(std::memcmp(static_cast<unsigned char*>(restoredState)+36,receiveSaved.data()+36,1024)==0,"source name persists exactly");
    restored->control(restored,VST_EFFECT_OPCODE_DESTROY,0,0,nullptr,0);
    p->set_parameter(p,5,54.f/66.f);
    for(int i=0;i<30;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(std::abs(outL[100]-.375f*std::pow(10.f,-6.f/20.f))<.0001f,"Receive fader gain");
    p->set_parameter(p,5,60.f/66.f);
    SendMessageW(GetDlgItem(child,105),CB_SETCURSEL,1,0);
    SendMessageW(child,WM_COMMAND,MAKEWPARAM(105,CBN_SELCHANGE),0);
    for(int i=0;i<40;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(outL[100]<-.37f && outR[100]<-.12f,"switch Receive source without old source audio");
    auto fail=reinterpret_cast<void(*)(int)>(GetProcAddress(runtime,"TestFailure"));
    fail(3);
    for(int i=0;i<30;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(outL[100]==0 && p->get_parameter(p,2)<.5f,"disconnect becomes silence and waiting");
    fail(0);
    for(int i=0;i<30;++i) { p->process_float(p,inputs,outputs,480);Sleep(10); }
    require(outL[100]<-.37f,"reconnection recovers audio");
    p->set_parameter(p,0,0);p->process_float(p,inputs,outputs,480);
    require(outL[100]==0 && outR[100]==0,"disabled Receive is silent");
    p->control(p,VST_EFFECT_OPCODE_BYPASS,0,1,nullptr,0);
    p->process_float(p,inputs,outputs,480);
    require(outL==l && outR==r,"bypassed Receive passes local input");
    p->control(p,VST_EFFECT_OPCODE_BYPASS,0,0,nullptr,0);
    p->control(p,VST_EFFECT_OPCODE_EDITOR_CLOSE,0,0,nullptr,0);
    DestroyWindow(parent);
    p->control(p,VST_EFFECT_OPCODE_SUSPEND_RESUME,0,0,nullptr,0);
    require(senders()==0,"suspend releases NDI sender");
    auto receiveObjects=reinterpret_cast<int(*)()>(GetProcAddress(runtime,"TestReceiveObjects"));
    require(receiveObjects && receiveObjects()==0,"suspend releases finder receiver and FrameSync");
    p->control(p,VST_EFFECT_OPCODE_DESTROY,0,0,nullptr,0);
    FreeLibrary(dll);FreeLibrary(runtime);
    // Reload must work after the native window class and runtime were released.
    dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"DLL reload");
    entry=reinterpret_cast<Entry>(GetProcAddress(dll,"VSTPluginMain"));p=entry(host);
    require(p!=nullptr,"instance after reload");p->control(p,VST_EFFECT_OPCODE_DESTROY,0,0,nullptr,0);FreeLibrary(dll);
    std::puts("VST2 host: load/reload, OBS initialization order, float/double pass-through, chunks, NDI packet/gain, offline/bypass, UI/automation and cleanup passed");
}
