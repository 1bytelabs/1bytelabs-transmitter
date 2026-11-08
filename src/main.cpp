#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <cfg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "app_types.h"
#include "capture.h"
#include "ndi_sender.h"
#include "settings.h"
#include "tray_controller.h"

namespace {
using namespace std::chrono_literals;

constexpr wchar_t kWindowClass[] = L"TransmissorNDIPortatilV3";
constexpr wchar_t kPreviewClass[] = L"TransmissorNDIPreviewV3";
constexpr UINT kStatusMessage = WM_APP + 1;
constexpr UINT kWorkerEnded = WM_APP + 2;
constexpr UINT kReceiverMessage = WM_APP + 3;
constexpr UINT_PTR kPreviewTimer = 77;
constexpr auto kPrivacyScanInterval = 200ms;

constexpr COLORREF kBg = RGB(12, 18, 28);
constexpr COLORREF kPanel = RGB(24, 32, 45);
constexpr COLORREF kPanel2 = RGB(17, 24, 35);
constexpr COLORREF kBorder = RGB(46, 58, 76);
constexpr COLORREF kText = RGB(240, 244, 250);
constexpr COLORREF kMuted = RGB(158, 171, 191);

HBRUSH gBgBrush{};
HBRUSH gPanelBrush{};
HBRUSH gPanel2Brush{};
HBRUSH gEditBrush{};
HFONT gFont{};
HFONT gFontSmall{};
HFONT gFontBold{};
HFONT gFontTitle{};

enum ControlId {
    IdSourceName = 1001,
    IdCaptureKind,
    IdCaptureSource,
    IdRefresh,
    IdFps,
    IdCursor,
    IdProtected,
    IdQuick,
    IdAllowWhatsApp,
    IdAllowTelegram,
    IdStart,
    IdRelease,
    IdReceivers,
    IdStatus,
    IdHelp,
    IdPreview,
    IdManualPrivacy
};

enum class PrivateApp { None, WhatsApp, Telegram };

struct UiState {
    HWND window{};
    HWND sourceName{};
    HWND captureKind{};
    HWND captureSource{};
    HWND refresh{};
    HWND fps{};
    HWND cursor{};
    HWND protectedMode{};
    HWND quickMode{};
    HWND allowWhatsApp{};
    HWND allowTelegram{};
    HWND start{};
    HWND release{};
    HWND receivers{};
    HWND status{};
    HWND help{};
    HWND preview{};
    HWND manualPrivacy{};

    std::vector<MonitorSource> monitors;
    std::vector<WindowSource> windows;

    std::atomic_bool running{false};
    std::atomic_bool stopRequested{false};
    std::atomic_bool authorized{false};
    std::atomic_bool additionalConnectionLock{false};
    std::atomic_bool privacyBlocked{false};
    std::atomic_bool allowWa{false};
    std::atomic_bool allowTg{false};
    std::atomic_int rawConnections{0};
    std::atomic_int baselineConnections{0};
    bool activeProtectedMode{true};
    bool closeAfterWorker{false};
    bool closing{false};

    std::thread worker;
    std::mutex workerMutex;
    AppSettings settings;

    std::vector<unsigned char> previewFrame;
    int previewWidth{0};
    int previewHeight{0};
    std::wstring previewLabel;
    bool previewProtected{false};
};

UiState g;

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
}

std::filesystem::path iniPath() { return executableDirectory() / L"transmissor-ndi.ini"; }

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring getText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

bool contains(const std::wstring& text, const wchar_t* needle) {
    return lower(text).find(lower(std::wstring(needle))) != std::wstring::npos;
}

bool isChecked(HWND hwnd) {
    return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void setChecked(HWND hwnd, bool checked) {
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

void postStatus(const std::wstring& text) {
    auto* copy = new std::wstring(text);
    if (!PostMessageW(g.window, kStatusMessage, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

void postConnectionState(int rawCount) {
    PostMessageW(g.window, kReceiverMessage, static_cast<WPARAM>(rawCount), 0);
}

void applyDarkTheme(HWND hwnd) { SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr); }
void setControlFont(HWND hwnd, HFONT font) { SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }

CaptureKind selectedKind() {
    const LRESULT selected = SendMessageW(g.captureKind, CB_GETCURSEL, 0, 0);
    return selected == 1 ? CaptureKind::Window : CaptureKind::Monitor;
}

void refreshSources(int preferredIndex = -1) {
    SendMessageW(g.captureSource, CB_RESETCONTENT, 0, 0);
    const CaptureKind kind = selectedKind();
    if (kind != CaptureKind::Window) {
        g.monitors = enumerateMonitors();
        for (const auto& item : g.monitors) {
            const LRESULT index = SendMessageW(g.captureSource, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(item.label.c_str()));
            if (index >= 0) SendMessageW(g.captureSource, CB_SETITEMDATA, index,
                item.virtualDisplay ? 1 : 0);
        }
    } else {
        g.windows = enumerateWindows(g.window);
        for (const auto& item : g.windows)
            SendMessageW(g.captureSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label.c_str()));
    }
    const LRESULT count = SendMessageW(g.captureSource, CB_GETCOUNT, 0, 0);
    if (count > 0) {
        const int index = preferredIndex >= 0 && preferredIndex < count ? preferredIndex : 0;
        SendMessageW(g.captureSource, CB_SETCURSEL, index, 0);
    }
    g.previewFrame.clear();
    g.previewWidth = g.previewHeight = 0;
    if (g.preview) InvalidateRect(g.preview, nullptr, TRUE);
}

CaptureSource currentSource() {
    CaptureSource source;
    source.kind = selectedKind();
    const int index = static_cast<int>(SendMessageW(g.captureSource, CB_GETCURSEL, 0, 0));
    if (source.kind != CaptureKind::Window && index >= 0 && index < static_cast<int>(g.monitors.size())) {
        const auto& item = g.monitors[index];
        source.monitor = item.handle;
        source.bounds = item.bounds;
        source.label = item.label;
    } else if (source.kind == CaptureKind::Window && index >= 0 && index < static_cast<int>(g.windows.size())) {
        const auto& item = g.windows[index];
        source.window = item.handle;
        source.bounds = item.bounds;
        source.label = item.label;
    }
    return source;
}

std::wstring processName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        result = std::filesystem::path(std::wstring(path, size)).filename().wstring();
    }
    CloseHandle(process);
    return lower(result);
}

PrivateApp classifyPrivateWindow(HWND hwnd) {
    if (!hwnd || hwnd == g.window) return PrivateApp::None;
    const std::wstring proc = processName(hwnd);
    const std::wstring title = lower(getText(hwnd));

    // Aplicativos empacotados e versões Beta/Business podem usar processos
    // hospedeiros (por exemplo, WebView2 ou ApplicationFrameHost). Por isso a
    // proteção não pode depender apenas do nome do executável: o título da
    // janela também é uma identidade válida para qualquer processo.
    if (proc.find(L"whatsapp") != std::wstring::npos ||
        title.find(L"whatsapp") != std::wstring::npos)
        return PrivateApp::WhatsApp;
    if (proc.find(L"telegram") != std::wstring::npos ||
        title.find(L"telegram") != std::wstring::npos)
        return PrivateApp::Telegram;

    // Alguns hosts exibem o título apenas na janela raiz ou proprietária.
    const HWND relatedWindows[] = {GetAncestor(hwnd, GA_ROOT), GetWindow(hwnd, GW_OWNER)};
    for (HWND candidate : relatedWindows) {
        if (!candidate || candidate == hwnd || candidate == g.window) continue;
        const std::wstring candidateProc = processName(candidate);
        const std::wstring candidateTitle = lower(getText(candidate));
        if (candidateProc.find(L"whatsapp") != std::wstring::npos ||
            candidateTitle.find(L"whatsapp") != std::wstring::npos)
            return PrivateApp::WhatsApp;
        if (candidateProc.find(L"telegram") != std::wstring::npos ||
            candidateTitle.find(L"telegram") != std::wstring::npos)
            return PrivateApp::Telegram;
    }
    return PrivateApp::None;
}

bool appAllowed(PrivateApp app) {
    if (app == PrivateApp::WhatsApp) return g.allowWa.load();
    if (app == PrivateApp::Telegram) return g.allowTg.load();
    return true;
}

bool windowActuallyVisibleInBounds(HWND hwnd, const RECT& captureBounds) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) return false;
    RECT inter{};
    if (!IntersectRect(&inter, &wr, &captureBounds)) return false;

    const POINT points[] = {
        {(inter.left + inter.right) / 2, (inter.top + inter.bottom) / 2},
        {inter.left + 3, inter.top + 3},
        {inter.right - 3, inter.top + 3},
        {inter.left + 3, inter.bottom - 3},
        {inter.right - 3, inter.bottom - 3}
    };
    for (const POINT p : points) {
        HWND at = WindowFromPoint(p);
        HWND root = at ? GetAncestor(at, GA_ROOT) : nullptr;
        if (root == hwnd) return true;
    }
    return false;
}

struct PrivacyScanContext {
    RECT bounds{};
    bool blocked{false};
};

BOOL CALLBACK privacyEnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<PrivacyScanContext*>(lp);
    if (!ctx || ctx->blocked || hwnd == g.window) return TRUE;
    const PrivateApp app = classifyPrivateWindow(hwnd);
    if (app == PrivateApp::None || appAllowed(app)) return TRUE;
    if (windowActuallyVisibleInBounds(hwnd, ctx->bounds)) {
        ctx->blocked = true;
        return FALSE;
    }
    return TRUE;
}

bool protectedContentVisible(const CaptureSource& source) {
    if (source.kind == CaptureKind::Window) {
        const PrivateApp app = classifyPrivateWindow(source.window);
        return app != PrivateApp::None && !appAllowed(app);
    }
    PrivacyScanContext ctx{source.bounds, false};
    EnumWindows(privacyEnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.blocked;
}

void collectSettings() {
    g.settings.sourceName = getText(g.sourceName);
    g.settings.captureKind = selectedKind();
    g.settings.sourceIndex = static_cast<int>(SendMessageW(g.captureSource, CB_GETCURSEL, 0, 0));
    g.settings.fps = SendMessageW(g.fps, CB_GETCURSEL, 0, 0) == 1 ? 60 : 30;
    g.settings.showCursor = isChecked(g.cursor);
    g.settings.mode = isChecked(g.quickMode) ? TransmissionMode::Quick : TransmissionMode::Protected;
    saveSettings(iniPath(), g.settings);
}

// Match the hardware ID from the supported Virtual Display Driver INF.
constexpr GUID kDisplayClass{0x4d36e968, 0xe325, 0x11ce,
    {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
constexpr wchar_t kVirtualHardwareId[] = L"ROOT\\MttVDD\0";
struct VirtualDisplayState {
    DEVINST device{};
    bool attached{};
    bool canChange{};
    bool disabled{};
    bool started{};
    unsigned matches{};
    bool enumerated{};
};

bool hasPhysicalPrimary() {
    for (const auto& monitor : enumerateMonitors()) {
        MONITORINFO info{sizeof(info)};
        if (!monitor.virtualDisplay && GetMonitorInfoW(monitor.handle, &info) &&
            (info.dwFlags & MONITORINFOF_PRIMARY)) return true;
    }
    return false;
}

VirtualDisplayState getVirtualDisplayState() {
    VirtualDisplayState result;
    const auto monitors = enumerateMonitors();
    result.attached = std::any_of(monitors.begin(), monitors.end(),
        [](const MonitorSource& monitor) { return monitor.virtualDisplay; });
    const HDEVINFO devices = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return result;

    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA device{sizeof(device)};
        if (!SetupDiEnumDeviceInfo(devices, i, &device)) break;
        wchar_t ids[512]{};
        DWORD type{};
        if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, &type,
                reinterpret_cast<PBYTE>(ids), sizeof(ids) - 2 * sizeof(wchar_t), nullptr) ||
            type != REG_MULTI_SZ) continue;
        for (const wchar_t* id = ids; *id; id += wcslen(id) + 1) {
            if (_wcsicmp(id, L"ROOT\\MttVDD") == 0) {
                result.device = device.DevInst;
                ++result.matches;
                break;
            }
        }
    }
    result.enumerated = GetLastError() == ERROR_NO_MORE_ITEMS;
    SetupDiDestroyDeviceInfoList(devices);
    if (!result.enumerated || result.matches != 1) return result;
    ULONG status{}, problem{};
    if (CM_Get_DevNode_Status(&status, &problem, result.device, 0) != CR_SUCCESS) return result;
    result.disabled = (status & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED;
    result.started = (status & DN_STARTED) != 0;
    result.canChange = hasPhysicalPrimary() && (result.disabled || result.started);
    return result;
}

DWORD installVirtualDriver(HWND owner, bool repair) {
    const auto display = getVirtualDisplayState();
    if (!display.enumerated || display.matches > 1) return ERROR_INVALID_DATA;
    if (display.matches && !repair) return ERROR_SUCCESS;
    if (!hasPhysicalPrimary()) return ERROR_NOT_SUPPORTED;
    const auto inf = executableDirectory() / L"virtual-display" / L"MttVDD.inf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(inf, error)) return ERROR_FILE_NOT_FOUND;

    HDEVINFO devices = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA device{sizeof(device)};
    if (!display.matches) {
        devices = SetupDiCreateDeviceInfoList(&kDisplayClass, owner);
        if (devices == INVALID_HANDLE_VALUE) return GetLastError();
        if (!SetupDiCreateDeviceInfoW(devices, L"Display", &kDisplayClass, nullptr,
                owner, DICD_GENERATE_ID, &device) ||
            !SetupDiSetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID,
                reinterpret_cast<const BYTE*>(kVirtualHardwareId), sizeof(kVirtualHardwareId)) ||
            !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices, &device)) {
            const DWORD result = GetLastError();
            SetupDiDestroyDeviceInfoList(devices);
            return result;
        }
    }

    BOOL restart = FALSE;
    const BOOL installed = UpdateDriverForPlugAndPlayDevicesW(owner, kVirtualHardwareId,
        inf.c_str(), INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &restart);
    const DWORD result = installed ? (restart ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS) :
        GetLastError();
    if (devices != INVALID_HANDLE_VALUE) {
        if (!installed) SetupDiCallClassInstaller(DIF_REMOVE, devices, &device);
        SetupDiDestroyDeviceInfoList(devices);
    }
    return result;
}

DWORD removeVirtualDevice(HWND owner) {
    const auto display = getVirtualDisplayState();
    if (!display.enumerated || display.matches != 1 || !hasPhysicalPrimary())
        return ERROR_NOT_SUPPORTED;
    wchar_t deviceId[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(display.device, deviceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        return ERROR_NOT_FOUND;
    const HDEVINFO devices = SetupDiCreateDeviceInfoList(&kDisplayClass, owner);
    if (devices == INVALID_HANDLE_VALUE) return GetLastError();
    SP_DEVINFO_DATA device{sizeof(device)};
    SP_REMOVEDEVICE_PARAMS remove{};
    remove.ClassInstallHeader.cbSize = sizeof(remove.ClassInstallHeader);
    remove.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    remove.Scope = DI_REMOVEDEVICE_GLOBAL;
    if (!SetupDiOpenDeviceInfoW(devices, deviceId, owner, 0, &device)) {
        const DWORD result = GetLastError();
        SetupDiDestroyDeviceInfoList(devices);
        return result;
    }
    wchar_t infName[MAX_PATH]{};
    const HKEY driverKey = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0,
        DIREG_DRV, KEY_QUERY_VALUE);
    if (driverKey != INVALID_HANDLE_VALUE) {
        DWORD bytes = sizeof(infName);
        if (RegGetValueW(driverKey, nullptr, L"InfPath", RRF_RT_REG_SZ, nullptr, infName, &bytes)
            != ERROR_SUCCESS) infName[0] = L'\0';
        RegCloseKey(driverKey);
    }
    const BOOL removed =
        SetupDiSetClassInstallParamsW(devices, &device, &remove.ClassInstallHeader, sizeof(remove)) &&
        SetupDiCallClassInstaller(DIF_REMOVE, devices, &device);
    DWORD result = removed ? ERROR_SUCCESS : GetLastError();
    SP_DEVINSTALL_PARAMS_W params{sizeof(params)};
    if (removed && SetupDiGetDeviceInstallParamsW(devices, &device, &params) &&
        (params.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))) result = ERROR_SUCCESS_REBOOT_REQUIRED;
    SetupDiDestroyDeviceInfoList(devices);

    // Only remove the OEM INF bound to this exact device. Never force deletion:
    // Windows must preserve the package if another device still uses it.
    const std::wstring_view published(infName);
    if (removed && published.size() > 7 && published.starts_with(L"oem") &&
        published.ends_with(L".inf") &&
        std::all_of(published.begin() + 3, published.end() - 4,
            [](wchar_t digit) { return digit >= L'0' && digit <= L'9'; }) &&
        !SetupUninstallOEMInfW(infName, 0, nullptr) && result == ERROR_SUCCESS)
        result = GetLastError();
    return result;
}

bool extendVirtualDisplay() {
    const auto monitors = enumerateMonitors();
    const MonitorSource* neighbor = nullptr;
    for (const auto& monitor : monitors) {
        if (!monitor.virtualDisplay && (!neighbor || monitor.bounds.right > neighbor->bounds.right))
            neighbor = &monitor;
    }
    if (!neighbor) return false;

    std::wstring adapter;
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
        if (device.StateFlags & (DISPLAY_DEVICE_MIRRORING_DRIVER | DISPLAY_DEVICE_PRIMARY_DEVICE))
            continue;
        if (_wcsicmp(device.DeviceID, L"ROOT\\MttVDD") != 0 &&
            wcscmp(device.DeviceString, L"Virtual Display Driver") != 0) continue;
        if (!adapter.empty()) return false;
        adapter = device.DeviceName;
    }
    if (adapter.empty()) return false;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(adapter.c_str(), ENUM_REGISTRY_SETTINGS, &mode, 0) ||
        !mode.dmPelsWidth || !mode.dmPelsHeight) {
        mode = {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(adapter.c_str(), 0, &mode, 0)) return false;
    }
    mode.dmPosition = {neighbor->bounds.right, neighbor->bounds.top};
    mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    return ChangeDisplaySettingsExW(adapter.c_str(), &mode, nullptr, CDS_UPDATEREGISTRY, nullptr)
        == DISP_CHANGE_SUCCESSFUL;
}

bool setVirtualDisplayAttached(const VirtualDisplayState& display, bool attach) {
    if (!display.canChange) return false;
    if (attach && display.attached) return true;
    if (!attach && !display.attached) return false;
    if (attach && display.disabled && CM_Enable_DevNode(display.device, 0) != CR_SUCCESS)
        return false;
    if (!attach && CM_Disable_DevNode(display.device, CM_DISABLE_UI_NOT_OK) != CR_SUCCESS)
        return false;

    // Starting a PnP device does not necessarily attach it to the desktop.
    bool extensionApplied = false;
    for (int attempt = 0; attempt < 25; ++attempt) {
        const auto current = getVirtualDisplayState();
        if (attach && current.attached) return true;
        if (!attach && current.disabled && !current.attached) return true;
        if (attach && current.canChange && current.started && !extensionApplied)
            extensionApplied = extendVirtualDisplay();
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

void closeTransmitter(HWND hwnd) {
    if (g.closing) return;
    g.closing = true;
    trayShutdown();
    collectSettings();
    if (g.settings.detachVirtualOnExit) {
        const auto display = getVirtualDisplayState();
        if (display.attached) {
            const int index = g.settings.sourceIndex;
            const bool virtualSelected = g.settings.captureKind == CaptureKind::Monitor && index >= 0 &&
                static_cast<size_t>(index) < g.monitors.size() && g.monitors[index].virtualDisplay;
            if (setVirtualDisplayAttached(display, false)) {
                g.settings.restoreVirtualOnOpen = true;
                if (virtualSelected) {
                    g.settings.captureKind = CaptureKind::VirtualMonitor;
                    g.settings.sourceIndex = 0;  // Only one virtual display is managed.
                }
                saveSettings(iniPath(), g.settings);
            } else {
                MessageBoxW(hwnd, L"Não foi possível desativar a tela virtual. "
                    L"Tente novamente em Configurar tela virtual.",
                    L"Tela virtual", MB_OK | MB_ICONINFORMATION);
            }
        }
    }
    DestroyWindow(hwnd);
}

void updateControls() {
    const bool running = g.running.load();
    const bool stopping = g.stopRequested.load();

    if (stopping) {
        EnableWindow(g.start, FALSE);
        SetWindowTextW(g.start, L"Parando...");
    } else {
        EnableWindow(g.start, TRUE);
        SetWindowTextW(g.start, running ? L"Parar transmissão" : L"Iniciar transmissão");
    }

    EnableWindow(g.sourceName, !running);
    EnableWindow(g.captureKind, !running);
    EnableWindow(g.captureSource, !running);
    EnableWindow(g.refresh, !running);
    EnableWindow(GetDlgItem(g.window, 1400), !running);
    EnableWindow(g.fps, !running);
    EnableWindow(g.cursor, !running);
    EnableWindow(g.protectedMode, !running);
    EnableWindow(g.quickMode, !running);
    EnableWindow(g.manualPrivacy, running && !stopping);
    if (g.manualPrivacy)
        SetWindowTextW(g.manualPrivacy, trayImageHidden() ? L"Mostrar imagem" : L"Ocultar imagem");

    const int raw = g.rawConnections.load();
    const int baseline = g.baselineConnections.load();
    const bool extraLock = g.additionalConnectionLock.load();
    const bool groupIsSafe = raw > 0 && (baseline == 0 || raw <= baseline);
    const bool canRelease = running && !stopping && g.activeProtectedMode && !g.authorized.load() && groupIsSafe;
    EnableWindow(g.release, canRelease);

    if (g.release) {
        if (g.authorized.load()) SetWindowTextW(g.release, L"Transmissão liberada ✓");
        else SetWindowTextW(g.release, extraLock ? L"Liberar novamente" : L"Liberar transmissão");
    }
}

std::vector<unsigned char> createMessageFrame(int width, int height,
                                               const std::wstring& title,
                                               const std::wstring& body,
                                               const std::wstring& sourceName) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* dibPixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
    if (!bitmap || !dibPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        if (screen) ReleaseDC(nullptr, screen);
        return pixels;
    }

    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    RECT full{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(RGB(15, 22, 33));
    FillRect(dc, &full, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);

    LOGFONTW lf{};
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -38;
    lf.lfWeight = FW_SEMIBOLD;
    HFONT titleFont = CreateFontIndirectW(&lf);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    SetTextColor(dc, RGB(245, 246, 248));
    RECT r{40, height / 2 - 100, width - 40, height / 2 - 20};
    DrawTextW(dc, title.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    lf.lfHeight = -22;
    lf.lfWeight = FW_NORMAL;
    HFONT bodyFont = CreateFontIndirectW(&lf);
    SelectObject(dc, bodyFont);
    SetTextColor(dc, RGB(185, 196, 212));
    r = {40, height / 2 - 20, width - 40, height / 2 + 60};
    DrawTextW(dc, body.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    lf.lfHeight = -16;
    HFONT sourceFont = CreateFontIndirectW(&lf);
    SelectObject(dc, sourceFont);
    SetTextColor(dc, RGB(120, 136, 158));
    r = {40, height - 70, width - 40, height - 25};
    DrawTextW(dc, sourceName.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::memcpy(pixels.data(), dibPixels, pixels.size());
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    DeleteObject(sourceFont);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return pixels;
}

void updatePreview() {
    if (!g.preview || !IsWindowVisible(g.preview) || IsIconic(g.preview) || IsIconic(g.window)) return;
    CaptureSource source = currentSource();
    if (source.bounds.right <= source.bounds.left || source.bounds.bottom <= source.bounds.top) {
        g.previewFrame.clear();
        g.previewWidth = g.previewHeight = 0;
        InvalidateRect(g.preview, nullptr, FALSE);
        return;
    }

    const bool blocked = trayImageHidden() || protectedContentVisible(source);
    g.previewProtected = blocked;
    if (blocked) {
        const bool manual = trayImageHidden();
        g.previewFrame = createMessageFrame(1280, 720,
            manual ? L"Imagem ocultada" : L"Conteúdo protegido",
            manual ? L"A privacidade manual está ativa." : L"Um aplicativo privado está aberto no monitor compartilhado.",
            source.label);
        g.previewWidth = 1280;
        g.previewHeight = 720;
        g.previewLabel = manual ? L"Privacidade manual" : L"Conteúdo protegido";
        InvalidateRect(g.preview, nullptr, FALSE);
        return;
    }

    std::vector<unsigned char> frame;
    int width = 0;
    int height = 0;
    if (captureToBuffer(source, isChecked(g.cursor), frame, width, height)) {
        g.previewFrame = std::move(frame);
        g.previewWidth = width;
        g.previewHeight = height;
        g.previewLabel = source.label;
        InvalidateRect(g.preview, nullptr, FALSE);
    } else {
        g.previewFrame.clear();
        g.previewWidth = g.previewHeight = 0;
        InvalidateRect(g.preview, nullptr, FALSE);
    }
}

void drawPreview(HDC dc, const RECT& client) {
    HBRUSH bg = CreateSolidBrush(RGB(8, 13, 21));
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    if (g.previewFrame.empty() || g.previewWidth <= 0 || g.previewHeight <= 0) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMuted);
        HFONT old = static_cast<HFONT>(SelectObject(dc, gFont));
        RECT text = client;
        DrawTextW(dc, L"Prévia indisponível", -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        return;
    }

    const int cw = client.right - client.left;
    const int ch = client.bottom - client.top;
    const double scale = std::min(static_cast<double>(cw) / g.previewWidth,
                                  static_cast<double>(ch) / g.previewHeight);
    const int dw = static_cast<int>(g.previewWidth * scale);
    const int dh = static_cast<int>(g.previewHeight * scale);
    const int dx = (cw - dw) / 2;
    const int dy = (ch - dh) / 2;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = g.previewWidth;
    info.bmiHeader.biHeight = -g.previewHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, g.previewWidth, g.previewHeight,
                  g.previewFrame.data(), &info, DIB_RGB_COLORS, SRCCOPY);

    RECT label{12, client.bottom - 38, client.right - 12, client.bottom - 10};
    HBRUSH overlay = CreateSolidBrush(RGB(16, 23, 34));
    FillRect(dc, &label, overlay);
    DeleteObject(overlay);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    HFONT old = static_cast<HFONT>(SelectObject(dc, gFontSmall));
    std::wstring text = g.previewLabel;
    if (!g.previewProtected && !text.empty())
        text += L"  ·  " + std::to_wstring(g.previewWidth) + L" × " + std::to_wstring(g.previewHeight);
    DrawTextW(dc, text.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

void transmissionWorker(CaptureSource source, std::wstring sourceDisplayName,
                        std::string sourceName, int fps, bool showCursor,
                        TransmissionMode mode) {
    NdiSender sender;
    std::wstring error;
    if (!sender.open(executableDirectory() / L"Processing.NDI.Lib.x64.dll", sourceName, error)) {
        postStatus(L"Erro: " + error);
        g.running = false;
        PostMessageW(g.window, kWorkerEnded, 0, 0);
        return;
    }

    constexpr int placeholderWidth = 1280;
    constexpr int placeholderHeight = 720;
    const auto waitingFrame = createMessageFrame(placeholderWidth, placeholderHeight,
        L"Transmissão protegida", L"Aguardando autorização no computador transmissor.", sourceDisplayName);
    const auto extraConnectionFrame = createMessageFrame(placeholderWidth, placeholderHeight,
        L"Transmissão temporariamente bloqueada", L"Foi detectada uma conexão adicional.", sourceDisplayName);
    const auto privacyFrame = createMessageFrame(placeholderWidth, placeholderHeight,
        L"Conteúdo protegido", L"Um aplicativo privado está aberto no monitor compartilhado.", sourceDisplayName);

    g.authorized = mode == TransmissionMode::Quick;
    g.additionalConnectionLock = false;
    g.privacyBlocked = false;
    g.rawConnections = 0;
    g.baselineConnections = 0;
    postConnectionState(0);
    postStatus(mode == TransmissionMode::Protected ? L"Aguardando receptor" : L"Transmitindo");

    std::vector<unsigned char> frame;
    int width = 0;
    int height = 0;
    int previousConnections = -1;
    bool privacy = trayImageHidden() || protectedContentVisible(source);
    bool previousPrivacy = privacy;
    g.privacyBlocked = privacy;
    auto nextPrivacyScan = std::chrono::steady_clock::now() + kPrivacyScanInterval;
    auto nextFrame = std::chrono::steady_clock::now();

    while (!g.stopRequested.load()) {
        const int connections = std::max(0, sender.connections());
        if (connections != previousConnections) {
            previousConnections = connections;
            g.rawConnections = connections;
            postConnectionState(connections);
            if (mode == TransmissionMode::Protected) {
                const int baseline = g.baselineConnections.load();
                if (connections == 0) {
                    g.authorized = false;
                    g.additionalConnectionLock = false;
                    g.baselineConnections = 0;
                    postStatus(L"Aguardando receptor");
                } else if (baseline > 0 && connections > baseline) {
                    g.authorized = false;
                    g.additionalConnectionLock = true;
                    postStatus(L"Bloqueada · conexão adicional");
                } else if (!g.authorized.load()) {
                    postStatus(g.additionalConnectionLock.load() ? L"Aguardando nova liberação" : L"Receptor aguardando liberação");
                }
            } else {
                postStatus(connections > 0 ? L"Transmitindo" : L"Ativa · sem receptor");
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextPrivacyScan) {
            privacy = trayImageHidden() || protectedContentVisible(source);
            nextPrivacyScan = now + kPrivacyScanInterval;
        }
        g.privacyBlocked = privacy;
        if (privacy != previousPrivacy) {
            previousPrivacy = privacy;
            if (privacy) postStatus(trayImageHidden() ? L"Privacidade manual · imagem e áudio ocultos" : L"Privacidade · conteúdo protegido");
            else if (mode == TransmissionMode::Protected && !g.authorized.load()) postStatus(L"Receptor aguardando liberação");
            else postStatus(L"Transmitindo");
            InvalidateRect(g.window, nullptr, FALSE);
        }

        if (privacy) {
            if (!sender.sendFrame(privacyFrame.data(), placeholderWidth, placeholderHeight, fps)) break;
        } else if (mode == TransmissionMode::Protected && !g.authorized.load()) {
            const auto& protectedFrame = g.additionalConnectionLock.load() ? extraConnectionFrame : waitingFrame;
            if (!sender.sendFrame(protectedFrame.data(), placeholderWidth, placeholderHeight, fps)) break;
        } else {
            if (!captureToBuffer(source, showCursor, frame, width, height)) {
                postStatus(L"Falha de captura");
                break;
            }
            if (!sender.sendFrame(frame.data(), width, height, fps)) {
                postStatus(L"Falha no envio NDI");
                break;
            }
        }

        nextFrame += std::chrono::microseconds(1000000 / fps);
        std::this_thread::sleep_until(nextFrame);
        if (std::chrono::steady_clock::now() - nextFrame > 1s) nextFrame = std::chrono::steady_clock::now();
    }

    sender.close();
    g.running = false;
    g.stopRequested = false;
    g.authorized = false;
    g.additionalConnectionLock = false;
    g.privacyBlocked = false;
    g.rawConnections = 0;
    g.baselineConnections = 0;
    postConnectionState(0);
    postStatus(L"Pronto para transmitir");
    PostMessageW(g.window, kWorkerEnded, 0, 0);
}

void startTransmission() {
    if (g.running.load() || g.stopRequested.load()) return;
    const std::wstring sourceName = getText(g.sourceName);
    if (sourceName.empty()) {
        MessageBoxW(g.window, L"Informe um nome para a fonte NDI.", L"Transmissor NDI", MB_OK | MB_ICONWARNING);
        return;
    }
    CaptureSource source = currentSource();
    if (source.bounds.right <= source.bounds.left || source.bounds.bottom <= source.bounds.top) {
        MessageBoxW(g.window, L"Selecione uma fonte de captura válida.", L"Transmissor NDI", MB_OK | MB_ICONWARNING);
        return;
    }

    collectSettings();
    const int fps = g.settings.fps;
    const bool showCursor = g.settings.showCursor;
    const TransmissionMode mode = g.settings.mode;
    g.activeProtectedMode = mode == TransmissionMode::Protected;
    traySetImageHidden(false);
    g.authorized = mode == TransmissionMode::Quick;
    g.additionalConnectionLock = false;
    g.privacyBlocked = false;
    g.rawConnections = 0;
    g.baselineConnections = 0;
    g.stopRequested = false;
    g.running = true;
    g.closeAfterWorker = false;
    updateControls();
    InvalidateRect(g.window, nullptr, FALSE);

    std::lock_guard<std::mutex> lock(g.workerMutex);
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread(transmissionWorker, source, sourceName, utf8(sourceName), fps, showCursor, mode);
}

void releaseTransmission() {
    if (!g.running.load() || g.stopRequested.load() || !g.activeProtectedMode) return;
    const int raw = g.rawConnections.load();
    const int baseline = g.baselineConnections.load();
    if (raw <= 0 || (baseline > 0 && raw > baseline)) return;
    if (baseline == 0) g.baselineConnections = raw;
    g.additionalConnectionLock = false;
    g.authorized = true;
    postStatus(g.privacyBlocked.load() ? L"Liberada · privacidade ativa" : L"Transmissão liberada");
    updateControls();
    InvalidateRect(g.window, nullptr, FALSE);
}

void stopTransmission() {
    if (!g.running.load() || g.stopRequested.load()) return;
    g.stopRequested = true;
    postStatus(L"Parando transmissão...");
    updateControls();
    InvalidateRect(g.window, nullptr, FALSE);
}

void showHelp() {
    MessageBoxW(g.window,
        L"1. Escolha Monitor ou Janela e confirme a fonte na prévia.\n"
        L"2. Desmarque Enviar áudio se quiser transmitir somente a imagem.\n"
        L"3. No Modo protegido, libere o receptor quando ele for detectado.\n"
        L"4. Um receptor NDI pode abrir mais de uma conexão técnica; o app memoriza a linha de base.\n"
        L"5. Se as conexões subirem acima da linha de base, a imagem é bloqueada.\n"
        L"6. Todas as versões do WhatsApp e do Telegram são ocultadas automaticamente.\n"
        L"7. Marque a permissão correspondente para exibir um deles temporariamente.\n"
        L"8. Use Ocultar imagem para bloquear manualmente a imagem e o áudio sem desconectar a fonte.\n\n"
        L"O botão principal alterna entre Iniciar transmissão e Parar transmissão.",
        L"Como usar — V3", MB_OK | MB_ICONINFORMATION);
}

HWND addControl(const wchar_t* cls, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id, HFONT font = nullptr) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (font) setControlFont(hwnd, font);
    applyDarkTheme(hwnd);
    return hwnd;
}

void drawPanel(HDC dc, RECT r) {
    HBRUSH brush = CreateSolidBrush(kPanel);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawSectionTitle(HDC dc, int x, int y, const wchar_t* text) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    HFONT old = static_cast<HFONT>(SelectObject(dc, gFontBold));
    RECT r{x, y, x + 330, y + 26};
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

void drawSmallText(HDC dc, int x, int y, const wchar_t* text, int width = 300) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kMuted);
    HFONT old = static_cast<HFONT>(SelectObject(dc, gFontSmall));
    RECT r{x, y, x + width, y + 22};
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

void createUi() {
    addControl(L"BUTTON", L"Configurar tela virtual", BS_PUSHBUTTON,
        36, 308, 252, 36, 1400, gFont);
    addControl(L"BUTTON", L"Ver prévia", BS_PUSHBUTTON, 302, 308, 192, 36, 1401, gFont);
    g.help = addControl(L"BUTTON", L"?  Como usar", BS_PUSHBUTTON, 806, 20, 116, 38, IdHelp, gFont);
    g.start = addControl(L"BUTTON", L"Iniciar transmissão", BS_DEFPUSHBUTTON, 476, 18, 190, 42, IdStart, gFontBold);
    g.sourceName = addControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 36, 204, 458, 34, IdSourceName, gFont);
    g.captureKind = addControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 36, 264, 128, 220, IdCaptureKind, gFont);
    SendMessageW(g.captureKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Monitor"));
    SendMessageW(g.captureKind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Janela"));
    g.captureSource = addControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 176, 264, 270, 260, IdCaptureSource, gFont);
    g.refresh = addControl(L"BUTTON", L"↻", BS_PUSHBUTTON, 456, 264, 38, 34, IdRefresh, gFontBold);
    g.fps = addControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 36, 366, 106, 120, IdFps, gFont);
    SendMessageW(g.fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 FPS"));
    SendMessageW(g.fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60 FPS"));
    g.cursor = addControl(L"BUTTON", L"Mostrar cursor", BS_AUTOCHECKBOX, 154, 368, 144, 28, IdCursor, gFont);
    g.protectedMode = addControl(L"BUTTON", L"Modo protegido",
        BS_AUTORADIOBUTTON | WS_GROUP, 540, 184, 174, 38, IdProtected, gFont);
    g.quickMode = addControl(L"BUTTON", L"Modo rápido",
        BS_AUTORADIOBUTTON, 726, 184, 180, 38, IdQuick, gFont);
    g.allowWhatsApp = addControl(L"BUTTON", L"Permitir WhatsApp", BS_AUTOCHECKBOX,
        540, 451, 179, 30, IdAllowWhatsApp, gFont);
    g.allowTelegram = addControl(L"BUTTON", L"Permitir Telegram", BS_AUTOCHECKBOX,
        726, 451, 180, 30, IdAllowTelegram, gFont);

    g.release = addControl(L"BUTTON", L"Liberar transmissão", BS_PUSHBUTTON, 540, 324, 366, 38, IdRelease, gFontBold);
    g.manualPrivacy = addControl(L"BUTTON", L"Ocultar imagem", BS_PUSHBUTTON, 540, 371, 366, 38, IdManualPrivacy, gFontBold);
    g.receivers = addControl(L"STATIC", L"Nenhum receptor", SS_LEFT | SS_ENDELLIPSIS, 316, 99, 193, 22, IdReceivers, gFontBold);
    g.status = addControl(L"STATIC", L"Pronto para transmitir", SS_LEFT | SS_ENDELLIPSIS, 36, 99, 262, 22, IdStatus, gFontBold);
}

LRESULT CALLBACK previewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g.preview = nullptr;
        g.previewFrame.clear();
        g.previewWidth = g.previewHeight = 0;
        return 0;
    }
    if (msg == WM_SIZE) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, rc.right, rc.bottom);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        drawPreview(dc, rc);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.window = hwnd;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        g.settings = loadSettings(iniPath());
        // Also recover installations whose previous build cleared the restore flag
        // when PnP started, before the virtual monitor was added to the desktop.
        bool virtualRestoreFailed = false;
        if (g.settings.restoreVirtualOnOpen || g.settings.detachVirtualOnExit) {
            const auto display = getVirtualDisplayState();
            if (!display.attached && display.canChange) {
                virtualRestoreFailed = !setVirtualDisplayAttached(display, true);
                g.settings.restoreVirtualOnOpen = virtualRestoreFailed;
                saveSettings(iniPath(), g.settings);
            } else if (display.attached && g.settings.restoreVirtualOnOpen) {
                g.settings.restoreVirtualOnOpen = false;
                saveSettings(iniPath(), g.settings);
            }
        }
        bool missingSavedVirtual = false;
        // Migrate the old filtered virtual-monitor index to the unified monitor list.
        if (g.settings.captureKind == CaptureKind::VirtualMonitor) {
            const auto monitors = enumerateMonitors();
            int virtualIndex = 0;
            int selectedIndex = -1;
            for (size_t i = 0; i < monitors.size(); ++i) {
                if (monitors[i].virtualDisplay && virtualIndex++ == g.settings.sourceIndex) {
                    selectedIndex = static_cast<int>(i);
                    break;
                }
            }
            g.settings.captureKind = CaptureKind::Monitor;
            // Leave the source unselected if the saved virtual display is unavailable.
            g.settings.sourceIndex = selectedIndex;
            missingSavedVirtual = selectedIndex < 0;
        }
        createUi();
        SetWindowTextW(g.sourceName, g.settings.sourceName.c_str());
        SendMessageW(g.captureKind, CB_SETCURSEL, static_cast<int>(g.settings.captureKind), 0);
        refreshSources(g.settings.sourceIndex);
        if (missingSavedVirtual) SendMessageW(g.captureSource, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        SendMessageW(g.fps, CB_SETCURSEL, g.settings.fps == 60 ? 1 : 0, 0);
        setChecked(g.cursor, g.settings.showCursor);
        setChecked(g.settings.mode == TransmissionMode::Quick ? g.quickMode : g.protectedMode, true);
        setChecked(g.allowWhatsApp, false);
        setChecked(g.allowTelegram, false);
        g.allowWa = false;
        g.allowTg = false;
        updateControls();
        SetTimer(hwnd, kPreviewTimer, 250, nullptr);
        if (virtualRestoreFailed) postStatus(L"Tela virtual inativa · abra Configurar tela virtual");
        if (g.settings.showHelpOnStart) PostMessageW(hwnd, WM_COMMAND, IdHelp, 0);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        // Active capture validates monitor geometry and stops on topology changes.
        if (g.captureSource && !g.running.load()) refreshSources();
        return 0;

    case WM_TIMER:
        if (wp == kPreviewTimer) {
            updatePreview();
            if (g.manualPrivacy) {
                SetWindowTextW(g.manualPrivacy, trayImageHidden() ? L"Mostrar imagem" : L"Ocultar imagem");
                InvalidateRect(g.manualPrivacy, nullptr, FALSE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        // Todo o fundo é pintado no WM_PAINT. Impedir a limpeza anterior evita
        // o clarão/piscar entre a pintura do painel e a dos controles filhos.
        return 1;

    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == IdCaptureKind && HIWORD(wp) == CBN_SELCHANGE) refreshSources();
        else if (id == IdCaptureSource && HIWORD(wp) == CBN_SELCHANGE) updatePreview();
        else if (id == 1401) {
            if (!g.preview) {
                g.preview = CreateWindowExW(WS_EX_TOOLWINDOW, kPreviewClass, L"Prévia da transmissão",
                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 400,
                    hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (g.preview) {
                    BOOL dark = TRUE;
                    DwmSetWindowAttribute(g.preview, 20, &dark, sizeof(dark));
                }
            }
            if (g.preview) {
                ShowWindow(g.preview, SW_RESTORE);
                SetForegroundWindow(g.preview);
                updatePreview();
            }
        }
        else if (id == 1400 && !g.running.load()) {
            const auto display = getVirtualDisplayState();
            std::vector<TASKDIALOG_BUTTON> buttons;
            if (display.canChange)
                buttons.push_back({2001, display.attached ?
                    L"Desativar tela virtual\nRetire a tela da área de trabalho sem remover o driver." :
                    L"Ativar tela virtual\nDisponibilize um monitor extra para apresentar ou transmitir."});
            if (display.enumerated && display.matches == 0)
                buttons.push_back({2002, L"Instalar driver\nAdicione a tela virtual ao Windows."});
            if (display.enumerated && display.matches == 1) {
                buttons.push_back({2003, L"Reparar driver\nReinstale o driver se a tela não aparecer ou não ativar."});
                buttons.push_back({2004, L"Desinstalar driver\nRemova a tela virtual do Windows."});
            }

            TASKDIALOGCONFIG dialog{};
            dialog.cbSize = sizeof(dialog);
            dialog.hwndParent = hwnd;
            dialog.hInstance = GetModuleHandleW(nullptr);
            dialog.pszMainIcon = MAKEINTRESOURCEW(1);
            dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS |
                TDF_POSITION_RELATIVE_TO_WINDOW |
                (g.settings.detachVirtualOnExit ? TDF_VERIFICATION_FLAG_CHECKED : 0);
            dialog.dwCommonButtons = TDCBF_CLOSE_BUTTON;
            dialog.pszWindowTitle = L"1Byte Labs Transmitter · Tela virtual";
            dialog.pszMainInstruction = display.attached ? L"Tela virtual ativa" :
                display.matches ? L"Tela virtual inativa" : L"Configure sua tela virtual";
            dialog.pszContent = display.attached ?
                L"O monitor extra está disponível na lista de fontes do Transmitter e em outros aplicativos." :
                L"Use um monitor extra para enviar slides e manter suas anotações na tela principal.";
            dialog.pszFooter = L"Driver de vídeo: VirtualDrivers · Licença MIT";
            dialog.cButtons = static_cast<UINT>(buttons.size());
            dialog.pButtons = buttons.empty() ? nullptr : buttons.data();
            dialog.nDefaultButton = IDCLOSE;
            dialog.pszVerificationText = L"Desativar tela virtual ao sair";
            dialog.cxWidth = 375;
            int choice = IDCLOSE;
            BOOL checked = g.settings.detachVirtualOnExit;
            if (FAILED(TaskDialogIndirect(&dialog, &choice, nullptr, &checked))) return 0;
            g.settings.detachVirtualOnExit = checked != FALSE;
            collectSettings();

            if (choice == 2001) {
                if (setVirtualDisplayAttached(getVirtualDisplayState(), !display.attached)) {
                    g.settings.restoreVirtualOnOpen = display.attached;
                    refreshSources();
                    collectSettings();
                } else {
                    MessageBoxW(hwnd, L"O Windows não confirmou a alteração. "
                        L"Se a tela continuar inativa, use Reparar driver neste painel.",
                        L"Tela virtual", MB_OK | MB_ICONINFORMATION);
                }
            } else if (choice >= 2002 && choice <= 2004) {
                const bool removing = choice == 2004;
                if (removing && MessageBoxW(hwnd,
                    L"Remover a tela virtual? Ela também deixará de aparecer para outros aplicativos.",
                    L"Desinstalar driver", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return 0;
                const auto cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                const DWORD result = removing ? removeVirtualDevice(hwnd) : installVirtualDriver(hwnd, choice == 2003);
                bool active = false;
                if (result == ERROR_SUCCESS && !removing) {
                    for (int attempt = 0; attempt < 20; ++attempt) {
                        const auto updated = getVirtualDisplayState();
                        if (updated.attached || (updated.canChange && setVirtualDisplayAttached(updated, true))) {
                            active = true;
                            break;
                        }
                        if (updated.canChange) break;
                        std::this_thread::sleep_for(100ms);
                    }
                }
                SetCursor(cursor);
                if (result == ERROR_SUCCESS || result == ERROR_SUCCESS_REBOOT_REQUIRED) {
                    g.settings.restoreVirtualOnOpen = !removing && !active;
                    refreshSources();
                    if (active && selectedKind() == CaptureKind::Monitor) {
                        const auto monitor = std::find_if(g.monitors.begin(), g.monitors.end(),
                            [](const MonitorSource& source) { return source.virtualDisplay; });
                        if (monitor != g.monitors.end())
                            SendMessageW(g.captureSource, CB_SETCURSEL, monitor - g.monitors.begin(), 0);
                    }
                    collectSettings();
                    MessageBoxW(hwnd,
                        result == ERROR_SUCCESS_REBOOT_REQUIRED ? L"Reinicie o Windows para concluir a alteração." :
                        removing ? L"Tela virtual removida." :
                        active ? L"Tela virtual pronta para usar." :
                        L"Driver instalado. O Windows ainda não disponibilizou a tela virtual. "
                        L"Você pode tentar Ativar tela virtual neste painel.",
                        L"Tela virtual", MB_OK | MB_ICONINFORMATION);
                } else {
                    const auto message = std::wstring(L"O Windows não concluiu a operação. Código: ") +
                        std::to_wstring(result) + L".";
                    MessageBoxW(hwnd, message.c_str(), L"Tela virtual", MB_OK | MB_ICONWARNING);
                }
            }
        }
        else if (id == IdRefresh) refreshSources();
        else if (id == IdStart) {
            if (g.running.load()) stopTransmission(); else startTransmission();
        } else if (id == IdRelease) releaseTransmission();
        else if (id == IdManualPrivacy && g.running.load()) {
            const bool hidden = !trayImageHidden();
            traySetImageHidden(hidden);
            g.privacyBlocked = hidden;
            postStatus(hidden ? L"Privacidade manual · imagem e áudio ocultos" : L"Privacidade manual desativada");
            updatePreview();
            updateControls();
        }
        else if (id == IdAllowWhatsApp) {
            g.allowWa = isChecked(g.allowWhatsApp);
            postStatus(g.allowWa.load() ? L"WhatsApp permitido nesta execução" : L"Proteção do WhatsApp ativada");
            updatePreview();
        } else if (id == IdAllowTelegram) {
            g.allowTg = isChecked(g.allowTelegram);
            postStatus(g.allowTg.load() ? L"Telegram permitido nesta execução" : L"Proteção do Telegram ativada");
            updatePreview();
        } else if (id == IdHelp) showHelp();
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return reinterpret_cast<INT_PTR>(gPanelBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel2);
        return reinterpret_cast<INT_PTR>(gEditBrush);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        FillRect(dc, &client, gBgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        HFONT old = static_cast<HFONT>(SelectObject(dc, gFontTitle));
        HICON appIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (appIcon) DrawIconEx(dc, 28, 17, appIcon, 42, 42, 0, nullptr, DI_NORMAL);
        RECT title{82, 16, 450, 46};
        DrawTextW(dc, L"1Byte Labs Transmitter", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, gFontSmall);
        SetTextColor(dc, kMuted);
        RECT sub{82, 46, 450, 67};
        DrawTextW(dc, L"Envio NDI", -1, &sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        drawPanel(dc, {20, 78, 924, 128});
        drawSmallText(dc, 36, 79, L"Transmissão");
        drawSmallText(dc, 316, 79, L"Receptor", 176);
        drawSmallText(dc, 544, 79, L"Envio", 150);
        drawSmallText(dc, 728, 79, L"Desempenho", 180);

        drawPanel(dc, {20, 142, 510, 500});
        drawPanel(dc, {522, 142, 924, 500});
        drawSectionTitle(dc, 36, 152, L"Fonte de transmissão");
        drawSmallText(dc, 36, 181, L"Nome da fonte NDI");
        drawSmallText(dc, 36, 241, L"Tipo", 120);
        drawSmallText(dc, 176, 241, L"Fonte selecionada", 270);
        drawSectionTitle(dc, 540, 152, L"Acesso e privacidade");

        SelectObject(dc, gFontSmall);
        SetTextColor(dc, kMuted);
        RECT access{540, 283, 906, 321};
        const wchar_t* accessText = isChecked(g.quickMode)
            ? L"A imagem é enviada sem autorização prévia."
            : g.additionalConnectionLock.load()
                ? L"Conexão adicional detectada. Envio bloqueado."
                : g.authorized.load()
                    ? L"Receptor autorizado. Transmissão liberada."
                    : L"Autorize o receptor antes de liberar a imagem.";
        DrawTextW(dc, accessText, -1, &access, DT_LEFT | DT_WORDBREAK);
        SetTextColor(dc, g.privacyBlocked.load() || trayImageHidden() ? RGB(255, 204, 144) : RGB(144, 226, 192));
        SelectObject(dc, gFontBold);
        RECT privacy{540, 418, 906, 446};
        DrawTextW(dc, trayImageHidden() ? L"Imagem ocultada" :
            g.privacyBlocked.load() ? L"Conteúdo privado bloqueado" : L"Privacidade ativa",
            -1, &privacy, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        drawPanel(dc, {20, 511, 924, 549});
        SelectObject(dc, old);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case kStatusMessage: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
        SetWindowTextW(g.status, text->c_str());
        updateControls();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case kReceiverMessage: {
        const int raw = static_cast<int>(wp);
        if (raw == 0) SetWindowTextW(g.receivers, L"Nenhum receptor");
        else if (g.additionalConnectionLock.load()) SetWindowTextW(g.receivers, L"Conexão adicional");
        else if (g.authorized.load()) SetWindowTextW(g.receivers, L"Receptor autorizado");
        else SetWindowTextW(g.receivers, L"Receptor detectado");
        if (g.activeProtectedMode && g.additionalConnectionLock.load()) {
            FLASHWINFO flash{sizeof(flash), hwnd, FLASHW_TRAY, 1, 0};
            FlashWindowEx(&flash);
        }
        updateControls();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case kWorkerEnded: {
        if (g.worker.joinable()) {
            std::lock_guard<std::mutex> lock(g.workerMutex);
            if (g.worker.joinable()) g.worker.join();
        }
        updateControls();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (g.closeAfterWorker) {
            g.closeAfterWorker = false;
            closeTransmitter(hwnd);
        }
        return 0;
    }

    case WM_CLOSE:
        if (g.closing || g.closeAfterWorker) return 0;
        if (g.running.load()) {
            if (MessageBoxW(hwnd, L"Há uma transmissão ativa. Deseja parar e sair?", L"Transmissão ativa", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            g.closeAfterWorker = true;
            stopTransmission();
            return 0;
        }
        closeTransmitter(hwnd);
        return 0;

    case WM_DESTROY:
        trayShutdown();
        KillTimer(hwnd, kPreviewTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR arguments, int show) {
    if (wcscmp(arguments, L"--install-virtual-driver") == 0)
        return static_cast<int>(installVirtualDriver(nullptr, false));
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    gBgBrush = CreateSolidBrush(kBg);
    gPanelBrush = CreateSolidBrush(kPanel);
    gPanel2Brush = CreateSolidBrush(kPanel2);
    gEditBrush = CreateSolidBrush(kPanel2);

    LOGFONTW lf{};
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -16; gFont = CreateFontIndirectW(&lf);
    lf.lfHeight = -14; gFontSmall = CreateFontIndirectW(&lf);
    lf.lfHeight = -16; lf.lfWeight = FW_SEMIBOLD; gFontBold = CreateFontIndirectW(&lf);
    lf.lfHeight = -23; gFontTitle = CreateFontIndirectW(&lf);

    WNDCLASSEXW previewClass{sizeof(previewClass)};
    previewClass.lpfnWndProc = previewProc;
    previewClass.hInstance = instance;
    previewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    previewClass.hbrBackground = gPanel2Brush;
    previewClass.lpszClassName = kPreviewClass;
    if (!RegisterClassExW(&previewClass)) return 1;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = gBgBrush;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"1Byte Labs Transmitter — Envio NDI",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 594, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.worker.joinable()) {
        g.stopRequested = true;
        g.worker.join();
    }
    DeleteObject(gFont);
    DeleteObject(gFontSmall);
    DeleteObject(gFontBold);
    DeleteObject(gFontTitle);
    DeleteObject(gBgBrush);
    DeleteObject(gPanelBrush);
    DeleteObject(gPanel2Brush);
    DeleteObject(gEditBrush);
    return static_cast<int>(msg.wParam);
}
