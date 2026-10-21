#include "HostService.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "DashController.h"
#include "FrameRouter.h"
#include "FrameText.h"
#include "HidMatch.h"
#include "SequencedSink.h"
#include "VolumeLimit.h"
#include "WinHidLink.h"

namespace stm32_audio::host::win {

namespace {

// Logs every frame, then hands it to the real transport.
class LoggingSink : public IFrameSink {
public:
    LoggingSink(IFrameSink& next, const LogFn& log) : next_(next), log_(log) {}

    bool sendFrame(const Frame& frame) override
    {
        ProtoMessage msg{};
        const bool decoded = Proto_Decode(frame.data(), &msg) == PROTO_OK;
        const bool sent = next_.sendFrame(frame);
        log_(std::string("-> ") + (decoded ? describeMessage(msg) : std::string("(invalid frame)")) +
             (sent ? "" : "  [send failed]"));
        return sent;
    }

private:
    IFrameSink& next_;
    const LogFn& log_;
};

void logReceived(const Frame& frame, const LogFn& log)
{
    ProtoMessage msg{};
    const ProtoStatus result = Proto_Decode(frame.data(), &msg);
    if (result == PROTO_OK) {
        log("<- " + describeMessage(msg));
    } else {
        log("<- (invalid frame, Proto_Decode=" + std::to_string(static_cast<int>(result)) + ")");
    }
}

// Windows' local time as the value of TIME (protocol.md).
std::uint32_t localTimeNow()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return localTimeValue(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

void logVolumeLimited(const VolumeLimited& v, const LogFn& log)
{
    log("Volume limit: \"" + v.name + "\" " + std::to_string(v.from) + "% -> " + std::to_string(v.to) + "%");
}

}  // namespace

bool loadPreferences(WinPreferenceFile& file, PreferenceStore& store, bool quiet, const LogFn& log)
{
    if (!file.ok()) {
        log("WARN: %APPDATA% not set; preferences are not saved.");
        return false;
    }
    switch (store.load()) {
    case PreferenceStore::LoadResult::Loaded:
        if (!quiet) {
            log("Preferences: " + file.location());
        }
        break;
    case PreferenceStore::LoadResult::Missing:
        if (!quiet) {
            log("Preferences: " + file.location() + " (new)");
        }
        break;
    case PreferenceStore::LoadResult::Unreadable:
        log("WARN: " + file.location() + " is not readable; kept as preferences.bad, using defaults.");
        break;
    }
    return true;
}

void handleAudioEvent(HostEvent event, WinAudioSystem& audio, HostController& controller)
{
    switch (event) {
    case HostEvent::DevicesChanged:
        controller.onDevicesChanged();
        break;
    case HostEvent::DefaultChanged:
        audio.rebindVolumeNotifications();
        controller.onDefaultChanged();
        break;
    case HostEvent::VolumeChanged:
        controller.onVolumeChanged();
        break;
    default:
        break;
    }
}

int runHostService(WinAudioSystem& audio, EventQueue& events, const HostServiceHooks& hooks)
{
    const LogFn& log = hooks.log;
    WinHidLink link(events);
    LoggingSink logged(link, log);
    SequencedSink sink(logged);  // one sequence counter for both controllers (ADR-006)
    HostController controller(audio, sink);
    DashController dash(sink, localTimeNow);
    FrameRouter router(controller, dash, sink);
    WinPreferenceFile file;
    PreferenceStore store(file, controller.preferences());
    const bool saving = loadPreferences(file, store, false, log);
    std::uint8_t language = 0xFFU;

    // Report the settings record's language when it changes (tray menu).
    auto reportLanguage = [&] {
        const std::uint8_t now = controller.preferences().settings().language;
        if (now != language) {
            language = now;
            if (hooks.language) {
                hooks.language(now);
            }
        }
    };

    controller.onPreferencesChanged();  // the stored order and aliases
    controller.setPreferencesChangedHandler([&store] { store.markChanged(); });
    controller.setVolumeLimitedHandler([&log](const VolumeLimited& v) { logVolumeLimited(v, log); });
    reportLanguage();

    if (!link.start()) {
        log("FAIL: cannot start the HID thread.");
        return 1;
    }
    char waiting[96];
    std::snprintf(waiting, sizeof(waiting), "Waiting for the controller (USB HID %04X:%04X).",
                  static_cast<unsigned>(kControllerVid), static_cast<unsigned>(kControllerPid));
    log(waiting);
    if (hooks.connection) {
        hooks.connection(false);
    }

    for (;;) {
        const std::optional<QueuedEvent> item = events.pop(std::chrono::milliseconds(500));
        if (saving && store.poll(GetTickCount64())) {
            log("Preferences reloaded from " + file.location());
            controller.onPreferencesChanged();
        }
        reportLanguage();
        dash.onTick(GetTickCount64());
        if (!item) {
            continue;
        }
        switch (item->event) {
        case HostEvent::FrameReceived:
            logReceived(item->frame, log);
            router.onFrame(item->frame);
            break;
        case HostEvent::HidConnected:
            log("Controller connected: " + link.description());
            controller.onConnected();
            dash.onConnected();
            if (hooks.connection) {
                hooks.connection(true);
            }
            break;
        case HostEvent::HidDisconnected:
            log("Controller disconnected.");
            controller.onDisconnected();
            dash.onDisconnected();
            if (hooks.connection) {
                hooks.connection(false);
            }
            break;
        case HostEvent::Quit: {
            link.stop();
            if (saving && !store.flush()) {
                log("WARN: could not save " + file.location());
            }
            const HostController::Stats& st = controller.stats();
            char stopped[192];
            std::snprintf(stopped, sizeof(stopped),
                          "Stopped. sent=%u send_failures=%u rejected=%u ignored=%u errors_received=%u dropped=%lu",
                          st.framesSent, st.sendFailures, st.framesRejected, st.framesIgnored, st.errorsReceived,
                          static_cast<unsigned long>(events.droppedFrames()));
            log(stopped);
            return 0;
        }
        default:
            handleAudioEvent(item->event, audio, controller);
            break;
        }
    }
}

}  // namespace stm32_audio::host::win
