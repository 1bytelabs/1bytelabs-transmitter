#pragma once
#include "ndi_audio.h"
#include <windows.h>
#include <cstdint>

namespace zosma {
class EditorClient {
public:
    virtual ~EditorClient()=default;
    virtual void editParameter(std::uint32_t id,double normalized)=0;
};
// Native Win32 editor with no dependency on the Steinberg VST3 SDK.
class NativeEditor final {
public:
    NativeEditor(EditorClient* controller,NdiAudio& audio);
    ~NativeEditor();
    bool open(HWND parent);
    void close();
    HWND window() const { return window_; }
    int width() const { return 640; }
    int height() const { return 604; }
private:
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    void paint(HDC dc);
    void tick();
    void edit(std::uint32_t id,double value);
    EditorClient* controller_;
    NdiAudio& audio_;
    HWND window_{},power_{},source_{},volume_{},diagnostic_{};
    HFONT normal_{},small_{},title_{},number_{},comboFont_{};
    HDC bufferDc_{};
    HBITMAP bufferBitmap_{},originalBitmap_{};
    int bufferWidth_{},bufferHeight_{};
    float meter_[2]{};
    ULONGLONG lastTick_{},clipUntil_[2]{};
    double scale_{1.};
};
}
