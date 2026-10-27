#include "HostService.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "ApprovalService.h"
#include "DashController.h"
#include "FrameRouter.h"
#include "FrameText.h"
#include "HidMatch.h"
#include "MediaKeys.h"
#include "MediaService.h"
#include "MenuService.h"
#include "NotificationService.h"
#include "PictureService.h"
#include "SequencedSink.h"
#include "VolumeLimit.h"
#include "WinHidLink.h"
#include "WinHookPipe.h"
#include "WeatherService.h"
#include "WinHttpClient.h"
#include "WinMediaKeys.h"
#include "WinMediaSession.h"
#include "WinMenuImport.h"
#include "WinNotificationListener.h"
#include "WinPicture.h"
#include "WinSettingsSignal.h"

namespace stm32_audio::host::win {

namespace {

// Logs every frame, then hands it to the real transport. Of a cover's 534
// ART_PARTs and PICTURE_PARTs only the first and the last are logged.
class LoggingSink : public IFrameSink {
public:
    LoggingSink(IFrameSink& next, const LogFn& log) : next_(next), log_(log) {}

    bool sendFrame(const Frame& frame) override
    {
        ProtoMessage msg{};
        const bool decoded = Proto_Decode(frame.data(), &msg) == PROTO_OK;
        const bool sent = next_.sendFrame(frame);
        const bool imagePart = msg.command == PROTO_CMD_ART_PART || msg.command == PROTO_CMD_PICTURE_PART;
        const unsigned parts = msg.command == PROTO_CMD_ART_PART ? PROTO_ART_PARTS : PROTO_PICTURE_PARTS;
        if (decoded && imagePart && msg.u.art.index != 0U && msg.u.art.index + 1U != parts && sent) {
            return sent;
        }
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
    WinMediaKeys mediaKeys;
    // Now playing (ADR-008): the Windows media session, read on its own
    // thread; the controls go to it, or to the media keys without one.
    WinMediaSession mediaSession(events, log);
    MediaService media(sink, mediaSession, mediaKeys);
    dash.setMediaRequestHandler([&media] { media.onRequest(GetTickCount64()); });
    dash.setSeekHandler([&media](std::uint32_t seconds) { return media.onSeek(seconds, GetTickCount64()); });
    // Claude Code permission requests (ADR-007): the pipe from
    // "dashboard-host hook permission", the queue, the prompt and card 3.
    WinHookPipe hookPipe(events, log);
    ApprovalService approvals(
        dash, localTimeNow, [&hookPipe](std::uint64_t ticket, HookAnswer answer) { hookPipe.answer(ticket, answer); },
        log);
    // Weather card and detail (slot 0, ADR-006); the forecast is fetched on
    // a worker thread.
    WinHttpClient http;
    WinPreferenceFile weatherFile(L"weather");
    WeatherService weather(dash, weatherFile, http, localTimeNow, log);
    // Menu card and day detail (slot 1), from the month files (#20),
    // coloured by the food lists; the morning warning (ADR-010).
    WinMenuFiles menuFiles;
    WinPreferenceFile menuLists(L"menu-lists");
    WinPreferenceFile menuWarned(L"menu-warned");
    WinPreferenceFile menuSettings(L"menu-settings");
    MenuService menu(dash, menuFiles, menuLists, menuWarned, menuSettings, localTimeNow, log);
    // The bell, its list and the banners (slot 2, ADR-009, ADR-010): the
    // notification listener, polled on its own thread.
    WinNotificationListener notificationListener(events, log);
    WinFocusState focus;
    NotificationService notifications(dash, notificationListener, focus, log);
    // The picture card (ADR-010): the file chosen in the tray menu.
    WinPicture picture(log);
    SettingsWatcher settingsWatcher(events);
    PictureService pictureService(sink, picture);
    dash.setCardsSentHandler([&pictureService] { pictureService.onRequest(); });
    // The media controls (strip and Music tab), the prompts' buttons (the
    // menu warning, id 255, or a Claude Code request) and the clear button
    // of the notification list; other card buttons are answered with ERROR.
    dash.setActionHandler([&](const ProtoAction& action) {
        if (action.kind == PROTO_ACTION_PROMPT) {
            return menu.onAction(action) || approvals.onAction(action, GetTickCount64());
        }
        if (action.kind == PROTO_ACTION_CARD) {
            return notifications.onAction(action);
        }
        return media.onAction(action);
    });
    dash.setDetailHandler(
        [&weather, &menu, &approvals, &notifications](std::uint8_t slot,
                                                      std::int8_t arg) -> std::optional<std::string> {
            if (slot == WeatherService::kSlot) {
                return weather.detail(arg);
            }
            if (slot == MenuService::kSlot) {
                return menu.detail(arg);
            }
            if (slot == NotificationService::kSlot && arg == 0) {
                return notifications.detail();
            }
            if (slot == ApprovalService::kSlot && arg == 0) {
                return approvals.detail();
            }
            return std::nullopt;
        });
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

    // What the settings window shows (ADR-011): published when it changes.
    bool connected = false;
    ServiceSnapshot published;
    bool publishedOnce = false;
    auto publishSnapshot = [&] {
        if (!hooks.snapshot) {
            return;
        }
        ServiceSnapshot s;
        s.connected = connected;
        if (connected) {
            s.board = link.boardVersion();
        }
        s.settings = controller.preferences().settings();
        s.picture = picture.currentId();
        for (const DeviceRegistry::Device& d : controller.registry().devices()) {
            s.devices.push_back(SettingsDevice{d.endpoint.id, d.endpoint.name, d.alias, d.favorite});
        }
        if (!publishedOnce || s != published) {
            published = s;
            publishedOnce = true;
            hooks.snapshot(published);
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
    if (hookPipe.start()) {
        log("Claude Code hook pipe ready.");
    }
    if (!mediaSession.start()) {
        log("WARN: cannot start the media session thread; the media strip uses media keys.");
    }
    picture.reload();
    if (!settingsWatcher.start()) {
        log("WARN: cannot watch for settings changes; restart the program after changing them.");
    }
    if (!notificationListener.start()) {
        log("WARN: cannot start the notification thread; the notifications card stays empty.");
    }
    approvals.onTick(GetTickCount64());  // the card

    for (;;) {
        // While a cover is being sent, come back at once for its next parts.
        const std::optional<QueuedEvent> item =
            events.pop(media.sendingArt() || pictureService.sending() ? std::chrono::milliseconds(0)
                                                                       : std::chrono::milliseconds(500));
        if (saving) {
            store.onTick(GetTickCount64());  // save what the board changed
        }
        reportLanguage();
        weather.setLanguage(controller.preferences().settings().language);
        weather.onTick(GetTickCount64());
        menu.setLanguage(controller.preferences().settings().language);
        menu.onTick(GetTickCount64());
        approvals.setLanguage(controller.preferences().settings().language);
        approvals.onTick(GetTickCount64());
        notifications.setLanguage(controller.preferences().settings().language);
        notifications.onTick(GetTickCount64());
        menu.updateWarning(approvals.waiting() == 0U);  // a Claude Code request goes first
        {
            ProtoIndicators indicators{};
            indicators.count = 2;
            indicators.items[0] = notifications.indicator();
            indicators.items[1] = approvals.indicator();
            dash.setIndicators(indicators);
        }
        picture.onTick(GetTickCount64());  // the shuffle (ADR-012)
        pictureService.onTick();
        dash.onTick(GetTickCount64());
        media.onTick(GetTickCount64());
        publishSnapshot();
        if (!item) {
            continue;
        }
        switch (item->event) {
        case HostEvent::FrameReceived:
            logReceived(item->frame, log);
            router.onFrame(item->frame);
            break;
        case HostEvent::HidConnected:
            connected = true;
            log("Controller connected: " + link.description());
            {
                const BoardVersion v = link.boardVersion();
                log("Firmware: " + (v.firmware.empty() ? std::string("unknown (older than ADR-011)") : v.firmware) +
                    (v.protocol != 0U ? ", protocol " + std::to_string(v.protocol) : std::string()));
                if (v.protocol != 0U && v.protocol != PROTO_VERSION) {
                    log("WARN: the board speaks protocol " + std::to_string(v.protocol) + ", this program " +
                        std::to_string(PROTO_VERSION) + ": update the " +
                        (v.protocol < PROTO_VERSION ? "firmware." : "program."));
                }
            }
            controller.onConnected();
            dash.onConnected();
            media.onConnected(GetTickCount64());
            if (hooks.connection) {
                hooks.connection(true);
            }
            break;
        case HostEvent::HidDisconnected:
            connected = false;
            log("Controller disconnected.");
            controller.onDisconnected();
            approvals.onDisconnected();  // nobody can answer: back to the terminal
            dash.onDisconnected();
            notifications.onDisconnected();
            pictureService.onDisconnected();
            media.onDisconnected();
            if (hooks.connection) {
                hooks.connection(false);
            }
            break;
        case HostEvent::HookRequest:
            approvals.onRequest(item->ticket, item->text, GetTickCount64());
            break;
        case HostEvent::HookCancelled:
            approvals.onCancelled(item->ticket, GetTickCount64());
            break;
        case HostEvent::MediaChanged:
        case HostEvent::NotificationsChanged:
            break;  // the onTicks above took the new snapshot
        case HostEvent::SettingsChanged:
            // The settings window or a console tool wrote setting files
            // (#61); nothing reads them otherwise.
            log("Settings changed; reading the setting files again.");
            if (saving && store.reload()) {
                log("Preferences reloaded from " + file.location());
                controller.onPreferencesChanged();
            }
            weather.reload();
            menu.reload();
            picture.reloadIfChanged();
            break;
        case HostEvent::SettingsRequest: {
            // The settings window (ADR-011): a preference, applied as if the
            // board had changed it, then sent to the board and saved.
            const std::optional<SettingsRequest> request = parseSettingsRequest(item->text);
            if (request && request->kind == SettingsRequestKind::PictureShow) {
                // The picture the window chose (ADR-012); the card changes
                // when all its parts have arrived.
                if (!picture.show(request->name, GetTickCount64())) {
                    log("WARN: settings window: no picture " + request->name + ".");
                }
                publishSnapshot();
            } else if (request && isServiceRequest(request->kind) &&
                       applySettingsRequest(*request, controller.preferences())) {
                controller.onPreferencesChanged();
                store.markChanged();
                reportLanguage();
                publishSnapshot();
            }
            break;
        }
        case HostEvent::MenuChanged:
            log("Menu imported; reading the month files again.");
            menu.reload();
            break;
        case HostEvent::Quit: {
            approvals.cancelAll();
            hookPipe.stop();
            settingsWatcher.stop();
            mediaSession.stop();
            notificationListener.stop();
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
