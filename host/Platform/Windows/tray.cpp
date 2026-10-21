// dashboard-tray: the host as a system tray program (issue #74).
//
// Serves the controller like "dashboard-host run", without a console
// window: the service loop (HostService) runs on a worker thread and logs
// to %APPDATA%\stm32-desk-dashboard\host.log; the main thread owns a
// hidden window, the tray icon and its menu:
//
//   <state>                     "Kumanda bağlı" / "Kumanda bekleniyor" (greyed)
//   Windows ile başlat          check mark; HKCU\...\Run (WinStartup)
//   Ayarlar klasörünü aç        %APPDATA%\stm32-desk-dashboard
//   Günlüğü aç                  host.log
//   Çıkış
//
// The text follows the language of the board's settings record. Only one
// program serves the controller at a time (SingleInstance).

#include <windows.h>
#include <shellapi.h>
#include <share.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

#include "EventQueue.h"
#include "HostService.h"
#include "Startup.h"
#include "WinAudioSystem.h"
#include "WinPreferenceFile.h"
#include "WinStartup.h"
#include "WinStrings.h"
#include "tray_resource.h"

using namespace stm32_audio::host;

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;   // icon events
constexpr UINT kStateMessage = WM_APP + 2;  // worker: connection or language changed
constexpr UINT kWorkerDone = WM_APP + 3;    // worker: service loop returned
constexpr UINT kIconId = 1;
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;

enum MenuId : UINT {
    kMenuState = 1,
    kMenuStartup,
    kMenuFolder,
    kMenuLog,
    kMenuExit,
};

struct App {
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    NOTIFYICONDATAW icon{};
    EventQueue events;
    std::thread worker;
    std::atomic<bool> connected{false};
    std::atomic<std::uint8_t> language{0};
    std::wstring dir;       // %APPDATA%\stm32-desk-dashboard
    std::wstring logPath;   // ...\host.log
    FILE* log = nullptr;    // written by the worker only (and before/after it)
};

App g_app;

std::wstring text(TrayText id)
{
    return win::toWide(trayText(id, g_app.language.load()));
}

// --------------------------------------------------------------------- log

void openLog()
{
    if (g_app.dir.empty()) {
        return;
    }
    (void)CreateDirectoryW(g_app.dir.c_str(), nullptr);
    g_app.logPath = g_app.dir + L"\\host.log";
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(g_app.logPath.c_str(), GetFileExInfoStandard, &info)) {
        const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > kMaxLogBytes) {
            // Keep one older log.
            (void)MoveFileExW(g_app.logPath.c_str(), (g_app.logPath + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    g_app.log = _wfsopen(g_app.logPath.c_str(), L"ab", _SH_DENYWR);
}

void logLine(const std::string& line)
{
    if (g_app.log == nullptr) {
        return;
    }
    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::fprintf(g_app.log, "%04u-%02u-%02u %02u:%02u:%02u %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                 t.wSecond, line.c_str());
    std::fflush(g_app.log);
}

// ------------------------------------------------------------------ worker

void workerMain()
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        logLine("FAIL: COM initialisation failed.");
        PostMessageW(g_app.window, kWorkerDone, 1, 0);
        return;
    }
    int result = 1;
    {
        win::WinAudioSystem audio(g_app.events);
        if (!audio.ok()) {
            logLine("FAIL: cannot open the Windows audio device enumerator.");
        } else {
            win::HostServiceHooks hooks;
            hooks.log = logLine;
            hooks.connection = [](bool connected) {
                g_app.connected = connected;
                PostMessageW(g_app.window, kStateMessage, 0, 0);
            };
            hooks.language = [](std::uint8_t language) {
                g_app.language = language;
                PostMessageW(g_app.window, kStateMessage, 0, 0);
            };
            result = win::runHostService(audio, g_app.events, hooks);
        }
    }
    CoUninitialize();
    PostMessageW(g_app.window, kWorkerDone, static_cast<WPARAM>(result), 0);
}

void stopWorker()
{
    if (g_app.worker.joinable()) {
        g_app.events.push(HostEvent::Quit);
        g_app.worker.join();
    }
}

// -------------------------------------------------------------------- icon

void setTooltip()
{
    const std::wstring tip = text(TrayText::Title) + L"\n" +
                             text(g_app.connected ? TrayText::Connected : TrayText::Waiting);
    (void)wcsncpy_s(g_app.icon.szTip, tip.c_str(), _TRUNCATE);
}

void addIcon()
{
    g_app.icon.cbSize = sizeof(g_app.icon);
    g_app.icon.hWnd = g_app.window;
    g_app.icon.uID = kIconId;
    g_app.icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_app.icon.uCallbackMessage = kTrayMessage;
    g_app.icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (g_app.icon.hIcon == nullptr) {
        g_app.icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    setTooltip();
    (void)Shell_NotifyIconW(NIM_ADD, &g_app.icon);
    g_app.icon.uVersion = NOTIFYICON_VERSION_4;
    (void)Shell_NotifyIconW(NIM_SETVERSION, &g_app.icon);
}

void showMenu(POINT at)
{
    const std::wstring exe = win::trayProgramPath();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuState,
                text(g_app.connected ? TrayText::Connected : TrayText::Waiting).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (win::startupEnabled(exe) ? MF_CHECKED : MF_UNCHECKED), kMenuStartup,
                text(TrayText::StartWithWindows).c_str());
    AppendMenuW(menu, MF_STRING | (g_app.dir.empty() ? MF_GRAYED : 0U), kMenuFolder, text(TrayText::OpenFolder).c_str());
    AppendMenuW(menu, MF_STRING | (g_app.log == nullptr ? MF_GRAYED : 0U), kMenuLog, text(TrayText::OpenLog).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, text(TrayText::Exit).c_str());

    // The menu closes when the user clicks elsewhere only if our window is
    // in the foreground (documented TrackPopupMenu behaviour).
    SetForegroundWindow(g_app.window);
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, at.x,
                                                        at.y, g_app.window, nullptr));
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case kMenuStartup: {
        const bool enable = !win::startupEnabled(exe);
        if (win::setStartup(enable, exe)) {
            logLine(std::string("Start with Windows: ") + (enable ? "on" : "off"));
        } else {
            MessageBoxW(g_app.window, text(TrayText::StartupFailed).c_str(), text(TrayText::Title).c_str(),
                        MB_OK | MB_ICONWARNING);
        }
        break;
    }
    case kMenuFolder:
        (void)ShellExecuteW(nullptr, L"open", g_app.dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case kMenuLog:
        (void)ShellExecuteW(nullptr, L"open", g_app.logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case kMenuExit:
        DestroyWindow(g_app.window);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == kTrayMessage) {
        // NOTIFYICON_VERSION_4: the event in LOWORD(lParam), the point in wParam.
        const UINT event = LOWORD(lparam);
        if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) {
            POINT at{ static_cast<short>(LOWORD(wparam)), static_cast<short>(HIWORD(wparam)) };
            showMenu(at);
        }
        return 0;
    }
    if (msg == kStateMessage) {
        setTooltip();
        g_app.icon.uFlags = NIF_TIP | NIF_SHOWTIP;
        (void)Shell_NotifyIconW(NIM_MODIFY, &g_app.icon);
        return 0;
    }
    if (msg == kWorkerDone) {
        logLine("Service stopped (" + std::to_string(static_cast<int>(wparam)) + ").");
        if (wparam != 0) {
            DestroyWindow(hwnd);  // could not serve the controller: nothing to show
        }
        return 0;
    }
    if (msg == g_app.taskbarCreated && g_app.taskbarCreated != 0) {
        addIcon();  // Explorer restarted
        return 0;
    }
    switch (msg) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wparam != 0) {
            stopWorker();  // logoff, shutdown or an installer: save the preferences first
            if ((static_cast<UINT>(lparam) & ENDSESSION_CLOSEAPP) != 0) {
                DestroyWindow(hwnd);  // the Restart Manager (installer) asks us to exit
            }
        }
        return 0;
    case WM_DESTROY:
        (void)Shell_NotifyIconW(NIM_DELETE, &g_app.icon);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    win::SingleInstance single;
    if (!single.first()) {
        MessageBoxW(nullptr, text(TrayText::AlreadyRunning).c_str(), text(TrayText::Title).c_str(),
                    MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    win::WinPreferenceFile prefs;
    g_app.dir = prefs.directory();
    openLog();
    logLine("dashboard-tray started.");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"dashboard-tray";
    if (RegisterClassExW(&wc) == 0) {
        return 1;
    }
    // A hidden top-level window (not message-only: those miss the
    // TaskbarCreated broadcast).
    g_app.window = CreateWindowExW(0, wc.lpszClassName, L"dashboard-tray", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                   nullptr, instance, nullptr);
    if (g_app.window == nullptr) {
        return 1;
    }
    g_app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addIcon();
    g_app.worker = std::thread(workerMain);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stopWorker();
    logLine("dashboard-tray stopped.");
    if (g_app.log != nullptr) {
        std::fclose(g_app.log);
    }
    return 0;
}
