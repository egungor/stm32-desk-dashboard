// The settings window of the tray program (ADR-011): a top-level window
// with a WebView2 control that shows settings.html (a resource of
// dashboard-tray). The page and the program talk in JSON strings
// (SettingsModel.h): the page posts requests, post() sends it the state.
//
// Runs on the tray's UI thread (single-threaded apartment, message loop).
// Needs the Microsoft Edge WebView2 Runtime (part of Windows 11; show()
// returns false without it). WebView2's data folder:
// %LOCALAPPDATA%\stm32-desk-dashboard\WebView2.
#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <functional>
#include <string>

struct ICoreWebView2;
struct ICoreWebView2Controller;

namespace stm32_audio::host::win {

class WinSettingsWindow {
public:
    using MessageFn = std::function<void(const std::string& json)>;

    WinSettingsWindow(HINSTANCE instance, MessageFn onMessage);
    ~WinSettingsWindow();
    WinSettingsWindow(const WinSettingsWindow&) = delete;
    WinSettingsWindow& operator=(const WinSettingsWindow&) = delete;

    // Open the window with @p html, or bring it to the front. False if the
    // WebView2 Runtime is missing or the window cannot be made.
    bool show(const std::wstring& title, const std::wstring& html, HICON icon);

    // Send @p json to the page; dropped until the page has sent its first
    // request (it asks for the state when it loads).
    void post(const std::string& json);

    bool isOpen() const { return window_ != nullptr; }
    void setTitle(const std::wstring& title);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void createWebView();
    void resize();
    void destroyed();

    HINSTANCE instance_;
    MessageFn onMessage_;
    HWND window_ = nullptr;
    std::wstring html_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    bool pageReady_ = false;
};

}  // namespace stm32_audio::host::win
