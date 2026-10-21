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
//
// Aliases, the device order, favorites and the board's settings are kept in
// %APPDATA%\stm32-desk-dashboard\preferences.txt (ADR-004). "run" saves
// what the board changes and picks up changes made by "alias" within a
// second.

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "AudioMapping.h"
#include "DeviceRegistry.h"
#include "EventQueue.h"
#include "FrameText.h"
#include "HostController.h"
#include "HostService.h"
#include "PreferenceStore.h"
#include "WinAudioSystem.h"
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
        "                        alias <n> <name> | alias <n> --clear | startup on|off|status\n");
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
