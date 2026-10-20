#include "editor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>

namespace zosma {
namespace {
constexpr int Width=640, Height=604;
constexpr COLORREF Bg=RGB(13,17,24), Card=RGB(22,29,39), Edge=RGB(39,49,64);
constexpr COLORREF Text=RGB(235,241,248), Muted=RGB(147,163,183);
constexpr COLORREF Green=RGB(63,222,166), Amber=RGB(250,190,80), Red=RGB(255,105,119);
const wchar_t* WindowClass=L"OneByteAudioVst2Editor_0200";
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
std::wstring wide(const std::string& value) {
    const int count=MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,nullptr,0);
    if(count<=0) return L"";
    std::wstring result(static_cast<std::size_t>(count),0);
    MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,result.data(),count);
    result.resize(static_cast<std::size_t>(count-1)); return result;
}
HFONT font(int size,int weight=FW_NORMAL) {
    return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}
}
NativeEditor::NativeEditor(EditorClient* controller,NdiAudio& audio)
    : controller_(controller),audio_(audio) {
    normal_=font(16); small_=font(13); title_=font(30,FW_SEMIBOLD); number_=font(24,FW_SEMIBOLD);
}
NativeEditor::~NativeEditor() {
    close(); DeleteObject(normal_); DeleteObject(small_); DeleteObject(title_); DeleteObject(number_);
}
bool NativeEditor::open(HWND parent) {
    if(!parent || !IsWindow(parent) || window_) return false;
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
    // Fixed logical size: follows the host's DPI context without resizing its frame.
    scale_=1.;
    window_=CreateWindowExW(0,WindowClass,L"1Byte Labs Audio",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,Width,Height,parent,nullptr,module,this);
    if(!window_) return false;
    auto p=[this](int value){return static_cast<int>(value*scale_);};
    power_=CreateWindowExW(0,L"BUTTON",L"Ligar envio NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
        p(414),p(112),p(178),p(46),window_,reinterpret_cast<HMENU>(100),module,nullptr);
    diagnostic_=CreateWindowExW(0,L"BUTTON",L"Diagnostico NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        p(414),p(184),p(178),p(30),window_,reinterpret_cast<HMENU>(103),module,nullptr);
    source_=CreateWindowExW(0,L"COMBOBOX",L"Numero da fonte NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
        p(240),p(190),p(80),p(280),window_,reinterpret_cast<HMENU>(101),module,nullptr);
    for(int i=1;i<=16;++i) {
        wchar_t label[16]; std::swprintf(label,16,L"%02d",i); SendMessageW(source_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    }
    mode_=CreateWindowExW(0,L"COMBOBOX",L"Modo",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
        p(414),p(74),p(178),p(120),window_,reinterpret_cast<HMENU>(104),module,nullptr);
    SendMessageW(mode_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Enviar"));
    SendMessageW(mode_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Receber"));
    receiveSource_=CreateWindowExW(0,L"COMBOBOX",L"Fonte NDI para receber",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
        p(48),p(184),p(350),p(280),window_,reinterpret_cast<HMENU>(105),module,nullptr);
    SendMessageW(receiveSource_,CB_SETDROPPEDWIDTH,540,0);
    comboFont_=font(p(16));
    SendMessageW(mode_,WM_SETFONT,reinterpret_cast<WPARAM>(comboFont_),TRUE);
    SendMessageW(receiveSource_,WM_SETFONT,reinterpret_cast<WPARAM>(comboFont_),TRUE);
    SendMessageW(source_,WM_SETFONT,reinterpret_cast<WPARAM>(comboFont_),TRUE);
    SetWindowTheme(source_,L"DarkMode_Explorer",nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    volume_=CreateWindowExW(0,TRACKBAR_CLASSW,L"Volume de envio NDI",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,
        p(48),p(432),p(430),p(28),window_,reinterpret_cast<HMENU>(102),module,nullptr);
    SendMessageW(volume_,TBM_SETRANGE,TRUE,MAKELPARAM(0,660));
    SendMessageW(volume_,TBM_SETPAGESIZE,0,10);
    SendMessageW(volume_,TBM_SETPOS,TRUE,600);
    website_=CreateWindowExW(0,L"BUTTON",L"1bytelabs.com.br",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
        p(220),p(570),p(200),p(22),window_,reinterpret_cast<HMENU>(106),module,nullptr);
    audio_.peaks[0].exchange(0); audio_.peaks[1].exchange(0);
    lastTick_=GetTickCount64(); tick();
    SetTimer(window_,1,66,nullptr);
    return true;
}
void NativeEditor::close() {
    if(bufferDc_) {
        SelectObject(bufferDc_,originalBitmap_);
        DeleteObject(bufferBitmap_); DeleteDC(bufferDc_);
        bufferDc_=nullptr; bufferBitmap_=originalBitmap_=nullptr;
        bufferWidth_=bufferHeight_=0;
    }
    if(window_) {
        KillTimer(window_,1);
        DestroyWindow(window_); window_=power_=source_=volume_=diagnostic_=mode_=receiveSource_=website_=nullptr;
        if(comboFont_) { DeleteObject(comboFont_); comboFont_=nullptr; }
    }
}
void NativeEditor::edit(std::uint32_t id,double value) {
    controller_->editParameter(id,value);
    tick();
}
void NativeEditor::tick() {
    const bool receive=audio_.receiveMode.load();
    // Never reset a combo's tentative mouse selection while its list is open.
    if(!SendMessageW(mode_,CB_GETDROPPEDSTATE,0,0) &&
       SendMessageW(mode_,CB_GETCURSEL,0,0)!=(receive ? 1 : 0))
        SendMessageW(mode_,CB_SETCURSEL,receive ? 1 : 0,0);
    const bool sourceVisible=(GetWindowLongPtrW(source_,GWL_STYLE)&WS_VISIBLE)!=0;
    const bool receiveVisible=(GetWindowLongPtrW(receiveSource_,GWL_STYLE)&WS_VISIBLE)!=0;
    if(sourceVisible==receive) ShowWindow(source_,receive ? SW_HIDE : SW_SHOW);
    if(receiveVisible!=receive) ShowWindow(receiveSource_,receive ? SW_SHOW : SW_HIDE);
    if(receive && !SendMessageW(receiveSource_,CB_GETDROPPEDSTATE,0,0)) {
        auto names=audio_.sources(); const auto selected=audio_.selectedSource();
        if(!selected.empty() && std::find(names.begin(),names.end(),selected)==names.end()) names.push_back(selected);
        if(names!=visibleSources_ || SendMessageW(receiveSource_,CB_GETCOUNT,0,0)==0) {
            visibleSources_=names; SendMessageW(receiveSource_,CB_RESETCONTENT,0,0);
            SendMessageW(receiveSource_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Selecione uma fonte NDI"));
            for(const auto& name:names) {
                const auto label=wide(name);
                SendMessageW(receiveSource_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
        }
        const auto found=std::find(visibleSources_.begin(),visibleSources_.end(),selected);
        const auto desired=found==visibleSources_.end() ? 0 : 1+std::distance(visibleSources_.begin(),found);
        if(SendMessageW(receiveSource_,CB_GETCURSEL,0,0)!=desired)
            SendMessageW(receiveSource_,CB_SETCURSEL,desired,0);
    }
    const auto now=GetTickCount64();
    const float decay=static_cast<float>(std::pow(.12,std::min<ULONGLONG>(now-lastTick_,1000)/1000.));
    for(int i=0;i<2;++i) {
        const float peak=audio_.peaks[i].exchange(0,std::memory_order_relaxed);
        if(peak>=1.f) clipUntil_[i]=now+1000;
        meter_[i]=std::max(peak,meter_[i]*decay);
    }
    lastTick_=now;
    const auto position=static_cast<int>(std::lround((std::clamp(audio_.sendDb.load(),-60.f,6.f)+60.f)*10.f));
    if(SendMessageW(volume_,TBM_GETPOS,0,0)!=position) SendMessageW(volume_,TBM_SETPOS,TRUE,position);
    const auto slot=std::clamp(audio_.slot.load(),1,16)-1;
    if(SendMessageW(source_,CB_GETCURSEL,0,0)!=slot && !SendMessageW(source_,CB_GETDROPPEDSTATE,0,0))
        SendMessageW(source_,CB_SETCURSEL,slot,0);
    const wchar_t* label=audio_.enabled.load() ? L"Desligar" : receive ? L"Ligar recepcao NDI" : L"Ligar envio NDI";
    wchar_t previous[64]{}; GetWindowTextW(power_,previous,64);
    if(std::wcscmp(previous,label)) SetWindowTextW(power_,label);
    InvalidateRect(window_,nullptr,FALSE); InvalidateRect(power_,nullptr,FALSE);
}
void NativeEditor::paint(HDC target) {
    RECT client{}; GetClientRect(window_,&client);
    if(client.right<=0 || client.bottom<=0) return;
    // Reuse the backing bitmap instead of allocating a full frame every tick.
    if(!bufferDc_) bufferDc_=CreateCompatibleDC(target);
    if(!bufferDc_) return;
    if(!bufferBitmap_ || bufferWidth_!=client.right || bufferHeight_!=client.bottom) {
        if(bufferBitmap_) { SelectObject(bufferDc_,originalBitmap_); DeleteObject(bufferBitmap_); bufferBitmap_=nullptr; }
        bufferBitmap_=CreateCompatibleBitmap(target,client.right,client.bottom);
        if(!bufferBitmap_) return;
        auto previous=static_cast<HBITMAP>(SelectObject(bufferDc_,bufferBitmap_));
        if(!originalBitmap_) originalBitmap_=previous;
        bufferWidth_=client.right; bufferHeight_=client.bottom;
    }
    HDC dc=bufferDc_;
    fill(dc,client,Bg);
    SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,Width,Height,nullptr);
    SetViewportExtEx(dc,client.right,client.bottom,nullptr);
    text(dc,small_,L"1BYTE LABS  /  AUDIO OVER NETWORK",28,18,480,22,Green);
    text(dc,title_,L"1Byte Labs Audio",28,44,400,42);
    text(dc,small_,L"VST2 / 0.4.1",480,51,132,24,Muted,DT_RIGHT);
    rounded(dc,28,100,584,128,Card);
    const bool enabled=audio_.enabled.load(), processing=audio_.processing.load(), receive=audio_.receiveMode.load();
    const int status=audio_.status.load();
    const wchar_t* state=!enabled ? L"Desligado" : !processing ? L"Host pausou o processamento" :
        status==2 ? (receive ? L"Fonte conectada" : L"Receptor conectado") : status==3 ? L"Erro ao iniciar NDI" : (receive ? L"Aguardando fonte" : L"Aguardando receptor");
    const COLORREF stateColor=!enabled ? Muted : status==3 ? Red : status==2 && processing ? Green : Amber;
    rounded(dc,48,120,9,9,stateColor,stateColor);
    text(dc,normal_,state,68,110,330,28,stateColor);
    wchar_t source[80]; std::swprintf(source,80,L"1Byte Labs Send %02d",audio_.slot.load());
    const auto selected=wide(audio_.selectedSource());
    text(dc,receive ? small_ : number_,receive ? (selected.empty() ? L"Escolha a fonte abaixo" : selected.c_str()) : source,48,142,342,36);
    if(!receive) text(dc,small_,L"NUMERO DA FONTE",48,190,186,24,Muted);

    rounded(dc,28,244,584,128,Card);
    text(dc,small_,receive ? L"AUDIO RECEBIDO / APOS FADER" : L"AUDIO PARA ENVIO / APOS FADER",48,254,300,22,Muted);
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
    rounded(dc,28,388,584,88,Card);
    text(dc,small_,receive ? L"VOLUME RECEBIDO" : L"VOLUME DE ENVIO",48,398,300,22,Muted);
    text(dc,small_,L"-60 dB",48,414,70,18,Muted);
    text(dc,small_,L"0 dB",410,414,34,18,Muted);
    text(dc,small_,L"+6 dB",448,414,36,18,Muted);
    wchar_t volumeText[32]; std::swprintf(volumeText,32,L"%+.1f dB",audio_.sendDb.load());
    text(dc,number_,volumeText,482,414,110,34,Green,DT_RIGHT);
    rounded(dc,28,492,282,68,Card); rounded(dc,326,492,286,68,Card);
    text(dc,small_,receive ? L"BUFFER DE RECEPCAO" : L"FILA DE ENVIO",48,498,220,20,Muted);
    const double queued=std::clamp(audio_.queuedSamples()*1000./audio_.sampleRate.load(),0.,100.);
    wchar_t queue[64]; std::swprintf(queue,64,L"%.1f ms",queued);
    text(dc,number_,queue,48,520,220,30,queued>=75 ? Amber : Text);
    const auto drops=receive ? audio_.underruns.load() : audio_.dropped.load(); wchar_t dropped[64]; std::swprintf(dropped,64,L"%llu",static_cast<unsigned long long>(drops));
    text(dc,small_,receive ? L"FALTAS DE BUFFER" : L"SAMPLES DESCARTADOS",346,498,246,20,Muted);
    text(dc,number_,dropped,346,520,246,30,drops ? Amber : Green);
    SetMapMode(dc,MM_TEXT);
    BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
}
LRESULT CALLBACK NativeEditor::windowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto* self=reinterpret_cast<NativeEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        self=static_cast<NativeEditor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(!self) return DefWindowProcW(window,msg,wp,lp);
    switch(msg) {
    case WM_SETCURSOR:
        if(reinterpret_cast<HWND>(wp)==self->website_) { SetCursor(LoadCursorW(nullptr,IDC_HAND)); return TRUE; }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if(wp==1) self->tick(); return 0;
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(lp)==self->volume_) {
            const auto position=SendMessageW(self->volume_,TBM_GETPOS,0,0);
            self->edit(5,position/660.);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        auto* notification=reinterpret_cast<NMCUSTOMDRAW*>(lp);
        if(notification->hdr.hwndFrom!=self->volume_ || notification->hdr.code!=NM_CUSTOMDRAW) break;
        if(notification->dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if(notification->dwDrawStage==CDDS_ITEMPREPAINT) {
            const auto& r=notification->rc;
            if(notification->dwItemSpec==TBCD_CHANNEL) {
                fill(notification->hdc,r,Bg); return CDRF_SKIPDEFAULT;
            }
            if(notification->dwItemSpec==TBCD_THUMB) {
                rounded(notification->hdc,r.left,r.top,r.right-r.left,r.bottom-r.top,Green,Green);
                return CDRF_SKIPDEFAULT;
            }
        }
        return CDRF_DODEFAULT;
    }
    case WM_PAINT: { PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); self->paint(dc); EndPaint(window,&ps); return 0; }
    case WM_COMMAND:
        if(LOWORD(wp)==106 && HIWORD(wp)==BN_CLICKED) {
            ShellExecuteW(window,L"open",L"https://1bytelabs.com.br",nullptr,nullptr,SW_SHOWNORMAL);
            return 0;
        }
        if(LOWORD(wp)==104 && HIWORD(wp)==CBN_SELCHANGE) self->edit(6,SendMessageW(self->mode_,CB_GETCURSEL,0,0)==1 ? 1. : 0.);
        if(LOWORD(wp)==105 && HIWORD(wp)==CBN_SELCHANGE) {
            const auto selected=SendMessageW(self->receiveSource_,CB_GETCURSEL,0,0);
            self->audio_.selectSource(selected>0 && static_cast<std::size_t>(selected)<=self->visibleSources_.size() ?
                self->visibleSources_[static_cast<std::size_t>(selected-1)] : "");
            self->edit(6,1.); // Notify host to save the updated source in its state chunk.
        }
        if(LOWORD(wp)==103 && HIWORD(wp)==BN_CLICKED) {
            wchar_t host[32768]{}; GetModuleFileNameW(nullptr,host,32768);
            const auto report=L"1Byte Labs Audio VST2 0.4.1 / Windows x64\r\nHost: "+
                std::wstring(host)+L"\r\nFonte selecionada: "+wide(self->audio_.selectedSource())+L"\r\n\r\n"+self->audio_.diagnostic();
            MessageBoxW(window,report.c_str(),L"Diagnostico NDI",MB_OK|MB_ICONINFORMATION);
        }
        if(LOWORD(wp)==100 && HIWORD(wp)==BN_CLICKED) self->edit(0,self->audio_.enabled.load() ? 0. : 1.);
        if(LOWORD(wp)==101 && HIWORD(wp)==CBN_SELCHANGE) {
            const auto selected=SendMessageW(self->source_,CB_GETCURSEL,0,0);
            if(selected>=0) self->edit(1,selected/15.);
        }
        return 0;
    case WM_DRAWITEM: {
        auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item->CtlID==106) {
            fill(item->hDC,item->rcItem,Bg);
            text(item->hDC,self->small_,L"1bytelabs.com.br",0,0,
                item->rcItem.right,item->rcItem.bottom,Green,DT_CENTER);
            if(item->itemState&ODS_FOCUS) {
                RECT focus=item->rcItem; InflateRect(&focus,-2,-2); DrawFocusRect(item->hDC,&focus);
            }
            return TRUE;
        }
        if(item->CtlID!=100) break;
        const bool on=self->audio_.enabled.load();
        auto color=on ? RGB(39,84,72) : Green;
        fill(item->hDC,item->rcItem,Card);
        const int saved=SaveDC(item->hDC);
        SetMapMode(item->hDC,MM_ANISOTROPIC); SetWindowExtEx(item->hDC,178,46,nullptr);
        SetViewportExtEx(item->hDC,item->rcItem.right,item->rcItem.bottom,nullptr);
        rounded(item->hDC,0,0,178,46,color,color);
        text(item->hDC,self->normal_,on ? L"Desligar" : self->audio_.receiveMode ? L"Ligar recepcao" : L"Ligar envio",0,0,178,46,on ? Green : Bg,DT_CENTER);
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
