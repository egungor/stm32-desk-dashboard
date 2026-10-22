#include "WinSettingsWindow.h"

#include <WebView2.h>
#include <shlobj.h>
#include <wrl.h>

#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

constexpr const wchar_t* kClassName = L"dashboard-tray-settings";
constexpr int kWidth = 980;   // at 96 dpi
constexpr int kHeight = 700;
constexpr int kMinWidth = 720;
constexpr int kMinHeight = 480;

using Microsoft::WRL::Callback;

// %LOCALAPPDATA%\stm32-desk-dashboard\WebView2 (not next to the program:
// WebView2 writes its cache there).
std::wstring dataFolder()
{
    PWSTR local = nullptr;
    std::wstring folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local != nullptr) {
        folder = std::wstring(local) + L"\\stm32-desk-dashboard\\WebView2";
    }
    CoTaskMemFree(local);
    return folder;
}

bool runtimeInstalled()
{
    LPWSTR version = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    const bool ok = SUCCEEDED(hr) && version != nullptr;
    CoTaskMemFree(version);
    return ok;
}

int scaled(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi), 96);
}

}  // namespace

WinSettingsWindow::WinSettingsWindow(HINSTANCE instance, MessageFn onMessage)
    : instance_(instance), onMessage_(std::move(onMessage))
{
}

WinSettingsWindow::~WinSettingsWindow()
{
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

bool WinSettingsWindow::show(const std::wstring& title, const std::wstring& html, HICON icon)
{
    if (window_ != nullptr) {
        if (IsIconic(window_)) {
            ShowWindow(window_, SW_RESTORE);
        }
        SetForegroundWindow(window_);
        return true;
    }
    if (!runtimeInstalled()) {
        return false;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon = icon;
        wc.hIconSm = icon;
        if (RegisterClassExW(&wc) == 0) {
            return false;
        }
        registered = true;
    }
    const UINT dpi = GetDpiForSystem();
    html_ = html;
    window_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              scaled(kWidth, dpi), scaled(kHeight, dpi), nullptr, nullptr, instance_, this);
    if (window_ == nullptr) {
        return false;
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    createWebView();
    return true;
}

void WinSettingsWindow::setTitle(const std::wstring& title)
{
    if (window_ != nullptr) {
        SetWindowTextW(window_, title.c_str());
    }
}

void WinSettingsWindow::post(const std::string& json)
{
    if (webview_ && pageReady_) {
        (void)webview_->PostWebMessageAsString(toWide(json).c_str());
    }
}

void WinSettingsWindow::createWebView()
{
    const std::wstring folder = dataFolder();
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, folder.empty() ? nullptr : folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || environment == nullptr || window_ == nullptr) {
                    return S_OK;
                }
                return environment->CreateCoreWebView2Controller(
                    window_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(created) || controller == nullptr || window_ == nullptr) {
                                return S_OK;
                            }
                            controller_ = controller;
                            if (FAILED(controller_->get_CoreWebView2(&webview_)) || !webview_) {
                                return S_OK;
                            }
                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webview_->get_Settings(&settings)) && settings) {
                                (void)settings->put_AreDevToolsEnabled(FALSE);
                                (void)settings->put_AreDefaultContextMenusEnabled(FALSE);
                                (void)settings->put_IsStatusBarEnabled(FALSE);
                                (void)settings->put_IsZoomControlEnabled(FALSE);
                            }
                            if (!mapHost_.empty() && !mapFolder_.empty()) {
                                (void)CreateDirectoryW(mapFolder_.c_str(), nullptr);
                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                                if (SUCCEEDED(webview_.As(&webview3)) && webview3) {
                                    (void)webview3->SetVirtualHostNameToFolderMapping(
                                        mapHost_.c_str(), mapFolder_.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                                }
                            }
                            EventRegistrationToken token{};
                            (void)webview_->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR text = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text != nullptr) {
                                            const std::string json = toUtf8(text);
                                            CoTaskMemFree(text);
                                            pageReady_ = true;
                                            if (onMessage_) {
                                                onMessage_(json);
                                            }
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            // The page is the whole program: nothing navigates away
                            // or opens windows.
                            (void)webview_->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                        return args->put_Handled(TRUE);
                                    })
                                    .Get(),
                                &token);
                            resize();
                            (void)webview_->NavigateToString(html_.c_str());
                            return S_OK;
                        })
                        .Get());
            })
            .Get());
    if (FAILED(hr)) {
        DestroyWindow(window_);
    }
}

void WinSettingsWindow::resize()
{
    if (controller_ && window_ != nullptr) {
        RECT bounds{};
        GetClientRect(window_, &bounds);
        (void)controller_->put_Bounds(bounds);
    }
}

void WinSettingsWindow::destroyed()
{
    if (controller_) {
        (void)controller_->Close();
    }
    webview_.Reset();
    controller_.Reset();
    pageReady_ = false;
    window_ = nullptr;
}

LRESULT CALLBACK WinSettingsWindow::windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<WinSettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_SIZE:
        if (self != nullptr) {
            self->resize();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        const UINT dpi = GetDpiForWindow(hwnd);
        info->ptMinTrackSize.x = scaled(kMinWidth, dpi);
        info->ptMinTrackSize.y = scaled(kMinHeight, dpi);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        if (self != nullptr) {
            self->destroyed();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace stm32_audio::host::win
