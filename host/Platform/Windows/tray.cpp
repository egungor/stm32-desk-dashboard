// dashboard-tray: the host as a system tray program (issue #74).
//
// Serves the controller like "dashboard-host run", without a console
// window: the service loop (HostService) runs on a worker thread and logs
// to %APPDATA%\stm32-desk-dashboard\host.log; the main thread owns a
// hidden window, the tray icon and its menu:
//
//   <state>                     "Kumanda bağlı" / "Kumanda bekleniyor" (greyed)
//   Ayarlar…                    the settings window (ADR-011; also a left click
//                               on the icon): every setting, the versions
//   Günlüğü aç                  host.log
//   Çıkış
//
// Everything else (start with Windows, menu images, picture, food lists,
// the Claude Code hook, the settings folder) is in the settings window
// (#66).
// A right click shows the menu, a left click opens the settings window.
// The text follows the language of the board's settings record. Only one
// program serves the controller at a time (SingleInstance).

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <share.h>

#include <algorithm>
#include <atomic>
#include <memory>
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
#include "MenuImport.h"
#include "MenuService.h"
#include "SettingsModel.h"
#include "WeatherLocation.h"
#include "WinHttpClient.h"
#if DASHBOARD_SETTINGS_WINDOW
#include "WinSettingsWindow.h"
#endif
#include "tray_resource.h"

using namespace stm32_audio::host;

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;   // icon events
constexpr UINT kStateMessage = WM_APP + 2;  // worker: connection or language changed
constexpr UINT kWorkerDone = WM_APP + 3;    // worker: service loop returned
constexpr UINT kMenuImportDone = WM_APP + 4;  // importer: menu import finished
constexpr UINT kSnapshotMessage = WM_APP + 5;  // worker: what the settings window shows changed
constexpr UINT kPlacesMessage = WM_APP + 6;    // weather search finished
constexpr UINT kIconId = 1;
// The settings window finds the pictures folder here (ADR-012).
constexpr const wchar_t* kPicturesHost = L"pictures.desk-dashboard";
constexpr const char* kPicturesUrl = "https://pictures.desk-dashboard/";
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;

enum MenuId : UINT {
    kMenuState = 1,
    kMenuSettings,
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
    std::mutex snapshotMutex;
    ServiceSnapshot snapshot;       // under snapshotMutex; from the worker
    std::thread search;             // weather search for the settings window
    std::atomic<bool> searching{false};
    std::mutex placesMutex;
    std::string places;             // under placesMutex: placesJson of the last search
#if DASHBOARD_SETTINGS_WINDOW
    std::unique_ptr<win::WinSettingsWindow> settings;
#endif
};

App g_app;

std::wstring text(TrayText id)
{
    return win::toWide(trayText(id, g_app.language.load()));
}

// The settings window, if open (below).
void postState();
void postToPage(const std::string& json);

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
            hooks.snapshot = [](const ServiceSnapshot& snapshot) {
                {
                    std::lock_guard<std::mutex> lock(g_app.snapshotMutex);
                    g_app.snapshot = snapshot;
                }
                PostMessageW(g_app.window, kSnapshotMessage, 0, 0);
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
std::vector<std::wstring> chooseImages(const std::wstring& name, const std::wstring& patterns,
                                       const std::wstring& title)
{
    std::vector<wchar_t> buffer(32768, L'\0');
    std::wstring filter = name;
    filter.push_back(L'\0');
    filter += patterns;
    filter.push_back(L'\0');
    filter.push_back(L'\0');

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

[[maybe_unused]] void loadMenuImages()  // only the settings window starts it
{
    if (g_app.importing) {
        return;
    }
    const std::vector<std::wstring> paths =
        chooseImages(text(TrayText::MenuImages) + L" (*.png, *.jpg, *.webp, *.gif, *.html)",
                     L"*.png;*.jpg;*.jpeg;*.webp;*.gif;*.html;*.htm", text(TrayText::LoadMenu));
    if (paths.empty()) {
        return;
    }
    if (g_app.importer.joinable()) {
        g_app.importer.join();  // the previous import has finished
    }
    g_app.importing = true;
    const std::uint8_t language = g_app.language.load();
    logLine("Menu import: " + std::to_string(paths.size()) + " file(s).");
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
    postToPage(noticeJson(result.ok, result.message));
    postState();
}

// The Claude Code hook snippet (ADR-007) on the clipboard: dashboard-host.exe
// next to this program, exec form.
[[maybe_unused]] void copyHookSettings(const std::wstring& trayExe)
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

// ------------------------------------------------------- settings window

#if DASHBOARD_SETTINGS_WINDOW
// The page (settings.html, an RCDATA resource) as UTF-16.
std::wstring settingsHtml()
{
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SETTINGS_HTML), RT_RCDATA);
    HGLOBAL loaded = found != nullptr ? LoadResource(nullptr, found) : nullptr;
    const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
    if (data == nullptr) {
        return L"<p>settings.html is missing</p>";
    }
    return win::toWide(std::string(static_cast<const char*>(data), SizeofResource(nullptr, found)));
}

SettingsState buildState()
{
    SettingsState s;
    s.hostVersion = DASHBOARD_VERSION;
    {
        std::lock_guard<std::mutex> lock(g_app.snapshotMutex);
        s.service = g_app.snapshot;
    }
    win::WinPreferenceFile weather(L"weather");
    if (const std::optional<std::string> text = weather.read()) {
        s.weather = parseLocationFile(*text);
    }
    s.pictures = win::readPictureLibrary().value_or(PictureLibrary());
    win::WinPreferenceFile lists(L"menu-lists");
    s.foodLists = lists.read().value_or(kMenuListsTemplate);
    // A file of before liked dishes gets today's comments (the lists stay).
    if (const std::optional<std::string> upgraded = upgradeMenuListsComments(s.foodLists)) {
        if (lists.write(*upgraded)) {
            logLine("Food lists: comments updated (liked dishes).");
            s.foodLists = *upgraded;
        }
    }
    win::WinPreferenceFile menuSettings(L"menu-settings");
    s.menuNextDayHour = parseMenuNextDayHour(menuSettings.read());
    win::WinPreferenceFile claude(L"claude");
    const ClaudeConfig config = parseClaudeConfig(claude.read(), std::string());
    s.claudeKeySet = !config.apiKey.empty();
    s.claudeModel = config.model;
    s.startup = win::startupEnabled(win::trayProgramPath());
    s.importing = g_app.importing;
    s.folder = win::toUtf8(g_app.dir.c_str());
    return s;
}
#endif

void postToPage(const std::string& json)
{
#if DASHBOARD_SETTINGS_WINDOW
    if (g_app.settings && g_app.settings->isOpen()) {
        g_app.settings->post(json);
    }
#else
    (void)json;
#endif
}

void postState()
{
#if DASHBOARD_SETTINGS_WINDOW
    if (g_app.settings && g_app.settings->isOpen()) {
        g_app.settings->post(settingsStateJson(buildState(), kPicturesUrl));
    }
#endif
}

#if DASHBOARD_SETTINGS_WINDOW
// A setting file was written (or not): the service reads it again (#61).
void settingsFileSaved(bool ok, const char* name)
{
    if (ok) {
        g_app.events.push(HostEvent::SettingsChanged);
        postToPage(noticeJson(true, win::toUtf8(text(TrayText::SettingsSaved).c_str())));
    } else {
        logLine(std::string("WARN: settings window: could not save ") + name + ".");
        postToPage(noticeJson(false, win::toUtf8(text(TrayText::SettingsNotSaved).c_str())));
    }
    postState();
}

void searchWeather(const std::string& query)
{
    if (g_app.searching.exchange(true)) {
        return;  // one at a time
    }
    if (g_app.search.joinable()) {
        g_app.search.join();
    }
    const std::uint8_t language = g_app.language.load();
    g_app.search = std::thread([query, language] {
        win::WinHttpClient http;
        const std::optional<std::string> answer = http.get(geocodingUrl(query, language));
        const std::vector<Place> places = answer ? parseGeocoding(*answer) : std::vector<Place>();
        {
            std::lock_guard<std::mutex> lock(g_app.placesMutex);
            g_app.places = placesJson(places);
        }
        g_app.searching = false;
        PostMessageW(g_app.window, kPlacesMessage, 0, 0);
    });
}

// Photos for the picture card (ADR-012): copied into the pictures folder
// with a preview; the service reads the library again.
void addPictures()
{
    const std::vector<std::wstring> paths =
        chooseImages(text(TrayText::PictureFiles) + L" (*.jpg, *.png, *.heic, *.webp, *.bmp, *.gif, *.tif)",
                     L"*.jpg;*.jpeg;*.png;*.heic;*.heif;*.webp;*.bmp;*.gif;*.tif;*.tiff", text(TrayText::AddPictures));
    if (paths.empty()) {
        return;
    }
    PictureLibrary library = win::readPictureLibrary().value_or(PictureLibrary());
    unsigned added = 0;
    std::string failed;
    for (const std::wstring& path : paths) {
        std::string error;
        if (win::addLibraryPicture(library, path, error)) {
            ++added;
        } else {
            logLine("WARN: picture " + win::toUtf8(path.c_str()) + ": " + error);
            failed += (failed.empty() ? "" : ", ") + win::toUtf8(path.substr(path.find_last_of(L"\\/") + 1U).c_str());
        }
    }
    logLine("Pictures added: " + std::to_string(added) + ".");
    if (added > 0U) {
        settingsFileSaved(win::writePictureLibrary(library), "pictures.json");
    }
    if (!failed.empty()) {
        postToPage(noticeJson(false, win::toUtf8(text(TrayText::PicturesNotAdded).c_str()) + " " + failed));
    }
    postState();
}

// Remove a picture, change its part, its place in the shuffle, or the
// shuffle's settings: pictures.json, then the service reads it again.
void editPictures(const SettingsRequest& request)
{
    PictureLibrary library = win::readPictureLibrary().value_or(PictureLibrary());
    auto it = std::find_if(library.pictures.begin(), library.pictures.end(),
                           [&request](const LibraryPicture& p) { return p.id == request.name; });
    switch (request.kind) {
    case SettingsRequestKind::PictureShuffle:
        library.shuffle = request.shuffle;
        break;
    case SettingsRequestKind::PictureRemove:
        if (it == library.pictures.end()) {
            return;
        }
        win::removeLibraryFiles(*it);
        library.pictures.erase(it);
        break;
    case SettingsRequestKind::PictureInclude:
        if (it == library.pictures.end()) {
            return;
        }
        it->shuffle = request.value != 0U;
        break;
    case SettingsRequestKind::PictureCrop:
        if (it == library.pictures.end() || !validCrop(request.crop, it->width, it->height)) {
            logLine("WARN: settings window: a picture part that does not fit was not saved.");
            postState();
            return;
        }
        it->crop = request.crop;
        break;
    default:
        return;
    }
    settingsFileSaved(win::writePictureLibrary(library), "pictures.json");
}

// A request from the page (SettingsModel.h).
void onSettingsMessage(const std::string& json)
{
    const std::optional<SettingsRequest> request = parseSettingsRequest(json);
    if (!request) {
        logLine("WARN: settings window: a request was not understood.");
        return;
    }
    const std::wstring exe = win::trayProgramPath();
    switch (request->kind) {
    case SettingsRequestKind::Get:
        postState();
        break;
    case SettingsRequestKind::Setting:
    case SettingsRequestKind::Alias:
    case SettingsRequestKind::Favorite:
    case SettingsRequestKind::Order:
        // The service owns the preferences; its snapshot brings the result.
        g_app.events.pushText(HostEvent::SettingsRequest, json);
        break;
    case SettingsRequestKind::WeatherSearch:
        searchWeather(request->text);
        break;
    case SettingsRequestKind::WeatherSet: {
        win::WinPreferenceFile file(L"weather");
        settingsFileSaved(file.write(formatLocationFile(request->location)), "weather.txt");
        break;
    }
    case SettingsRequestKind::PicturesAdd:
        addPictures();
        break;
    case SettingsRequestKind::PictureShow:
        // The service keeps which picture is shown (ADR-012).
        g_app.events.pushText(HostEvent::SettingsRequest, json);
        break;
    case SettingsRequestKind::PictureRemove:
    case SettingsRequestKind::PictureCrop:
    case SettingsRequestKind::PictureInclude:
    case SettingsRequestKind::PictureShuffle:
        editPictures(*request);
        break;
    case SettingsRequestKind::FoodLists: {
        win::WinPreferenceFile file(L"menu-lists");
        settingsFileSaved(file.write(request->text), "menu-lists.txt");
        break;
    }
    case SettingsRequestKind::MenuNextDay: {
        win::WinPreferenceFile file(L"menu-settings");
        settingsFileSaved(file.write(formatMenuSettings(request->value)), "menu-settings.txt");
        break;
    }
    case SettingsRequestKind::ClaudeKey: {
        // Read at the next menu import; the service does not need it.
        win::WinPreferenceFile file(L"claude");
        const bool ok = file.write(claudeFileWithKey(file.read(), request->text));
        logLine(ok ? "Settings window: Claude API key saved." : "WARN: settings window: could not save claude.txt.");
        postToPage(noticeJson(ok, win::toUtf8(text(ok ? TrayText::SettingsSaved : TrayText::SettingsNotSaved).c_str())));
        postState();
        break;
    }
    case SettingsRequestKind::MenuImport:
        loadMenuImages();
        postState();
        break;
    case SettingsRequestKind::Startup: {
        const bool enable = request->value != 0U;
        if (win::setStartup(enable, exe)) {
            logLine(std::string("Start with Windows: ") + (enable ? "on" : "off"));
        } else {
            postToPage(noticeJson(false, win::toUtf8(text(TrayText::StartupFailed).c_str())));
        }
        postState();
        break;
    }
    case SettingsRequestKind::CopyHook:
        copyHookSettings(exe);
        break;
    case SettingsRequestKind::OpenFolder:
        (void)ShellExecuteW(nullptr, L"open", g_app.dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case SettingsRequestKind::OpenLog:
        (void)ShellExecuteW(nullptr, L"open", g_app.logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
}

void openSettings()
{
    if (!g_app.settings) {
        g_app.settings = std::make_unique<win::WinSettingsWindow>(GetModuleHandleW(nullptr), onSettingsMessage);
        g_app.settings->mapFolder(kPicturesHost, win::picturesDirectory());  // the previews (ADR-012)
    }
    if (!g_app.settings->show(text(TrayText::SettingsTitle), settingsHtml(), g_app.icon.hIcon)) {
        logLine("WARN: settings window: the WebView2 Runtime is missing or the window failed.");
        MessageBoxW(g_app.window, text(TrayText::WebViewMissing).c_str(), text(TrayText::Title).c_str(),
                    MB_OK | MB_ICONWARNING);
    }
}
#endif

void showMenu(POINT at)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kMenuState,
                text(g_app.connected ? TrayText::Connected : TrayText::Waiting).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
#if DASHBOARD_SETTINGS_WINDOW
    AppendMenuW(menu, MF_STRING, kMenuSettings, text(TrayText::Settings).c_str());
    SetMenuDefaultItem(menu, kMenuSettings, FALSE);
#endif
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
#if DASHBOARD_SETTINGS_WINDOW
    case kMenuSettings:
        openSettings();
        break;
#endif
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
        POINT at{ static_cast<short>(LOWORD(wparam)), static_cast<short>(HIWORD(wparam)) };
#if DASHBOARD_SETTINGS_WINDOW
        if (event == NIN_SELECT) {
            openSettings();  // a left click
        } else if (event == WM_CONTEXTMENU || event == NIN_KEYSELECT) {
            showMenu(at);
        }
#else
        if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) {
            showMenu(at);
        }
#endif
        return 0;
    }
    if (msg == kStateMessage) {
        setTooltip();
        g_app.icon.uFlags = NIF_TIP | NIF_SHOWTIP;
        (void)Shell_NotifyIconW(NIM_MODIFY, &g_app.icon);
#if DASHBOARD_SETTINGS_WINDOW
        if (g_app.settings) {
            g_app.settings->setTitle(text(TrayText::SettingsTitle));
        }
#endif
        return 0;
    }
    if (msg == kSnapshotMessage) {
        postState();
        return 0;
    }
    if (msg == kPlacesMessage) {
        std::string places;
        {
            std::lock_guard<std::mutex> lock(g_app.placesMutex);
            places = g_app.places;
        }
        postToPage(places);
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
#if DASHBOARD_SETTINGS_WINDOW
    // Sharp text in the settings window on scaled screens.
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    // The file dialog (and WebView2) want a single-threaded apartment on this thread.
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
    {
        // The picture chosen before #72 becomes the library's first.
        std::string message;
        if (win::migrateOldPicture(message) && !message.empty()) {
            logLine(message);
        }
    }

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

#if DASHBOARD_SETTINGS_WINDOW
    g_app.settings.reset();
#endif
    stopWorker();
    if (g_app.search.joinable()) {
        g_app.search.join();  // ends by the HTTP timeout
    }
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
