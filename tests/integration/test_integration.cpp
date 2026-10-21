// Firmware <-> host loopback test (M5).
//
// The STM32 protocol client (firmware/link/link_client.c) and the host
// side as HostService wires it (HostController and DashController behind a
// FrameRouter, one SequencedSink) run against each other over a simulated USB link, with a
// fake Windows audio system. Time is simulated in 1 ms steps:
//
// - USB: one 64-byte report per direction every kUsbIntervalMs (interrupt
//   endpoints, ADR-003).
// - STM32 receive queue: kFirmwareRxDepth frames (USB_LINK_RX_DEPTH); when
//   it is full the USB interrupt drops the frame (App/Src/usb_link.c).
// - STM32 UI loop: every kFirmwareLoopMs, at most kFirmwareFramesPerRun
//   frames, then Link_Poll (App/Src/link_port.c). It can be "busy" (LVGL
//   drawing) for a while.
// - Host: the HID interface is opened some time after the board is
//   plugged in (WinHidLink scans once a second); reports sent while no
//   handle is open are lost.
//
// What runs here is the real protocol code on both sides; USB, the LCD and
// Windows are models. No test framework; returns non-zero on failure.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "DashController.h"
#include "DeviceRegistry.h"
#include "FrameRouter.h"
#include "HostController.h"
#include "SequencedSink.h"
#include "link_client.h"

using namespace stm32_audio::host;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_scenario = "";

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::printf("FAIL [%s] %s:%d: %s\n", g_scenario, __FILE__, __LINE__, #cond);  \
        }                                                                                 \
    } while (0)

// Mirrors firmware/STM32AudioController (usb_link.h, link_port.c, app.c).
constexpr unsigned kUsbIntervalMs = 4;  // bInterval 5 ms, rounded down by Windows
constexpr std::size_t kFirmwareRxDepth = 40;
constexpr unsigned kFirmwareLoopMs = 10;
constexpr unsigned kFirmwareFramesPerRun = 8;
constexpr unsigned kHostOpenDelayMs = 700;  // within one WinHidLink scan period

// --------------------------------------------------------------- Windows

class FakeAudio : public IAudioSystem {
public:
    std::vector<AudioEndpoint> endpoints;
    std::string defaultId;
    bool failSetDefault = false;
    std::vector<std::string> setDefaultCalls;
    unsigned volume = 40;
    bool mute = false;
    unsigned setVolumeCalls = 0;
    bool volumeNotificationDue = false;  // Windows echoes changes made through the API

    std::vector<AudioEndpoint> renderEndpoints() override { return endpoints; }
    std::string defaultEndpointId() override { return defaultId; }
    bool setDefaultEndpoint(const std::string& id) override
    {
        setDefaultCalls.push_back(id);
        if (failSetDefault) {
            return false;
        }
        defaultId = id;
        return true;
    }
    // Like Core Audio: no volume or mute without a default device.
    std::optional<unsigned> volumePercent() override
    {
        return defaultId.empty() ? std::nullopt : std::optional<unsigned>(volume);
    }
    bool setVolumePercent(unsigned percent) override
    {
        if (defaultId.empty()) {
            return false;
        }
        ++setVolumeCalls;
        volume = percent;
        volumeNotificationDue = true;
        return true;
    }
    std::optional<bool> muted() override
    {
        return defaultId.empty() ? std::nullopt : std::optional<bool>(mute);
    }
    bool setMuted(bool m) override
    {
        if (defaultId.empty()) {
            return false;
        }
        mute = m;
        volumeNotificationDue = true;
        return true;
    }
    // Only the default device has a volume in this simulation.
    std::optional<unsigned> endpointVolumePercent(const std::string& id) override
    {
        return id == defaultId ? volumePercent() : std::nullopt;
    }
    bool setEndpointVolumePercent(const std::string& id, unsigned percent) override
    {
        return id == defaultId && setVolumePercent(percent);
    }
};

AudioEndpoint endpoint(const std::string& id, const std::string& name, FormFactor ff)
{
    AudioEndpoint e;
    e.id = id;
    e.name = name;
    e.formFactor = ff;
    return e;
}

// ------------------------------------------------------------------- rig

// What the LCD would show, as driven by the link client's callbacks.
struct FirmwareScreen {
    bool waiting = true;
    std::vector<UiDevice> devices;
    std::uint8_t active = UI_NO_DEVICE;
    std::uint8_t pending = UI_NO_DEVICE;
    int listsShown = 0;
    int notices[3] = {0, 0, 0};
    ProtoSettings settings{};
    int settingsApplied = 0;
    std::uint8_t volume = UI_VOLUME_UNKNOWN;
    std::uint8_t muted = UI_VOLUME_UNKNOWN;
    std::vector<std::uint8_t> volumeShown;  // every value the slider was set to
};

struct WireStats {
    std::vector<std::uint8_t> toHost;    // commands, in order
    std::vector<std::uint8_t> toDevice;
    std::vector<std::uint8_t> errorCodesToHost;
    std::vector<std::uint8_t> errorCodesToDevice;
    std::vector<std::uint8_t> sequencesToDevice;
    unsigned firmwareRxDropped = 0;

    unsigned count(const std::vector<std::uint8_t>& v, std::uint8_t cmd) const
    {
        unsigned n = 0;
        for (std::uint8_t c : v) {
            n += (c == cmd) ? 1U : 0U;
        }
        return n;
    }
};

class Rig : public IFrameSink {
public:
    FakeAudio audio;
    FirmwareScreen screen;
    WireStats wire;
    LinkClient link{};
    SequencedSink hostSink{*this};  // HostService: one sequence for both controllers
    std::optional<HostController> host;
    std::optional<DashController> dash;
    std::optional<FrameRouter> router;
    std::uint32_t now;

    // Start close to the 32-bit wrap: the firmware's tick wraps too.
    explicit Rig(std::uint32_t start = 0xFFFFF000U)
        : now(start), hostOpenAt_(start), firmwareNextRun_(start), firmwareBusyUntil_(start)
    {
        static const LinkCallbacks callbacks = {
            &Rig::fwSend, &Rig::fwWaiting, &Rig::fwDevices, &Rig::fwActive, &Rig::fwPending, &Rig::fwNotice,
            &Rig::fwVolume, &Rig::fwMuted, nullptr, &Rig::fwSettings,
        };
        LinkCallbacks cb = callbacks;
        cb.ctx = this;
        Link_Init(&link, &cb);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // ---- the physical world

    void plugIn()
    {
        cable_ = true;
        hostOpenAt_ = now + kHostOpenDelayMs;
    }

    void unplug()
    {
        cable_ = false;
        hostOut_.clear();
        firmwareRx_.clear();
        firmwareTx_.clear();
        if (hostOpen_) {
            hostOpen_ = false;
            if (host) {
                host->onDisconnected();  // HidDisconnected
                dash->onDisconnected();
            }
        }
    }

    // dashboard-host started / stopped while the board stays plugged in.
    void startHost()
    {
        host.emplace(audio, hostSink);
        dash.emplace(hostSink, [this] { return 1792593125U + now / 1000U; });
        router.emplace(*host, *dash, hostSink);
        hostRunning_ = true;
        hostOpenAt_ = now + kHostOpenDelayMs;
    }

    void stopHost()
    {
        router.reset();
        dash.reset();
        host.reset();
        hostRunning_ = false;
        hostOpen_ = false;
        hostOut_.clear();
    }

    // The STM32 UI loop does not run for @p ms (e.g. LVGL redraw).
    void firmwareBusy(unsigned ms) { firmwareBusyUntil_ = now + ms; }

    // ---- Windows (notifications reach the host's consumer thread at once)

    void windowsSetDefault(const std::string& id)
    {
        audio.defaultId = id;
        if (host) {
            host->onDefaultChanged();
        }
    }

    void windowsAdd(const AudioEndpoint& e)
    {
        audio.endpoints.push_back(e);
        if (host) {
            host->onDevicesChanged();
        }
    }

    // Removing the default makes Windows pick another one (first left).
    void windowsRemove(const std::string& id)
    {
        for (std::size_t i = 0; i < audio.endpoints.size(); ++i) {
            if (audio.endpoints[i].id == id) {
                audio.endpoints.erase(audio.endpoints.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        const bool wasDefault = (audio.defaultId == id);
        if (wasDefault) {
            audio.defaultId = audio.endpoints.empty() ? std::string() : audio.endpoints.front().id;
        }
        if (host) {
            host->onDevicesChanged();
            if (wasDefault) {
                host->onDefaultChanged();
            }
        }
    }

    void windowsVolume(unsigned percent)
    {
        audio.volume = percent;
        if (host) {
            host->onVolumeChanged();
        }
    }

    void windowsMute(bool m)
    {
        audio.mute = m;
        if (host) {
            host->onVolumeChanged();
        }
    }

    // ---- the user

    void tap(std::uint8_t deviceId) { Link_Select(&link, deviceId, now); }
    // The finger moves the knob itself; the link is told the new value.
    void slide(std::uint8_t volume)
    {
        screen.volume = volume;
        Link_SetVolume(&link, volume, now);
    }
    void tapMute(bool m) { Link_SetMute(&link, m); }
    bool setAlias(std::uint8_t id, const char* alias) { return Link_SetAlias(&link, id, alias, now); }
    bool setFavorite(std::uint8_t id, bool fav) { return Link_SetFavorite(&link, id, fav, now); }
    bool move(std::uint8_t id, std::uint8_t position) { return Link_MoveDevice(&link, id, position, now); }
    void changeSettings(const ProtoSettings& s) { Link_SetSettings(&link, &s, now); }

    std::optional<std::uint8_t> shownIdOf(const std::string& name) const
    {
        for (const UiDevice& d : screen.devices) {
            if (name == d.name) {
                return d.id;
            }
        }
        return std::nullopt;
    }

    // ---- time

    void run(unsigned ms)
    {
        for (unsigned i = 0; i < ms; ++i) {
            step();
        }
    }

    // ---- checks

    bool hostOpen() const { return hostOpen_; }

    // The screen shows exactly the host's current list and the Windows
    // default, with nothing pending.
    bool inSync() const
    {
        if (!host || screen.waiting || screen.pending != UI_NO_DEVICE) {
            return false;
        }
        const std::vector<DeviceRegistry::Device>& devices = host->registry().devices();
        if (screen.devices.size() != devices.size()) {
            return false;
        }
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const UiDevice& shown = screen.devices[i];
            if (shown.id != devices[i].index || shown.icon != iconFor(devices[i].endpoint.formFactor) ||
                devices[i].displayName != shown.name || devices[i].alias != shown.alias ||
                (devices[i].favorite ? 1U : 0U) != shown.favorite) {
                return false;
            }
        }
        const std::optional<std::uint8_t> def = host->registry().indexOf(audio.defaultId);
        return screen.active == (def ? *def : UI_NO_DEVICE);
    }

    // The board sends a raw frame (a dashboard request the link client of
    // this step does not make itself).
    void boardSends(const ProtoMessage& msg)
    {
        Frame f{};
        if (Proto_Encode(&msg, f.data()) == PROTO_OK && cable_) {
            firmwareTx_.push_back(f);
        }
    }

    // IFrameSink: SequencedSink -> WinHidLink::sendFrame.
    bool sendFrame(const Frame& frame) override
    {
        if (!hostOpen_) {
            return false;
        }
        hostOut_.push_back(frame);
        return true;
    }

private:
    static Rig& self(void* ctx) { return *static_cast<Rig*>(ctx); }

    // UsbLink_Send: the firmware's IN endpoint queue (blocking send).
    static bool fwSend(const std::uint8_t frame[PROTO_FRAME_SIZE], void* ctx)
    {
        Rig& r = self(ctx);
        if (!r.cable_) {
            return false;
        }
        Frame f{};
        std::memcpy(f.data(), frame, f.size());
        r.firmwareTx_.push_back(f);
        return true;
    }
    static void fwWaiting(void* ctx)
    {
        FirmwareScreen& s = self(ctx).screen;
        s.waiting = true;
        s.devices.clear();
        s.active = UI_NO_DEVICE;
        s.pending = UI_NO_DEVICE;
    }
    static void fwDevices(const UiDevice* devices, std::size_t count, void* ctx)
    {
        FirmwareScreen& s = self(ctx).screen;
        s.waiting = false;
        s.devices.assign(devices, devices + count);
        s.pending = UI_NO_DEVICE;
        ++s.listsShown;
    }
    static void fwActive(std::uint8_t id, void* ctx)
    {
        FirmwareScreen& s = self(ctx).screen;
        s.active = id;
        s.pending = UI_NO_DEVICE;  // Ui_SetActive clears the pending mark
    }
    static void fwPending(std::uint8_t id, void* ctx) { self(ctx).screen.pending = id; }
    static void fwNotice(LinkNotice n, void* ctx) { ++self(ctx).screen.notices[n]; }
    static void fwVolume(std::uint8_t v, void* ctx)
    {
        FirmwareScreen& s = self(ctx).screen;
        s.volume = v;
        s.volumeShown.push_back(v);
    }
    static void fwMuted(std::uint8_t m, void* ctx) { self(ctx).screen.muted = m; }
    static void fwSettings(const ProtoSettings* s, void* ctx)
    {
        FirmwareScreen& screen = self(ctx).screen;
        screen.settings = *s;
        ++screen.settingsApplied;
    }

    static bool reached(std::uint32_t now, std::uint32_t at)
    {
        return static_cast<std::int32_t>(now - at) >= 0;
    }

    static void note(const Frame& f, std::vector<std::uint8_t>& cmds, std::vector<std::uint8_t>& errors)
    {
        ProtoMessage msg{};
        if (Proto_Decode(f.data(), &msg) != PROTO_OK) {
            cmds.push_back(0xFF);
            return;
        }
        cmds.push_back(msg.command);
        if (msg.command == PROTO_CMD_ERROR) {
            errors.push_back(msg.u.error.code);
        }
    }

    void step()
    {
        ++now;

        // Host opens the HID interface (WinHidLink scan) -> HidConnected.
        if (cable_ && hostRunning_ && !hostOpen_ && reached(now, hostOpenAt_)) {
            hostOpen_ = true;
            host->onConnected();
            dash->onConnected();
        }

        // Core Audio's notification for a change made through the API
        // reaches the host a few milliseconds later.
        if (audio.volumeNotificationDue && now % 5 == 0 && host) {
            audio.volumeNotificationDue = false;
            host->onVolumeChanged();
        }

        // USB, one report per direction per interval.
        if (cable_ && now % kUsbIntervalMs == 0) {
            if (!hostOut_.empty()) {
                const Frame f = hostOut_.front();
                hostOut_.pop_front();
                note(f, wire.toDevice, wire.errorCodesToDevice);
                wire.sequencesToDevice.push_back(f[3]);
                if (firmwareRx_.size() < kFirmwareRxDepth) {
                    firmwareRx_.push_back(f);
                } else {
                    ++wire.firmwareRxDropped;  // usb_link.c: rx_dropped
                }
            }
            if (!firmwareTx_.empty()) {
                const Frame f = firmwareTx_.front();
                firmwareTx_.pop_front();
                if (hostOpen_) {  // no handle open: Windows discards the report
                    note(f, wire.toHost, wire.errorCodesToHost);
                    router->onFrame(f);
                }
            }
        }

        // STM32 UI loop (LinkPort_Run).
        if (reached(now, firmwareNextRun_) && reached(now, firmwareBusyUntil_)) {
            firmwareNextRun_ = now + kFirmwareLoopMs;
            if (cable_ != firmwareConfigured_) {
                firmwareConfigured_ = cable_;
                if (cable_) {
                    Link_OnConnected(&link, now);
                } else {
                    Link_OnDisconnected(&link);
                }
            }
            for (unsigned i = 0; i < kFirmwareFramesPerRun && !firmwareRx_.empty(); ++i) {
                const Frame f = firmwareRx_.front();
                firmwareRx_.pop_front();
                Link_OnFrame(&link, f.data(), now);
            }
            Link_Poll(&link, now);
        }
    }

    bool cable_ = false;
    bool hostRunning_ = false;
    bool hostOpen_ = false;
    std::uint32_t hostOpenAt_ = 0;
    bool firmwareConfigured_ = false;
    std::uint32_t firmwareNextRun_ = 0;
    std::uint32_t firmwareBusyUntil_ = 0;
    std::deque<Frame> hostOut_;
    std::deque<Frame> firmwareRx_;
    std::deque<Frame> firmwareTx_;
};

// Three devices, default = headphones.
void addHomeSetup(FakeAudio& audio)
{
    audio.endpoints = {
        endpoint("{spk}", "Hoparl\xC3\xB6r", FormFactor::Speakers),
        endpoint("{hp}", "Kulakl\xC4\xB1k", FormFactor::Headphones),
        endpoint("{hdmi}", "LG HDR 4K", FormFactor::Display),
    };
    audio.defaultId = "{hp}";
}

// Host running, board plugged in, list shown.
void connect(Rig& r)
{
    r.startHost();
    r.plugIn();
    r.run(3000);
}

// ------------------------------------------------------------- scenarios

void testConnect()
{
    g_scenario = "connect";
    Rig r;
    addHomeSetup(r.audio);
    r.startHost();
    r.plugIn();
    CHECK(r.screen.waiting);

    // The host opens the device after kHostOpenDelayMs and sends the list
    // at once; the board's first GET_DEVICE_LIST was lost (no handle open
    // yet) and its 1 s retry is not needed.
    r.run(kHostOpenDelayMs + 200);
    CHECK(r.inSync());
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.link.state == LINK_STATE_READY);
    CHECK(r.screen.devices.size() == 3);
    CHECK(r.shownIdOf("Kulakl\xC4\xB1k") == r.screen.active);
    CHECK(r.screen.devices.size() == 3 && r.screen.devices[0].icon == UI_ICON_SPEAKERS &&
          r.screen.devices[1].icon == UI_ICON_HEADPHONES && r.screen.devices[2].icon == UI_ICON_DISPLAY);
    CHECK(r.wire.count(r.wire.toHost, PROTO_CMD_GET_DEVICE_LIST) == 0);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_LIST) == 3);
    CHECK(r.wire.errorCodesToHost.empty());
    CHECK(r.wire.errorCodesToDevice.empty());
    CHECK(r.wire.firmwareRxDropped == 0);
    CHECK(r.link.frames_rejected == 0);
    CHECK(r.host->stats().framesRejected == 0 && r.host->stats().sendFailures == 0);
}

void testHostStartedLate()
{
    g_scenario = "host started late";
    Rig r;
    addHomeSetup(r.audio);
    r.plugIn();
    r.run(5000);
    CHECK(r.screen.waiting);
    CHECK(r.screen.listsShown == 0);

    r.startHost();
    r.run(2500);
    CHECK(r.inSync());
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());
}

void testSelectFromBoard()
{
    g_scenario = "select from board";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const std::uint8_t spk = *r.shownIdOf("Hoparl\xC3\xB6r");
    const unsigned changedBefore = r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_CHANGED);

    r.tap(spk);
    CHECK(r.screen.pending == spk);
    CHECK(r.screen.active != spk);  // not active before the host confirms
    r.run(100);
    CHECK(r.audio.setDefaultCalls == std::vector<std::string>{"{spk}"});
    CHECK(r.audio.defaultId == "{spk}");
    CHECK(r.screen.active == spk);
    CHECK(r.screen.pending == UI_NO_DEVICE);

    // Windows' own notification of the switch comes later: no second
    // DEVICE_CHANGED, no new list.
    r.windowsSetDefault("{spk}");
    r.run(100);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_CHANGED) == changedBefore + 1);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_LIST) == 3);
    CHECK(r.inSync());
    CHECK(r.screen.notices[LINK_NOTICE_SELECT_FAILED] == 0 && r.screen.notices[LINK_NOTICE_HOST_TIMEOUT] == 0);
}

void testWindowsDefaultChange()
{
    g_scenario = "Windows default change";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const unsigned listsBefore = r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_LIST);

    r.windowsSetDefault("{hdmi}");
    r.run(100);
    CHECK(r.inSync());
    CHECK(r.screen.active == *r.shownIdOf("LG HDR 4K"));
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_DEVICE_LIST) == listsBefore);
}

void testDevicesAddedAndRemoved()
{
    g_scenario = "devices added and removed";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const std::uint8_t hp = *r.shownIdOf("Kulakl\xC4\xB1k");
    const std::uint8_t hdmi = *r.shownIdOf("LG HDR 4K");

    r.windowsAdd(endpoint("{usb}", "USB Headset", FormFactor::Headset));
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 4);
    CHECK(r.shownIdOf("Kulakl\xC4\xB1k") == hp);  // indices are kept
    CHECK(r.shownIdOf("LG HDR 4K") == hdmi);
    CHECK(r.screen.active == hp);

    r.windowsRemove("{spk}");
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 3);
    CHECK(!r.shownIdOf("Hoparl\xC3\xB6r"));
    CHECK(r.shownIdOf("Kulakl\xC4\xB1k") == hp);
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());
}

void testDefaultDeviceRemoved()
{
    g_scenario = "default device removed";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    r.windowsRemove("{hp}");  // Windows falls back to the speakers
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 2);
    CHECK(r.screen.active == *r.shownIdOf("Hoparl\xC3\xB6r"));
}

void testAllDevicesRemoved()
{
    g_scenario = "all devices removed";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    r.windowsRemove("{spk}");
    r.windowsRemove("{hdmi}");
    r.windowsRemove("{hp}");
    r.run(300);
    CHECK(r.inSync());
    CHECK(!r.screen.waiting);
    CHECK(r.screen.devices.empty());
    CHECK(r.screen.active == UI_NO_DEVICE);

    r.windowsAdd(endpoint("{spk}", "Hoparl\xC3\xB6r", FormFactor::Speakers));
    r.windowsSetDefault("{spk}");
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 1);
}

void testWindowsRefusesSwitch()
{
    g_scenario = "Windows refuses the switch";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const std::uint8_t hp = *r.shownIdOf("Kulakl\xC4\xB1k");
    r.audio.failSetDefault = true;

    r.tap(*r.shownIdOf("LG HDR 4K"));
    r.run(100);
    CHECK(r.screen.notices[LINK_NOTICE_SELECT_FAILED] == 1);
    CHECK(r.screen.active == hp);
    CHECK(r.inSync());
    CHECK(r.wire.errorCodesToDevice == std::vector<std::uint8_t>{PROTO_ERROR_OPERATION_FAILED});
    CHECK(r.wire.errorCodesToHost.empty());

    // Nothing else happens afterwards (no timeout notice, no re-request).
    const std::size_t framesBefore = r.wire.toHost.size();
    r.run(5000);
    CHECK(r.screen.notices[LINK_NOTICE_HOST_TIMEOUT] == 0);
    CHECK(r.wire.toHost.size() == framesBefore);
}

void testSelectRacesDeviceRemoval()
{
    g_scenario = "select races device removal";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    // The user taps the speakers just as Windows removes them; the new
    // list is still on its way when SELECT_DEVICE leaves the board.
    const std::uint8_t spk = *r.shownIdOf("Hoparl\xC3\xB6r");
    r.windowsRemove("{spk}");
    r.tap(spk);
    r.run(3000);
    CHECK(r.audio.setDefaultCalls.empty());
    CHECK(r.wire.errorCodesToDevice == std::vector<std::uint8_t>{PROTO_ERROR_UNKNOWN_DEVICE});
    // The new list arrives before the ERROR and drops the pending mark
    // (the tapped device is no longer on screen), so no notice.
    CHECK(r.screen.notices[LINK_NOTICE_SELECT_FAILED] == 0);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 2);
}

void testUsbUnplugAndReplug()
{
    g_scenario = "USB unplug and replug";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    r.unplug();
    r.run(50);
    CHECK(r.screen.waiting);

    // Windows changes while the board is away: nothing is sent.
    r.windowsAdd(endpoint("{usb}", "USB Headset", FormFactor::Headset));
    r.windowsSetDefault("{usb}");
    r.run(1000);
    CHECK(r.host->stats().sendFailures == 0);

    r.plugIn();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 4);
    CHECK(r.screen.active == *r.shownIdOf("USB Headset"));
}

void testUnplugDuringList()
{
    g_scenario = "unplug during the list";
    Rig r;
    addHomeSetup(r.audio);
    for (int i = 0; i < 20; ++i) {
        r.audio.endpoints.push_back(
            endpoint("{x" + std::to_string(i) + "}", "Device " + std::to_string(i), FormFactor::Unknown));
    }
    r.startHost();
    r.plugIn();
    // The host opens the device after kHostOpenDelayMs and sends the
    // 26-frame list. Unplug half way through it.
    r.run(kHostOpenDelayMs + 50);
    CHECK(r.screen.waiting);
    r.unplug();
    r.run(100);
    r.plugIn();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 23);
}

void testHostTimeoutAndRecovery()
{
    g_scenario = "host stops answering";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    // dashboard-host is closed; the board stays plugged in and does not
    // know. The user taps a device.
    r.stopHost();
    r.tap(*r.shownIdOf("LG HDR 4K"));
    r.run(LINK_RESPONSE_TIMEOUT_MS + 100);
    CHECK(r.screen.notices[LINK_NOTICE_HOST_TIMEOUT] == 1);
    CHECK(r.screen.waiting);

    r.run(5000);  // keeps asking for the list
    CHECK(r.screen.waiting);

    r.startHost();
    r.run(2500);
    CHECK(r.inSync());
    CHECK(r.audio.setDefaultCalls.empty());  // the lost tap is not replayed
}

// The host program restarts while the board stays plugged in. Device
// indices are session-local (protocol.md); the new session sends its list
// when it opens the device, so the board drops the old one without a tap.
void testHostRestart()
{
    g_scenario = "host restart";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    CHECK(r.shownIdOf("LG HDR 4K") == 2);

    r.stopHost();
    r.run(500);
    // While the host is closed: the speakers go away, a DAC arrives and
    // becomes the default. The board cannot know.
    r.audio.endpoints.erase(r.audio.endpoints.begin());
    r.audio.endpoints.push_back(endpoint("{dac}", "USB DAC", FormFactor::DigitalOutput));
    r.audio.defaultId = "{dac}";
    r.run(500);
    CHECK(r.screen.devices.size() == 3 && r.shownIdOf("Hoparl\xC3\xB6r"));

    r.startHost();
    r.run(kHostOpenDelayMs + 200);
    CHECK(r.inSync());
    CHECK(!r.shownIdOf("Hoparl\xC3\xB6r"));
    CHECK(r.screen.active == *r.shownIdOf("USB DAC"));
    // The new session numbered {hp}=0 {hdmi}=1 {dac}=2.
    CHECK(r.shownIdOf("LG HDR 4K") == 1);
    CHECK(r.wire.errorCodesToDevice.empty());
    CHECK(r.screen.notices[LINK_NOTICE_SELECT_FAILED] == 0);

    // A tap uses the new indices.
    r.tap(*r.shownIdOf("LG HDR 4K"));
    r.run(100);
    CHECK(r.audio.setDefaultCalls == std::vector<std::string>{"{hdmi}"});
    CHECK(r.inSync());
}

void testManyDevicesAndLongNames()
{
    g_scenario = "40 devices, long names";
    Rig r;
    // 40 endpoints; long names with 2-byte characters across the 48-byte
    // limit; the default is past the 32-device cut.
    for (int i = 0; i < 40; ++i) {
        std::string name = "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F " + std::to_string(i) + " ";
        while (name.size() < 60) {
            name += "\xC4\x9F\xC3\xBC\xC5\x9F";  // ğüş
        }
        r.audio.endpoints.push_back(endpoint("{d" + std::to_string(i) + "}", name, FormFactor::Speakers));
    }
    r.audio.defaultId = "{d39}";
    connect(r);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 32);
    CHECK(r.screen.active != UI_NO_DEVICE);
    bool lengthsOk = true;
    for (const UiDevice& d : r.screen.devices) {
        const std::size_t len = std::strlen(d.name);
        // Never more than 48 bytes, never a cut UTF-8 sequence.
        lengthsOk = lengthsOk && len <= UI_MAX_NAME_BYTES && len > 40 &&
                    (static_cast<unsigned char>(d.name[len - 1]) & 0xC0U) != 0xC0U;
    }
    CHECK(lengthsOk);
    CHECK(r.wire.firmwareRxDropped == 0);
}

void testVolumeFramesIgnoredByBoard()
{
    g_scenario = "volume frames";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const int lists = r.screen.listsShown;

    r.windowsVolume(10);
    r.windowsVolume(20);
    r.run(100);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_VOLUME_CHANGED) == 3);  // 1 on connect + 2
    CHECK(r.wire.errorCodesToHost.empty());
    CHECK(r.host->stats().errorsReceived == 0);
    CHECK(r.screen.listsShown == lists);
    CHECK(r.inSync());
}

void testVolumeFromWindows()
{
    g_scenario = "volume from Windows";
    Rig r;
    addHomeSetup(r.audio);
    r.startHost();
    r.plugIn();
    r.run(100);
    CHECK(r.screen.volume == UI_VOLUME_UNKNOWN && r.screen.muted == UI_VOLUME_UNKNOWN);
    r.run(3000);
    CHECK(r.screen.volume == 40 && r.screen.muted == 0);

    r.windowsVolume(70);
    r.run(50);
    CHECK(r.screen.volume == 70);
    r.windowsMute(true);
    r.run(50);
    CHECK(r.screen.muted == 1);
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());
}

// The user drags the slider from 40 to 90 over half a second.
void testVolumeDragOnBoard()
{
    g_scenario = "volume drag on the board";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    const std::size_t shownBefore = r.screen.volumeShown.size();

    for (unsigned v = 41; v <= 90; ++v) {
        r.slide(static_cast<std::uint8_t>(v));
        r.run(10);
    }
    // The host's echoes of intermediate values never move the knob back.
    CHECK(r.screen.volumeShown.size() == shownBefore);
    r.run(1000);
    CHECK(r.audio.volume == 90);
    CHECK(r.screen.volume == 90);
    CHECK(r.screen.volumeShown.size() == shownBefore);  // nothing to correct
    // Throttled: about one SET_VOLUME per 100 ms of dragging, not 50.
    const unsigned sets = r.wire.count(r.wire.toHost, PROTO_CMD_SET_VOLUME);
    CHECK(sets >= 5 && sets <= 7);
    CHECK(r.audio.setVolumeCalls == sets);
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());

    // Afterwards Windows changes show again.
    r.windowsVolume(25);
    r.run(50);
    CHECK(r.screen.volume == 25);
}

void testMuteFromBoard()
{
    g_scenario = "mute from the board";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    r.tapMute(true);
    CHECK(r.screen.muted == 0);  // shown only when the host confirms
    r.run(50);
    CHECK(r.audio.mute);
    CHECK(r.screen.muted == 1);
    r.tapMute(false);
    r.run(50);
    CHECK(!r.audio.mute && r.screen.muted == 0);
}

// Without a default device there is nothing to control; when one comes
// back, volume and mute are shown again even if their values did not
// change.
void testVolumeWithoutDefaultDevice()
{
    g_scenario = "volume without a default device";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    r.windowsRemove("{spk}");
    r.windowsRemove("{hdmi}");
    r.windowsRemove("{hp}");
    r.run(200);
    CHECK(r.screen.volume == UI_VOLUME_UNKNOWN && r.screen.muted == UI_VOLUME_UNKNOWN);
    r.slide(80);
    r.tapMute(true);
    r.run(200);
    CHECK(r.wire.count(r.wire.toHost, PROTO_CMD_SET_VOLUME) == 0);
    CHECK(r.wire.count(r.wire.toHost, PROTO_CMD_SET_MUTE) == 0);

    r.windowsAdd(endpoint("{spk}", "Hoparl\xC3\xB6r", FormFactor::Speakers));
    r.windowsSetDefault("{spk}");
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.volume == 40 && r.screen.muted == 0);
}

// The STM32 UI loop is held up (LVGL redraw) while a long list arrives.
void testBoardBusyDuringLongList()
{
    g_scenario = "board busy during a long list";
    Rig r;
    addHomeSetup(r.audio);
    for (int i = 0; i < 25; ++i) {
        r.audio.endpoints.push_back(
            endpoint("{x" + std::to_string(i) + "}", "Device " + std::to_string(i), FormFactor::Unknown));
    }
    connect(r);
    CHECK(r.inSync());

    // A device is added: the host sends a new 29-entry list. The board is
    // busy for 100 ms just then; the receive queue holds the frames.
    r.windowsAdd(endpoint("{usb}", "USB Headset", FormFactor::Headset));
    r.firmwareBusy(100);
    r.run(500);
    CHECK(r.wire.firmwareRxDropped == 0);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 29);

    // Two devices arrive one after the other (two lists, 61 frames) while
    // the board is busy for 400 ms: the queue overflows and the second
    // list loses its tail. The board asks again 1 s after the last entry.
    const unsigned requests = r.wire.count(r.wire.toHost, PROTO_CMD_GET_DEVICE_LIST);
    r.firmwareBusy(400);
    r.windowsAdd(endpoint("{usb2}", "USB DAC", FormFactor::DigitalOutput));
    r.windowsAdd(endpoint("{usb3}", "Dock", FormFactor::Speakers));
    r.windowsSetDefault("{usb2}");
    r.run(600);
    CHECK(r.wire.firmwareRxDropped > 0);
    CHECK(!r.screen.waiting);
    r.run(2000);
    CHECK(r.wire.count(r.wire.toHost, PROTO_CMD_GET_DEVICE_LIST) == requests + 1);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 31);
    CHECK(r.screen.active == *r.shownIdOf("USB DAC"));
}

}  // namespace


// ---------------------------------------------- protocol v2 (ADR-004)

std::vector<std::string> shownNames(const Rig& r)
{
    std::vector<std::string> out;
    for (const UiDevice& d : r.screen.devices) {
        out.push_back(d.alias[0] != '\0' ? d.alias : d.name);
    }
    return out;
}

void testEditsFromBoard()
{
    g_scenario = "alias, favorite and order from the board";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);
    CHECK(r.inSync());

    // Alias for the monitor.
    CHECK(r.setAlias(*r.shownIdOf("LG HDR 4K"), "TV"));
    CHECK(!r.setFavorite(*r.shownIdOf("LG HDR 4K"), true));  // one edit at a time
    r.run(200);
    CHECK(!Link_EditPending(&r.link));
    CHECK(r.inSync());
    CHECK(shownNames(r) == std::vector<std::string>({"Hoparl\xC3\xB6r", "Kulakl\xC4\xB1k", "TV"}));
    CHECK(r.host->preferences().device("{hdmi}")->alias == "TV");

    // Favorite, then move the monitor to the front.
    CHECK(r.setFavorite(*r.shownIdOf("LG HDR 4K"), true));
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.screen.devices[2].favorite == 1);
    CHECK(r.move(*r.shownIdOf("LG HDR 4K"), 0));
    r.run(200);
    CHECK(r.inSync());
    CHECK(shownNames(r) == std::vector<std::string>({"TV", "Hoparl\xC3\xB6r", "Kulakl\xC4\xB1k"}));

    // Removing the alias shows the Windows name again.
    CHECK(r.setAlias(*r.shownIdOf("LG HDR 4K"), ""));
    r.run(200);
    CHECK(r.inSync());
    CHECK(shownNames(r).front() == "LG HDR 4K");

    // Selection still works with the newer list generation.
    r.tap(*r.shownIdOf("LG HDR 4K"));
    r.run(100);
    CHECK(r.audio.defaultId == "{hdmi}");
    CHECK(r.inSync());

    // The order survives an unplug.
    r.unplug();
    r.run(500);
    r.plugIn();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(shownNames(r).front() == "LG HDR 4K");
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());
    CHECK(r.screen.notices[LINK_NOTICE_EDIT_FAILED] == 0 && r.screen.notices[LINK_NOTICE_HOST_TIMEOUT] == 0);
}

void testEditCrossesWindowsChange()
{
    g_scenario = "edit crosses a Windows device change";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    // The board's request still names the old list when it reaches the
    // host: refused, the board fetches the list again.
    CHECK(r.setFavorite(*r.shownIdOf("Hoparl\xC3\xB6r"), true));
    r.windowsAdd(endpoint("{usb}", "USB DAC", FormFactor::DigitalOutput));
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.screen.devices.size() == 4);
    CHECK(r.host->preferences().device("{spk}") == nullptr);  // not applied
    CHECK(r.wire.count(r.wire.errorCodesToDevice, PROTO_ERROR_UNKNOWN_DEVICE) == 1);
    CHECK(!Link_EditPending(&r.link));

    // Done again on the new list, it works.
    CHECK(r.setFavorite(*r.shownIdOf("Hoparl\xC3\xB6r"), true));
    r.run(200);
    CHECK(r.inSync());
    CHECK(r.host->preferences().device("{spk}")->favorite);
}

void testSettingsRoundTrip()
{
    g_scenario = "settings";
    Rig r;
    addHomeSetup(r.audio);
    connect(r);

    ProtoSettings defaults{};
    Proto_DefaultSettings(&defaults);
    CHECK(std::memcmp(&r.screen.settings, &defaults, sizeof(defaults)) == 0 || r.screen.settingsApplied == 0);

    // Changed on the board: applied at once, stored by the host.
    ProtoSettings s = *Link_Settings(&r.link);
    s.theme = 1;
    s.layout = 2;
    r.changeSettings(s);
    CHECK(r.screen.settings.theme == 1);
    r.run(100);
    CHECK(r.host->preferences().settings().theme == 1 && r.host->preferences().settings().layout == 2);
    CHECK(r.wire.count(r.wire.toHost, PROTO_CMD_SET_SETTINGS) == 1);

    // Flipped while unplugged: the board's change wins when it reconnects.
    r.unplug();
    r.run(200);
    s.flipped = 1;
    r.changeSettings(s);
    CHECK(r.screen.settings.flipped == 1);
    r.plugIn();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.screen.settings.flipped == 1);
    CHECK(r.host->preferences().settings().flipped == 1);
    CHECK(r.host->preferences().settings().theme == 1);

    // Host program restarted without stored settings (no preference file;
    // saving is the program's job, not the controller's): the board
    // applies the host's record.
    r.stopHost();
    r.run(100);
    r.startHost();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.screen.settings.theme == 0 && r.screen.settings.flipped == 0);
    CHECK(r.wire.errorCodesToHost.empty() && r.wire.errorCodesToDevice.empty());
    CHECK(r.screen.notices[LINK_NOTICE_EDIT_FAILED] == 0);
}

// Protocol v3 (ADR-006, #12): the dashboard side answers next to the audio
// side; the board's link client ignores what it does not use yet, and the
// host numbers every frame from one counter.
void testDashboardNextToAudio()
{
    g_scenario = "dashboard next to audio";
    Rig r;
    addHomeSetup(r.audio);
    r.startHost();
    r.plugIn();
    r.run(3000);
    CHECK(r.inSync());
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_TIME) == 1U);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_CARD_CLEAR) == PROTO_MAX_CARDS);
    CHECK(r.wire.errorCodesToHost.empty());  // the board drops them without complaint

    // A card set on the host reaches the board while the audio list stays.
    r.dash->setCard(0, recordLine({"0", "Hava", ""}) + "\n14 C");
    ProtoMessage get{};
    get.command = PROTO_CMD_GET_CARDS;
    r.boardSends(get);
    r.run(500);
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_CARD_PART) == 2U);  // on change, then on GET_CARDS
    CHECK(r.wire.count(r.wire.toDevice, PROTO_CMD_TIME) == 2U);
    CHECK(r.inSync());

    // OPEN_DETAIL without a detail handler: ERROR OPERATION_FAILED.
    ProtoMessage open{};
    open.command = PROTO_CMD_OPEN_DETAIL;
    open.u.open_detail.slot = 0;
    r.boardSends(open);
    r.run(200);
    CHECK(r.wire.errorCodesToDevice.size() == 1U &&
          r.wire.errorCodesToDevice.back() == PROTO_ERROR_OPERATION_FAILED);

    // Audio still works through the router.
    const std::optional<std::uint8_t> speakers = r.shownIdOf("Hoparl\xC3\xB6r");
    CHECK(speakers.has_value());
    if (speakers) {
        r.tap(*speakers);
    }
    r.run(500);
    CHECK(r.audio.defaultId == "{spk}");
    CHECK(r.inSync());

    // One counter for both controllers: 0, 1, 2, ... with no gap.
    bool consecutive = true;
    for (std::size_t i = 0; i < r.wire.sequencesToDevice.size(); ++i) {
        consecutive = consecutive && (r.wire.sequencesToDevice[i] == static_cast<std::uint8_t>(i));
    }
    CHECK(consecutive);
}

int main()
{
    testConnect();
    testHostStartedLate();
    testSelectFromBoard();
    testWindowsDefaultChange();
    testDevicesAddedAndRemoved();
    testDefaultDeviceRemoved();
    testAllDevicesRemoved();
    testWindowsRefusesSwitch();
    testSelectRacesDeviceRemoval();
    testUsbUnplugAndReplug();
    testUnplugDuringList();
    testHostTimeoutAndRecovery();
    testHostRestart();
    testManyDevicesAndLongNames();
    testVolumeFramesIgnoredByBoard();
    testBoardBusyDuringLongList();
    testEditsFromBoard();
    testEditCrossesWindowsChange();
    testSettingsRoundTrip();
    testDashboardNextToAudio();
    testVolumeFromWindows();
    testVolumeDragOnBoard();
    testMuteFromBoard();
    testVolumeWithoutDefaultDevice();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
