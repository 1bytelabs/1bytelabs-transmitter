#include "editor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <uxtheme.h>

namespace zosma {
namespace {
constexpr int Width=640, Height=500;
constexpr COLORREF Bg=RGB(13,17,24), Card=RGB(22,29,39), Edge=RGB(39,49,64);
constexpr COLORREF Text=RGB(235,241,248), Muted=RGB(147,163,183);
constexpr COLORREF Green=RGB(63,222,166), Amber=RGB(250,190,80), Red=RGB(255,105,119);
const wchar_t* WindowClass=L"ZosmaSendEditor_0101";
struct RegisteredClass {
    HINSTANCE module{};
    ~RegisteredClass() { if(module) UnregisterClassW(WindowClass,module); }
} registeredClass;
void fill(HDC dc,RECT r,COLORREF color) {
    auto brush=CreateSolidBrush(color); FillRect(dc,&r,brush); DeleteObject(brush);
}
void rounded(HDC dc,int x,int y,int w,int h,COLORREF color,COLORREF border=Edge) {
    auto brush=CreateSolidBrush(color); auto pen=CreatePen(PS_SOLID,1,border);
    auto oldBrush=SelectObject(dc,brush); auto oldPen=SelectObject(dc,pen);
    RoundRect(dc,x,y,x+w,y+h,16,16);
    SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(pen); DeleteObject(brush);
}
void text(HDC dc,HFONT font,const wchar_t* value,int x,int y,int w,int h,COLORREF color=Text,UINT flags=DT_LEFT) {
    auto old=SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    RECT rect{x,y,x+w,y+h}; DrawTextW(dc,value,-1,&rect,flags|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);
}
HFONT font(int size,int weight=FW_NORMAL) {
    return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}
}
SendEditor::SendEditor(Steinberg::Vst::EditController* controller,NdiAudio& audio)
    : controller_(controller),audio_(audio) {
    controller_->addRef();
    normal_=font(16); small_=font(13); title_=font(30,FW_SEMIBOLD); number_=font(24,FW_SEMIBOLD);
    rect={0,0,Width,Height};
}
SendEditor::~SendEditor() {
    close(); DeleteObject(normal_); DeleteObject(small_); DeleteObject(title_); DeleteObject(number_);
    controller_->release();
}
Steinberg::tresult PLUGIN_API SendEditor::isPlatformTypeSupported(Steinberg::FIDString type) {
    return type && std::strcmp(type,Steinberg::kPlatformTypeHWND)==0 ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}
Steinberg::tresult PLUGIN_API SendEditor::attached(void* parent,Steinberg::FIDString type) {
    if(!parent || !IsWindow(static_cast<HWND>(parent)) || isPlatformTypeSupported(type)!=Steinberg::kResultTrue || window_)
        return Steinberg::kResultFalse;
    static std::once_flag registration;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&windowProc),&module);
    std::call_once(registration,[module] {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=windowProc; wc.hInstance=module;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=WindowClass;
        RegisterClassExW(&wc);
        registeredClass.module=module;
    });
    const auto dpi=GetDpiForWindow(static_cast<HWND>(parent));
    scale_=dpi ? dpi/96. : 1.;
    rect={0,0,static_cast<Steinberg::int32>(Width*scale_),static_cast<Steinberg::int32>(Height*scale_)};
    window_=CreateWindowExW(0,WindowClass,L"Zosma Send",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,rect.getWidth(),rect.getHeight(),static_cast<HWND>(parent),nullptr,module,this);
    if(!window_) return Steinberg::kResultFalse;
    auto p=[this](int value){return static_cast<int>(value*scale_);};
    power_=CreateWindowExW(0,L"BUTTON",L"Ligar envio NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
        p(414),p(112),p(178),p(46),window_,reinterpret_cast<HMENU>(100),module,nullptr);
    source_=CreateWindowExW(0,L"COMBOBOX",L"Numero da fonte NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
        p(240),p(190),p(80),p(280),window_,reinterpret_cast<HMENU>(101),module,nullptr);
    for(int i=1;i<=16;++i) {
        wchar_t label[16]; std::swprintf(label,16,L"%02d",i); SendMessageW(source_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    }
    comboFont_=font(p(16));
    SendMessageW(source_,WM_SETFONT,reinterpret_cast<WPARAM>(comboFont_),TRUE);
    SetWindowTheme(source_,L"DarkMode_Explorer",nullptr);
    audio_.peaks[0].exchange(0); audio_.peaks[1].exchange(0);
    lastTick_=GetTickCount64(); tick();
    SetTimer(window_,1,33,nullptr);
    CPluginView::attached(parent,type);
    if(plugFrame) plugFrame->resizeView(this,&rect);
    return Steinberg::kResultOk;
}
void SendEditor::close() {
    if(window_) {
        KillTimer(window_,1);
        DestroyWindow(window_); window_=power_=source_=nullptr;
        if(comboFont_) { DeleteObject(comboFont_); comboFont_=nullptr; }
    }
}
Steinberg::tresult PLUGIN_API SendEditor::removed() { close(); return CPluginView::removed(); }
Steinberg::tresult PLUGIN_API SendEditor::onSize(Steinberg::ViewRect* size) {
    if(!size) return Steinberg::kInvalidArgument;
    CPluginView::onSize(size);
    if(window_) SetWindowPos(window_,nullptr,0,0,size->getWidth(),size->getHeight(),SWP_NOMOVE|SWP_NOZORDER);
    return Steinberg::kResultOk;
}
Steinberg::tresult PLUGIN_API SendEditor::checkSizeConstraint(Steinberg::ViewRect* size) {
    if(!size) return Steinberg::kInvalidArgument;
    size->right=size->left+static_cast<int>(Width*scale_); size->bottom=size->top+static_cast<int>(Height*scale_);
    return Steinberg::kResultTrue;
}
void SendEditor::edit(Steinberg::Vst::ParamID id,double value) {
    controller_->beginEdit(id); controller_->setParamNormalized(id,value);
    controller_->performEdit(id,value); controller_->endEdit(id);
    tick();
}
void SendEditor::tick() {
    const auto now=GetTickCount64();
    const float decay=static_cast<float>(std::pow(.12,std::min<ULONGLONG>(now-lastTick_,1000)/1000.));
    for(int i=0;i<2;++i) {
        const float peak=audio_.peaks[i].exchange(0,std::memory_order_relaxed);
        if(peak>=1.f) clipUntil_[i]=now+1000;
        meter_[i]=std::max(peak,meter_[i]*decay);
    }
    lastTick_=now;
    const auto slot=std::clamp(audio_.slot.load(),1,16)-1;
    if(SendMessageW(source_,CB_GETCURSEL,0,0)!=slot && !SendMessageW(source_,CB_GETDROPPEDSTATE,0,0))
        SendMessageW(source_,CB_SETCURSEL,slot,0);
    const wchar_t* label=audio_.enabled.load() ? L"Desligar envio NDI" : L"Ligar envio NDI";
    wchar_t previous[64]{}; GetWindowTextW(power_,previous,64);
    if(std::wcscmp(previous,label)) SetWindowTextW(power_,label);
    InvalidateRect(window_,nullptr,FALSE); InvalidateRect(power_,nullptr,FALSE);
}
void SendEditor::paint(HDC target) {
    RECT client{}; GetClientRect(window_,&client);
    HDC dc=CreateCompatibleDC(target); auto bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);
    auto previous=SelectObject(dc,bitmap);
    fill(dc,client,Bg);
    SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,Width,Height,nullptr);
    SetViewportExtEx(dc,client.right,client.bottom,nullptr);
    text(dc,small_,L"ZOSMA LABS  /  AUDIO OVER NETWORK",28,18,480,22,Green);
    text(dc,title_,L"Zosma Send",28,44,400,42);
    text(dc,small_,L"VST3  /  0.1.1",480,51,132,24,Muted,DT_RIGHT);
    rounded(dc,28,100,584,128,Card);
    const bool enabled=audio_.enabled.load(), processing=audio_.processing.load();
    const int status=audio_.status.load();
    const wchar_t* state=!enabled ? L"Envio desligado" : !processing ? L"REAPER pausou o processamento" :
        status==2 ? L"Receptor conectado" : status==3 ? L"Erro ao iniciar NDI" : L"Aguardando receptor";
    const COLORREF stateColor=!enabled ? Muted : status==3 ? Red : status==2 && processing ? Green : Amber;
    rounded(dc,48,120,9,9,stateColor,stateColor);
    text(dc,normal_,state,68,110,330,28,stateColor);
    wchar_t source[80]; std::swprintf(source,80,L"Zosma Send %02d",audio_.slot.load());
    text(dc,number_,source,48,142,342,36);
    text(dc,small_,L"NUMERO DA FONTE",48,190,186,24,Muted);
    text(dc,small_,L"Estereo / NDI",428,190,164,24,Muted,DT_RIGHT);
    rounded(dc,28,244,584,128,Card);
    text(dc,small_,L"ENTRADA DO REAPER",48,254,300,22,Muted);
    wchar_t rate[80]; std::swprintf(rate,80,L"%.1f kHz  /  2 canais",audio_.sampleRate.load()/1000.);
    text(dc,small_,rate,340,254,252,22,Muted,DT_RIGHT);
    for(int channel=0;channel<2;++channel) {
        const int y=292+channel*34;
        text(dc,small_,channel ? L"R" : L"L",48,y-3,18,23,Muted);
        const float db=meter_[channel]>0 ? 20.f*std::log10(meter_[channel]) : -90.f;
        const int value=static_cast<int>(std::clamp((db+60.f)/60.f,0.f,1.f)*430);
        fill(dc,{76,y,506,y+12},Bg);
        const int green=std::min(value,344), yellow=std::min(value,409);
        if(green) fill(dc,{76,y,76+green,y+12},Green);
        if(yellow>344) fill(dc,{420,y,76+yellow,y+12},Amber);
        if(value>409) fill(dc,{485,y,76+value,y+12},Red);
        wchar_t level[32];
        if(db<=-90.f) std::wcscpy(level,L"-inf"); else std::swprintf(level,32,L"%.1f",db);
        text(dc,small_,level,518,y-5,58,24,clipUntil_[channel]>GetTickCount64() ? Red : Text,DT_RIGHT);
    }
    text(dc,small_,L"-60                 -36                  -18                  -6      0 dBFS",76,345,516,19,Muted);
    rounded(dc,28,388,282,68,Card); rounded(dc,326,388,286,68,Card);
    text(dc,small_,L"FILA DE ENVIO",48,394,220,20,Muted);
    const double queued=std::clamp(audio_.queue.available()*1000./audio_.sampleRate.load(),0.,100.);
    wchar_t queue[64]; std::swprintf(queue,64,L"%.1f ms",queued);
    text(dc,number_,queue,48,416,220,30,queued>=75 ? Amber : Text);
    const auto drops=audio_.dropped.load(); wchar_t dropped[64]; std::swprintf(dropped,64,L"%llu",static_cast<unsigned long long>(drops));
    text(dc,small_,L"SAMPLES DESCARTADOS",346,394,246,20,Muted);
    text(dc,number_,dropped,346,416,246,30,drops ? Amber : Green);
    text(dc,small_,L"Fila nao e latencia total. Conectado indica um receptor NDI na rede.",28,466,584,22,Muted);
    SetMapMode(dc,MM_TEXT);
    BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
}
LRESULT CALLBACK SendEditor::windowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto* self=reinterpret_cast<SendEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        self=static_cast<SendEditor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(!self) return DefWindowProcW(window,msg,wp,lp);
    switch(msg) {
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if(wp==1) self->tick(); return 0;
    case WM_PAINT: { PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); self->paint(dc); EndPaint(window,&ps); return 0; }
    case WM_COMMAND:
        if(LOWORD(wp)==100 && HIWORD(wp)==BN_CLICKED) self->edit(0,self->audio_.enabled.load() ? 0. : 1.);
        if(LOWORD(wp)==101 && HIWORD(wp)==CBN_SELCHANGE) {
            const auto selected=SendMessageW(self->source_,CB_GETCURSEL,0,0);
            if(selected>=0) self->edit(1,selected/15.);
        }
        return 0;
    case WM_DRAWITEM: {
        auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item->CtlID!=100) break;
        const bool on=self->audio_.enabled.load();
        auto color=on ? RGB(39,84,72) : Green;
        fill(item->hDC,item->rcItem,Card);
        const int saved=SaveDC(item->hDC);
        SetMapMode(item->hDC,MM_ANISOTROPIC); SetWindowExtEx(item->hDC,178,46,nullptr);
        SetViewportExtEx(item->hDC,item->rcItem.right,item->rcItem.bottom,nullptr);
        rounded(item->hDC,0,0,178,46,color,color);
        text(item->hDC,self->normal_,on ? L"Desligar envio" : L"Ligar envio",0,0,178,46,on ? Green : Bg,DT_CENTER);
        if(item->itemState&ODS_FOCUS) { RECT focus{5,5,173,41}; DrawFocusRect(item->hDC,&focus); }
        RestoreDC(item->hDC,saved);
        return TRUE;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wp),Text); SetBkColor(reinterpret_cast<HDC>(wp),Card);
        SetDCBrushColor(reinterpret_cast<HDC>(wp),Card);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
