// dashboard-host: Windows console program.
//
// Console front end of the host application:
//
//   dashboard-host run               connect to the controller over USB HID
//                                      and serve it; log every frame (Ctrl+C)
//   dashboard-host list              playback devices, default, volume
//   dashboard-host set-default <n>   make device <n> (from "list") the default
//   dashboard-host volume <0-100>    volume of the default device
//   dashboard-host mute on|off       mute of the default device
//   dashboard-host watch             run HostController; print the frames it
//                                      would send to the STM32 (Ctrl+C to stop)
//   dashboard-host alias <n> <name>  set the alias of device <n> (from "list")
//   dashboard-host alias <n> --clear remove it
//   dashboard-host startup on|off|status
//                                      start the tray program (dashboard-tray,
//                                      next to this program) at Windows logon
//   dashboard-host weather           fetch and print the weather card
//   dashboard-host weather <city>    find the place (Open-Meteo geocoding) and
//                                      make it the weather location
//   dashboard-host weather <lat> <lon> [name]
//                                      set the weather location exactly
//   dashboard-host hook permission   Claude Code PermissionRequest hook (ADR-007):
//                                      asks the tray program, which shows the
//                                      request on the board; prints the decision
//                                      or nothing (the question in the terminal)
//   dashboard-host hook probe        Claude Code PermissionRequest hook of the
//                                      step 10 spike (#24): logs its input to
//                                      hook-probe.log, answers as hook-probe.txt
//                                      says (docs/hook-spike.md)
//   dashboard-host media             what plays now in the Windows media session
//                                      (ADR-008): app, texts, status, position, cover
//   dashboard-host media previous|play-pause|next
//                                      send that control to the session, then print
//   dashboard-host notify probe [n]  notification spike (#33): try the
//                                      notification listener and the
//                                      notification database; last n toasts
//   dashboard-host notify watch      print new toasts as they arrive (Ctrl+C)
//   dashboard-host notify            the notifications card, its detail and the
//                                      banner of the newest toast (ADR-009)
//   dashboard-host notify focus      print whether Windows accepts pop-ups now
//                                      (Do not disturb), at each change (Ctrl+C)
//   dashboard-host menu              print the menu card and today's detail
//   dashboard-host menu import <image> [<image> ...]
//                                      read menu images with the Claude API into
//                                      menu-YYYY-MM.json (key: claude.txt or
//                                      ANTHROPIC_API_KEY)
//
// Aliases, the device order, favorites and the board's settings are kept in
// %APPDATA%\stm32-desk-dashboard\preferences.txt (ADR-004). "run" saves
// what the board changes and picks up changes made by "alias" within a
// second. The weather location is kept in
// %APPDATA%\stm32-desk-dashboard\weather.txt; "run" reads it every minute.

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "AudioMapping.h"
#include "ClaudeHook.h"
#include "DashController.h"
#include "DeviceRegistry.h"
#include "EventQueue.h"
#include "FrameText.h"
#include "HostController.h"
#include "HostService.h"
#include "MediaSession.h"
#include "PreferenceStore.h"
#include "MenuService.h"
#include "NotificationService.h"
#include "WeatherCard.h"
#include "WeatherLocation.h"
#include "WinAudioSystem.h"
#include "WinHttpClient.h"
#include "WinHookPipe.h"
#include "WinMediaSession.h"
#include "WinNotificationListener.h"
#include "WinNotificationProbe.h"
#include "WinMenuImport.h"
#include "WinPreferenceFile.h"
#include "WinStartup.h"
#include "WinStrings.h"

using namespace stm32_audio::host;

namespace {

EventQueue* g_events = nullptr;

BOOL WINAPI onConsoleCtrl(DWORD type)
{
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) && g_events != nullptr) {
        g_events->push(HostEvent::Quit);
        return TRUE;
    }
    return FALSE;
}

const char* formFactorName(FormFactor f)
{
    switch (f) {
    case FormFactor::Speakers:
        return "speakers";
    case FormFactor::Headphones:
        return "headphones";
    case FormFactor::Headset:
        return "headset";
    case FormFactor::Display:
        return "display";
    case FormFactor::DigitalOutput:
        return "digital";
    case FormFactor::Unknown:
        break;
    }
    return "generic";
}

// Prints every frame instead of sending it over HID.
class PrintSink : public IFrameSink {
public:
    bool sendFrame(const Frame& frame) override
    {
        ProtoMessage msg{};
        if (Proto_Decode(frame.data(), &msg) != PROTO_OK) {
            std::printf("-> (invalid frame)\n");
            return false;
        }
        std::printf("-> %s\n", describeMessage(msg).c_str());
        std::fflush(stdout);
        return true;
    }
};

void installCtrlHandler(EventQueue* events)
{
    g_events = events;
    SetConsoleCtrlHandler(onConsoleCtrl, events != nullptr ? TRUE : FALSE);
}

// Console log: one line on stdout.
void printLine(const std::string& line)
{
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

bool loadPreferences(win::WinPreferenceFile& file, PreferenceStore& store, bool quiet)
{
    return win::loadPreferences(file, store, quiet, printLine);
}

int cmdList(win::WinAudioSystem& audio)
{
    const std::vector<AudioEndpoint> endpoints = audio.renderEndpoints();
    const std::string def = audio.defaultEndpointId();
    win::WinPreferenceFile file;
    Preferences prefs;
    PreferenceStore store(file, prefs);
    (void)loadPreferences(file, store, true);

    if (endpoints.empty()) {
        std::printf("No active playback devices.\n");
    }
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        const AudioEndpoint& e = endpoints[i];
        std::printf("%2lu %s %-10s %s\n", static_cast<unsigned long>(i + 1), e.id == def ? "*" : " ",
                    formFactorName(e.formFactor), e.name.c_str());
        const DevicePreferences* p = prefs.device(e.id);
        if (p != nullptr && (!p->alias.empty() || p->favorite)) {
            std::printf("       alias: %s%s\n", p->alias.empty() ? "-" : ("\"" + p->alias + "\"").c_str(),
                        p->favorite ? "  favorite" : "");
        }
        std::printf("       %s\n", e.id.c_str());
    }
    const std::optional<unsigned> volume = audio.volumePercent();
    const std::optional<bool> muted = audio.muted();
    std::printf("Default volume: %s%%, muted: %s\n", volume ? std::to_string(*volume).c_str() : "?",
                muted ? (*muted ? "yes" : "no") : "?");
    return 0;
}

int cmdSetDefault(win::WinAudioSystem& audio, const char* arg)
{
    const std::vector<AudioEndpoint> endpoints = audio.renderEndpoints();
    const long n = std::strtol(arg, nullptr, 10);

    if (n < 1 || static_cast<std::size_t>(n) > endpoints.size()) {
        std::printf("Device number must be 1..%lu (see \"list\").\n",
                    static_cast<unsigned long>(endpoints.size()));
        return 2;
    }
    const AudioEndpoint& e = endpoints[static_cast<std::size_t>(n - 1)];
    if (!audio.setDefaultEndpoint(e.id)) {
        std::printf("FAIL: could not make \"%s\" the default device.\n", e.name.c_str());
        return 1;
    }
    std::printf("OK: \"%s\" is now the default device.\n", e.name.c_str());
    return 0;
}

int cmdVolume(win::WinAudioSystem& audio, const char* arg)
{
    const long percent = std::strtol(arg, nullptr, 10);
    if (percent < 0 || percent > 100 || !audio.setVolumePercent(static_cast<unsigned>(percent))) {
        std::printf("FAIL: volume must be 0..100 and a default device must exist.\n");
        return 1;
    }
    std::printf("OK: volume %ld%%\n", percent);
    return 0;
}

int cmdMute(win::WinAudioSystem& audio, const std::string& arg)
{
    if ((arg != "on" && arg != "off") || !audio.setMuted(arg == "on")) {
        std::printf("FAIL: use \"mute on\" or \"mute off\"; a default device must exist.\n");
        return 1;
    }
    std::printf("OK: mute %s\n", arg.c_str());
    return 0;
}

int cmdAlias(win::WinAudioSystem& audio, const std::vector<std::string>& args)
{
    const std::vector<AudioEndpoint> endpoints = audio.renderEndpoints();
    const long n = std::strtol(args[2].c_str(), nullptr, 10);

    if (n < 1 || static_cast<std::size_t>(n) > endpoints.size()) {
        std::printf("Device number must be 1..%lu (see \"list\").\n",
                    static_cast<unsigned long>(endpoints.size()));
        return 2;
    }
    std::string alias;
    if (args[3] != "--clear") {
        for (std::size_t i = 3; i < args.size(); ++i) {
            alias += (i > 3 ? " " : "") + args[i];
        }
    }
    const AudioEndpoint& e = endpoints[static_cast<std::size_t>(n - 1)];
    win::WinPreferenceFile file;
    Preferences prefs;
    PreferenceStore store(file, prefs);
    if (!loadPreferences(file, store, true)) {
        return 1;
    }
    (void)prefs.arrange(endpoints);  // keep new devices in the stored order
    (void)prefs.setAlias(e.id, alias);
    store.markChanged();
    if (!store.flush()) {
        std::printf("FAIL: could not write %s\n", file.location().c_str());
        return 1;
    }
    const DevicePreferences* p = prefs.device(e.id);
    if (p == nullptr || p->alias.empty()) {
        std::printf("OK: \"%s\" has no alias.\n", e.name.c_str());
    } else {
        std::printf("OK: \"%s\" is shown as \"%s\"%s.\n", e.name.c_str(), p->alias.c_str(),
                    p->alias.size() < alias.size() ? " (cut to 32 bytes)" : "");
    }
    return 0;
}

int cmdWatch(win::WinAudioSystem& audio, EventQueue& events)
{
    PrintSink sink;
    HostController controller(audio, sink);
    win::WinPreferenceFile file;
    PreferenceStore store(file, controller.preferences());
    (void)loadPreferences(file, store, false);  // read only: watch does not save

    installCtrlHandler(&events);

    // Act as if the STM32 had just been opened: the full list goes out.
    controller.onConnected();
    std::printf("Watching Windows audio changes. Change the default device or the volume; Ctrl+C stops.\n");
    std::fflush(stdout);

    for (;;) {
        const std::optional<QueuedEvent> item = events.pop(std::chrono::milliseconds(500));
        if (!item) {
            continue;
        }
        if (item->event == HostEvent::Quit) {
            installCtrlHandler(nullptr);
            std::printf("Stopped.\n");
            return 0;
        }
        win::handleAudioEvent(item->event, audio, controller);
    }
}

int usage();

int cmdRun(win::WinAudioSystem& audio, EventQueue& events)
{
    win::SingleInstance single;
    if (!single.first()) {
        std::printf("FAIL: the controller is already served (dashboard-tray or another \"run\").\n");
        return 1;
    }
    win::HostServiceHooks hooks;
    hooks.log = printLine;
    std::printf("Ctrl+C stops.\n");
    installCtrlHandler(&events);
    const int result = win::runHostService(audio, events, hooks);
    installCtrlHandler(nullptr);
    return result;
}

// A record with its field separators visible, for the console.
void printRecord(const std::string& record)
{
    std::string text;
    for (const char c : record) {
        if (c == '\x1F') {
            text += " | ";
        } else {
            text += c;
        }
    }
    std::printf("%s\n", text.c_str());
}

std::optional<double> parseCoordinate(const std::string& text)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return value;
}

int saveWeatherLocation(const WeatherLocation& location)
{
    win::WinPreferenceFile file(L"weather");
    if (!parseLocationFile(formatLocationFile(location)) || !file.write(formatLocationFile(location))) {
        std::printf("FAIL: could not write %s\n", file.location().c_str());
        return 1;
    }
    std::printf("OK: weather location \"%s\" (%.4f, %.4f) saved in %s\n", location.name.c_str(),
                location.latitude, location.longitude, file.location().c_str());
    return 0;
}

int cmdWeather(const std::vector<std::string>& args)
{
    win::WinHttpClient http;

    if (args.size() == 2) {
        win::WinPreferenceFile file(L"weather");
        const std::optional<std::string> text = file.read();
        const std::optional<WeatherLocation> location = text ? parseLocationFile(*text) : std::nullopt;
        if (!location) {
            std::printf("No weather location in %s. Set one: dashboard-host weather <city>\n",
                        file.location().c_str());
            return 1;
        }
        WeatherCard weather;
        weather.setLocation(location);
        const std::string url = weather.fetchDue(0).value_or("");
        std::printf("GET %s\n", url.c_str());
        SYSTEMTIME t{};
        GetLocalTime(&t);
        const std::uint32_t now = localTimeValue(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        (void)weather.onFetched(0, now, url, http.get(url));
        if (!weather.report()) {
            std::printf("FAIL: no forecast (network, or an unexpected answer).\n");
            return 1;
        }
        std::printf("\nCard:\n");
        printRecord(weather.card(now));
        std::printf("\nDetail:\n");
        printRecord(weather.detail(now).value_or(""));
        return 0;
    }

    // weather <lat> <lon> [name]
    const std::optional<double> lat = parseCoordinate(args[2]);
    const std::optional<double> lon = args.size() >= 4 ? parseCoordinate(args[3]) : std::nullopt;
    if (lat && lon) {
        WeatherLocation location;
        location.latitude = *lat;
        location.longitude = *lon;
        for (std::size_t i = 4; i < args.size(); ++i) {
            location.name += (i > 4 ? " " : "") + args[i];
        }
        return saveWeatherLocation(location);
    }

    // weather <city>
    std::string name;
    for (std::size_t i = 2; i < args.size(); ++i) {
        name += (i > 2 ? " " : "") + args[i];
    }
    const std::optional<std::string> answer = http.get(geocodingUrl(name, 0));
    if (!answer) {
        std::printf("FAIL: no answer from the Open-Meteo geocoding API.\n");
        return 1;
    }
    const std::vector<Place> places = parseGeocoding(*answer);
    if (places.empty()) {
        std::printf("FAIL: no place called \"%s\" found.\n", name.c_str());
        return 1;
    }
    std::printf("Found: %s\n", places[0].description.c_str());
    const int result = saveWeatherLocation(places[0].location);
    if (places.size() > 1U) {
        std::printf("Other matches (set one with: dashboard-host weather <lat> <lon> <name>):\n");
        for (std::size_t i = 1; i < places.size(); ++i) {
            std::printf("  %s  %.4f %.4f\n", places[i].description.c_str(), places[i].location.latitude,
                        places[i].location.longitude);
        }
    }
    return result;
}

// "2026-10-21 15:04:05.123"
std::string localTimeText()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u:%02u.%03u", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, t.wMilliseconds);
    return buffer;
}

// One line at the end of %APPDATA%\stm32-desk-dashboard\hook-probe.log;
// parallel hooks may write at the same time (append is atomic per write).
void appendProbeLog(const std::wstring& dir, const std::string& line)
{
    if (dir.empty()) {
        return;
    }
    (void)CreateDirectoryW(dir.c_str(), nullptr);
    const HANDLE file = CreateFileW((dir + L"\\hook-probe.log").c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string text = line + "\r\n";
    DWORD written = 0;
    (void)WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

// The probe hook of the step 10 spike (ADR-007, #24). Prints nothing but
// the decision, so Claude Code reads stdout as the hook's answer.
int cmdHookProbe()
{
    const ULONGLONG started = GetTickCount64();
    const DWORD pid = GetCurrentProcessId();
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    std::string input;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        input.append(buffer, n);
    }

    win::WinPreferenceFile config(L"hook-probe");
    const HookProbeConfig probe = parseHookProbeConfig(config.read());
    appendProbeLog(config.directory(), hookProbeLogLine("start", localTimeText(), GetTickCount64() - started, pid,
                                                        input, probe));
    if (probe.waitSeconds > 0) {
        Sleep(static_cast<DWORD>(probe.waitSeconds) * 1000U);
    }
    const std::string out = permissionDecisionJson(probe.answer);
    if (!out.empty()) {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }
    appendProbeLog(config.directory(),
                   hookProbeLogLine("end", localTimeText(), GetTickCount64() - started, pid, input, probe));
    return probe.exitCode;
}

// The Claude Code hook (ADR-007): stdin -> the tray program's pipe -> the
// board; the decision on stdout. Every failure (no tray, no board, no
// answer in time, a broken pipe) prints nothing and exits 0, which leaves
// the decision to the terminal question. Never "allow" by itself.
int cmdHookPermission()
{
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    std::string input;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        input.append(buffer, n);
    }
    const std::optional<std::string> request = hookRequestLine(input);
    if (!request) {
        return 0;
    }

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 2 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        if (!WaitNamedPipeW(win::kHookPipeName, 1000)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) {
                return 0;  // no tray program
            }
            continue;
        }
        pipe = CreateFileW(win::kHookPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        return 0;
    }
    HANDLE io = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto transfer = [&](bool write, char* data, DWORD size, DWORD timeoutMs) -> DWORD {
        OVERLAPPED ov{};
        ResetEvent(io);
        ov.hEvent = io;
        DWORD done = 0;
        const BOOL started = write ? WriteFile(pipe, data, size, nullptr, &ov) : ReadFile(pipe, data, size, nullptr, &ov);
        if (!started && GetLastError() != ERROR_IO_PENDING) {
            return 0;
        }
        if (WaitForSingleObject(io, timeoutMs) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &ov);
            (void)GetOverlappedResult(pipe, &ov, &done, TRUE);
            return 0;
        }
        return GetOverlappedResult(pipe, &ov, &done, FALSE) ? done : 0;
    };

    std::string line = *request;
    HookAnswer answer = HookAnswer::None;
    if (transfer(true, line.data(), static_cast<DWORD>(line.size()), 5000) == line.size()) {
        std::string reply;
        const ULONGLONG started = GetTickCount64();
        while (reply.find('\n') == std::string::npos && reply.size() < 64U) {
            const ULONGLONG spent = GetTickCount64() - started;
            if (spent >= kHookClientWaitMs) {
                break;
            }
            char chunk[64];
            const DWORD got = transfer(false, chunk, sizeof(chunk), static_cast<DWORD>(kHookClientWaitMs - spent));
            if (got == 0) {
                break;
            }
            reply.append(chunk, got);
        }
        if (reply.find('\n') != std::string::npos) {
            answer = parseHookReply(reply.substr(0, reply.find('\n')));
        }
    }
    CloseHandle(io);
    CloseHandle(pipe);

    const std::string out = permissionDecisionJson(answer);
    if (!out.empty()) {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }
    return 0;
}

int cmdMenu(const std::vector<std::string>& args)
{
    if (args.size() >= 4 && args[2] == "import") {
        std::vector<std::wstring> paths;
        for (std::size_t i = 3; i < args.size(); ++i) {
            paths.push_back(win::toWide(args[i]));
        }
        std::printf("Sending %lu image(s) to the Claude API; this can take a minute...\n",
                    static_cast<unsigned long>(paths.size()));
        std::fflush(stdout);
        const MenuImportResult result = win::importMenuFiles(paths, 0);
        std::printf("%s%s\n", result.ok ? "OK: " : "FAIL: ", result.message.c_str());
        return result.ok ? 0 : 1;
    }
    if (args.size() != 2) {
        return usage();
    }

    // No board: the records go nowhere but are printed.
    class NullSink : public IFrameSink {
    public:
        bool sendFrame(const Frame&) override { return true; }
    } sink;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    const std::uint32_t now = localTimeValue(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    DashController dash(sink, [now] { return now; });
    win::WinMenuFiles files;
    MenuService menu(dash, files, [now] { return now; }, printLine);
    const long today = static_cast<long>(now / 86400U);
    std::printf("Card:\n");
    printRecord(menu.card(today));
    std::printf("\nDetail:\n");
    printRecord(menu.detailFor(MenuService::cardDay(today)));
    return 0;
}

// The notifications card as the tray would send it (ADR-009), or the focus
// state at each change.
int cmdNotify(const std::vector<std::string>& args)
{
    if (args.size() == 3 && args[2] == "focus") {
        std::printf("Turn Do not disturb on and off; Ctrl+C stops.\n");
        std::string last;
        for (;;) {
            const std::string now = win::describeFocusState();
            if (now != last) {
                SYSTEMTIME t{};
                GetLocalTime(&t);
                std::printf("%02u:%02u:%02u  %s\n", static_cast<unsigned>(t.wHour), static_cast<unsigned>(t.wMinute),
                            static_cast<unsigned>(t.wSecond), now.c_str());
                std::fflush(stdout);
                last = now;
            }
            Sleep(500);
        }
    }
    if (args.size() != 2) {
        return usage();
    }

    class NullSink : public IFrameSink {
    public:
        bool sendFrame(const Frame&) override { return true; }
    } sink;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    const std::uint32_t now = localTimeValue(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    DashController dash(sink, [now] { return now; });
    EventQueue events;
    win::WinNotificationListener listener(events, printLine);
    if (!listener.start()) {
        std::printf("FAIL: cannot start the notification thread.\n");
        return 1;
    }
    for (int i = 0; i < 60 && listener.snapshot().version == 0U; ++i) {
        Sleep(50);
    }
    win::WinFocusState focus;
    NotificationService notifications(dash, listener, focus, printLine);
    notifications.onTick(GetTickCount64());
    const NotificationSnapshot snap = listener.snapshot();
    listener.stop();
    std::printf("Card:\n");
    printRecord(notifications.card());
    std::printf("\nDetail:\n");
    printRecord(notifications.detail().value_or(""));
    if (!snap.toasts.empty()) {
        std::printf("\nBanner of the newest:\n");
        printRecord(notifications.banner(snap.toasts.front()));
    }
    std::printf("\nFocus: %s (banners %s)\n", win::describeFocusState().c_str(),
                focus.acceptsPopups() ? "shown" : "not shown");
    return 0;
}

int cmdMedia(const std::vector<std::string>& args)
{
    std::optional<MediaKey> key;
    if (args.size() == 3) {
        if (args[2] == "previous") {
            key = MediaKey::Previous;
        } else if (args[2] == "play-pause") {
            key = MediaKey::PlayPause;
        } else if (args[2] == "next") {
            key = MediaKey::Next;
        } else {
            return usage();
        }
    } else if (args.size() != 2) {
        return usage();
    }

    EventQueue events;
    win::WinMediaSession session(events, printLine);
    if (!session.start()) {
        std::printf("FAIL: cannot start the media session thread.\n");
        return 1;
    }
    // A few polls: the session, then the thumbnail of the track.
    auto waitMs = [&events](std::uint64_t ms) {
        const std::uint64_t end = GetTickCount64() + ms;
        while (GetTickCount64() < end) {
            (void)events.pop(std::chrono::milliseconds(100));
        }
    };
    waitMs(3000);
    if (key) {
        if (!session.command(*key)) {
            std::printf("FAIL: no media session.\n");
            session.stop();
            return 1;
        }
        waitMs(1500);
    }

    static const char* const status[] = {"no media session", "stopped", "playing", "paused"};
    const MediaSnapshot m = session.snapshot();
    session.stop();
    std::printf("Status:   %s\n", m.status < PROTO_MEDIA_STATUS_COUNT ? status[m.status] : "?");
    if (m.status == PROTO_MEDIA_STATUS_NONE) {
        return 0;
    }
    std::printf("App:      %s\n", m.app.c_str());
    std::printf("Title:    %s\n", m.title.c_str());
    std::printf("Artist:   %s\n", m.artist.c_str());
    std::printf("Album:    %s\n", m.album.c_str());
    std::printf("Position: %u:%02u / %u:%02u%s\n", m.positionS / 60U, m.positionS % 60U, m.durationS / 60U,
                m.durationS % 60U, m.durationS == 0U ? " (duration unknown)" : "");
    std::printf("Controls: %s%s%s\n", (m.controls & PROTO_MEDIA_CAN_PREVIOUS) != 0U ? "previous " : "",
                (m.controls & PROTO_MEDIA_CAN_PLAY_PAUSE) != 0U ? "play/pause " : "",
                (m.controls & PROTO_MEDIA_CAN_NEXT) != 0U ? "next" : "");
    std::printf("Cover:    %s\n", m.cover ? "120 x 120 (RGB565)" : "none");
    return 0;
}

int cmdStartup(const std::string& arg)
{
    const std::wstring tray = win::trayProgramPath();
    const std::string trayUtf8 = win::toUtf8(tray.c_str());

    if (arg == "status") {
        std::printf("Start with Windows: %s (%s)\n", win::startupEnabled(tray) ? "on" : "off", trayUtf8.c_str());
        return 0;
    }
    if (arg != "on" && arg != "off") {
        return usage();
    }
    if (arg == "on" && GetFileAttributesW(tray.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("FAIL: %s not found (it is built next to this program).\n", trayUtf8.c_str());
        return 1;
    }
    if (!win::setStartup(arg == "on", tray)) {
        std::printf("FAIL: could not change HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run.\n");
        return 1;
    }
    std::printf("OK: start with Windows %s (%s)\n", arg.c_str(), trayUtf8.c_str());
    return 0;
}

int usage()
{
    std::printf(
        "usage: dashboard-host run | list | set-default <n> | volume <0-100> | mute on|off | watch |\n"
        "                        alias <n> <name> | alias <n> --clear | startup on|off|status |\n"
        "                        weather | weather <city> | weather <lat> <lon> [name] |\n"
        "                        menu | menu import <image> [<image> ...] | hook permission | hook probe |\n"
        "                        media | media previous|play-pause|next | notify probe [n] | notify watch\n"
        "                        notify | notify focus\n");
    return 2;
}

// The command line as UTF-8 (argv of main() is in the ANSI code page,
// which cannot hold every alias).
std::vector<std::string> utf8Arguments()
{
    std::vector<std::string> args;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        for (int i = 0; i < count; ++i) {
            args.push_back(win::toUtf8(wide[i]));
        }
        LocalFree(wide);
    }
    return args;
}

int run(const std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return usage();
    }
    const std::string& cmd = args[1];

    if (cmd == "startup" && args.size() == 3) {
        return cmdStartup(args[2]);  // no audio needed
    }
    if (cmd == "hook" && args.size() == 3 && args[2] == "probe") {
        return cmdHookProbe();  // started by Claude Code: no audio, nothing else on stdout
    }
    if (cmd == "hook" && args.size() == 3 && args[2] == "permission") {
        return cmdHookPermission();  // the same
    }
    if (cmd == "weather") {
        return cmdWeather(args);
    }
    if (cmd == "menu") {
        return cmdMenu(args);
    }
    if (cmd == "media") {
        return cmdMedia(args);
    }
    if (cmd == "notify" && args.size() >= 3 && args.size() <= 4 && args[2] == "probe") {
        const unsigned long count = args.size() == 4 ? std::strtoul(args[3].c_str(), nullptr, 10) : 10UL;
        return win::runNotificationProbe(count == 0UL || count > 100UL ? 10U : static_cast<unsigned>(count),
                                         printLine);
    }
    if (cmd == "notify" && args.size() == 3 && args[2] == "watch") {
        return win::runNotificationWatch(printLine);
    }
    if (cmd == "notify") {
        return cmdNotify(args);
    }

    EventQueue events;
    win::WinAudioSystem audio(events);
    if (!audio.ok()) {
        std::printf("FAIL: cannot open the Windows audio device enumerator.\n");
        return 1;
    }

    if (cmd == "list") {
        return cmdList(audio);
    }
    if (cmd == "set-default" && args.size() == 3) {
        return cmdSetDefault(audio, args[2].c_str());
    }
    if (cmd == "volume" && args.size() == 3) {
        return cmdVolume(audio, args[2].c_str());
    }
    if (cmd == "mute" && args.size() == 3) {
        return cmdMute(audio, args[2]);
    }
    if (cmd == "alias" && args.size() >= 4) {
        return cmdAlias(audio, args);
    }
    if (cmd == "watch") {
        return cmdWatch(audio, events);
    }
    if (cmd == "run") {
        return cmdRun(audio, events);
    }
    return usage();
}

}  // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);  // device names are printed as UTF-8

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::printf("FAIL: COM initialisation failed.\n");
        return 1;
    }
    const int result = run(utf8Arguments());  // COM objects are released inside run()
    CoUninitialize();
    return result;
}
