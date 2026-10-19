#include "../editor.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;
void require(bool value) { if(!value) { std::fprintf(stderr,"Editor smoke test failed\n"); std::abort(); } }
class Controller final : public EditController {
public:
    zosma::NdiAudio audio;
    tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue value) override {
        if(id==0) audio.enabled=value>=.5;
        if(id==1) audio.slot=1+static_cast<int>(std::lround(value*15));
        return kResultOk;
    }
};
void capture(HWND window,const wchar_t* path) {
    RECT rect{}; GetClientRect(window,&rect);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=rect.right; info.bmiHeader.biHeight=-rect.bottom;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(window), buffer=CreateCompatibleDC(dc); void* pixels{};
    HBITMAP image=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    require(image!=nullptr); auto previous=SelectObject(buffer,image);
    PrintWindow(window,buffer,PW_CLIENTONLY);
    const DWORD bytes=rect.right*rect.bottom*4;
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+bytes;
    FILE* output{}; _wfopen_s(&output,path,L"wb"); require(output!=nullptr);
    std::fwrite(&file,sizeof(file),1,output); std::fwrite(&info.bmiHeader,sizeof(BITMAPINFOHEADER),1,output);
    std::fwrite(pixels,bytes,1,output); std::fclose(output);
    SelectObject(buffer,previous); DeleteObject(image); DeleteDC(buffer); ReleaseDC(window,dc);
}
int main() {
    SetProcessDPIAware();
    auto* controller=new Controller;
    controller->initialize(nullptr);
    auto parent=CreateWindowExW(0,L"STATIC",L"1Byte Labs Send UI smoke test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        0,0,720,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr);
    auto* editor=new zosma::SendEditor(controller,controller->audio);
    require(editor->isPlatformTypeSupported(kPlatformTypeHWND)==kResultTrue);
    require(editor->attached(parent,kPlatformTypeHWND)==kResultOk);
    auto child=editor->window();
    require(GetDlgItem(child,100)!=nullptr && GetDlgItem(child,101)!=nullptr);
    SendMessageW(child,WM_COMMAND,MAKEWPARAM(100,BN_CLICKED),0);
    require(controller->audio.enabled.load());
    SendMessageW(GetDlgItem(child,101),CB_SETCURSEL,6,0);
    SendMessageW(child,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),0);
    require(controller->audio.slot.load()==7);
    controller->audio.status=2; controller->audio.processing=true;
    controller->audio.peaks[0]=.45f; controller->audio.peaks[1]=.25f;
    SendMessageW(child,WM_TIMER,1,0);
    RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    capture(child,L"editor-preview.bmp");
    controller->audio.status=3; controller->audio.dropped=123;
    SendMessageW(child,WM_TIMER,1,0);
    RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    capture(child,L"editor-error-preview.bmp");
    require(editor->removed()==kResultOk);
    for(int i=0;i<10;++i) {
        require(editor->attached(parent,kPlatformTypeHWND)==kResultOk);
        require(editor->removed()==kResultOk);
    }
    editor->release(); controller->terminate(); controller->release(); DestroyWindow(parent);
    std::puts("Editor: HWND attach, send toggle, source change, live telemetry paint and repeated detach/reattach passed");
}
