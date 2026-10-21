// Audio side of protocol v3 on the host (protocol/protocol.md,
// "Exchanges"; ADR-004 for preferences and settings). The dashboard
// commands go to DashController; FrameRouter splits the frames.
//
// Glues the STM32 (frames in through onFrame, out through IFrameSink) to
// the audio system (IAudioSystem). Platform independent; the Windows layer
// owns the HID device and the Core Audio notifications and forwards them.
//
// Not thread-safe: call every method from one thread. Core Audio delivers
// notifications on its own threads, so the Windows layer must queue them
// and call onDevicesChanged() / onDefaultChanged() / onVolumeChanged()
// from the thread that also calls onFrame().
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "DeviceRegistry.h"
#include "IAudioSystem.h"
#include "IFrameSink.h"
#include "Preferences.h"
#include "VolumeLimit.h"
#include "protocol.h"

namespace stm32_audio::host {

class HostController {
public:
    struct Stats {
        unsigned framesSent = 0;
        unsigned sendFailures = 0;
        unsigned framesRejected = 0;  // failed Proto_Decode
        unsigned framesIgnored = 0;   // valid, but not for the host (wrong direction)
        unsigned errorsReceived = 0;  // ERROR frames from the STM32
    };

    HostController(IAudioSystem& audio, IFrameSink& sink);

    // A frame arrived from the STM32 (decoded here; invalid frames are
    // answered with ERROR). Used where this controller is alone.
    void onFrame(const Frame& frame);

    // A decoded audio message (0x01..0x0E) or an ERROR (FrameRouter).
    void onMessage(const ProtoMessage& msg);

    // Windows reported a change. Each sends only what differs from what
    // the STM32 was last told, and nothing before the full list has been
    // sent since the last connect (onConnected or GET_DEVICE_LIST).
    void onDevicesChanged();  // endpoints added, removed or renamed
    void onDefaultChanged();
    void onVolumeChanged();   // volume or mute of the default endpoint

    // The HID device was opened: forget what was sent and send the full
    // list, default, volume and mute without waiting for GET_DEVICE_LIST
    // (protocol.md, "Exchanges"), so a board that stayed plugged in while
    // the host restarted drops the previous session's list.
    void onConnected();

    // The HID device went away: forget what was sent; nothing is sent
    // until the next onConnected() or GET_DEVICE_LIST.
    void onDisconnected();

    const DeviceRegistry& registry() const { return registry_; }
    const Stats& stats() const { return stats_; }

    // Aliases, order, favorites and settings. After changing them from
    // outside (e.g. loaded from a file), call onPreferencesChanged().
    Preferences& preferences() { return prefs_; }
    const Preferences& preferences() const { return prefs_; }

    // The preferences were replaced from outside: send a new list and the
    // settings if the STM32 is synced.
    void onPreferencesChanged();

    // Called after the STM32 changed a preference (alias, favorite, order,
    // settings), e.g. to save them.
    void setPreferencesChangedHandler(std::function<void()> handler) { prefsChanged_ = std::move(handler); }

    // Called when the volume limit lowered a device the STM32 selected
    // (e.g. to log it).
    void setVolumeLimitedHandler(std::function<void(const VolumeLimited&)> handler)
    {
        volumeLimited_ = std::move(handler);
    }

private:
    bool synced() const { return sentGeneration_.has_value(); }
    void forgetSent();
    void handleGetDeviceList();
    void handleSelectDevice(const ProtoMessage& msg);
    void handleSetVolume(const ProtoMessage& msg);
    void handleSetMute(const ProtoMessage& msg);
    void handleEdit(const ProtoMessage& msg);
    void handleSetSettings(const ProtoMessage& msg);

    // The device named by a request, if its generation is the list sent
    // last in this session; otherwise answers ERROR UNKNOWN_DEVICE.
    const DeviceRegistry::Device* requestedDevice(const ProtoMessage& msg);
    void sendEditedList();
    void sendSettings();

    void refreshRegistry();
    void sendDeviceList();
    void sendDefault(bool force);
    void sendVolumeAndMute(bool force);
    void sendError(ProtoErrorCode code, std::uint8_t refCommand, std::uint8_t refSequence);
    void send(ProtoMessage msg);

    IAudioSystem& audio_;
    IFrameSink& sink_;
    DeviceRegistry registry_;
    Preferences prefs_;
    std::function<void()> prefsChanged_;
    std::function<void(const VolumeLimited&)> volumeLimited_;
    Stats stats_;
    std::uint8_t sequence_ = 0;

    // Last values sent to the STM32 (nullopt: not sent since connect).
    std::optional<std::uint8_t> sentGeneration_;
    std::optional<std::uint8_t> sentDefault_;
    std::optional<unsigned> sentVolume_;
    std::optional<bool> sentMuted_;
};

}  // namespace stm32_audio::host
