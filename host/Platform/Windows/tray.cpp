// dashboard-tray: the host as a system tray program (issue #74).
//
// Serves the controller like "dashboard-host run", without a console
// window: the service loop (HostService) runs on a worker thread and logs
// to %APPDATA%\stm32-desk-dashboard\host.log; the main thread owns a
// hidden window, the tray icon and its menu:
//
//   <state>                     "Kumanda bağlı" / "Kumanda bekleniyor" (greyed)
//   Windows ile başlat          check mark; HKCU\...\Run (WinStartup)
//   Menü görüntülerini yükle…   images -> Claude API -> menu-YYYY-MM.json (#20);
//                               a balloon shows the summary
//   Resim seç…                  the picture of the Dashboard's picture card (ADR-010)
//   Yemek listelerini düzenle…  menu-lists.txt in Notepad (ADR-010)
//   Claude Code hook ayarını kopyala
//                               the settings.json snippet for this PC (#26)
//   Ayarlar klasörünü aç        %APPDATA%\stm32-desk-dashboard
//   Günlüğü aç                  host.log
//   Çıkış
//
// The text follows the language of the board's settings record. Only one
// program serves the controller at a time (SingleInstance).

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <share.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "EventQueue.h"
#include "HostService.h"
#include "Startup.h"
#include "WinAudioSystem.h"
#include "WinPreferenceFile.h"
#include "WinStartup.h"
#include "ClaudeHook.h"
#include "WinMenuImport.h"
#include "MenuLists.h"
#include "WinPicture.h"
#include "WinStrings.h"
#include "tray_resource.h"

using namespace stm32_audio::host;

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;   // icon events
constexpr UINT kStateMessage = WM_APP + 2;  // worker: connection or language changed
constexpr UINT kWorkerDone = WM_APP + 3;    // worker: service loop returned
constexpr UINT kMenuImportDone = WM_APP + 4;  // importer: menu import finished
constexpr UINT kIconId = 1;
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;

enum MenuId : UINT {
    kMenuState = 1,
    kMenuStartup,
    kMenuLoadMenu,
    kMenuPicture,
    kMenuFoodLists,
    kMenuCopyHook,
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
    FILE* log = nullptr;    // written by the worker and the importer (the CRT locks each line)
    std::thread importer;   // menu import (#20), one at a time
    std::atomic<bool> importing{false};
    std::mutex importMutex;
    MenuImportResult importResult;  // under importMutex
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

// ---------------------------------------------------------- menu import

void showBalloon(const std::wstring& title, const std::wstring& body, bool warning)
{
    g_app.icon.uFlags = NIF_INFO;
    (void)wcsncpy_s(g_app.icon.szInfoTitle, title.c_str(), _TRUNCATE);
    (void)wcsncpy_s(g_app.icon.szInfo, body.c_str(), _TRUNCATE);
    g_app.icon.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
    (void)Shell_NotifyIconW(NIM_MODIFY, &g_app.icon);
}

// The images chosen in the file dialog; empty if cancelled.
std::vector<std::wstring> chooseImages()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    std::wstring filter = text(TrayText::MenuImages) + L" (*.png, *.jpg, *.webp, *.gif)";
    filter.push_back(L'\0');
    filter += L"*.png;*.jpg;*.jpeg;*.webp;*.gif";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    const std::wstring title = text(TrayText::LoadMenu);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.window;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    SetForegroundWindow(g_app.window);
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    // One file: its full path. Several: the folder, then the names, each
    // ended by a NUL, the list by a second NUL.
    std::vector<std::wstring> parts;
    for (const wchar_t* p = buffer.data(); *p != L'\0'; p += wcslen(p) + 1U) {
        parts.emplace_back(p);
    }
    if (parts.size() <= 1U) {
        return parts;
    }
    std::vector<std::wstring> paths;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        paths.push_back(parts[0] + L"\\" + parts[i]);
    }
    return paths;
}

void loadMenuImages()
{
    if (g_app.importing) {
        return;
    }
    const std::vector<std::wstring> paths = chooseImages();
    if (paths.empty()) {
        return;
    }
    if (g_app.importer.joinable()) {
        g_app.importer.join();  // the previous import has finished
    }
    g_app.importing = true;
    const std::uint8_t language = g_app.language.load();
    logLine("Menu import: " + std::to_string(paths.size()) + " image(s).");
    showBalloon(text(TrayText::Title), text(TrayText::MenuImporting), false);
    g_app.importer = std::thread([paths, language] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MenuImportResult result = win::importMenuFiles(paths, language);
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        logLine(std::string(result.ok ? "Menu import: " : "FAIL: menu import: ") + result.message);
        {
            std::lock_guard<std::mutex> lock(g_app.importMutex);
            g_app.importResult = std::move(result);
        }
        PostMessageW(g_app.window, kMenuImportDone, 0, 0);
    });
}

void onMenuImportDone()
{
    MenuImportResult result;
    {
        std::lock_guard<std::mutex> lock(g_app.importMutex);
        result = g_app.importResult;
    }
    g_app.importing = false;
    if (result.ok) {
        g_app.events.push(HostEvent::MenuChanged);  // the card follows at once
    }
    showBalloon(text(result.ok ? TrayText::MenuImported : TrayText::MenuImportFailed), win::toWide(result.message),
                !result.ok);
}

// The picture card (ADR-010): one image file; its path goes to picture.txt
// and the service loop reads it.
void choosePicture()
{
    std::vector<wchar_t> buffer(MAX_PATH * 4, L'\0');
    std::wstring filter = text(TrayText::PictureFiles) + L" (*.jpg, *.png, *.bmp, *.gif, *.webp, *.heic)";
    filter.push_back(L'\0');
    filter += L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.heic;*.tif;*.tiff";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    const std::wstring title = text(TrayText::ChoosePicture);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.window;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    SetForegroundWindow(g_app.window);
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    const std::string path = win::toUtf8(buffer.data());
    if (!win::storePicturePath(path)) {
        logLine("WARN: could not save picture.txt.");
        return;
    }
    g_app.events.push(HostEvent::SettingsChanged);
}

// menu-lists.txt (ADR-010) in the default editor; created with the
// explanation when it does not exist yet.
void editFoodLists()
{
    win::WinPreferenceFile lists(L"menu-lists");
    if (!lists.ok()) {
        return;
    }
    if (!lists.read() && !lists.write(kMenuListsTemplate)) {
        logLine("WARN: could not create menu-lists.txt.");
        return;
    }
    const std::wstring path = lists.directory() + L"\\menu-lists.txt";
    (void)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// The Claude Code hook snippet (ADR-007) on the clipboard: dashboard-host.exe
// next to this program, exec form.
void copyHookSettings(const std::wstring& trayExe)
{
    const std::size_t slash = trayExe.find_last_of(L'\\');
    const std::wstring host = (slash == std::wstring::npos ? std::wstring() : trayExe.substr(0, slash + 1U)) +
                              L"dashboard-host.exe";
    const std::wstring snippet = win::toWide(hookSettingsSnippet(win::toUtf8(host.c_str())));
    bool copied = false;
    if (OpenClipboard(g_app.window)) {
        const SIZE_T bytes = (snippet.size() + 1U) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory != nullptr) {
            void* p = GlobalLock(memory);
            if (p != nullptr) {
                std::memcpy(p, snippet.c_str(), bytes);
                GlobalUnlock(memory);
                (void)EmptyClipboard();
                copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }
            if (!copied) {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }
    logLine(copied ? "Claude Code hook setting copied." : "WARN: could not copy the Claude Code hook setting.");
    if (copied) {
        showBalloon(text(TrayText::Title), text(TrayText::HookSettingsCopied), false);
    }
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
    AppendMenuW(menu, MF_STRING | (g_app.importing ? MF_GRAYED : 0U), kMenuLoadMenu, text(TrayText::LoadMenu).c_str());
    AppendMenuW(menu, MF_STRING, kMenuPicture, text(TrayText::ChoosePicture).c_str());
    AppendMenuW(menu, MF_STRING | (g_app.dir.empty() ? MF_GRAYED : 0U), kMenuFoodLists,
                text(TrayText::EditFoodLists).c_str());
    AppendMenuW(menu, MF_STRING, kMenuCopyHook, text(TrayText::CopyHookSettings).c_str());
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
    case kMenuLoadMenu:
        loadMenuImages();
        break;
    case kMenuPicture:
        choosePicture();
        break;
    case kMenuFoodLists:
        editFoodLists();
        break;
    case kMenuCopyHook:
        copyHookSettings(exe);
        break;
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
    if (msg == kMenuImportDone) {
        onMenuImportDone();
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
    // The file dialog wants a single-threaded apartment on this thread.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

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
    if (g_app.importer.joinable()) {
        g_app.importer.join();  // an import still waiting for the API ends by its timeout
    }
    logLine("dashboard-tray stopped.");
    if (g_app.log != nullptr) {
        std::fclose(g_app.log);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}
