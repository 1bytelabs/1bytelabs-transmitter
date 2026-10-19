#pragma once
#include "ndi_audio.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <windows.h>

namespace zosma {
// All drawing and host edits happen on the window thread. Audio and network
// threads expose only lock-free atomics to the editor.
class SendEditor final : public Steinberg::CPluginView {
public:
    SendEditor(Steinberg::Vst::EditController* controller, NdiAudio& audio);
    ~SendEditor() override;
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* size) override;
    HWND window() const { return window_; }
private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC dc);
    void tick();
    void edit(Steinberg::Vst::ParamID id, double value);
    void close();
    Steinberg::Vst::EditController* controller_;
    NdiAudio& audio_;
    HWND window_{}, power_{}, source_{};
    HFONT normal_{}, small_{}, title_{}, number_{}, comboFont_{};
    float meter_[2]{};
    ULONGLONG lastTick_{}, clipUntil_[2]{};
    double scale_{1.};
};
}
